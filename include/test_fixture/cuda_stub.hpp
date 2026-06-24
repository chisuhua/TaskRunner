// SCOPE: TEST-FIXTURE
/**
 * cuda_stub.hpp - CUDA Driver API 封装
 *
 * DDS v1.2 架构定义:
 * - 封装 CUDA Driver API (cuMemAlloc, cuLaunchKernel, etc.)
 * - 提供统一的错误处理
 * - Phase 1: 支持基本内存和 Kernel 操作
 *
 * H-2.5 重构 (D9: 命名空间迁移 + IGpuDriver 实现):
 * - 整个文件迁移到 namespace async_task::gpu (与 IGpuDriver 一致)
 * - class CudaStub : public IGpuDriver (28 方法覆盖)
 * - 既有 CUDA Driver API 方法 (mem_alloc / memcpy_h2d / launch_kernel 等) 保留
 *   作为 CudaScheduler 的既有调用路径
 * - 旧 namespace taskrunner 提供 using alias 兼容 1 release
 */

#ifndef TASKRUNNER_CUDA_STUB_HPP
#define TASKRUNNER_CUDA_STUB_HPP

#include <string>
#include <cstdint>
#include <cstddef>
#include <map>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <iostream>
#include <stdexcept>

// 统一抽象接口 (H-2.5)
#include "shared/igpu_driver.hpp"

namespace async_task {
namespace gpu {

/**
 * CUDA 错误码封装
 */
enum class CudaResult {
    SUCCESS = 0,
    ERROR_INVALID_VALUE = 1,
    ERROR_OUT_OF_MEMORY = 2,
    ERROR_NOT_INITIALIZED = 3,
    ERROR_UNKNOWN = 999
};

/**
 * 转换 CUDA 错误码为字符串
 */
inline const char* cuda_result_to_string(CudaResult result) {
    switch (result) {
        case CudaResult::SUCCESS:
            return "CUDA_SUCCESS";
        case CudaResult::ERROR_INVALID_VALUE:
            return "CUDA_ERROR_INVALID_VALUE";
        case CudaResult::ERROR_OUT_OF_MEMORY:
            return "CUDA_ERROR_OUT_OF_MEMORY";
        case CudaResult::ERROR_NOT_INITIALIZED:
            return "CUDA_ERROR_NOT_INITIALIZED";
        default:
            return "CUDA_ERROR_UNKNOWN";
    }
}

/**
 * LaunchParams - Kernel 启动参数
 */
struct LaunchParams {
    const char* kernel_name{nullptr};
    void* params{nullptr};
    size_t params_size{0};

    uint32_t grid_dim_x{1};
    uint32_t grid_dim_y{1};
    uint32_t grid_dim_z{1};

    uint32_t block_dim_x{256};
    uint32_t block_dim_y{1};
    uint32_t block_dim_z{1};

    uint32_t shared_mem_bytes{0};
};

/**
 * CudaStub - CUDA Driver API 封装类
 *
 * H-2.5: 实现 IGpuDriver 28 个方法（mock 语义: 递增 handle + 内存 mock + 零 ioctl）
 *
 * 职责:
 * 1. 管理 CUDA context
 * 2. 封装 cuMemAlloc/cuMemFree
 * 3. 封装 cuMemcpyH2D/cuMemcpyD2H/cuMemcpyD2D
 * 4. 封装 cuLaunchKernel
 * 5. 封装 CUevent 创建和同步
 * 6. 实现 IGpuDriver 抽象方法 (H-2.5)
 */
class CudaStub : public IGpuDriver {
public:
    CudaStub();
    ~CudaStub() override;

    // 禁止拷贝
    CudaStub(const CudaStub&) = delete;
    CudaStub& operator=(const CudaStub&) = delete;

    // ========== 既有 CUDA Driver API 方法（保留，调用方零修改）==========
    CudaResult initialize();
    void shutdown();
    bool is_initialized() const { return initialized_; }

    CudaResult mem_alloc(size_t size, uint64_t* device_ptr);
    CudaResult mem_free(uint64_t device_ptr);
    CudaResult memcpy_h2d(uint64_t dst, const void* src, size_t size);
    CudaResult memcpy_d2h(void* dst, uint64_t src, size_t size);
    CudaResult memcpy_d2d(uint64_t dst, uint64_t src, size_t size);
    CudaResult launch_kernel(const LaunchParams& params, uint64_t* task_id);

    CudaResult create_event(uint64_t* event_id);
    CudaResult record_event(uint64_t event_id);
    CudaResult wait_event(uint64_t event_id, uint64_t timeout_ms = 0);
    CudaResult query_event(uint64_t event_id, int* signaled);
    CudaResult destroy_event(uint64_t event_id);

    void set_stub_mode(bool enable) { stub_mode_ = enable; }
    bool is_stub_mode() const { return stub_mode_; }

    // ============================================================
    // IGpuDriver 实现 (H-2.5, 28 方法) - mock 语义
    // ============================================================

    // 核心生命周期 (3) - mock 总是 "open"
    int open() override { return 0; }
    int close() override { return 0; }
    bool is_open() const override { return true; }

    // FD 访问 (1) - mock 无 fd
    int fd() const override { return -1; }

    // 设备信息 (8) - 返回 canned value
    int get_device_info(struct gpu_device_info* out) override;
    uint32_t get_warp_size() override { return 32; }      // mock NVIDIA
    uint32_t get_simd_count() override { return 80; }     // mock SM count
    uint32_t get_peak_fp32_gflops() override { return 10000; }  // mock 10 TFLOPS
    uint32_t get_max_clock_frequency() override { return 1500; }  // mock 1.5 GHz
    int get_driver_version_string(char* out, size_t size) override;
    int get_marketing_name(char* out, size_t size) override;
    void print_device_info(std::ostream& os = std::cout) override;

    // 缓冲区对象 (4) - mock 语义 (递增 handle + host malloc)
    uint64_t alloc_bo(uint64_t size, uint32_t flags) override;
    uint64_t alloc_bo_vram(uint64_t size, uint32_t flags) override;
    int free_bo(uint64_t bo_handle) override;
    void* map_bo(uint64_t bo_handle, uint64_t size) override;

    // 提交 (3) - 返回递增 fence_id
    int64_t submit_batch(uint32_t stream_id, const struct gpu_gpfifo_entry* entries,
                         uint32_t count, uint32_t flags) override;
    int64_t submit_memcpy(uint32_t stream_id, uint64_t src_addr, uint64_t dst_addr,
                          uint64_t size, bool is_h2d) override;
    int64_t submit_launch(uint32_t stream_id, uint32_t kernel_index,
                          uint32_t grid_x, uint32_t grid_y, uint32_t grid_z,
                          uint32_t block_x, uint32_t block_y,
                          uint32_t block_z) override;

    // Fence 等待 (2 重载) - mock 立即成功
    int wait_fence(uint64_t fence_id, uint32_t timeout_ms, uint32_t* status_out) override;
    int wait_fence(uint64_t fence_id) override;

    // VA Space 透传 (2) - mock 内部 state
    void set_current_va_space(uint64_t va_space_handle) override {
        current_va_space_handle_ = va_space_handle;
    }
    uint64_t get_current_va_space() const override {
        return current_va_space_handle_;
    }

    // H-3 Phase 2 (5) - mock 语义 (monotonic handle + in-memory resource tracking)
    uint64_t create_va_space(uint32_t flags) override;
    int destroy_va_space(uint64_t va_space_handle) override;
    int register_gpu(uint64_t va_space_handle, uint32_t gpu_id, uint32_t flags) override;
    uint64_t create_queue(uint64_t va_space_handle, uint32_t queue_type,
                          uint32_t priority, uint64_t ring_buffer_size) override;
    int destroy_queue(uint64_t queue_handle) override;

private:
    bool initialized_{false};
    bool stub_mode_{false};

    // Event 管理（既有 CUDA Driver API 用）
    std::map<uint64_t, void*> events_;
    std::atomic<uint64_t> next_event_id_{1};
    std::mutex events_mutex_;

    // Task ID 计数器（既有 CUDA Driver API 用）
    std::atomic<uint64_t> next_task_id_{1};

    // H-2.5: IGpuDriver mock 状态
    std::atomic<uint64_t> next_bo_handle_{1};
    std::atomic<uint64_t> next_fence_id_{1};
    uint64_t current_va_space_handle_{0};

    // H-3: Phase 2 mock 状态 (monotonic handle + resource tracking)
    std::atomic<uint64_t> next_va_space_handle_{1};  // monotonic from 1 (R2 spec)
    std::atomic<uint64_t> next_queue_handle_{1};      // monotonic from 1 (R2 spec)
    std::unordered_map<uint64_t, bool> va_space_map_;  // mock 资源跟踪
    std::unordered_map<uint64_t, bool> queue_map_;     // mock 资源跟踪
    mutable std::mutex mock_state_mutex_;              // 保护 map 操作
};

} // namespace gpu
} // namespace async_task

// ============================================================
// Deprecated namespace alias (D9, 1 release 过渡)
// 旧调用方 (taskrunner::CudaStub) 零修改
// ============================================================
namespace taskrunner {
    using CudaStub = async_task::gpu::CudaStub;
    using CudaResult = async_task::gpu::CudaResult;
    using LaunchParams = async_task::gpu::LaunchParams;
    inline const char* cuda_result_to_string(async_task::gpu::CudaResult result) {
        return async_task::gpu::cuda_result_to_string(result);
    }
} // namespace taskrunner

#endif // TASKRUNNER_CUDA_STUB_HPP
