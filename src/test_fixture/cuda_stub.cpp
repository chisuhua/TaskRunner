/**
 * cuda_stub.cpp - CUDA Driver API 封装实现
 *
 * Phase 1: Stub 模式实现（无真实 GPU）
 * Phase 2: 集成真实 CUDA Driver API
 *
 * H-2.5 重构: 迁移到 namespace async_task::gpu + 实现 IGpuDriver 28 方法
 */

#include "test_fixture/cuda_stub.hpp"
#include <cstring>
#include <cstdlib>
#include <map>
#include <mutex>
#include <atomic>

namespace async_task {
namespace gpu {

// ============================================================
// 既有 CUDA Driver API 方法 (保留，调用方零修改)
// ============================================================

CudaStub::CudaStub() = default;

CudaStub::~CudaStub() {
    shutdown();
}

CudaResult CudaStub::initialize() {
    if (stub_mode_) {
        // Stub 模式：无需真实初始化
        initialized_ = true;
        return CudaResult::SUCCESS;
    }

    // Phase 2: 真实 CUDA 初始化
    // CUresult cuInit(unsigned int Flags);
    // CUresult cuCtxCreate(CUcontext *pctx, unsigned int flags, CUdevice dev);

    // 暂时返回 SUCCESS（即使没有真实 GPU）
    initialized_ = true;
    return CudaResult::SUCCESS;
}

void CudaStub::shutdown() {
    if (!initialized_) return;

    // 清理所有 Event
    {
        std::lock_guard<std::mutex> lock(events_mutex_);
        for (auto& [id, event] : events_) {
            // Phase 2: cuEventDestroy(event);
            (void)event;  // Stub 模式：无操作
        }
        events_.clear();
    }

    // Phase 2: cuCtxDestroy(context_);
    // (CUcontext 字段已移除, IGpuDriver 不暴露)

    initialized_ = false;
}

// ========== 内存管理 ==========

CudaResult CudaStub::mem_alloc(size_t size, uint64_t* device_ptr) {
    if (!initialized_) {
        return CudaResult::ERROR_NOT_INITIALIZED;
    }

    if (!device_ptr || size == 0) {
        return CudaResult::ERROR_INVALID_VALUE;
    }

    if (stub_mode_) {
        // Stub 模式：模拟分配
        static std::atomic<uint64_t> next_ptr{0x10000};
        *device_ptr = next_ptr.fetch_add(size + 0x1000, std::memory_order_relaxed);
        return CudaResult::SUCCESS;
    }

    // Phase 2: 真实 CUDA 分配
    // CUdeviceptr ptr;
    // CUresult cuMemAlloc(CUdeviceptr *dptr, size_t bytesize);

    // 暂时返回模拟结果
    *device_ptr = 0x10000;
    return CudaResult::SUCCESS;
}

CudaResult CudaStub::mem_free(uint64_t device_ptr) {
    if (!initialized_) {
        return CudaResult::ERROR_NOT_INITIALIZED;
    }

    if (device_ptr == 0) {
        return CudaResult::ERROR_INVALID_VALUE;
    }

    if (stub_mode_) {
        // Stub 模式：模拟释放
        return CudaResult::SUCCESS;
    }

    // Phase 2: 真实 CUDA 释放
    // CUresult cuMemFree(CUdeviceptr dptr);

    return CudaResult::SUCCESS;
}

CudaResult CudaStub::memcpy_h2d(uint64_t dst, const void* src, size_t size) {
    if (!initialized_) {
        return CudaResult::ERROR_NOT_INITIALIZED;
    }

    if (!src || dst == 0) {
        return CudaResult::ERROR_INVALID_VALUE;
    }

    if (stub_mode_) {
        // Stub 模式：模拟拷贝
        return CudaResult::SUCCESS;
    }

    // Phase 2: 真实 CUDA 拷贝
    // CUresult cuMemcpyHtoD(CUdeviceptr dstDevice, const void *srcHost, size_t ByteCount);

    return CudaResult::SUCCESS;
}

CudaResult CudaStub::memcpy_d2h(void* dst, uint64_t src, size_t size) {
    if (!initialized_) {
        return CudaResult::ERROR_NOT_INITIALIZED;
    }

    if (!dst || src == 0) {
        return CudaResult::ERROR_INVALID_VALUE;
    }

    if (stub_mode_) {
        // Stub 模式：模拟拷贝（用 0 填充）
        std::memset(dst, 0, size);
        return CudaResult::SUCCESS;
    }

    // Phase 2: 真实 CUDA 拷贝
    // CUresult cuMemcpyDtoH(void *dstDevice, CUdeviceptr srcDevice, size_t ByteCount);

    return CudaResult::SUCCESS;
}

CudaResult CudaStub::memcpy_d2d(uint64_t dst, uint64_t src, size_t size) {
    if (!initialized_) {
        return CudaResult::ERROR_NOT_INITIALIZED;
    }

    if (dst == 0 || src == 0) {
        return CudaResult::ERROR_INVALID_VALUE;
    }

    if (stub_mode_) {
        // Stub 模式：模拟拷贝
        return CudaResult::SUCCESS;
    }

    // Phase 2: 真实 CUDA 拷贝
    // CUresult cuMemcpyDtoD(CUdeviceptr dstDevice, CUdeviceptr srcDevice, size_t ByteCount);

    return CudaResult::SUCCESS;
}

// ========== Kernel 启动 ==========

CudaResult CudaStub::launch_kernel(const LaunchParams& params, uint64_t* task_id) {
    if (!initialized_) {
        return CudaResult::ERROR_NOT_INITIALIZED;
    }

    if (!params.kernel_name || !task_id) {
        return CudaResult::ERROR_INVALID_VALUE;
    }

    if (stub_mode_) {
        // Stub 模式：模拟启动
        *task_id = next_task_id_.fetch_add(1, std::memory_order_relaxed);
        return CudaResult::SUCCESS;
    }

    // Phase 2: 真实 CUDA Kernel 启动
    *task_id = next_task_id_.fetch_add(1, std::memory_order_relaxed);
    return CudaResult::SUCCESS;
}

// ========== 同步原语 ==========

CudaResult CudaStub::create_event(uint64_t* event_id) {
    if (!initialized_) {
        return CudaResult::ERROR_NOT_INITIALIZED;
    }

    if (!event_id) {
        return CudaResult::ERROR_INVALID_VALUE;
    }

    std::lock_guard<std::mutex> lock(events_mutex_);

    if (stub_mode_) {
        // Stub 模式：模拟创建
        *event_id = next_event_id_.fetch_add(1, std::memory_order_relaxed);
        return CudaResult::SUCCESS;
    }

    // Phase 2: 真实 CUDA Event 创建
    *event_id = next_event_id_.fetch_add(1, std::memory_order_relaxed);
    return CudaResult::SUCCESS;
}

CudaResult CudaStub::record_event(uint64_t event_id) {
    if (!initialized_) {
        return CudaResult::ERROR_NOT_INITIALIZED;
    }

    if (stub_mode_) {
        return CudaResult::SUCCESS;
    }

    return CudaResult::SUCCESS;
}

CudaResult CudaStub::wait_event(uint64_t event_id, uint64_t timeout_ms) {
    if (!initialized_) {
        return CudaResult::ERROR_NOT_INITIALIZED;
    }

    if (stub_mode_) {
        (void)event_id;
        (void)timeout_ms;
        return CudaResult::SUCCESS;
    }

    (void)event_id;
    (void)timeout_ms;
    return CudaResult::SUCCESS;
}

CudaResult CudaStub::query_event(uint64_t event_id, int* signaled) {
    if (!initialized_) {
        return CudaResult::ERROR_NOT_INITIALIZED;
    }

    if (!signaled) {
        return CudaResult::ERROR_INVALID_VALUE;
    }

    if (stub_mode_) {
        *signaled = 1;
        return CudaResult::SUCCESS;
    }

    *signaled = 1;
    return CudaResult::SUCCESS;
}

CudaResult CudaStub::destroy_event(uint64_t event_id) {
    if (!initialized_) {
        return CudaResult::ERROR_NOT_INITIALIZED;
    }

    std::lock_guard<std::mutex> lock(events_mutex_);

    if (stub_mode_) {
        events_.erase(event_id);
        return CudaResult::SUCCESS;
    }

    events_.erase(event_id);
    return CudaResult::SUCCESS;
}

// ============================================================
// IGpuDriver 28 方法实现 (H-2.5)
// mock 语义: 递增 handle + host malloc + 零 ioctl
// ============================================================

// ----- 设备信息 -----

int CudaStub::get_device_info(struct gpu_device_info* out) {
    if (!out) return -1;
    std::memset(out, 0, sizeof(*out));
    // mock 设备信息
    out->vendor_id = 0x10DE;  // NVIDIA mock
    out->device_id = 0x1234;
    out->vram_size = 8ULL * 1024 * 1024 * 1024;  // 8 GB
    out->bar0_size = 16 * 1024 * 1024;            // 16 MB
    out->compute_units = 80;
    out->warp_size = 32;
    out->simd_count = 80;
    out->driver_version = 0x000500;  // v0.5.0
    out->firmware_version = 0x000100;
    out->max_clock_frequency = 1500;
    out->max_memory_clock_frequency = 1000;
    out->memory_bus_width = 256;
    out->peak_fp32_gflops = 10000;
    out->pcie_bandwidth = 16000;
    out->architecture_id = 0xC0DE;
    std::strncpy(out->marketing_name, "CudaStub Mock GPU", sizeof(out->marketing_name) - 1);
    return 0;
}

int CudaStub::get_driver_version_string(char* out, size_t size) {
    if (!out || size == 0) return -1;
    std::strncpy(out, "v0.5.0-mock", size - 1);
    out[size - 1] = '\0';
    return 0;
}

int CudaStub::get_marketing_name(char* out, size_t size) {
    if (!out || size == 0) return -1;
    std::strncpy(out, "CudaStub Mock GPU", size - 1);
    out[size - 1] = '\0';
    return 0;
}

void CudaStub::print_device_info(std::ostream& os) {
    char version[32];
    char name[64];
    get_driver_version_string(version, sizeof(version));
    get_marketing_name(name, sizeof(name));
    os << "=== CudaStub Mock Device Info ===\n";
    os << "  Driver Ver: " << version << "\n";
    os << "  Marketing:  " << name << "\n";
    os << "  Warp:       " << get_warp_size() << "\n";
    os << "  SIMD:       " << get_simd_count() << "\n";
    os << "  FP32 Peak:  " << get_peak_fp32_gflops() << " GFLOPS\n";
}

// ----- 缓冲区对象 (4) - mock 语义 -----

uint64_t CudaStub::alloc_bo(uint64_t size, uint32_t flags) {
    (void)size;
    (void)flags;
    if (size == 0) return 0;
    return next_bo_handle_.fetch_add(1, std::memory_order_relaxed);
}

uint64_t CudaStub::alloc_bo_vram(uint64_t size, uint32_t flags) {
    // mock: 与 alloc_bo 一致（mock 不区分 VRAM/HOST）
    return alloc_bo(size, flags);
}

int CudaStub::free_bo(uint64_t bo_handle) {
    if (bo_handle == 0) return -1;
    // mock: 立即成功，不追踪分配
    return 0;
}

void* CudaStub::map_bo(uint64_t bo_handle, uint64_t size) {
    if (bo_handle == 0 || size == 0) return nullptr;
    // mock: 用 host malloc 模拟 CPU 映射
    // 注意: caller 应通过 free_bo 释放（或接受测试中的 leak）
    return std::malloc(size);
}

// ----- 提交 (3) - 返回递增 fence_id -----

int64_t CudaStub::submit_batch(uint32_t stream_id,
                                const struct gpu_gpfifo_entry* entries,
                                uint32_t count, uint32_t flags) {
    (void)stream_id;
    (void)entries;
    (void)count;
    (void)flags;
    // mock: 总是返回递增 fence_id
    return static_cast<int64_t>(next_fence_id_.fetch_add(1, std::memory_order_relaxed));
}

int64_t CudaStub::submit_memcpy(uint32_t stream_id, uint64_t src_addr, uint64_t dst_addr,
                                 uint64_t size, bool is_h2d) {
    (void)stream_id;
    (void)src_addr;
    (void)dst_addr;
    (void)size;
    (void)is_h2d;
    return static_cast<int64_t>(next_fence_id_.fetch_add(1, std::memory_order_relaxed));
}

int64_t CudaStub::submit_launch(uint32_t stream_id, uint32_t kernel_index,
                                 uint32_t grid_x, uint32_t grid_y, uint32_t grid_z,
                                 uint32_t block_x, uint32_t block_y,
                                 uint32_t block_z) {
    (void)stream_id;
    (void)kernel_index;
    (void)grid_x; (void)grid_y; (void)grid_z;
    (void)block_x; (void)block_y; (void)block_z;
    return static_cast<int64_t>(next_fence_id_.fetch_add(1, std::memory_order_relaxed));
}

// ----- Fence 等待 (2 重载) - mock 立即成功 -----

int CudaStub::wait_fence(uint64_t fence_id, uint32_t timeout_ms, uint32_t* status_out) {
    (void)fence_id;
    (void)timeout_ms;
    if (status_out) *status_out = 1;  // signaled
    return 0;
}

int CudaStub::wait_fence(uint64_t fence_id) {
    (void)fence_id;
    return 0;
}

// ============================================================
// H-3 Phase 2 (5) - mock 语义 (pure in-memory state machine)
// ============================================================

uint64_t CudaStub::create_va_space(uint32_t flags) {
    // mock: is_open() 永远 true（CudaStub 是 in-process mock）
    // 递增 handle（monotonic from 1 per R2 spec）
    uint64_t handle = next_va_space_handle_.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(mock_state_mutex_);
        va_space_map_[handle] = true;  // mock 资源登记
    }
    std::cerr << "[CudaStub] create_va_space(flags=" << flags << ") → handle=" << handle
              << std::endl;
    return handle;
}

int CudaStub::destroy_va_space(uint64_t va_space_handle) {
    if (va_space_handle == 0) {
        // H-1 sentinel guard - 按 spec R2 一致性**不**打 log
        return -1;
    }
    std::lock_guard<std::mutex> lock(mock_state_mutex_);
    auto it = va_space_map_.find(va_space_handle);
    if (it == va_space_map_.end()) {
        std::cerr << "[CudaStub] destroy_va_space: handle " << va_space_handle
                  << " not found (mock 资源未登记)" << std::endl;
        return -1;  // mock: 资源不存在
    }
    va_space_map_.erase(it);
    std::cerr << "[CudaStub] destroy_va_space(handle=" << va_space_handle
              << ") → success (mock)" << std::endl;
    return 0;
}

int CudaStub::register_gpu(uint64_t va_space_handle, uint32_t gpu_id, uint32_t flags) {
    if (va_space_handle == 0) {
        // H-1 sentinel guard - 按 spec R3 一致性**不**打 log
        return -1;
    }
    std::cerr << "[CudaStub] register_gpu(va_space=" << va_space_handle
              << ", gpu_id=" << gpu_id << ", flags=" << flags << ") → mock success"
              << std::endl;
    return 0;
}

uint64_t CudaStub::create_queue(uint64_t va_space_handle, uint32_t queue_type,
                                uint32_t priority, uint64_t ring_buffer_size) {
    if (va_space_handle == 0) {
        // H-1 sentinel guard - 按 spec R2 一致性**不**打 log
        return 0;
    }
    if (priority > 100) {
        std::cerr << "[CudaStub] create_queue: invalid priority " << priority
                  << " (valid range: 0-100)" << std::endl;
        return 0;
    }
    if (ring_buffer_size == 0) {
        std::cerr << "[CudaStub] create_queue: invalid ring_buffer_size 0"
                  << std::endl;
        return 0;
    }
    uint64_t handle = next_queue_handle_.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(mock_state_mutex_);
        queue_map_[handle] = true;  // mock 资源登记
    }
    std::cerr << "[CudaStub] create_queue(va_space=" << va_space_handle
              << ", type=" << queue_type << ", priority=" << priority
              << ", ring_size=" << ring_buffer_size << ") → handle=" << handle
              << std::endl;
    return handle;
}

int CudaStub::destroy_queue(uint64_t queue_handle) {
    if (queue_handle == 0) {
        // H-1 sentinel guard - 按 spec R2 一致性**不**打 log
        return -1;
    }
    std::lock_guard<std::mutex> lock(mock_state_mutex_);
    auto it = queue_map_.find(queue_handle);
    if (it == queue_map_.end()) {
        std::cerr << "[CudaStub] destroy_queue: handle " << queue_handle
                  << " not found (mock 资源未登记)" << std::endl;
        return -1;
    }
    queue_map_.erase(it);
    std::cerr << "[CudaStub] destroy_queue(handle=" << queue_handle
              << ") → success (mock)" << std::endl;
    return 0;
}

} // namespace gpu
} // namespace async_task
