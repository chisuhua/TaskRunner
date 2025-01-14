#ifndef CMD_PROCESSOR_H
#define CMD_PROCESSOR_H

#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <variant>
#include <functional>
#include "TaskQueue.h"
#include "EventQueue.h"
#include "CmdBuffer.h"
#include "Barrier.h"
#include "TaskRunner.h"

namespace async_task {

class CmdProcessor {
public:
    CmdProcessor(TaskQueue& taskQueue, EventQueue& dispatchToProcessorQueue, EventQueue& processorToDispatchQueue)
        : taskQueue_(taskQueue), dispatchToProcessorQueue_(dispatchToProcessorQueue), processorToDispatchQueue_(processorToDispatchQueue) {}

    void updateAllTaskQueues(const std::vector<TaskQueue*>& allTaskQueues) {
        allTaskQueues_ = allTaskQueues;
    }

    void start() {
        thread_ = std::thread(&CmdProcessor::eventLoop, this);
    }

    void stop() {
        dispatchToProcessorQueue_.push({Event::TODO_OTHERS, nullptr, []{}});
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    void eventLoop() {
        while (true) {
            bool hasWork = false;

            // Check dispatchToProcessorQueue
            if (!dispatchToProcessorQueue_.isEmpty()) {
                try {
                    std::unique_lock<std::mutex> lock(dispatchToProcessorQueue_.mutex_);
                    if (!dispatchToProcessorQueue_.isEmpty()) {
                        Event event = dispatchToProcessorQueue_.pop();
                        lock.unlock();
                        switch (event.type) {
                            case Event::TASK:
                                processActiveQueue(*event.activeQueue, taskQueue_);
                                hasWork = true;
                                break;
                            case Event::TODO_OTHERS:
                                event.callback();
                                return;
                        }
                    }
                } catch (const std::exception& e) {
                    // Handle exception if needed
                    std::cerr << "Exception in eventLoop: " << e.what() << std::endl;
                }
            }

            // Steal work if no work found in dispatchToProcessorQueue
            if (!hasWork) {
                hasWork = stealWork();
            }

            // Process tasks in taskQueue_ if work is still not found
            if (!hasWork && !taskQueue_.isEmpty()) {
                work();
                hasWork = true;
            }

            // If all queues and taskQueue_ are empty, wait
            if (!hasWork) {
                std::unique_lock<std::mutex> lock(globalMutex_);
                globalCondVar_.wait(lock, [this] { return !allQueuesEmpty() || !taskQueue_.isEmpty(); });
            }
        }
    }

    void processActiveQueue(CmdBuffer& activeQueue, TaskQueue& taskQueue) {
        if (activeQueue.isOrdered()) {
            processOrderedQueue(activeQueue, taskQueue);
        } else {
            processUnorderedQueue(activeQueue, taskQueue);
        }
    }

    void handleBarrier(BarrierType type, CmdBuffer& activeQueue, Barrier& barrier) {
        switch (type) {
            case RELEASE:
                activeQueue.setFenceValue(barrier.promise, barrier.groupId);
                break;
            case ACQUIRE:
                if (barrier.promise->get_future().wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                    // If the promise is already ready, resume immediately
                    TaskRunner::getInstance().resumeCmdBuffer(&activeQueue);
                } else {
                    TaskRunner::getInstance().pauseCmdBuffer(&activeQueue, barrier.promise);
                }
                break;
            case WAIT:
                auto waitPromise = std::make_shared<std::promise<bool>>();
                activeQueue.setFenceValue(waitPromise, 0); // Set the fence value with default group ID 0
                TaskRunner::getInstance().pauseCmdBuffer(&activeQueue, waitPromise);
                break;
            case GROUP:
                activeQueue.addPendingCmdCnt(barrier.groupId);
                break;
        }
    }

    void processOrderedQueue(CmdBuffer& activeQueue, TaskQueue& taskQueue) {
        while (true) {
            auto task = activeQueue.getTask();
            if (!task) break;
            activeQueue.removeTask();
            std::visit([&](auto& t) {
                if constexpr (std::is_same_v<decltype(t), Task>) {
                    auto wrappedTask = [task, &activeQueue] {
                        task();
                        activeQueue.decrementPendingCmdCnt();
                    };
                    taskQueue.push(wrappedTask);
                    activeQueue.incrementPendingCmdCnt();
                } else if constexpr (std::is_same_v<decltype(t), Barrier&>) {
                    handleBarrier(t.type, activeQueue, t);
                    // Stop processing this CmdBuffer after encountering an ACQUIRE or WAIT barrier
                    if (t.type == ACQUIRE || t.type == WAIT) {
                        return;
                    }
                } else if constexpr (std::is_same_v<decltype(t), CmdBuffer&>) {
                    TaskRunner::getInstance().addToActiveQueues(&t);
                }
            }, *task);
        }
    }

    void processUnorderedQueue(CmdBuffer& activeQueue, TaskQueue& taskQueue) {
        while (true) {
            auto task = activeQueue.getTask();
            if (!task) break;
            activeQueue.removeTask();
            std::visit([&](auto& t) {
                if constexpr (std::is_same_v<decltype(t), Task>) {
                    taskQueue.push(std::move(t));
                } else if constexpr (std::is_same_v<decltype(t), Barrier&>) {
                    handleBarrier(t.type, activeQueue, t);
                } else if constexpr (std::is_same_v<decltype(t), CmdBuffer&>) {
                    TaskRunner::getInstance().addToActiveQueues(&t);
                }
            }, *task);
        }
    }

    void work() {
        while (true) {
            if (taskQueue_.isEmpty()) {
                break;
            }
            std::unique_lock<std::mutex> lock(taskQueue_.mutex_);
            if (taskQueue_.isEmpty()) {
                lock.unlock();
                break;
            }
            auto task = taskQueue_.pop();
            lock.unlock();
            if (task.index() == 0) {
                std::get<Task>(task)();
            } else if (task.index() == 1) {
                auto& taskBuffer = std::get<TaskBuffer>(task);
                while (!taskBuffer.isEmpty()) {
                    auto task = taskBuffer.pop();
                    if (task) task();
                }
            } else {
                break;
            }
        }
    }

    bool allQueuesEmpty() const {
        return dispatchToProcessorQueue_.isEmpty();
    }

    bool stealWork() {
        for (auto& otherTaskQueue : allTaskQueues_) {
            if (&otherTaskQueue != &taskQueue_ && !otherTaskQueue->isEmpty()) {
                std::unique_lock<std::mutex> lock(otherTaskQueue->mutex_);
                if (!otherTaskQueue->queue_.empty()) {
                    auto task = otherTaskQueue->queue_.front();
                    otherTaskQueue->queue_.pop();
                    lock.unlock(); // Unlock before processing the task
                    taskQueue_.push(task);
                    return true;
                }
            }
        }
        return false;
    }

    TaskQueue& taskQueue_;
    EventQueue& dispatchToProcessorQueue_;
    std::vector<TaskQueue*> allTaskQueues_;
    std::thread thread_;
    mutable std::mutex mutex_;
    std::condition_variable cond_var_;
    EventQueue& processorToDispatchQueue_;
};

} // namespace async_task

#endif // CMD_PROCESSOR_H

