#pragma once

#include <vector>
#include <memory>
#include <unordered_map>
#include <set>
#include <mutex>
#include <condition_variable>
#include "taskqueue.h"
#include "worker.h"

class Scheduler {

public:
    Scheduler(size_t num_queues, size_t num_workers);

    ~Scheduler();

    TaskQueue& get_queue(size_t queue_id);

    void assign_work_to_worker(size_t worker_id, size_t queue_id);

    void worker_ready_for_work(size_t worker_id);

    size_t get_next_ready_worker();

    size_t get_assigned_queue(size_t worker_id);

private:
    size_t num_queues_;
    size_t num_workers_;
    std::vector<std::unique_ptr<TaskQueue>> queues_;
    std::vector<Worker> workers_;
    std::mutex worker_assignment_mutex_;
    std::condition_variable assignment_condition_;
    std::unordered_map<size_t, size_t> worker_assignments_;
    std::mutex worker_ready_mutex_;
    std::condition_variable ready_condition_;
    std::set<size_t> ready_workers_;
};
