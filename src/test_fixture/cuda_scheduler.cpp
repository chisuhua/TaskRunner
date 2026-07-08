// SCOPE: TEST-FIXTURE
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

#include "test_fixture/cuda_scheduler.hpp"
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
    // ASan leak fix: shutdown() returns early if !initialized_, so we still
    // need to release any driver_ we own (allocated in ctor).
    if (owns_driver_ && driver_) {
        delete driver_;
        driver_ = nullptr;
        owns_driver_ = false;
    }
}

int CudaScheduler::initialize(bool stub_mode) {
    if (initialized_) {
        return -EALREADY;
    }

    if (!driver_) {
        driver_ = new async_task::gpu::CudaStub();
        owns_driver_ = true;
    }

    // H-3.5: 通过 IGpuDriver 抽象调用 (删除 dynamic_cast 抽象泄漏)
    driver_->set_stub_mode(stub_mode);
    int result = driver_->initialize();
    if (result != 0) {
        if (owns_driver_) {
            delete driver_;
            driver_ = nullptr;
            owns_driver_ = false;
        }
        return -EIO;
    }

    initialized_ = true;
    return 0;
}

void CudaScheduler::shutdown() {
    if (!initialized_) return;

    // H-3.5: 通过 IGpuDriver 抽象调用 (删除 dynamic_cast 抽象泄漏)
    driver_->shutdown();

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

    // Phase 1.5: 通过 IGpuDriver 虚接口分配 (删除 dynamic_cast 抽象泄漏)
    uint64_t bo_handle = driver_->alloc_bo_vram(size, 0);
    if (bo_handle == 0) {
        memory_mgr_.free(mem);
        result.status = -EIO;
        return result;
    }
    bo_handles_[mem.device_ptr] = bo_handle;

    // 创建 fence (alloc 是同步操作，立即 signal)
    auto fence = sync_mgr_.create_fence();
    sync_mgr_.signal_fence(fence);

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

    // Phase 1.5: 通过 IGpuDriver 虚接口释放 (删除 dynamic_cast 抽象泄漏)
    auto it = bo_handles_.find(device_ptr);
    if (it != bo_handles_.end()) {
        int ret = driver_->free_bo(it->second);
        if (ret != 0) {
            return -EIO;
        }
        bo_handles_.erase(it);
    }

    memory_mgr_.free(mem);

    auto fence = sync_mgr_.create_fence();
    sync_mgr_.signal_fence(fence);

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

    // Phase 1.5: 通过 IGpuDriver 虚接口提交 memcpy (is_h2d=true)
    int64_t driver_fence = driver_->submit_memcpy(0, reinterpret_cast<uint64_t>(host_ptr),
                                                   device_ptr + offset, size, true);
    if (driver_fence < 0) {
        return -EIO;
    }

    memory_mgr_.memcpy_h2d(mem, host_ptr, size);

    auto fence = sync_mgr_.create_fence();
    sync_mgr_.signal_fence(fence);

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

    int64_t driver_fence = driver_->submit_memcpy(0, device_ptr + offset,
                                                   reinterpret_cast<uint64_t>(host_ptr), size, false);
    if (driver_fence < 0) {
        return -EIO;
    }

    memory_mgr_.memcpy_d2h(host_ptr, mem, size);

    auto fence = sync_mgr_.create_fence();
    sync_mgr_.signal_fence(fence);

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

    int64_t driver_fence = driver_->submit_launch(0, 0,
        params.grid_dim_x, params.grid_dim_y, params.grid_dim_z,
        params.block_dim_x, params.block_dim_y, params.block_dim_z);
    if (driver_fence < 0) {
        task.state = Task::State::FAILED;
        task.error_code = -EIO;

        std::lock_guard<std::mutex> lock(tasks_mutex_);
        pending_tasks_[task_id] = task;

        result.status = -EIO;
        return result;
    }

    auto fence = sync_mgr_.create_fence();
    task.state = Task::State::COMPLETED;
    sync_mgr_.signal_fence(fence);

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
