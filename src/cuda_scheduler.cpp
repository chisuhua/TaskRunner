/**
 * cuda_scheduler.cpp - CUDA 调度器实现
 *
 * DDS v1.2 架构实现：
 * - 独立调度器（与 Vulkan 调度器并列）
 * - 接收 TaskRunner 命令，调度 CUDA 任务
 * - 管理同步原语和内存资源
 *
 * H-2.5 (D10): driver_ 类型从 CudaStub* 改为 async_task::gpu::IGpuDriver*
 * stub_->xxx() 全部改为 driver_->xxx() (API 兼容)
 */

#include "cuda_scheduler.hpp"
#include <cerrno>
#include <cstring>

namespace taskrunner {

CudaScheduler::CudaScheduler(async_task::gpu::IGpuDriver* driver) {
    // D10: 接受 IGpuDriver* 注入
    if (driver != nullptr) {
        driver_ = driver;
        owns_driver_ = false;
    } else {
        driver_ = new async_task::gpu::CudaStub();
        owns_driver_ = true;
    }
}

CudaScheduler::~CudaScheduler() {
    shutdown();
}

int CudaScheduler::initialize(bool stub_mode) {
    if (initialized_) {
        return -EALREADY;
    }

    if (!driver_) {
        driver_ = new async_task::gpu::CudaStub();
        owns_driver_ = true;
    }

    if (auto* stub = dynamic_cast<async_task::gpu::CudaStub*>(driver_)) {
        stub->set_stub_mode(stub_mode);
        auto result = stub->initialize();
        if (result != async_task::gpu::CudaResult::SUCCESS) {
            if (owns_driver_) {
                delete driver_;
                driver_ = nullptr;
                owns_driver_ = false;
            }
            return -EIO;
        }
    }

    initialized_ = true;
    return 0;
}

void CudaScheduler::shutdown() {
    if (!initialized_) return;

    if (auto* stub = dynamic_cast<async_task::gpu::CudaStub*>(driver_)) {
        stub->shutdown();
    }

    if (owns_driver_ && driver_) {
        delete driver_;
        driver_ = nullptr;
        owns_driver_ = false;
    }

    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        pending_tasks_.clear();
    }

    initialized_ = false;
}

// ========== 内存管理 ==========

CudaScheduler::AllocationResult CudaScheduler::submit_mem_alloc(size_t size) {
    AllocationResult result;

    if (!initialized_) {
        result.status = -ENOTCONN;
        return result;
    }

    // 分配内存 (MemoryManager)
    auto mem = memory_mgr_.allocate(size);
    if (!mem.is_valid()) {
        result.status = -ENOMEM;
        return result;
    }

    // 调用 driver 分配真实 GPU 内存 (D10: CudaStub 路径)
    auto* stub = dynamic_cast<async_task::gpu::CudaStub*>(driver_);
    if (!stub) {
        memory_mgr_.free(mem);
        result.status = -ENOSYS;
        return result;
    }

    uint64_t device_ptr = 0;
    auto cuda_result = stub->mem_alloc(size, &device_ptr);
    if (cuda_result != async_task::gpu::CudaResult::SUCCESS) {
        memory_mgr_.free(mem);
        result.status = -EIO;
        return result;
    }

    // 创建 fence
    auto fence = sync_mgr_.create_fence();

    // 在 stub 模式下，立即 signal fence
    if (stub->is_stub_mode()) {
        sync_mgr_.signal_fence(fence);
    }

    result.device_ptr = mem.device_ptr;
    result.fence_id = fence->id;
    result.status = 0;

    return result;
}

int CudaScheduler::submit_mem_free(uint64_t device_ptr) {
    if (!initialized_) {
        return -ENOTCONN;
    }

    if (device_ptr == 0) {
        return -EINVAL;
    }

    // 查找内存描述符
    auto mem = memory_mgr_.find(device_ptr);
    if (!mem.is_valid()) {
        return -ENOENT;
    }

    // D10: 调用 driver 释放
    auto* stub = dynamic_cast<async_task::gpu::CudaStub*>(driver_);
    if (!stub) {
        return -ENOSYS;
    }

    auto cuda_result = stub->mem_free(device_ptr);
    if (cuda_result != async_task::gpu::CudaResult::SUCCESS) {
        return -EIO;
    }

    memory_mgr_.free(mem);

    auto fence = sync_mgr_.create_fence();
    if (stub->is_stub_mode()) {
        sync_mgr_.signal_fence(fence);
    }

    return 0;
}

// ========== 内存拷贝 ==========

int CudaScheduler::submit_memcpy_h2d(uint64_t device_ptr, uint64_t offset,
                                      const void* host_ptr, size_t size) {
    if (!initialized_) {
        return -ENOTCONN;
    }

    if (!host_ptr || device_ptr == 0) {
        return -EINVAL;
    }

    auto mem = memory_mgr_.find(device_ptr);
    if (!mem.is_valid()) {
        return -ENOENT;
    }

    if (offset + size > mem.size) {
        return -EOVERFLOW;
    }

    auto* stub = dynamic_cast<async_task::gpu::CudaStub*>(driver_);
    if (!stub) {
        return -ENOSYS;
    }

    auto cuda_result = stub->memcpy_h2d(device_ptr + offset, host_ptr, size);
    if (cuda_result != async_task::gpu::CudaResult::SUCCESS) {
        return -EIO;
    }

    memory_mgr_.memcpy_h2d(mem, host_ptr, size);

    auto fence = sync_mgr_.create_fence();
    if (stub->is_stub_mode()) {
        sync_mgr_.signal_fence(fence);
    }

    return static_cast<int>(fence->id);
}

int CudaScheduler::submit_memcpy_d2h(void* host_ptr, uint64_t device_ptr,
                                      uint64_t offset, size_t size) {
    if (!initialized_) {
        return -ENOTCONN;
    }

    if (!host_ptr || device_ptr == 0) {
        return -EINVAL;
    }

    auto mem = memory_mgr_.find(device_ptr);
    if (!mem.is_valid()) {
        return -ENOENT;
    }

    if (offset + size > mem.size) {
        return -EOVERFLOW;
    }

    auto* stub = dynamic_cast<async_task::gpu::CudaStub*>(driver_);
    if (!stub) {
        return -ENOSYS;
    }

    auto cuda_result = stub->memcpy_d2h(host_ptr, device_ptr + offset, size);
    if (cuda_result != async_task::gpu::CudaResult::SUCCESS) {
        return -EIO;
    }

    memory_mgr_.memcpy_d2h(host_ptr, mem, size);

    auto fence = sync_mgr_.create_fence();
    if (stub->is_stub_mode()) {
        sync_mgr_.signal_fence(fence);
    }

    return static_cast<int>(fence->id);
}

// ========== Kernel 启动 ==========

CudaScheduler::LaunchResult CudaScheduler::submit_launch(const async_task::gpu::LaunchParams& params) {
    LaunchResult result;

    if (!initialized_) {
        result.status = -ENOTCONN;
        return result;
    }

    if (!params.kernel_name) {
        result.status = -EINVAL;
        return result;
    }

    uint64_t task_id = allocate_task_id();

    Task task;
    task.task_id = task_id;
    task.params = params;
    task.state = Task::State::RUNNING;

    auto* stub = dynamic_cast<async_task::gpu::CudaStub*>(driver_);
    if (!stub) {
        task.state = Task::State::FAILED;
        task.error_code = -ENOSYS;

        std::lock_guard<std::mutex> lock(tasks_mutex_);
        pending_tasks_[task_id] = task;

        result.status = -ENOSYS;
        return result;
    }

    uint64_t stub_task_id = 0;
    auto cuda_result = stub->launch_kernel(params, &stub_task_id);
    if (cuda_result != async_task::gpu::CudaResult::SUCCESS) {
        task.state = Task::State::FAILED;
        task.error_code = -EIO;

        std::lock_guard<std::mutex> lock(tasks_mutex_);
        pending_tasks_[task_id] = task;

        result.status = -EIO;
        return result;
    }

    auto fence = sync_mgr_.create_fence();

    if (stub->is_stub_mode()) {
        task.state = Task::State::COMPLETED;
        sync_mgr_.signal_fence(fence);
    }

    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        task.fence_id = fence->id;
        pending_tasks_[task_id] = task;
    }

    result.task_id = task_id;
    result.fence_id = fence->id;
    result.status = 0;

    return result;
}

// ========== 同步 ==========

int CudaScheduler::wait_fence(uint64_t fence_id, uint64_t timeout_ms) {
    if (!initialized_) {
        return -ENOTCONN;
    }

    auto fence = sync_mgr_.get_fence_by_id(fence_id);
    if (!fence) {
        return -ENOENT;
    }

    return sync_mgr_.wait_fence(fence, timeout_ms);
}

int CudaScheduler::query_fence(uint64_t fence_id) {
    if (!initialized_) {
        return -ENOTCONN;
    }

    auto fence = sync_mgr_.get_fence_by_id(fence_id);
    if (!fence) {
        return -ENOENT;
    }

    return sync_mgr_.query_fence(fence);
}

// ========== 任务查询 ==========

const Task* CudaScheduler::get_task(uint64_t task_id) const {
    std::lock_guard<std::mutex> lock(tasks_mutex_);

    auto it = pending_tasks_.find(task_id);
    if (it != pending_tasks_.end()) {
        return &it->second;
    }

    return nullptr;
}

size_t CudaScheduler::pending_task_count() const {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    return pending_tasks_.size();
}

// ========== 私有方法 ==========

uint64_t CudaScheduler::allocate_fence_id() {
    return next_fence_id_.fetch_add(1, std::memory_order_relaxed);
}

uint64_t CudaScheduler::allocate_task_id() {
    return next_task_id_.fetch_add(1, std::memory_order_relaxed);
}

} // namespace taskrunner
