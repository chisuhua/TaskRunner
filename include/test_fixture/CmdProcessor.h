#ifndef CMD_PROCESSOR_H
#define CMD_PROCESSOR_H

#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <variant>
#include <functional>
#include <iostream>
#include "test_fixture/TaskQueue.h"
#include "test_fixture/EventQueue.h"
#include "test_fixture/CmdBuffer.h"
#include "test_fixture/Barrier.h"
// Forward declare TaskRunner to break circular dependency
// Full definition will be included in CmdProcessor.cpp
namespace async_task {
    class TaskRunner;
}

namespace async_task {

class CmdProcessor {
public:
    CmdProcessor(TaskQueue& taskQueue, EventQueue& dispatchToProcessorQueue, EventQueue& processorToDispatchQueue)
        : taskQueue_(taskQueue), dispatchToProcessorQueue_(dispatchToProcessorQueue), processorToDispatchQueue_(processorToDispatchQueue) {}

    void updateAllTaskQueues(const std::vector<TaskQueue*>& allTaskQueues);
    void start();
    void stop();

private:
    void eventLoop();
    void processActiveQueue(CmdBuffer& activeQueue, TaskQueue& taskQueue);
    void processOrderedQueue(CmdBuffer& activeQueue, TaskQueue& taskQueue);
    void processUnorderedQueue(CmdBuffer& activeQueue, TaskQueue& taskQueue);
    void handleBarrier(BarrierType type, CmdBuffer& activeQueue, Barrier& barrier);
    void work();
    bool allQueuesEmpty() const;
    bool stealWork();

public:
    TaskQueue& taskQueue_;
    EventQueue& dispatchToProcessorQueue_;
private:
    std::vector<TaskQueue*> allTaskQueues_;
    std::thread thread_;
    mutable std::mutex mutex_;
    std::condition_variable cond_var_;
    EventQueue& processorToDispatchQueue_;
};

} // namespace async_task

#endif // CMD_PROCESSOR_H

