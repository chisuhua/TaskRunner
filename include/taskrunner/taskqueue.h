#pragma once

#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <future>
#include <vector>
#include <atomic>
#include <set>
#include <map>
#include "barrier.h"


class Worker;

class TaskQueue {
public:
    TaskQueue();

    template<typename Func, typename... Args>
    auto enqueue(Func func, Args&&... args) -> std::future<decltype(func(args...))>;

    void stop();

    bool try_dequeue_task(std::function<void()>& task);

    void barrier_arrive(size_t bar_id);

    void barrier_wait(size_t bar_id);

    bool is_paused(); 

    void pause();
    void resume();

    std::mutex& get_pause_mutex() { return pause_mutex_; }
    std::condition_variable& get_pause_condition() { return pause_condition_; }

private:
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable condition_;
    bool stop_;
    std::map<size_t, Barrier> barriers_;
    std::mutex barriers_mutex_;
    std::condition_variable barriers_cv_;
    std::set<TaskQueue*> paused_queues_;

    std::atomic<bool> paused_;
    std::mutex pause_mutex_;
    std::condition_variable pause_condition_;

    friend class Scheduler;
    friend class Worker;
};


template<typename Func, typename... Args>
auto TaskQueue::enqueue(Func func, Args&&... args) -> std::future<decltype(func(args...))> {
    using return_type = decltype(func(args...));

    std::packaged_task<return_type()> task(std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
    std::future<return_type> res = task.get_future();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.emplace([task=std::move(task)](){ task(); });
    }
    condition_.notify_one();

    return res;
}

