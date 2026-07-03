// SCOPE: UMD-EVOLUTION
// cu_event.cpp - Event management (Phase 2 PoC).
//
// Provides handle-tracking for cuEventCreate/Destroy with atomic ID
// allocation and chrono-based elapsed time computation.

#include <cuda.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace async_task::umd::shim {
namespace {

struct EventTable {
  std::atomic<std::uint64_t> next_id{1};
  // Event creation timestamp (for cuEventElapsedTime).
  std::unordered_map<CUevent, std::chrono::steady_clock::time_point> created;
  std::mutex mu;
};
EventTable g_events;

}  // namespace
}  // namespace async_task::umd::shim

extern "C" CUresult cuEventCreate(CUevent* phEvent, unsigned int Flags) {
  if (!phEvent) return CUDA_ERROR_INVALID_VALUE;
  auto& table = async_task::umd::shim::g_events;
  std::lock_guard<std::mutex> lock(table.mu);
  *phEvent = reinterpret_cast<CUevent>(
      static_cast<std::uintptr_t>(table.next_id.fetch_add(1)));
  table.created[*phEvent] = std::chrono::steady_clock::now();
  (void)Flags;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuEventDestroy(CUevent hEvent) {
  if (!hEvent) return CUDA_ERROR_INVALID_VALUE;
  auto& table = async_task::umd::shim::g_events;
  std::lock_guard<std::mutex> lock(table.mu);
  table.created.erase(hEvent);
  return CUDA_SUCCESS;
}

extern "C" CUresult cuEventRecord(CUevent hEvent, CUstream hStream) {
  if (!hEvent) return CUDA_ERROR_INVALID_VALUE;
  // Phase 2 PoC: record the current time as the event timestamp.
  auto& table = async_task::umd::shim::g_events;
  std::lock_guard<std::mutex> lock(table.mu);
  auto it = table.created.find(hEvent);
  if (it == table.created.end()) return CUDA_ERROR_INVALID_HANDLE;
  it->second = std::chrono::steady_clock::now();
  (void)hStream;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuEventSynchronize(CUevent hEvent) {
  // Phase 2 PoC: no-op (synchronous semantics — event recorded synchronously).
  if (!hEvent) return CUDA_ERROR_INVALID_VALUE;
  (void)hEvent;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuEventQuery(CUevent hEvent) {
  (void)hEvent;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuEventElapsedTime(float* pMilliseconds, CUevent hStart,
                                        CUevent hEnd) {
  if (!pMilliseconds) return CUDA_ERROR_INVALID_VALUE;

  auto& table = async_task::umd::shim::g_events;
  std::lock_guard<std::mutex> lock(table.mu);
  auto start_it = table.created.find(hStart);
  auto end_it = table.created.find(hEnd);
  if (start_it == table.created.end() ||
      end_it == table.created.end()) {
    return CUDA_ERROR_INVALID_HANDLE;
  }

  auto diff_us = std::chrono::duration_cast<std::chrono::microseconds>(
      end_it->second - start_it->second);
  *pMilliseconds = static_cast<float>(diff_us.count()) / 1000.0f;

  // CUDA spec: negative time is not allowed (indicates wrong order).
  if (*pMilliseconds < 0.0f) *pMilliseconds = 0.0f;

  return CUDA_SUCCESS;
}

// ---------------------------------------------------------------------------
// Phase 1.7 — A.4: cuEventCreateWithFlags
// ---------------------------------------------------------------------------

extern "C" CUresult cuEventCreateWithFlags(CUevent* phEvent,
                                            unsigned int flags) {
  (void)flags;
  return cuEventCreate(phEvent, 0);
}
