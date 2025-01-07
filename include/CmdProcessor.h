CmdProcessor.hpp

#ifndef CMD_PROCESSOR_HPP
#define CMD_PROCESSOR_HPP

#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <variant>
#include <functional>
#include "TaskQueue.hpp"
#include "EventQueue.hpp"
#include "CmdBuffer.hpp”
#include "Barrier.hpp"
#include "TaskRunner.hpp"

namespace async_task_system {

class CmdProcessor {
public:
    CmdProcessor(TaskQueue& taskQueue, std::vector<EventQueue*>& eventQueues)
        : taskQueue_(taskQueue), eventQueues_(eventQueues) {}

    void start() {
        thread_ = std::thread(&CmdProcessor::eventLoop, this);
    }

    void stop() {
        for (auto& eventQueue : eventQueues_) {
            eventQueue->push({Event::TODO_OTHERS, nullptr, []{}});
        }
        if (thread_.joinable()) {
            thread_.join();
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
                activeQueue.setFenceValue(true, barrier.promise);
                TaskRunner::getInstance().setFencePromise(&activeQueue, barrier.promise);
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
                TaskRunner::getInstance().pauseCmdBuffer(&activeQueue);
                break;
        }
    }

private:
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

    void eventLoop() {
        while (true) {
            bool hasWork = false;
            for (auto& eventQueue : eventQueues_) {
                if (!eventQueue->isEmpty()) {
                    Event event = eventQueue->pop();
                    switch (event.type) {
                        case Event::TASK:
                            processActiveQueue(*event.activeQueue, taskQueue_);
                            hasWork = true;
                            break;
                        case Event::TODO_OTHERS:
                            return;
                    }
                }
            }

            if (!hasWork) {
                work();
            }

            if (allQueuesEmpty()) {
                std::unique_lock<std::mutex> lock(mutex_);
                cond_var_.wait(lock, [this] { return !allQueuesEmpty(); });
            }
        }
    }

    void work() {
        while (true) {
            auto task = taskQueue_.pop();
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
        for (auto& eventQueue : eventQueues_) {
            if (!eventQueue->isEmpty()) return false;
        }
        return taskQueue_.isEmpty();
    }

    TaskQueue taskQueue_;
    std::vector<EventQueue*> eventQueues_;
    std::thread thread_;
    mutable std::mutex mutex_;
    std::condition_variable cond_var_;
};

} // namespace async_task_system

#endif // CMD_PROCESSOR_HPP

