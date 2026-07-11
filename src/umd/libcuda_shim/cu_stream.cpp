// SCOPE: UMD-EVOLUTION
// cu_stream.cpp - Stream management (Phase 2 PoC stubs).
//
// Provides handle-tracking for cuStreamCreate/Destroy with atomic ID
// allocation. Synchronization operations are no-ops (Phase 2 synchronous).

#include <cuda.h>

// Phase 4: cuStreamSynchronize uses g_gpu_client->wait_fence + stream fence registry
#include "test_fixture/gpu_driver_client.h"
#include "stream_fence_registry.hpp"

// g-gpu-client-meyers-singleton-fallback: Meyers fallback for null g_gpu_client
#include "cuda_driver_accessor.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace async_task::umd::shim {
namespace {

struct StreamTable {
  std::atomic<std::uint64_t> next_id{1};
  std::unordered_map<CUstream, std::uint64_t> active;
  std::mutex mu;
};
StreamTable g_streams;

}  // namespace
}  // namespace async_task::umd::shim

extern "C" CUresult cuStreamCreate(CUstream* phStream, unsigned int flags) {
  if (!phStream) return CUDA_ERROR_INVALID_VALUE;
  auto& table = async_task::umd::shim::g_streams;
  std::lock_guard<std::mutex> lock(table.mu);
  *phStream = reinterpret_cast<CUstream>(
      static_cast<std::uintptr_t>(table.next_id.fetch_add(1)));
  table.active[*phStream] = 1;
  (void)flags;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuStreamDestroy(CUstream hStream) {
  if (!hStream) return CUDA_ERROR_INVALID_VALUE;
  auto& table = async_task::umd::shim::g_streams;
  std::lock_guard<std::mutex> lock(table.mu);
  table.active.erase(hStream);
  return CUDA_SUCCESS;
}

extern "C" CUresult cuStreamSynchronize(CUstream hStream) {
  // 先查 fence：无 pending fence → SUCCESS（向后兼容无 driver、无 graph launch 场景）
  uint64_t fence_id = async_task::umd::shim::get_stream_last_fence(hStream);
  if (fence_id == 0) return CUDA_SUCCESS;
  // 有 pending fence 时通过 fallback accessor 获取 driver（Meyers singleton if null）
  auto* driver = async_task::umd::shim::get_driver_or_default();
  uint32_t status = 0;
  int ret = driver->wait_fence(fence_id, 0, &status);
  if (ret != 0)    return CUDA_ERROR_UNKNOWN;
  if (status == 1) return CUDA_SUCCESS;
  if (status == static_cast<uint32_t>(-1)) return CUDA_ERROR_UNKNOWN;
  return CUDA_ERROR_UNKNOWN;
}

extern "C" CUresult cuStreamQuery(CUstream hStream) {
  (void)hStream;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuStreamWaitEvent(CUstream hStream, CUevent hEvent,
                                      unsigned int Flags) {
  (void)hStream;
  (void)hEvent;
  (void)Flags;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuStreamCreateWithPriority(CUstream* phStream,
                                               unsigned int flags,
                                               int priority) {
  (void)priority;
  return cuStreamCreate(phStream, flags);
}

extern "C" CUresult cuStreamGetPriority(CUstream hStream, int* priority) {
  if (!priority) return CUDA_ERROR_INVALID_VALUE;
  *priority = 0;
  (void)hStream;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuStreamGetFlags(CUstream hStream, unsigned int* flags) {
  if (!flags) return CUDA_ERROR_INVALID_VALUE;
  *flags = 0;
  (void)hStream;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuStreamAddCallback(CUstream hStream,
                                        CUstreamCallback callback,
                                        void* userData, unsigned int flags) {
  (void)hStream;
  (void)callback;
  (void)userData;
  (void)flags;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuStreamWriteValue32(CUstream hStream, CUdeviceptr addr,
                                         cuuint32_t value,
                                         unsigned int flags) {
  (void)hStream;
  (void)addr;
  (void)value;
  (void)flags;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuStreamWaitValue32(CUstream hStream, CUdeviceptr addr,
                                        cuuint32_t value,
                                        unsigned int flags) {
  (void)hStream;
  (void)addr;
  (void)value;
  (void)flags;
  return CUDA_SUCCESS;
}

// ---------------------------------------------------------------------------
// Phase 1.7 — A.4: cuStreamCreateWithFlags
// ---------------------------------------------------------------------------

extern "C" CUresult cuStreamCreateWithFlags(CUstream* phStream,
                                             unsigned int flags) {
  (void)flags;
  return cuStreamCreate(phStream, 0);
}

// ---------------------------------------------------------------------------
// Phase 3.1: cuStreamGetCaptureInfo delegates to cu_stream_capture.cpp
// ---------------------------------------------------------------------------
//
// cuStreamGetCaptureInfo lives here (not in cu_stream_capture.cpp) because
// it is a query on stream state, and the existing cu_stream.cpp already owns
// cuStreamCreate/cuStreamDestroy handle tracking. It is implemented in terms
// of the cuStreamIsCapturing REAL_IMPL in cu_stream_capture.cpp so that all
// capture state transitions go through a single code path.

extern "C" CUresult cuStreamGetCaptureInfo(CUstream hStream,
                                            CUstreamCaptureStatus* captureStatus,
                                            cuuint64_t* id) {
  if (!captureStatus) return CUDA_ERROR_INVALID_VALUE;
  CUresult ret = cuStreamIsCapturing(hStream, captureStatus);
  if (ret == CUDA_SUCCESS && id != nullptr) {
    *id = 0;
  }
  return ret;
}

// ---------------------------------------------------------------------------
// Phase 4: stream fence registry — bridges cu_graph.cpp → cuStreamSynchronize
// ---------------------------------------------------------------------------

namespace async_task::umd::shim {
namespace {

struct StreamFenceRegistry {
  std::unordered_map<CUstream, uint64_t> last_fence;
  std::mutex mu;
};
StreamFenceRegistry g_stream_fences;

}  // namespace

void record_stream_fence(void* stream, uint64_t fence_id) {
  std::lock_guard<std::mutex> lock(g_stream_fences.mu);
  g_stream_fences.last_fence[stream] = fence_id;
}

uint64_t get_stream_last_fence(void* stream) {
  std::lock_guard<std::mutex> lock(g_stream_fences.mu);
  auto it = g_stream_fences.last_fence.find(stream);
  return (it != g_stream_fences.last_fence.end()) ? it->second : 0;
}

}  // namespace async_task::umd::shim
