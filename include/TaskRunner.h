#ifndef TASK_RUNNER_H
#define TASK_RUNNER_H

#include "CmdBuffer.h"
#include "CmdProcessor.h"
#include "CmdStream.h"
#include "EventQueue.h"
#include "TaskQueue.h"
#include "TaskBuffer.h"
#include "Barrier.h"

#include <memory>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <map>
#include <functional>
#include <future>

// Define doctest for unit testing
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

namespace async_task {

class TaskRunner {
public:
    static TaskRunner& getInstance();
    template<typename R>
    Future<R> launch(std::function<R()> task);
    void launch(Task task);
    void launch(CmdBuffer cmdBuffer);
    void launch(Barrier& barrier);
    Barrier allocateBarrier(BarrierType type, int groupId = 0);
    Barrier allocateBarrier(BarrierType type, std::shared_ptr<std::promise<bool>> promise, int groupId = 0);
    void addCmdStream(CmdStream& cmdStream);

private:
    TaskRunner();
    ~TaskRunner();
    void initialize();
    CmdBuffer allocateCmdBuffer(bool isOrdered);
    void ensureCmdStream();
    void pauseCmdBuffer(CmdBuffer* cmdBuffer, std::shared_ptr<std::promise<bool>> promise = nullptr);
    void resumeCmdBuffer(CmdBuffer* cmdBuffer);
    void checkFenceValue();
    void addToActiveQueues(CmdBuffer* cmdBuffer);

    // Dispatch loop related
    void dispatchLoop();
    void handleEvent(Event event);

    std::vector<std::unique_ptr<CmdProcessor>> cmdProcessors_;
    std::queue<CmdBuffer> idleBuffers_;
    mutable std::mutex mutex_;

    std::vector<CmdBuffer*> activeQueues_;
    std::vector<CmdBuffer*> pauseQueues_;

    // Global condition variable and mutex
    std::condition_variable globalCondVar_;
    mutable std::mutex globalMutex_;

    // Make cmdStream_ thread_local
    thread_local static std::unique_ptr<CmdStream> cmdStream_;

    std::vector<TaskQueue*> allTaskQueues_; // Store all TaskQueues for stealing
    int nextGroupId_ = 1; // Next available group ID

    // Event queues for inter-processor communication
    std::vector<EventQueue> dispatchToProcessorQueues_;
    std::vector<EventQueue> processorToDispatchQueues_;

    // Thread for dispatch loop
    std::thread dispatchThread_;

    // Vector of CmdStream EventQueues
    std::vector<EventQueue*> cmdStreamEventQueues_;
};

template<typename R>
class Future {
public:
    Future(std::future<R> future, CmdBuffer* cmdBuffer) : future_(std::move(future)), cmdBuffer_(cmdBuffer) {}

    R get() {
        if (!cmdBuffer_->isLaunched_) {
            cmdBuffer_->launch();
        }
        return future_.get();
    }

    void wait() {
        if (!cmdBuffer_->isLaunched_) {
            cmdBuffer_->launch();
        }
        future_.wait();
    }

private:
    std::future<R> future_;
    CmdBuffer* cmdBuffer_;
};

} // namespace async_task

// Implementations

namespace async_task {

TaskRunner& TaskRunner::getInstance() {
    static TaskRunner instance;
    return instance;
}

template<typename R>
Future<R> TaskRunner::launch(std::function<R()> task) {
    ensureCmdStream();
    auto future = cmdStream_->launch(task);
    return Future<R>(std::move(future), cmdStream_->cmdBuffer_.get());
}

void TaskRunner::launch(Task task) {
    ensureCmdStream();
    cmdStream_->launch(std::move(task));
}

void TaskRunner::launch(CmdBuffer cmdBuffer) {
    ensureCmdStream();
    cmdStream_->launch(std::move(cmdBuffer));
}

void TaskRunner::launch(Barrier& barrier) {
    ensureCmdStream();
    cmdStream_->launch(barrier);
}

Barrier TaskRunner::allocateBarrier(BarrierType type, int groupId = 0) {
    if (type == GROUP) {
        groupId = nextGroupId_++;
    }
    return {type, nullptr, groupId};
}

Barrier TaskRunner::allocateBarrier(BarrierType type, std::shared_ptr<std::promise<bool>> promise, int groupId = 0) {
    if (type == ACQUIRE) {
        if (!promise) {
            throw std::invalid_argument("ACQUIRE barrier must have a valid promise");
        }
    }
    return {type, promise, groupId};
}

void TaskRunner::addCmdStream(CmdStream& cmdStream) {
    std::unique_lock<std::mutex> lock(mutex_);
    cmdStreamEventQueues_.push_back(&cmdStream.eventQueue_);
}

TaskRunner::TaskRunner() {
    initialize();
}

TaskRunner::~TaskRunner() {
    dispatchThread_.join();
    for (auto& processor : cmdProcessors_) {
        processor->stop();
    }
}

void TaskRunner::initialize() {
    size_t numProcessors = std::thread::hardware_concurrency();
    for (size_t i = 0; i < numProcessors; ++i) {
        TaskQueue taskQueue;
        dispatchToProcessorQueues_.emplace_back();
        processorToDispatchQueues_.emplace_back();
        cmdProcessors_.emplace_back(std::make_unique<CmdProcessor>(taskQueue, dispatchToProcessorQueues_[i], processorToDispatchQueues_[i]));
    }

    // Collect all taskQueues after all CmdProcessors are created
    for (auto& processor : cmdProcessors_) {
        allTaskQueues_.push_back(&processor->taskQueue_);
    }

    // Update all CmdProcessors with the collected taskQueues
    for (auto& processor : cmdProcessors_) {
        processor->updateAllTaskQueues(allTaskQueues_);
    }

    // Start all CmdProcessors after updating all taskQueues
    for (auto& processor : cmdProcessors_) {
        processor->start();
    }

    // Start dispatch loop
    dispatchThread_ = std::thread(&TaskRunner::dispatchLoop, this);
}

CmdBuffer TaskRunner::allocateCmdBuffer(bool isOrdered) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!idleBuffers_.empty()) {
        auto buffer = std::move(idleBuffers_.front());
        idleBuffers_.pop();
        buffer->setOrdered(isOrdered);
        return buffer;
    }
    return std::make_unique<CmdBuffer>(isOrdered);
}

void TaskRunner::ensureCmdStream() {
    if (!cmdStream_) {
        cmdStream_ = std::make_unique<CmdStream>(false);
        addCmdStream(*cmdStream_);
        activeQueues_.push_back(cmdStream_->cmdBuffer_.get());
    }
}

void TaskRunner::pauseCmdBuffer(CmdBuffer* cmdBuffer, std::shared_ptr<std::promise<bool>> promise) {
    std::unique_lock<std::mutex> lock(mutex_);
    activeQueues_.erase(std::remove(activeQueues_.begin(), activeQueues_.end(), cmdBuffer), activeQueues_.end());
    pauseQueues_.push_back(cmdBuffer);
    if (promise) {
        cmdBuffer->setWaitingPromise(promise); // Set the waiting promise
    }
}

void TaskRunner::resumeCmdBuffer(CmdBuffer* cmdBuffer) {
    std::unique_lock<std::mutex> lock(mutex_);
    pauseQueues_.erase(std::remove(pauseQueues_.begin(), pauseQueues_.end(), cmdBuffer), pauseQueues_.end());
    activeQueues_.push_back(cmdBuffer);
}

void TaskRunner::checkFenceValue() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (auto* cmdBuffer : pauseQueues_) {
        auto waitingPromise = cmdBuffer->getWaitingPromise();
        if (waitingPromise && waitingPromise->get_future().wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            resumeCmdBuffer(cmdBuffer);
        }
    }
}

void TaskRunner::addToActiveQueues(CmdBuffer* cmdBuffer) {
    std::unique_lock<std::mutex> lock(mutex_);
    activeQueues_.push_back(cmdBuffer);
}

// Initialize the thread_local variable
thread_local std::unique_ptr<CmdStream> TaskRunner::cmdStream_ = nullptr;

// Dispatch loop
void TaskRunner::dispatchLoop() {
    size_t numProcessors = cmdProcessors_.size();
    size_t currentProcessorIndex = 0;

    while (true) {
        bool hasWork = false;

        // Check CmdStream EventQueues
        for (auto& eventQueue : cmdStreamEventQueues_) {
            if (!eventQueue->isEmpty()) {
                try {
                    std::unique_lock<std::mutex> lock(eventQueue->mutex_);
                    if (!eventQueue->isEmpty()) {
                        Event event = eventQueue->pop();
                        lock.unlock();
                        handleEvent(event);
                        hasWork = true;
                    }
                } catch (const std::exception& e) {
                    // Handle exception if needed
                    std::cerr << "Exception in dispatchLoop: " << e.what() << std::endl;
                }
            }
        }

        // Check processor-to-dispatch EventQueues
        for (auto& queue : processorToDispatchQueues_) {
            if (!queue.isEmpty()) {
                try {
                    std::unique_lock<std::mutex> lock(queue.mutex_);
                    if (!queue.isEmpty()) {
                        Event event = queue.pop();
                        lock.unlock();
                        event.callback();
                        hasWork = true;
                    }
                } catch (const std::exception& e) {
                    // Handle exception if needed
                    std::cerr << "Exception in dispatchLoop: " << e.what() << std::endl;
                }
            }
        }

        // If no work, wait
        if (!hasWork) {
            std::unique_lock<std::mutex> lock(globalMutex_);
            globalCondVar_.wait(lock, [this] { return !allQueuesEmpty(); });
        }

        // Rotate to the next processor
        currentProcessorIndex = (currentProcessorIndex + 1) % numProcessors;
    }
}

// Handle events in the dispatch loop
void TaskRunner::handleEvent(Event event) {
    switch (event.type) {
        case Event::TASK:
            dispatchToProcessorQueues_[currentProcessorIndex].push(event);
            break;
        case Event::TODO_OTHERS:
            event.callback();
            break;
    }
}

bool TaskRunner::allQueuesEmpty() const {
    for (auto& queue : dispatchToProcessorQueues_) {
        if (!queue.isEmpty()) return false;
    }
    for (auto& queue : processorToDispatchQueues_) {
        if (!queue.isEmpty()) return false;
    }
    for (auto& queue : cmdStreamEventQueues_) {
        if (!queue->isEmpty()) return false;
    }
    return true;
}

} // namespace async_task

// Unit tests with doctest
TEST_CASE("Test TaskRunner") {
    async_task::TaskRunner& taskRunner = async_task::TaskRunner::getInstance();

    // Test launching a simple task
    auto futureInt = taskRunner.launch([]() {
        return 42;
    });
    CHECK(futureInt.get() == 42);

    // Test launching a task with a RELEASE barrier
    async_task::CmdBuffer cmdBuffer = taskRunner.allocateCmdBuffer(true);
    cmdBuffer.emplace([]() {
        std::cout << "Task before RELEASE barrier\n";
    });

    auto barrierGroup = taskRunner.allocateBarrier(async_task::GROUP);
    cmdBuffer.emplace(barrierGroup);

    auto barrierRelease = taskRunner.allocateBarrier(async_task::RELEASE, barrierGroup.groupId);
    cmdBuffer.emplace(barrierRelease);

    cmdBuffer.emplace([]() {
        std::cout << "Task after RELEASE barrier\n";
    });

    taskRunner.launch(cmdBuffer);

    // Test launching a task with an ACQUIRE barrier
    async_task::CmdBuffer cmdBuffer2 = taskRunner.allocateCmdBuffer(true);
    cmdBuffer2.emplace([]() {
        std::cout << "Task before ACQUIRE barrier\n";
    });

    auto barrierAcquire = taskRunner.allocateBarrier(async_task::ACQUIRE, barrierRelease.promise);
    cmdBuffer2.emplace(barrierAcquire);

    cmdBuffer2.emplace([]() {
        std::cout << "Task after ACQUIRE barrier\n";
    });

    taskRunner.launch(cmdBuffer2);

    // Test launching a task with a WAIT barrier
    async_task::CmdBuffer unorderedCmdBuffer = taskRunner.allocateCmdBuffer(false);
    unorderedCmdBuffer.emplace([]() {
        std::cout << "Unordered Task from CmdBuffer in Thread\n";
    });

    auto barrierWait = taskRunner.allocateBarrier(async_task::WAIT);
    unorderedCmdBuffer.emplace(barrierWait);

    unorderedCmdBuffer.emplace([]() {
        std::cout << "Task after WAIT barrier in Thread\n";
    });

    taskRunner.launch(unorderedCmdBuffer);

    // Test task stealing
    SUBCASE("Test Task Stealing") {
        // Launch tasks in one CmdBuffer
        async_task::CmdBuffer cmdBufferSteal = taskRunner.allocateCmdBuffer(true);
        for (int i = 0; i < 10; ++i) {
            cmdBufferSteal.emplace([i]() {
                std::cout << "Task " << i << " in CmdBuffer for stealing\n";
            });
        }
        taskRunner.launch(cmdBufferSteal);

        // Launch a new CmdBuffer with fewer tasks to ensure stealing occurs
        async_task::CmdBuffer cmdBufferSteal2 = taskRunner.allocateCmdBuffer(true);
        cmdBufferSteal2.emplace([]() {
            std::cout << "Task in CmdBuffer for stealing 2\n";
        });

        taskRunner.launch(cmdBufferSteal2);

        // Wait for all tasks to complete
        for (int i = 0; i < 10; ++i) {
            cmdBufferSteal.getFuture().wait();
        }
        cmdBufferSteal2.getFuture().wait();
    }
}

#endif // TASK_RUNNER_H

