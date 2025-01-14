#include "TaskRunner.h"
#include <iostream>
#include <thread>
#include <future>

namespace async_task {

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
    auto futureIntFromCmdBuffer = cmdBuffer.emplace<int>([id]() {
        std::cout << "Task from CmdBuffer in Thread " << id << "\n";
        return 42 + id;
    });

    // Add a GROUP barrier
    auto barrierGroup = taskRunner.allocateBarrier(GROUP);
    cmdBuffer.emplace(barrierGroup);

    // Add a RELEASE barrier
    auto barrierRelease = taskRunner.allocateBarrier(RELEASE, barrierGroup.groupId);
    cmdBuffer.emplace(barrierRelease);

    // Add another task
    cmdBuffer.emplace([id]() {
        std::cout << "Task after RELEASE barrier in Thread " << id << "\n";
    });

    // Launch the CmdBuffer
    cmdBuffer.launch();

    // Create another CmdBuffer using TaskRunner's allocateCmdBuffer and launch it
    CmdBuffer cmdBuffer2 = taskRunner.allocateCmdBuffer(true); // Ordered CmdBuffer
    cmdBuffer2.emplace([id]() {
        std::cout << "Task from second CmdBuffer in Thread " << id << "\n";
    });

    // Add an ACQUIRE barrier
    auto barrierAcquire = taskRunner.allocateBarrier(ACQUIRE, barrierRelease.promise);
    cmdBuffer2.emplace(barrierAcquire);

    // Add another task
    cmdBuffer2.emplace([id]() {
        std::cout << "Task after ACQUIRE barrier in Thread " << id << "\n";
    });

    // Launch the second CmdBuffer
    cmdBuffer2.launch();

    // Create another CmdBuffer using TaskRunner's allocateCmdBuffer and launch it
    CmdBuffer unorderedCmdBuffer = taskRunner.allocateCmdBuffer(false); // Unordered CmdBuffer
    unorderedCmdBuffer.emplace([id]() {
        std::cout << "Unordered Task from CmdBuffer in Thread\n";
    });

    // Add a WAIT barrier
    auto barrierWait = taskRunner.allocateBarrier(WAIT);
    unorderedCmdBuffer.emplace(barrierWait);

    // Add another task
    unorderedCmdBuffer.emplace([id]() {
        std::cout << "Task after WAIT barrier in Thread\n";
    });

    // Launch the unordered CmdBuffer
    unorderedCmdBuffer.launch();

    // Wait for results from CmdStream
    std::cout << "Future int result from CmdStream in Thread " << id << ": " << futureIntFromStream.get() << "\n";
    futureVoidFromStream.wait();

    // Wait for results from CmdBuffer
    std::cout << "Future int result from CmdBuffer in Thread " << id << ": " << futureIntFromCmdBuffer.get() << "\n";
}

} // namespace async_task

int main() {
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; ++i) {
        threads.emplace_back(async_task::threadFunction, i);
    }

    for (auto& thread : threads) {
        thread.join();
    }

    return 0;
}

