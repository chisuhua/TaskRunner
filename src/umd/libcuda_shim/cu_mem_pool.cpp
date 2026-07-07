// SCOPE: UMD-EVOLUTION
// cu_mem_pool.cpp - CUDA memory pool API (Phase 3.2 REAL_IMPL).
//
// Implements 8 cuMemPool* APIs with self-contained atomic + map + mutex state.
// Matches the shim-local pattern used by cu_stream.cpp / cu_event.cpp / cu_mem.cpp
// / cu_stream_capture.cpp / cu_graph*.cpp.
//
// Shim does NOT call GpuDriverClient (D-S3-1 decision). Pool handles are
// allocated from a monotonic counter; alloc returns a synthetic VA pointer
// that is NOT dereferenceable (PoC limitation). Phase 4+ bridges to
// g_gpu_client->mem_pool_alloc() IOCTL for real VA sub-range reservation.
//
// B-2 enforced: cuMemPoolCreate rejects vaSpaceHandle=0 (H-1 sentinel).
// F-2 enforced: cuMemPoolSetAttribute/GetAttribute accept only RELEASE_THRESHOLD
// and REUSE_FOLLOW_EVENT_DEPENDENCIES; other attributes return NOT_SUPPORTED.

#include <cuda.h>

// Phase 4 (M2): expose g_gpu_client global + IGpuDriver interface
#include "test_fixture/gpu_driver_client.h"

#include <atomic>
#include <cstdint>
#include <cstring>
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
  if (!ptr || !pool) return CUDA_ERROR_INVALID_VALUE;
  if (size == 0) return CUDA_ERROR_INVALID_VALUE;
  (void)props;
  // PoC: synthetic non-dereferenceable VA = pool_handle | alloc_counter.
  // Lower bits vary per alloc so each allocation produces a unique handle.
  // Phase 4+ bridges to g_gpu_client->mem_pool_alloc() IOCTL for real VA.
  uint64_t aid = async_task::umd::shim::g_pools.next_alloc_id.fetch_add(1);
  *ptr = reinterpret_cast<CUmemPoolPtr>(static_cast<uintptr_t>(aid));
  return CUDA_SUCCESS;
}

extern "C" CUresult cuMemPoolFree(CUmemPoolPtr ptr, CUmemPool pool) {
  if (!ptr) return CUDA_ERROR_INVALID_VALUE;
  if (!pool) return CUDA_ERROR_INVALID_VALUE;
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
  (void)handleType;
  (void)flags;
  return CUDA_SUCCESS;
}