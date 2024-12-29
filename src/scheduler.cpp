#include "scheduler.h"
#include <iostream>

Scheduler::Scheduler(size_t num_queues, size_t num_workers)
    : num_queues_(num_queues), num_workers_(num_workers) {
    for (size_t i = 0; i < num_queues_; ++i) {
        queues_.push_back(std::make_unique<TaskQueue>());
    }
    for (size_t i = 0; i < num_workers_; ++i) {
        workers_.emplace_back(i, *this);
        workers_[i].start();
    }
}

Scheduler::~Scheduler() {
    for (auto& queue : queues_) {
        queue->stop();
    }
    for (auto& worker : workers_) {
        if (worker.thread_.joinable()) {
            worker.thread_.join();
        }
    }
}

TaskQueue& Scheduler::get_queue(size_t queue_id) {
    return *queues_[queue_id];
}

void Scheduler::assign_work_to_worker(size_t worker_id, size_t queue_id) {
    {
        std::lock_guard<std::mutex> lock(worker_assignment_mutex_);
        worker_assignments_[worker_id] = queue_id;
    }
    assignment_condition_.notify_one();
}

void Scheduler::worker_ready_for_work(size_t worker_id) {
    {
        std::lock_guard<std::mutex> lock(worker_ready_mutex_);
        ready_workers_.insert(worker_id);
    }
    ready_condition_.notify_one();
}

size_t Scheduler::get_next_ready_worker() {
    std::unique_lock<std::mutex> lock(worker_ready_mutex_);
    ready_condition_.wait(lock, [this] { return !ready_workers_.empty(); });
    size_t worker_id = *ready_workers_.begin();
    ready_workers_.erase(worker_id);
    return worker_id;
}

size_t Scheduler::get_assigned_queue(size_t worker_id) {
    std::lock_guard<std::mutex> lock(worker_assignment_mutex_);
    return worker_assignments_[worker_id];
}
