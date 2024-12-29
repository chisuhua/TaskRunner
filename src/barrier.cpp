#include "barrier.h"
#include <mutex>
#include <condition_variable>

void Barrier::arrive(size_t bar_id) {
    static std::mutex barriers_mutex_;
    static std::condition_variable barriers_cv_;

    {
        std::lock_guard<std::mutex> lock(barriers_mutex_);
        arrive_cnt.fetch_add(1, std::memory_order_relaxed);

        if (arrive_cnt.load(std::memory_order_acquire) == expect_arrive.load(std::memory_order_acquire)) {
            if (arrive_phase.load(std::memory_order_acquire) == wait_phase.load(std::memory_order_acquire)) {
                arrive_cnt.store(0, std::memory_order_release);
                arrive_phase.store(!arrive_phase.load(std::memory_order_acquire), std::memory_order_release);
                barriers_cv_.notify_all();
            } else {
                // Pause dequeue and notify scheduler
                // This part is handled in TaskQueue class
            }
        }
    }
}

void Barrier::wait(size_t bar_id) {
    static std::mutex barriers_mutex_;
    static std::condition_variable barriers_cv_;

    {
        std::unique_lock<std::mutex> lock(barriers_mutex_);
        wait_cnt.fetch_add(1, std::memory_order_relaxed);

        while (true) {
            if (arrive_phase.load(std::memory_order_acquire) != wait_phase.load(std::memory_order_acquire)) {
                // Wait until arrive_phase matches wait_phase
                barriers_cv_.wait(lock);
            } else if (wait_cnt.load(std::memory_order_acquire) == expect_wait.load(std::memory_order_acquire)) {
                wait_phase.store(!wait_phase.load(std::memory_order_acquire), std::memory_order_release);
                wait_cnt.store(0, std::memory_order_release);
                // Resume all paused queues
                // This part is handled in TaskQueue class
                barriers_cv_.notify_all();
                break;
            } else {
                // Pause dequeue and notify scheduler
                // This part is handled in TaskQueue class
                break;
            }
        }
    }
}
