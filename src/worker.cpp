#include "worker.h"
#include <iostream>

Worker::Worker(size_t id, Scheduler& scheduler)
    : id_(id), scheduler_(scheduler) {}

void Worker::start() {
    thread_ = std::thread(&Worker::operator(), this);
}

void Worker::operator()() {
    while (true) {
        // Get the assigned queue ID from the scheduler
        size_t queue_id = scheduler_.get_assigned_queue(id_);

        // Try to dequeue a task from the assigned queue
        std::function<void()> task;
        if (scheduler_.get_queue(queue_id).try_dequeue_task(task)) {
            task();
        } else {
            // Notify the scheduler that the worker is ready for new work
            scheduler_.worker_ready_for_work(id_);
        }

        // Check if the worker needs to pause due to barrier conditions
        {
            std::unique_lock<std::mutex> lock(scheduler_.get_queue(queue_id).pause_mutex_);
            while (scheduler_.get_queue(queue_id).is_paused()) {
                scheduler_.get_queue(queue_id).pause_condition_.wait(lock);
            }
        }
    }
}
