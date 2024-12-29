#include "scheduler.h"
#include "taskqueue.h"
#include <iostream>
#include <thread>

int main() {
    Scheduler scheduler(2, 2); // 2 queues, 2 workers

    auto barrier_arrive_func = [](TaskQueue& queue, size_t bar_id) {
        return [&queue, bar_id]() { queue.barrier_arrive(bar_id); };
    };

    auto barrier_wait_func = [](TaskQueue& queue, size_t bar_id) {
        return [&queue, bar_id]() { queue.barrier_wait(bar_id); };
    };

    // Assign initial tasks and barriers
    scheduler.get_queue(0).enqueue([]{ std::cout << "Producer Task 1 executed\n"; });
    scheduler.get_queue(0).enqueue(barrier_arrive_func(scheduler.get_queue(0), 0));
    scheduler.get_queue(0).enqueue([]{ std::cout << "Producer Task 2 executed\n"; });
    scheduler.get_queue(0).enqueue(barrier_arrive_func(scheduler.get_queue(0), 1));

    scheduler.get_queue(1).enqueue(barrier_wait_func(scheduler.get_queue(1), 0));
    scheduler.get_queue(1).enqueue([]{ std::cout << "Consumer Task 1 executed after barrier 0\n"; });
    scheduler.get_queue(1).enqueue(barrier_wait_func(scheduler.get_queue(1), 1));
    scheduler.get_queue(1).enqueue([]{ std::cout << "Consumer Task 2 executed after barrier 1\n"; });

    // Initial assignments
    scheduler.assign_work_to_worker(0, 0);
    scheduler.assign_work_to_worker(1, 1);

    // Simulate some scheduling decisions
    std::this_thread::sleep_for(std::chrono::seconds(1));
    scheduler.assign_work_to_worker(0, 1);
    scheduler.assign_work_to_worker(1, 0);

    // Let the system run for a while
    std::this_thread::sleep_for(std::chrono::seconds(5));

    return 0;
}
