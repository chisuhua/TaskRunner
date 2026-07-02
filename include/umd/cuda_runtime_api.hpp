// SCOPE: UMD-EVOLUTION
// STATUS: PROPOSED (Phase 1 implementation in progress; see docs/superpowers/specs/2026-06-30-umd-evolution-redesign.md)
// Phase 1: CUDA Runtime API surface (cudaMalloc / cudaMemcpy / cudaLaunchKernel) wrapping CudaScheduler.

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

// Forward declaration.
namespace taskrunner {
class CudaScheduler;
}  // namespace taskrunner

namespace async_task::umd {

// Minimal CUDA Runtime API compatible error codes.
enum class CudaError {
  Success = 0,
  InvalidValue = 1,
  NotSupported = 2,
  OutOfMemory = 3,
  Unknown = 999,
};

// CUDA Memcpy kinds (subset).
enum class CudaMemcpyKind {
  HostToDevice = 0,
  DeviceToHost = 1,
  DeviceToDevice = 2,
  HostToHost = 3,
};

// Lightweight grid/block dims (cuda runtime uses dim3).
struct Dim3 {
  unsigned int x{1};
  unsigned int y{1};
  unsigned int z{1};
};

/**
 * CudaRuntimeApi — Phase 1 PoC wrapper over CudaScheduler.
 *
 * Provides 3 CUDA Runtime API methods (cudaMalloc / cudaMemcpy / cudaLaunchKernel)
 * with synchronous semantics (default). Builds on the proven IGpuDriver 31-method
 * abstraction via CudaScheduler (which already handles MemoryManager + fence +
 * kernel arg serialization via LaunchParams).
 *
 * **Scope (Phase 1)**:
 * - H2D / D2H memcpy only; D2D and H2H return CudaError::NotSupported
 * - Single stream (cuStream_t parameter reserved, ignored in Phase 1)
 * - Kernel names manually registered via register_kernel()
 *
 * **Phase 1 known constraints**:
 * - Only CudaStub and MockGpuDriver backends are fully wired.
 *   GpuDriverClient backend returns CudaError::Unknown for some operations
 *   due to existing dynamic_cast<CudaStub*> abstraction leakage in
 *   CudaScheduler (tracked separately).
 */
class CudaRuntimeApi {
 public:
  /**
   * @brief Construct with a CudaScheduler instance (DI).
   * @param scheduler Non-owning pointer; must outlive this object.
   */
  explicit CudaRuntimeApi(taskrunner::CudaScheduler* scheduler);

  ~CudaRuntimeApi();

  // Disable copy (resource handles are tied to scheduler lifetime).
  CudaRuntimeApi(const CudaRuntimeApi&) = delete;
  CudaRuntimeApi& operator=(const CudaRuntimeApi&) = delete;

  /**
   * @brief Register a kernel name to index mapping (Phase 1 manual mapping).
   *
   * In Phase 1, the CudaScheduler submit_launch method takes a kernel_name
   * string via LaunchParams, but the lower-level IGpuDriver submit_launch uses
   * a uint32_t kernel_index. CudaRuntimeApi maintains this mapping so callers
   * can launch by name. Duplicate names return CudaError::InvalidValue.
   *
   * @param name  Kernel function name.
   * @param index Numeric index used by IGpuDriver submit_launch.
   * @return CudaError::Success on success.
   */
  CudaError register_kernel(const std::string& name, std::uint32_t index);

  /**
   * @brief Allocate device memory.
   *
   * Maps to CudaScheduler submit_mem_alloc + wait_fence.
   *
   * @param devPtr Output device pointer (receives handle value).
   * @param size   Bytes to allocate.
   * @return CudaError::Success on success.
   */
  CudaError malloc(void** devPtr, std::size_t size);

  /**
   * @brief Copy memory between host and device.
   *
   * Synchronous by default. Only H2D and D2H are supported; D2D and H2H
   * return CudaError::NotSupported.
   *
   * @param dst   Destination pointer (host or device depending on kind).
   * @param src   Source pointer.
   * @param count Bytes to copy.
   * @param kind  Direction enum.
   * @return CudaError::Success on success.
   */
  CudaError memcpy(void* dst, const void* src, std::size_t count,
                   CudaMemcpyKind kind);

  /**
   * @brief Launch a kernel by name with synchronous semantics.
   *
   * Maps to CudaScheduler submit_launch + wait_fence.
   * Args are passed through to CudaScheduler (which performs serialization).
   *
   * @param name      Kernel name (must be registered).
   * @param gridDim   Grid dimensions.
   * @param blockDim  Block dimensions.
   * @param args      Kernel arguments array (may be nullptr if num_args == 0).
   * @param sharedMem Dynamic shared memory size (bytes).
   * @return CudaError::Success on success.
   */
CudaError launch_kernel(const std::string& name, Dim3 gridDim,
                          Dim3 blockDim, void** args,
                          std::size_t sharedMem);

  /**
   * @brief Query the underlying IGpuDriver's total VRAM size.
   *
   * Returns the value of gpu_device_info::vram_size from the driver backend
   * (CudaStub mock value or GpuDriverClient real value). Used by shim layer
   * to back cuMemGetInfo without hardcoding fake values.
   *
   * @return Total device memory in bytes, or 0 if backend unavailable.
   */
  std::size_t get_total_memory();

  private:
  // Device memory allocation tracking.
  struct DeviceMem {
    std::uint64_t va_space_handle;
    std::uint64_t handle;  // device pointer (raw integer handle)
    std::size_t size;
  };

  taskrunner::CudaScheduler* scheduler_;
  std::uint64_t va_space_handle_{0};
  std::uint64_t queue_handle_{0};

  std::unordered_map<std::string, std::uint32_t> kernel_registry_;
  std::unordered_map<void*, DeviceMem> allocations_;

  std::mutex mu_;  // protects handles, registry, allocations
};

}  // namespace async_task::umd
