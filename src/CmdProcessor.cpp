#include "CmdProcessor.h"
#include "TaskRunner.h"

namespace async_task {

void CmdProcessor::eventLoop() {
    while (true) {
        bool hasWork = false;

        // Check dispatchToProcessorQueue
        if (!dispatchToProcessorQueue_.isEmpty()) {
            try {
                std::unique_lock<std::mutex> lock(dispatchToProcessorQueue_.getMutex());
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
            std::unique_lock<std::mutex> lock(TaskRunner::getInstance().globalMutex_);
            TaskRunner::getInstance().globalCondVar_.wait(lock, [this] { return !allQueuesEmpty() || !taskQueue_.isEmpty(); });
        }
    }
}

void CmdProcessor::processActiveQueue(CmdBuffer& activeQueue, TaskQueue& taskQueue) {
    if (activeQueue.isOrdered()) {
        processOrderedQueue(activeQueue, taskQueue);
    } else {
        processUnorderedQueue(activeQueue, taskQueue);
    }
}

void CmdProcessor::handleBarrier(BarrierType type, CmdBuffer& activeQueue, Barrier& barrier) {
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
        case WAIT: {
            auto waitPromise = std::make_shared<std::promise<bool>>();
            activeQueue.setFenceValue(waitPromise, 0); // Set the fence value with default group ID 0
            TaskRunner::getInstance().pauseCmdBuffer(&activeQueue, waitPromise);
            break;
        }
        case GROUP:
            activeQueue.addPendingCmdCnt(barrier.groupId);
            break;
    }
}

void CmdProcessor::processOrderedQueue(CmdBuffer& activeQueue, TaskQueue& taskQueue) {
    while (true) {
        auto taskOpt = activeQueue.getTask();
        if (!taskOpt) break;
        activeQueue.removeTask();
        std::visit([&](auto& t) {
            if constexpr (std::is_same_v<decltype(t), Task>) {
                auto wrappedTask = [&t, &activeQueue] {
                    t();
                    activeQueue.decrementPendingCmdCnt();
                };
                taskQueue.push(wrappedTask);
                activeQueue.incrementPendingCmdCnt();
            } else if constexpr (std::is_same_v<decltype(t), Barrier>) {
                handleBarrier(t.type, activeQueue, t);
                // Stop processing this CmdBuffer after encountering an ACQUIRE or WAIT barrier
                if (t.type == ACQUIRE || t.type == WAIT) {
                    return;
                }
            } else if constexpr (std::is_same_v<decltype(t), std::reference_wrapper<CmdBuffer>>) {
                TaskRunner::getInstance().addToActiveQueues(&t.get());
            }
        }, *taskOpt);
    }
}

void CmdProcessor::processUnorderedQueue(CmdBuffer& activeQueue, TaskQueue& taskQueue) {
    while (true) {
        auto taskOpt = activeQueue.getTask();
        if (!taskOpt) break;
        activeQueue.removeTask();
        std::visit([&](auto& t) {
            if constexpr (std::is_same_v<decltype(t), Task>) {
                taskQueue.push(std::move(t));
            } else if constexpr (std::is_same_v<decltype(t), Barrier>) {
                handleBarrier(t.type, activeQueue, t);
            } else if constexpr (std::is_same_v<decltype(t), std::reference_wrapper<CmdBuffer>>) {
                TaskRunner::getInstance().addToActiveQueues(&t.get());
            }
        }, *taskOpt);
    }
}

void CmdProcessor::work() {
    while (true) {
        if (taskQueue_.isEmpty()) {
            break;
        }
        std::unique_lock<std::mutex> lock(taskQueue_.getMutex());
        if (taskQueue_.isEmpty()) {
            lock.unlock();
            break;
        }
        auto taskOpt = taskQueue_.pop();
        lock.unlock();
        if (!taskOpt) continue;
        
        std::visit([&](auto& task) {
            using T = std::decay_t<decltype(task)>;
            if constexpr (std::is_same_v<T, Task>) {
                task();
            } else if constexpr (std::is_same_v<T, TaskBuffer>) {
                auto& taskBuffer = task;
                while (!taskBuffer.empty()) {
                    auto t = std::move(taskBuffer.front());
                    taskBuffer.pop_front();
                    if (t) t();
                }
            }
        }, *taskOpt);
    }
}

bool CmdProcessor::allQueuesEmpty() const {
    return dispatchToProcessorQueue_.isEmpty();
}

bool CmdProcessor::stealWork() {
    for (auto& otherTaskQueue : allTaskQueues_) {
        if (otherTaskQueue != &taskQueue_ && !otherTaskQueue->isEmptyInternal()) {
            std::unique_lock<std::mutex> lock(otherTaskQueue->getMutex());
            if (!otherTaskQueue->getQueue().empty()) {
                auto task = std::move(otherTaskQueue->getQueue().front());
                otherTaskQueue->getQueue().pop();
                lock.unlock(); // Unlock before processing the task
                std::visit([&taskQueue=this->taskQueue_](auto& t) {
                    taskQueue.push(std::move(t));
                }, task);
                return true;
            }
        }
    }
    return false;
}

void CmdProcessor::updateAllTaskQueues(const std::vector<TaskQueue*>& allTaskQueues) {
    allTaskQueues_ = allTaskQueues;
}

void CmdProcessor::start() {
    thread_ = std::thread(&CmdProcessor::eventLoop, this);
}

void CmdProcessor::stop() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

} // namespace async_task
