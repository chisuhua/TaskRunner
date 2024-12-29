#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "scheduler.h"
#include "taskqueue.h"
#include <future>
#include <thread>

TEST_CASE("Test TaskQueue Enqueue and Dequeue") {
    TaskQueue queue;
    int result = 0;

    auto future_result = queue.enqueue([&result]() {
        result = 42;
    });

    std::function<void()> task;
    CHECK(queue.try_dequeue_task(task));
    task();

    CHECK(result == 42);
}

TEST_CASE("Test Barrier Arrive and Wait") {
    TaskQueue queue;
    Barrier barrier(2, 2);

    std::promise<void> promise1;
    std::promise<void> promise2;

    queue.enqueue([&barrier, &promise1]() {
        barrier.arrive(0);
        promise1.set_value();
    });

    queue.enqueue([&barrier, &promise2]() {
        barrier.arrive(0);
        promise2.set_value();
    });

    promise1.get_future().wait();
    promise2.get_future().wait();

    std::promise<void> consumer_promise1;
    std::promise<void> consumer_promise2;

    queue.enqueue([&barrier, &consumer_promise1]() {
        barrier.wait(0);
        consumer_promise1.set_value();
    });

    queue.enqueue([&barrier, &consumer_promise2]() {
        barrier.wait(0);
        consumer_promise2.set_value();
    });

    consumer_promise1.get_future().wait();
    consumer_promise2.get_future().wait();
}

TEST_CASE("Test Scheduler Assignment") {
    Scheduler scheduler(2, 2);

    std::promise<void> producer_promise1;
    std::promise<void> producer_promise2;

    scheduler.get_queue(0).enqueue([&producer_promise1]() {
        producer_promise1.set_value();
    });

    scheduler.get_queue(0).enqueue([&producer_promise2]() {
        producer_promise2.set_value();
    });

    scheduler.assign_work_to_worker(0, 0);
    scheduler.assign_work_to_worker(1, 0);

    producer_promise1.get_future().wait();
    producer_promise2.get_future().wait();
}
