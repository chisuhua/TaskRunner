/**
 * cuda_scheduler.hpp - CUDA 调度器
 * 
 * DDS v1.2 架构定义：
 * - 独立调度器（与 Vulkan 调度器并列）
 * - 接收 TaskRunner 命令，调度 CUDA 任务
 * - 管理同步原语和内存资源
 */

#ifndef TASKRUNNER_CUDA_SCHEDULER_HPP
#define TASKRUNNER_CUDA_SCHEDULER_HPP

#include "sync_primitives.hpp"
#include "memory_manager.hpp"
#include "cuda_stub.hpp"

#include <map>
#include <atomic>
#include <mutex>
#include <cstdint>

namespace taskrunner {

/**
 * Task - 任务描述符
 */
struct Task {
    uint64_t task_id{0};
    uint64_t fence_id{0};
    
    enum class State {
        PENDING,
        RUNNING,
        COMPLETED,
        FAILED
    };
    
    State state{State::PENDING};
    LaunchParams params;
    int error_code{0};  // 失败时的错误码
};

/**
 * CudaScheduler - CUDA 调度器
 * 
 * 职责：
 * 1. 接收 TaskRunner 命令
 * 2. 调度 CUDA 任务到 GPU
 * 3. 管理同步原语（Fence/Barrier）
 * 4. 追踪任务状态
 */
class CudaScheduler {
public:
    /**
     * 构造函数
     * @param stub CUDA Stub 实例（可为 nullptr，自动创建）
     */
    explicit CudaScheduler(CudaStub* stub = nullptr);
    
    /**
     * 析构函数
     */
    ~CudaScheduler();
    
    // 禁止拷贝
    CudaScheduler(const CudaScheduler&) = delete;
    CudaScheduler& operator=(const CudaScheduler&) = delete;
    
    // ========== 初始化 ==========
    
    /**
     * 初始化调度器
     * @param stub_mode true = 使用 Stub 模式（无真实 GPU）
     * @return 0 = 成功，-errno = 失败
     */
    int initialize(bool stub_mode = false);
    
    /**
     * 关闭调度器
     */
    void shutdown();
    
    /**
     * 检查是否已初始化
     */
    bool is_initialized() const { return initialized_; }
    
    // ========== 内存管理（返回 fence_id 用于异步等待） ==========
    
    struct AllocationResult {
        uint64_t device_ptr{0};
        uint64_t fence_id{0};
        int status{0};  // 0=success, -errno on error
    };
    
    /**
     * 分配设备内存
     * @param size 分配大小（字节）
     * @return AllocationResult
     */
    AllocationResult submit_mem_alloc(size_t size);
    
    /**
     * 释放设备内存
     * @param device_ptr 设备指针
     * @return 0 = 成功，-errno = 失败
     */
    int submit_mem_free(uint64_t device_ptr);
    
    // ========== 内存拷贝（细粒度，支持 offset） ==========
    
    /**
     * Host to Device 拷贝
     * @param device_ptr 目标设备指针
     * @param offset 目标偏移
     * @param host_ptr 源主机指针
     * @param size 拷贝大小
     * @return fence_id（>0）或错误码（<=0）
     */
    int submit_memcpy_h2d(uint64_t device_ptr, uint64_t offset,
                          const void* host_ptr, size_t size);
    
    /**
     * Device to Host 拷贝
     * @param host_ptr 目标主机指针
     * @param device_ptr 源设备指针
     * @param offset 源偏移
     * @param size 拷贝大小
     * @return fence_id（>0）或错误码（<=0）
     */
    int submit_memcpy_d2h(void* host_ptr, uint64_t device_ptr,
                          uint64_t offset, size_t size);
    
    // ========== Kernel 启动 ==========
    
    struct LaunchResult {
        uint64_t task_id{0};
        uint64_t fence_id{0};
        int status{0};  // 0=success, -errno on error
    };
    
    /**
     * 启动 Kernel
     * @param params 启动参数
     * @return LaunchResult
     */
    LaunchResult submit_launch(const LaunchParams& params);
    
    // ========== 同步 ==========
    
    /**
     * 等待 Fence
     * @param fence_id Fence ID
     * @param timeout_ms 超时时间（毫秒），0 = 无限等待
     * @return 0 = 成功，-ETIMEDOUT = 超时，-errno = 错误
     */
    int wait_fence(uint64_t fence_id, uint64_t timeout_ms = 0);
    
    /**
     * 查询 Fence 状态
     * @param fence_id Fence ID
     * @return 1 = signaled, 0 = unsignaled, -errno = 错误
     */
    int query_fence(uint64_t fence_id);
    
    // ========== 任务查询 ==========
    
    /**
     * 通过 task_id 查询任务状态
     * @param task_id 任务 ID
     * @return Task 状态，不存在返回 nullptr
     */
    const Task* get_task(uint64_t task_id) const;
    
    /**
     * 获取待处理任务数量
     */
    size_t pending_task_count() const;
    
    // ========== 统计信息 ==========
    
    /**
     * 获取内存管理器（用于统计）
     */
    const MemoryManager& memory_manager() const { return memory_mgr_; }
    
    /**
     * 获取同步管理器
     */
    const sync::SyncManager& sync_manager() const { return sync_mgr_; }
    
private:
    CudaStub* stub_{nullptr};
    bool owns_stub_{false};  // 是否拥有 stub 所有权
    
    MemoryManager memory_mgr_;
    sync::SyncManager sync_mgr_;
    
    std::map<uint64_t, Task> pending_tasks_;
    mutable std::mutex tasks_mutex_;
    
    std::atomic<uint64_t> next_task_id_{1};
    std::atomic<uint64_t> next_fence_id_{1};
    
    bool initialized_{false};
    
    uint64_t allocate_fence_id();
    uint64_t allocate_task_id();
};

} // namespace taskrunner

#endif // TASKRUNNER_CUDA_SCHEDULER_HPP
