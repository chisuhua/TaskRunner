/**
 * sync_primitives.cpp - 分层同步原语实现
 */

#include "shared/sync_primitives.hpp"
#include <cerrno>
#include <thread>

namespace taskrunner {
namespace sync {

SyncManager::~SyncManager() {
    // 清理所有资源
    std::lock_guard<std::mutex> lock(mutex_);
    barriers_.clear();
    fences_.clear();
    events_.clear();
}

// ========== 私有方法 ==========

uint64_t SyncManager::allocate_id() {
    return next_id_.fetch_add(1, std::memory_order_relaxed);
}

// ========== Barrier 操作 ==========

std::shared_ptr<Barrier> SyncManager::create_barrier() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto barrier = std::make_shared<Barrier>(allocate_id());
    barriers_[barrier->id] = barrier;
    return barrier;
}

void SyncManager::signal_barrier(const std::shared_ptr<Barrier>& b) {
    if (!b) return;
    
    {
        std::lock_guard<std::mutex> lock(b->mtx);
        b->signaled = true;
    }
    b->cv.notify_all();
}

int SyncManager::wait_barrier(const std::shared_ptr<Barrier>& b, uint64_t timeout_ms) {
    if (!b) return -EINVAL;
    
    std::unique_lock<std::mutex> lock(b->mtx);
    
    if (timeout_ms == 0) {
        // 无限等待
        b->cv.wait(lock, [b]() { return b->signaled; });
        return 0;
    }
    
    // 超时等待
    auto timeout = std::chrono::steady_clock::now() + 
                   std::chrono::milliseconds(timeout_ms);
    
    if (b->cv.wait_until(lock, timeout, [b]() { return b->signaled; })) {
        return 0;
    }
    
    return -ETIMEDOUT;
}

// ========== Fence 操作 ==========

std::shared_ptr<Fence> SyncManager::create_fence() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto fence = std::make_shared<Fence>(allocate_id());
    fences_[fence->id] = fence;
    return fence;
}

void SyncManager::signal_fence(const std::shared_ptr<Fence>& f) {
    if (!f) return;
    f->state.store(Fence::State::SIGNALED, std::memory_order_release);
}

int SyncManager::wait_fence(const std::shared_ptr<Fence>& f, uint64_t timeout_ms) {
    if (!f) return -EINVAL;
    
    if (timeout_ms == 0) {
        // 无限等待 - 轮询直到完成
        while (f->state.load(std::memory_order_acquire) != Fence::State::SIGNALED) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        return 0;
    }
    
    // 超时等待
    auto start = std::chrono::steady_clock::now();
    auto timeout = std::chrono::milliseconds(timeout_ms);
    
    while (std::chrono::steady_clock::now() - start < timeout) {
        if (f->state.load(std::memory_order_acquire) == Fence::State::SIGNALED) {
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    return -ETIMEDOUT;
}

int SyncManager::query_fence(const std::shared_ptr<Fence>& f) {
    if (!f) return -EINVAL;
    
    auto state = f->state.load(std::memory_order_acquire);
    return (state == Fence::State::SIGNALED) ? 1 : 0;
}

std::shared_ptr<Fence> SyncManager::get_fence_by_id(uint64_t fence_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = fences_.find(fence_id);
    if (it != fences_.end()) {
        return it->second;
    }
    return nullptr;
}

// ========== Event 操作 ==========

std::shared_ptr<Event> SyncManager::create_event() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto event = std::make_shared<Event>(allocate_id());
    events_[event->id] = event;
    return event;
}

void SyncManager::record_event(const std::shared_ptr<Event>& e) {
    if (!e) return;
    e->record();
}

int SyncManager::wait_event(const std::shared_ptr<Event>& e, uint64_t timeout_ms) {
    if (!e) return -EINVAL;
    
    // Phase 1: Event 简单实现，立即返回
    // Phase 2: 实现真正的跨设备等待逻辑
    (void)timeout_ms;
    return 0;
}

} // namespace sync
} // namespace taskrunner
