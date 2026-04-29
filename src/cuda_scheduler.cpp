/**
 * cuda_scheduler.cpp - CUDA 调度器实现
 * 
 * DDS v1.2 架构实现：
 * - 独立调度器（与 Vulkan 调度器并列）
 * - 接收 TaskRunner 命令，调度 CUDA 任务
 * - 管理同步原语和内存资源
 */

#include "cuda_scheduler.hpp"
#include <cerrno>
#include <cstring>

namespace taskrunner {

CudaScheduler::CudaScheduler(CudaStub* stub)
    : stub_(stub) {
    // 如果未提供 stub，将在 initialize() 中创建
}

CudaScheduler::~CudaScheduler() {
    shutdown();
}

int CudaScheduler::initialize(bool stub_mode) {
    if (initialized_) {
        return -EALREADY;
    }
    
    // 创建 stub（如果未提供）
    if (!stub_) {
        stub_ = new CudaStub();
        owns_stub_ = true;
    }
    
    // 设置 stub 模式
    stub_->set_stub_mode(stub_mode);
    
    // 初始化 stub
    auto result = stub_->initialize();
    if (result != CudaResult::SUCCESS) {
        if (owns_stub_) {
            delete stub_;
            stub_ = nullptr;
            owns_stub_ = false;
        }
        return -EIO;
    }
    
    initialized_ = true;
    return 0;
}

void CudaScheduler::shutdown() {
    if (!initialized_) return;
    
    // 关闭 stub
    if (stub_) {
        stub_->shutdown();
    }
    
    // 清理 owned stub
    if (owns_stub_ && stub_) {
        delete stub_;
        stub_ = nullptr;
        owns_stub_ = false;
    }
    
    // 清理任务
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
    
    // 分配内存
    auto mem = memory_mgr_.allocate(size);
    if (!mem.is_valid()) {
        result.status = -ENOMEM;
        return result;
    }
    
    // 调用 stub 分配真实 GPU 内存
    uint64_t device_ptr = 0;
    auto cuda_result = stub_->mem_alloc(size, &device_ptr);
    if (cuda_result != CudaResult::SUCCESS) {
        // 回滚内存管理器分配
        memory_mgr_.free(mem);
        result.status = -EIO;
        return result;
    }
    
    // 创建 fence
    auto fence = sync_mgr_.create_fence();
    
    // 在 stub 模式下，立即 signal fence
    if (stub_->is_stub_mode()) {
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
    
    // 调用 stub 释放
    auto cuda_result = stub_->mem_free(device_ptr);
    if (cuda_result != CudaResult::SUCCESS) {
        return -EIO;
    }
    
    // 释放内存管理器追踪
    memory_mgr_.free(mem);
    
    // 创建 fence（异步释放）
    auto fence = sync_mgr_.create_fence();
    if (stub_->is_stub_mode()) {
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
    
    // 查找内存描述符
    auto mem = memory_mgr_.find(device_ptr);
    if (!mem.is_valid()) {
        return -ENOENT;
    }
    
    // 边界检查
    if (offset + size > mem.size) {
        return -EOVERFLOW;
    }
    
    // 调用 stub 拷贝
    auto cuda_result = stub_->memcpy_h2d(device_ptr + offset, host_ptr, size);
    if (cuda_result != CudaResult::SUCCESS) {
        return -EIO;
    }
    
    // 更新内存管理器
    memory_mgr_.memcpy_h2d(mem, host_ptr, size);
    
    // 创建 fence
    auto fence = sync_mgr_.create_fence();
    if (stub_->is_stub_mode()) {
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
    
    // 查找内存描述符
    auto mem = memory_mgr_.find(device_ptr);
    if (!mem.is_valid()) {
        return -ENOENT;
    }
    
    // 边界检查
    if (offset + size > mem.size) {
        return -EOVERFLOW;
    }
    
    // 调用 stub 拷贝
    auto cuda_result = stub_->memcpy_d2h(host_ptr, device_ptr + offset, size);
    if (cuda_result != CudaResult::SUCCESS) {
        return -EIO;
    }
    
    // 更新内存管理器
    memory_mgr_.memcpy_d2h(host_ptr, mem, size);
    
    // 创建 fence
    auto fence = sync_mgr_.create_fence();
    if (stub_->is_stub_mode()) {
        sync_mgr_.signal_fence(fence);
    }
    
    return static_cast<int>(fence->id);
}

// ========== Kernel 启动 ==========

CudaScheduler::LaunchResult CudaScheduler::submit_launch(const LaunchParams& params) {
    LaunchResult result;
    
    if (!initialized_) {
        result.status = -ENOTCONN;
        return result;
    }
    
    if (!params.kernel_name) {
        result.status = -EINVAL;
        return result;
    }
    
    // 分配 task_id
    uint64_t task_id = allocate_task_id();
    
    // 创建任务记录
    Task task;
    task.task_id = task_id;
    task.params = params;
    task.state = Task::State::RUNNING;
    
    // 调用 stub 启动 kernel
    uint64_t stub_task_id = 0;
    auto cuda_result = stub_->launch_kernel(params, &stub_task_id);
    if (cuda_result != CudaResult::SUCCESS) {
        task.state = Task::State::FAILED;
        task.error_code = -EIO;
        
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        pending_tasks_[task_id] = task;
        
        result.status = -EIO;
        return result;
    }
    
    // 创建 fence
    auto fence = sync_mgr_.create_fence();
    
    // 在 stub 模式下，立即 signal fence
    if (stub_->is_stub_mode()) {
        task.state = Task::State::COMPLETED;
        sync_mgr_.signal_fence(fence);
    }
    
    // 记录任务
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
