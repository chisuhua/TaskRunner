#include "taskqueue.h"
#include <iostream>
#include <utility>

TaskQueue::TaskQueue()
    : stop_(false)
    , paused_(false) {}

//template<typename Func, typename... Args>
//auto TaskQueue::enqueue(Func func, Args&&... args) -> std::future<decltype(func(args...))> {
    //using return_type = decltype(func(args...));

    //auto task = std::make_shared<std::packaged_task<return_type()>>(
        //std::bind(std::forward<Func>(func), std::forward<Args>(args)...)
    //);

    //std::future<return_type> res = task->get_future();
    //{
        //std::lock_guard<std::mutex> lock(mutex_);
        //tasks_.emplace([task]() { (*task)(); });
    //}
    //condition_.notify_one();

    //return res;
//}

void TaskQueue::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    condition_.notify_all();
}

bool TaskQueue::try_dequeue_task(std::function<void()>& task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_ && tasks_.empty()) return false;
        if (tasks_.empty()) return false;
        task = std::move(tasks_.front());
        tasks_.pop();
    }
    return true;
}

void TaskQueue::barrier_arrive(size_t bar_id) {
    {
        std::lock_guard<std::mutex> lock(barriers_mutex_);
        barriers_[bar_id].arrive_cnt.fetch_add(1, std::memory_order_relaxed);

        if (barriers_[bar_id].arrive_cnt.load(std::memory_order_acquire) == barriers_[bar_id].expect_arrive.load(std::memory_order_acquire)) {
            if (barriers_[bar_id].arrive_phase.load(std::memory_order_acquire) == barriers_[bar_id].wait_phase.load(std::memory_order_acquire)) {
                barriers_[bar_id].arrive_cnt.store(0, std::memory_order_release);
                barriers_[bar_id].arrive_phase.store(!barriers_[bar_id].arrive_phase.load(std::memory_order_acquire), std::memory_order_release);
                barriers_cv_.notify_all();
            } else {
                // Pause dequeue and notify scheduler
                paused_queues_.insert(this);
                pause_condition_.notify_all();
            }
        }
    }
}

void TaskQueue::barrier_wait(size_t bar_id) {
    {
        std::unique_lock<std::mutex> lock(barriers_mutex_);
        barriers_[bar_id].wait_cnt.fetch_add(1, std::memory_order_relaxed);

        while (true) {
            if (barriers_[bar_id].arrive_phase.load(std::memory_order_acquire) != barriers_[bar_id].wait_phase.load(std::memory_order_acquire)) {
                // Wait until arrive_phase matches wait_phase
                barriers_cv_.wait(lock);
            } else if (barriers_[bar_id].wait_cnt.load(std::memory_order_acquire) == barriers_[bar_id].expect_wait.load(std::memory_order_acquire)) {
                barriers_[bar_id].wait_phase.store(!barriers_[bar_id].wait_phase.load(std::memory_order_acquire), std::memory_order_release);
                barriers_[bar_id].wait_cnt.store(0, std::memory_order_release);
                // Resume all paused queues
                paused_queues_.clear();
                pause_condition_.notify_all();
                break;
            } else {
                // Pause dequeue and notify scheduler
                paused_queues_.insert(this);
                pause_condition_.notify_all();
                break;
            }
        }
    }
}

bool TaskQueue::is_paused() {
    std::lock_guard<std::mutex> lock(pause_mutex_);
    return paused_queues_.find(this) != paused_queues_.end();
}

void TaskQueue::pause() {
    paused_ = true;
}

void TaskQueue::resume() {
    paused_ = false;
    pause_condition_.notify_all();
}


