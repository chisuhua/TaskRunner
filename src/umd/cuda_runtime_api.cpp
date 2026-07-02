// SCOPE: UMD-EVOLUTION
// STATUS: PROPOSED (Phase 1 implementation)
/**
 * cuda_runtime_api.cpp - CudaRuntimeApi implementation
 *
 * Phase 1 PoC: wraps CudaScheduler to provide cudaMalloc/cudaMemcpy/cudaLaunchKernel.
 *
 * Key mappings:
 *   - malloc  → scheduler_->submit_mem_alloc() + wait_fence()
 *   - memcpy  → scheduler_->submit_memcpy_h2d/d2h() + wait_fence()
 *   - launch  → scheduler_->submit_launch(LaunchParams) + wait_fence()
 *   - ctor    → driver_->create_va_space() + driver_->create_queue()
 *   - dtor    → driver_->destroy_queue() + driver_->destroy_va_space()
 */

#include "umd/cuda_runtime_api.hpp"
#include "test_fixture/cuda_scheduler.hpp"

#include <stdexcept>
#include <utility>

namespace async_task::umd {

CudaRuntimeApi::CudaRuntimeApi(taskrunner::CudaScheduler* scheduler)
    : scheduler_(scheduler) {
  if (!scheduler_) {
    throw std::invalid_argument("CudaRuntimeApi: scheduler is nullptr");
  }

  std::lock_guard<std::mutex> lock(mu_);

  // Phase 1: Create VA space and queue via IGpuDriver (H-3 Phase 2 APIs).
  // flags=0 means default; queue_type=0=compute, priority=0, ring_size=4096 PoC default.
  va_space_handle_ = scheduler_->driver()->create_va_space(0);
  queue_handle_ = scheduler_->driver()->create_queue(
      va_space_handle_, /*queue_type=*/0, /*priority=*/0,
      /*ring_buffer_size=*/4096);
}

CudaRuntimeApi::~CudaRuntimeApi() {
  if (scheduler_) {
    auto* drv = scheduler_->driver();
    if (!drv) return;

    if (queue_handle_) {
      drv->destroy_queue(queue_handle_);
    }
    if (va_space_handle_) {
      drv->destroy_va_space(va_space_handle_);
    }
  }
}

CudaError CudaRuntimeApi::register_kernel(const std::string& name,
                                          std::uint32_t index) {
  if (name.empty()) return CudaError::InvalidValue;

  std::lock_guard<std::mutex> lock(mu_);
  auto [it, inserted] = kernel_registry_.try_emplace(name, index);
  if (!inserted) {
    return CudaError::InvalidValue;  // duplicate
  }
  return CudaError::Success;
}

CudaError CudaRuntimeApi::malloc(void** devPtr, std::size_t size) {
  if (!devPtr || size == 0) return CudaError::InvalidValue;

  std::lock_guard<std::mutex> lock(mu_);

  auto result = scheduler_->submit_mem_alloc(size);
  if (result.status != 0) {
    return CudaError::Unknown;
  }

  // Wait for completion (synchronous by default in Phase 1).
  if (scheduler_->wait_fence(result.fence_id) != 0) {
    return CudaError::Unknown;
  }

  void* ptr = reinterpret_cast<void*>(result.device_ptr);
  allocations_.emplace(ptr, DeviceMem{va_space_handle_, result.device_ptr, size});
  *devPtr = ptr;
  return CudaError::Success;
}

CudaError CudaRuntimeApi::memcpy(void* dst, const void* src,
                                 std::size_t count, CudaMemcpyKind kind) {
  if (!dst || !src || count == 0) return CudaError::InvalidValue;

  if (kind == CudaMemcpyKind::DeviceToDevice ||
      kind == CudaMemcpyKind::HostToHost) {
    return CudaError::NotSupported;  // Phase 1 limitation
  }

  std::lock_guard<std::mutex> lock(mu_);

  int fence_id = 0;
  if (kind == CudaMemcpyKind::HostToDevice) {
    // scheduler_->submit_memcpy_h2d(device_ptr, offset, host_ptr, size)
    fence_id = scheduler_->submit_memcpy_h2d(
        reinterpret_cast<std::uint64_t>(dst),
        /*offset=*/0, src, count);
  } else {  // DeviceToHost
    // scheduler_->submit_memcpy_d2h(host_ptr, device_ptr, offset, size)
    fence_id = scheduler_->submit_memcpy_d2h(
        dst, reinterpret_cast<std::uint64_t>(src),
        /*offset=*/0, count);
  }

  if (fence_id <= 0) return CudaError::Unknown;
  return scheduler_->wait_fence(static_cast<std::uint64_t>(fence_id)) == 0
             ? CudaError::Success
             : CudaError::Unknown;
}

CudaError CudaRuntimeApi::launch_kernel(const std::string& name,
                                        Dim3 gridDim, Dim3 blockDim,
                                        void** args, std::size_t sharedMem) {
  if (name.empty()) return CudaError::InvalidValue;

  // Build LaunchParams from the API call.  The kernel name is passed through;
  // args are serialized by CudaScheduler/Stub internally.
  async_task::gpu::LaunchParams params{};
  params.kernel_name = name.c_str();
  params.grid_dim_x = gridDim.x;
  params.grid_dim_y = gridDim.y;
  params.grid_dim_z = gridDim.z;
  params.block_dim_x = blockDim.x;
  params.block_dim_y = blockDim.y;
  params.block_dim_z = blockDim.z;
  params.shared_mem_bytes = static_cast<std::uint32_t>(sharedMem);
  // Phase 1: args passed as opaque pointer (LaunchParams::params).
  // CudaStub::launch_kernel currently ignores params and params_size
  // (mock semantics).  A future Phase N will wire serialisation.
  params.params = args;
  params.params_size = 0;  // caller-owned; serialisation TBD

  auto result = scheduler_->submit_launch(params);
  if (result.status != 0) return CudaError::Unknown;
  if (result.fence_id == 0) return CudaError::Unknown;

  return scheduler_->wait_fence(result.fence_id) == 0 ? CudaError::Success
                                                       : CudaError::Unknown;
}

std::size_t CudaRuntimeApi::get_total_memory() {
  if (!scheduler_) return 0;
  auto* drv = scheduler_->driver();
  if (!drv) return 0;
  gpu_device_info info{};
  if (drv->get_device_info(&info) != 0) return 0;
  return info.vram_size;
}

}  // namespace async_task::umd
