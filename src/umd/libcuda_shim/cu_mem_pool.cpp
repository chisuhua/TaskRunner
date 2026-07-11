// SCOPE: UMD-EVOLUTION
// cu_mem_pool.cpp - CUDA memory pool API (Phase 3.2+4 REAL bridge).
//
// Implements 8+2 cuMemPool* APIs with self-contained atomic + map + mutex state.
// Matches the shim-local pattern used by cu_stream.cpp / cu_event.cpp / cu_mem.cpp
// / cu_stream_capture.cpp / cu_graph*.cpp.
//
// Phase 4: cuMemPoolAlloc bridges to g_gpu_client->mem_pool_alloc(), new
// cuMemPoolAllocAsync / cuMemPoolFreeAsync bridge to the corresponding async
// driver methods, cuMemPoolExportToShareableHandle bridges to
// mem_pool_export_shareable. When g_gpu_client is nullptr, all bridged APIs
// fall back to a Meyers-singleton CudaStub (g-gpu-client-meyers-singleton-fallback).
//
// B-2 enforced: cuMemPoolCreate rejects vaSpaceHandle=0 (H-1 sentinel).
// F-2 enforced: cuMemPoolSetAttribute/GetAttribute accept only RELEASE_THRESHOLD
// and REUSE_FOLLOW_EVENT_DEPENDENCIES; other attributes return NOT_SUPPORTED.

#include <cuda.h>

// Phase 4 (M2): expose g_gpu_client global + IGpuDriver interface
#include "test_fixture/gpu_driver_client.h"

// g-gpu-client-meyers-singleton-fallback: Meyers fallback for null g_gpu_client
#include "cuda_driver_accessor.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <unordered_map>

namespace async_task::umd::shim {
namespace {

struct MemPoolTable {
  std::atomic<std::uint64_t> next_pool_id{1};
  std::atomic<std::uint64_t> next_alloc_id{1};
  std::unordered_map<CUmemPool, std::size_t> pool_maxSize;
  std::mutex mu;
};
MemPoolTable g_pools;

// Replaced by cuda_driver_accessor.hpp::get_driver_or_default() (Meyers fallback).

}  // namespace
}  // namespace async_task::umd::shim

extern "C" CUresult cuMemPoolCreate(CUmemPool* pool,
                                     const CUmemPoolProps* poolProps) {
  if (!pool || !poolProps) return CUDA_ERROR_INVALID_VALUE;
  if (poolProps->vaSpaceHandle == 0) return CUDA_ERROR_INVALID_VALUE;
  auto& t = async_task::umd::shim::g_pools;
  std::lock_guard<std::mutex> lock(t.mu);
  uint64_t id = t.next_pool_id.fetch_add(1);
  *pool = reinterpret_cast<CUmemPool>(static_cast<std::uintptr_t>(id));
  t.pool_maxSize[*pool] = poolProps->maxSize;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuMemPoolDestroy(CUmemPool pool) {
  if (!pool) return CUDA_ERROR_INVALID_VALUE;
  auto& t = async_task::umd::shim::g_pools;
  std::lock_guard<std::mutex> lock(t.mu);
  t.pool_maxSize.erase(pool);
  return CUDA_SUCCESS;
}

extern "C" CUresult cuMemPoolAlloc(CUmemPoolPtr* ptr, size_t size,
                                    CUmemPool pool, CUmemPoolProps* props) {
  if (!ptr || !pool || size == 0) return CUDA_ERROR_INVALID_VALUE;
  (void)props;
  auto* driver = async_task::umd::shim::get_driver_or_default();
  uint64_t va = 0;
  if (driver->mem_pool_alloc(reinterpret_cast<uint64_t>(pool), size, &va) < 0) {
    return CUDA_ERROR_UNKNOWN;
  }
  *ptr = reinterpret_cast<CUmemPoolPtr>(static_cast<uintptr_t>(va));
  return CUDA_SUCCESS;
}

extern "C" CUresult cuMemPoolFree(CUmemPoolPtr ptr, CUmemPool pool) {
  if (!ptr) return CUDA_ERROR_INVALID_VALUE;
  if (!pool) return CUDA_ERROR_INVALID_VALUE;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuMemPoolAllocAsync(CUmemPoolPtr* ptr, size_t size,
                                         CUmemPool pool, CUstream hStream,
                                         CUmemPoolProps* props) {
  if (!ptr || !pool || size == 0) return CUDA_ERROR_INVALID_VALUE;
  (void)props;
  auto* driver = async_task::umd::shim::get_driver_or_default();
  uint64_t va = 0;
  int64_t fence = driver->mem_pool_alloc_async(
      reinterpret_cast<uint64_t>(pool), size,
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hStream)),
      &va);
  if (fence < 0) return CUDA_ERROR_UNKNOWN;
  *ptr = reinterpret_cast<CUmemPoolPtr>(static_cast<uintptr_t>(va));
  return CUDA_SUCCESS;
}

extern "C" CUresult cuMemPoolFreeAsync(CUmemPoolPtr ptr, CUstream hStream, CUmemPool pool) {
  if (!ptr || !pool) return CUDA_ERROR_INVALID_VALUE;
  auto* driver = async_task::umd::shim::get_driver_or_default();
  int64_t fence = driver->mem_pool_free_async(
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ptr)),
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hStream)));
  if (fence < 0) return CUDA_ERROR_UNKNOWN;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuMemPoolSetAttribute(CUmemPool pool, CUmemPoolAttribute attr,
                                            const void* value) {
  if (!pool || !value) return CUDA_ERROR_INVALID_VALUE;
  if (attr != CU_MEMPOOL_ATTR_RELEASE_THRESHOLD &&
      attr != CU_MEMPOOL_ATTR_REUSE_FOLLOW_EVENT_DEPENDENCIES) {
    return CUDA_ERROR_NOT_SUPPORTED;
  }
  return CUDA_SUCCESS;
}

extern "C" CUresult cuMemPoolGetAttribute(CUmemPool pool, CUmemPoolAttribute attr,
                                            void* value) {
  if (!pool || !value) return CUDA_ERROR_INVALID_VALUE;
  if (attr != CU_MEMPOOL_ATTR_RELEASE_THRESHOLD &&
      attr != CU_MEMPOOL_ATTR_REUSE_FOLLOW_EVENT_DEPENDENCIES) {
    return CUDA_ERROR_NOT_SUPPORTED;
  }
  std::memset(value, 0, sizeof(int));
  return CUDA_SUCCESS;
}

extern "C" CUresult cuMemPoolTrimTo(CUmemPool pool, size_t minBytesToKeep) {
  if (!pool) return CUDA_ERROR_INVALID_VALUE;
  (void)minBytesToKeep;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuMemPoolExportToShareableHandle(void* shareableHandle,
                                                      CUmemPool pool,
                                                      CUmemPoolHandleType handleType,
                                                      unsigned int flags) {
  if (!shareableHandle || !pool) return CUDA_ERROR_INVALID_VALUE;
  if (handleType != CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR) {
    return CUDA_ERROR_NOT_SUPPORTED;
  }
  auto* driver = async_task::umd::shim::get_driver_or_default();
  int fd_out = -1;
  if (driver->mem_pool_export_shareable(
          reinterpret_cast<uint64_t>(pool),
          static_cast<uint32_t>(handleType),
          flags, &fd_out) < 0) {
    return CUDA_ERROR_UNKNOWN;
  }
  *reinterpret_cast<int*>(shareableHandle) = fd_out;
  return CUDA_SUCCESS;
}