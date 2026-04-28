#define DOCTEST_CONFIG_IMPLEMENT
#include "TaskRunner.h"
#include <iostream>
#include <thread>
#include <future>

namespace async_task {

void threadFunction(int id) {
    TaskRunner& taskRunner = TaskRunner::getInstance();

    // Launch a function that returns an integer via CmdStream
    auto futureIntFromStream = taskRunner.launch<int>([id]() -> int {
        std::cout << "Task returning int from CmdStream in Thread " << id << "\n";
        return 42 + id;
    });

    // Launch a function that returns void via CmdStream
    taskRunner.launch([id]() {
        std::cout << "Task returning void from CmdStream in Thread " << id << "\n";
    });

    // Create a CmdBuffer using TaskRunner's allocateCmdBuffer and launch it
    auto cmdBuffer = taskRunner.allocateCmdBuffer(true); // Ordered CmdBuffer, returns unique_ptr
    auto futureIntFromCmdBuffer = cmdBuffer->emplace<int>([id]() {
        std::cout << "Task from CmdBuffer in Thread " << id << "\n";
        return 42 + id;
    });

    // Add a GROUP barrier
    auto barrierGroup = taskRunner.allocateBarrier(GROUP);
    cmdBuffer->emplace(barrierGroup);

    // Add a RELEASE barrier
    auto barrierRelease = taskRunner.allocateBarrier(RELEASE, barrierGroup.groupId);
    cmdBuffer->emplace(barrierRelease);

    // Add another task
    cmdBuffer->emplace([id]() {
        std::cout << "Task after RELEASE barrier in Thread " << id << "\n";
    });

    // Launch the CmdBuffer
    cmdBuffer->launch();

    // Create another CmdBuffer using TaskRunner's allocateCmdBuffer and launch it
    auto cmdBuffer2 = taskRunner.allocateCmdBuffer(true); // Ordered CmdBuffer, returns unique_ptr
    cmdBuffer2->emplace([id]() {
        std::cout << "Task from second CmdBuffer in Thread " << id << "\n";
    });

    // Add an ACQUIRE barrier
    auto barrierAcquire = taskRunner.allocateBarrier(ACQUIRE, barrierRelease.promise);
    cmdBuffer2->emplace(barrierAcquire);

    // Add another task
    cmdBuffer2->emplace([id]() {
        std::cout << "Task after ACQUIRE barrier in Thread " << id << "\n";
    });

    // Launch the second CmdBuffer
    cmdBuffer2->launch();

    // Create another CmdBuffer using TaskRunner's allocateCmdBuffer and launch it
    auto unorderedCmdBuffer = taskRunner.allocateCmdBuffer(false); // Unordered CmdBuffer, returns unique_ptr
    unorderedCmdBuffer->emplace([id]() {
        std::cout << "Unordered Task from CmdBuffer in Thread\n";
    });

    // Add a WAIT barrier
    auto barrierWait = taskRunner.allocateBarrier(WAIT);
    unorderedCmdBuffer->emplace(barrierWait);

    // Add another task
    unorderedCmdBuffer->emplace([id]() {
        std::cout << "Task after WAIT barrier in Thread\n";
    });

    // Launch the unordered CmdBuffer
    unorderedCmdBuffer->launch();

    // Wait for results from CmdStream
    std::cout << "Future int result from CmdStream in Thread " << id << ": " << futureIntFromStream.get() << "\n";

    // Wait for results from CmdBuffer
    std::cout << "Future int result from CmdBuffer in Thread " << id << ": " << futureIntFromCmdBuffer.get() << "\n";
}

} // namespace async_task

// DOCTEST main implementation
DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

// Forward declare threadFunction to use it in main tests
namespace async_task {
    void threadFunction(int id);
}

// Wrapper function for CLI main to call
int test_main(int argc, char* argv[]) {
    return doctest::Context(argc, argv).run();
}

