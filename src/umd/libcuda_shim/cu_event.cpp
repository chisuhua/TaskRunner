// SCOPE: UMD-EVOLUTION
// cu_event.cpp - Event management (Phase 3.3a Event timing precision).
//
// Provides handle-tracking for cuEventCreate/Destroy with atomic ID
// allocation and chrono-based elapsed time computation.
//
// Phase 3.3a: Refactored EventTable from flat created-only time_point map
// to EventRecord struct supporting flags, created_at, recorded_at (optional),
// and is_destroyed. Pattern: atomic + map + mutex (per cu_stream_capture /
// cu_graph / cu_mem_pool).

#include <cuda.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace async_task::umd::shim {
namespace {

struct EventRecord {
  unsigned int flags;
  std::chrono::steady_clock::time_point created_at;
  std::optional<std::chrono::steady_clock::time_point> recorded_at;  // nullopt until Record
  bool is_destroyed{false};
};
struct EventTable {
  std::atomic<std::uint64_t> next_id{1};
  std::unordered_map<CUevent, EventRecord> events;
  std::mutex mu;
};
EventTable g_events;

}  // namespace
}  // namespace async_task::umd::shim

extern "C" CUresult cuEventCreate(CUevent* phEvent, unsigned int Flags) {
  if (!phEvent) return CUDA_ERROR_INVALID_VALUE;
  // CUDA 12.x spec: reserved flag bits must be 0.
  constexpr unsigned int kValidFlags = CU_EVENT_DEFAULT
                                      | CU_EVENT_BLOCKING_SYNC
                                      | CU_EVENT_DISABLE_TIMING
                                      | CU_EVENT_INTERPROCESS;
  if (Flags & ~kValidFlags) return CUDA_ERROR_INVALID_VALUE;

  auto& table = async_task::umd::shim::g_events;
  std::lock_guard<std::mutex> lock(table.mu);
  *phEvent = reinterpret_cast<CUevent>(
      static_cast<std::uintptr_t>(table.next_id.fetch_add(1)));
  table.events[*phEvent] = async_task::umd::shim::EventRecord{
      .flags = Flags,
      .created_at = std::chrono::steady_clock::now(),
      .recorded_at = std::nullopt,
      .is_destroyed = false,
  };
  return CUDA_SUCCESS;
}

extern "C" CUresult cuEventDestroy(CUevent hEvent) {
  if (!hEvent) return CUDA_ERROR_INVALID_VALUE;
  auto& table = async_task::umd::shim::g_events;
  std::lock_guard<std::mutex> lock(table.mu);
  auto it = table.events.find(hEvent);
  if (it == table.events.end()) return CUDA_ERROR_INVALID_HANDLE;
  if (it->second.is_destroyed) return CUDA_ERROR_INVALID_HANDLE;
  it->second.is_destroyed = true;
  table.events.erase(it);
  return CUDA_SUCCESS;
}

extern "C" CUresult cuEventRecord(CUevent hEvent, CUstream hStream) {
  if (!hEvent) return CUDA_ERROR_INVALID_VALUE;
  // Phase 3.3a: write recorded_at (NOT created_at — Phase 2 PoC bug fix).
  auto& table = async_task::umd::shim::g_events;
  std::lock_guard<std::mutex> lock(table.mu);
  auto it = table.events.find(hEvent);
  if (it == table.events.end()) return CUDA_ERROR_INVALID_HANDLE;
  if (it->second.is_destroyed) return CUDA_ERROR_INVALID_HANDLE;
  it->second.recorded_at = std::chrono::steady_clock::now();
  (void)hStream;  // Phase 3.3a: stream no-op (CudaStub synchronous)
  return CUDA_SUCCESS;
}

extern "C" CUresult cuEventSynchronize(CUevent hEvent) {
  // Phase 3.3a: no-op (synchronous semantics — event recorded synchronously).
  if (!hEvent) return CUDA_ERROR_INVALID_VALUE;
  auto& table = async_task::umd::shim::g_events;
  std::lock_guard<std::mutex> lock(table.mu);
  auto it = table.events.find(hEvent);
  if (it == table.events.end()) return CUDA_ERROR_INVALID_HANDLE;
  if (it->second.is_destroyed) return CUDA_ERROR_INVALID_HANDLE;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuEventQuery(CUevent hEvent) {
  if (!hEvent) return CUDA_ERROR_INVALID_VALUE;
  auto& table = async_task::umd::shim::g_events;
  std::lock_guard<std::mutex> lock(table.mu);
  auto it = table.events.find(hEvent);
  if (it == table.events.end()) return CUDA_ERROR_INVALID_HANDLE;
  if (it->second.is_destroyed) return CUDA_ERROR_INVALID_HANDLE;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuEventElapsedTime(float* pMilliseconds, CUevent hStart,
                                        CUevent hEnd) {
  if (!pMilliseconds) return CUDA_ERROR_INVALID_VALUE;

  auto& table = async_task::umd::shim::g_events;
  std::lock_guard<std::mutex> lock(table.mu);
  auto start_it = table.events.find(hStart);
  auto end_it = table.events.find(hEnd);
  if (start_it == table.events.end() ||
      end_it == table.events.end()) {
    return CUDA_ERROR_INVALID_HANDLE;
  }
  if (start_it->second.is_destroyed || end_it->second.is_destroyed)
    return CUDA_ERROR_INVALID_HANDLE;

  // Phase 3.3a strict semantics: both events must be recorded.
  if (!start_it->second.recorded_at.has_value() ||
      !end_it->second.recorded_at.has_value()) {
    return CUDA_ERROR_NOT_PERMITTED;
  }

  auto diff_us = std::chrono::duration_cast<std::chrono::microseconds>(
      *end_it->second.recorded_at - *start_it->second.recorded_at);
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
  return cuEventCreate(phEvent, flags);
}