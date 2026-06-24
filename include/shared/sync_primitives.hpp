/**
 * sync_primitives.hpp - 分层同步原语
 * 
 * DDS v1.2 架构定义：
 * - Barrier: 任务级同步（TaskRunner 内部等待）
 * - Fence: GPU 级同步（对应 CUDA fence/event）
 * - Event: 跨设备同步（Phase 2 CUDA-Vulkan 互操作）
 */

#ifndef TASKRUNNER_SYNC_PRIMITIVES_HPP
#define TASKRUNNER_SYNC_PRIMITIVES_HPP

#include <atomic>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <map>
#include <cstdint>

namespace taskrunner {
namespace sync {

/**
 * Barrier - 任务级同步
 * 
 * 用于 TaskRunner 内部等待任务完成
 * 基于 condition_variable 实现
 */
struct Barrier {
    uint64_t id;
    std::atomic<uint32_t> count{0};
    std::mutex mtx;
    std::condition_variable cv;
    bool signaled{false};
    
    Barrier() = default;
    explicit Barrier(uint64_t id) : id(id) {}
};

/**
 * Fence - GPU 级同步
 * 
 * 对应 CUDA fence/event，用于 GPU 操作完成通知
 * 可关联原生 CUDA event (CUevent)
 */
struct Fence {
    uint64_t id;
    
    enum class State : int {
        UNSIGNALED = 0,
        SIGNALED = 1
    };
    
    std::atomic<State> state{State::UNSIGNALED};
    void* native_handle{nullptr};  // CUevent (Phase 1 可选)
    
    Fence() = default;
    explicit Fence(uint64_t id) : id(id) {}
};

/**
 * Event - 跨设备同步
 * 
 * Phase 2: 用于 CUDA-Vulkan 互操作
 * 支持时间戳记录
 */
struct Event {
    uint64_t id;
    std::chrono::steady_clock::time_point timestamp;
    void* native_handle{nullptr};  // CUevent
    
    Event() = default;
    explicit Event(uint64_t id) : id(id) {}
    
    void record() {
        timestamp = std::chrono::steady_clock::now();
    }
};

/**
 * SyncManager - 同步原语管理器
 * 
 * 负责创建、管理和销毁同步对象
 */
class SyncManager {
public:
    SyncManager() = default;
    ~SyncManager();
    
    // 禁止拷贝
    SyncManager(const SyncManager&) = delete;
    SyncManager& operator=(const SyncManager&) = delete;
    
    // 允许移动
    SyncManager(SyncManager&&) = default;
    SyncManager& operator=(SyncManager&&) = default;
    
    // ========== Barrier 操作 ==========
    
    /**
     * 创建 Barrier
     * @return 新创建的 Barrier 共享指针
     */
    std::shared_ptr<Barrier> create_barrier();
    
    /**
     * 信号 Barrier（标记为完成）
     * @param b Barrier 对象
     */
    void signal_barrier(const std::shared_ptr<Barrier>& b);
    
    /**
     * 等待 Barrier
     * @param b Barrier 对象
     * @param timeout_ms 超时时间（毫秒），0 = 无限等待
     * @return 0 = 成功，-ETIMEDOUT = 超时
     */
    int wait_barrier(const std::shared_ptr<Barrier>& b, uint64_t timeout_ms = 0);
    
    // ========== Fence 操作 ==========
    
    /**
     * 创建 Fence
     * @return 新创建的 Fence 共享指针
     */
    std::shared_ptr<Fence> create_fence();
    
    /**
     * 信号 Fence（标记为完成）
     * @param f Fence 对象
     */
    void signal_fence(const std::shared_ptr<Fence>& f);
    
    /**
     * 等待 Fence
     * @param f Fence 对象
     * @param timeout_ms 超时时间（毫秒），0 = 无限等待
     * @return 0 = 成功，-ETIMEDOUT = 超时
     */
    int wait_fence(const std::shared_ptr<Fence>& f, uint64_t timeout_ms = 0);
    
    /**
     * 查询 Fence 状态
     * @param f Fence 对象
     * @return 1 = signaled, 0 = unsignaled
     */
    int query_fence(const std::shared_ptr<Fence>& f);
    
    /**
     * 通过 ID 获取 Fence
     * @param fence_id Fence ID
     * @return Fence 共享指针，不存在返回 nullptr
     */
    std::shared_ptr<Fence> get_fence_by_id(uint64_t fence_id);
    
    // ========== Event 操作 (Phase 2) ==========
    
    /**
     * 创建 Event
     * @return 新创建的 Event 共享指针
     */
    std::shared_ptr<Event> create_event();
    
    /**
     * 记录 Event 时间戳
     * @param e Event 对象
     */
    void record_event(const std::shared_ptr<Event>& e);
    
    /**
     * 等待 Event
     * @param e Event 对象
     * @param timeout_ms 超时时间（毫秒），0 = 无限等待
     * @return 0 = 成功，-ETIMEDOUT = 超时
     */
    int wait_event(const std::shared_ptr<Event>& e, uint64_t timeout_ms = 0);
    
private:
    std::map<uint64_t, std::shared_ptr<Barrier>> barriers_;
    std::map<uint64_t, std::shared_ptr<Fence>> fences_;
    std::map<uint64_t, std::shared_ptr<Event>> events_;
    
    std::atomic<uint64_t> next_id_{1};
    std::mutex mutex_;
    
    uint64_t allocate_id();
};

} // namespace sync
} // namespace taskrunner

#endif // TASKRUNNER_SYNC_PRIMITIVES_HPP
