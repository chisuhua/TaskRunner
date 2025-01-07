main.cpp

#include "TaskRunner.hpp"
#include <iostream>
#include <thread>
#include <future>

namespace async_task_system {

void threadFunction(int id) {
    TaskRunner& taskRunner = TaskRunner::getInstance();

    // Launch a function that returns an integer via CmdStream
    auto futureIntFromStream = taskRunner.launch([id]() {
        std::cout << "Task returning int from CmdStream in Thread " << id << "\n";
        return 42 + id;
    });

    // Launch a function that returns void via CmdStream
    auto futureVoidFromStream = taskRunner.launch([id]() {
        std::cout << "Task returning void from CmdStream in Thread " << id << "\n";
    });

    // Create a CmdBuffer using TaskRunner's allocateCmdBuffer and launch it
    CmdBuffer cmdBuffer = taskRunner.allocateCmdBuffer(true); // Ordered CmdBuffer
    cmdBuffer.emplace([id]() {
        std::cout << "Task from CmdBuffer in Thread " << id << "\n";
    });

    // Add a RELEASE barrier
    Barrier barrierRelease = taskRunner.allocateBarrier(RELEASE);
    cmdBuffer.emplace(barrierRelease);

    // Add another task
    cmdBuffer.emplace([id]() {
        std::cout << "Task after RELEASE barrier in Thread " << id << "\n";
    });

    // Launch the CmdBuffer
    taskRunner.launch(cmdBuffer);

    // Create another CmdBuffer using TaskRunner's allocateCmdBuffer and launch it
    CmdBuffer cmdBuffer2 = taskRunner.allocateCmdBuffer(true); // Ordered CmdBuffer
    cmdBuffer2.emplace([id]() {
        std::cout << "Task from second CmdBuffer in Thread " << id << "\n";
    });

    // Add an ACQUIRE barrier
    Barrier barrierAcquire = taskRunner.allocateBarrier(ACQUIRE);
    cmdBuffer2.emplace(barrierAcquire);

    // Add another task
    cmdBuffer2.emplace([id]() {
        std::cout << "Task after ACQUIRE barrier in Thread " << id << "\n";
    });

    // Launch the second CmdBuffer
    taskRunner.launch(cmdBuffer2);

    // Create another CmdBuffer using TaskRunner's allocateCmdBuffer and launch it
    CmdBuffer unorderedCmdBuffer = taskRunner.allocateCmdBuffer(false); // Unordered CmdBuffer
    unorderedCmdBuffer.emplace([id]() {
        std::cout << "Unordered Task from CmdBuffer in Thread " << id << "\n";
    });

    // Add a WAIT barrier
    Barrier barrierWait = taskRunner.allocateBarrier(WAIT);
    unorderedCmdBuffer.emplace(barrierWait);

    // Add another task
    unorderedCmdBuffer.emplace([id]() {
        std::cout << "Task after WAIT barrier in Thread " << id << "\n";
    });

    // Launch the unordered CmdBuffer
    taskRunner.launch(unorderedCmdBuffer);

    // Wait for results from CmdStream
    std::cout << "Future int result from CmdStream in Thread " << id << ": " << futureIntFromStream.get() << "\n";
    futureVoidFromStream.wait();
}

} // namespace async_task_system

int main() {
    async_task_system::threadFunction(1);
    return 0;
}

