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
  (void)Flags;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuEventDestroy(CUevent hEvent) {
  if (!hEvent) return CUDA_ERROR_INVALID_VALUE;
  auto& table = async_task::umd::shim::g_events;
  std::lock_guard<std::mutex> lock(table.mu);
  table.events.erase(hEvent);
  return CUDA_SUCCESS;
}

extern "C" CUresult cuEventRecord(CUevent hEvent, CUstream hStream) {
  if (!hEvent) return CUDA_ERROR_INVALID_VALUE;
  // Phase 2 PoC: record the current time as the event timestamp.
  auto& table = async_task::umd::shim::g_events;
  std::lock_guard<std::mutex> lock(table.mu);
  auto it = table.events.find(hEvent);
  if (it == table.events.end()) return CUDA_ERROR_INVALID_HANDLE;
  it->second = async_task::umd::shim::EventRecord{
      .flags = it->second.flags,
      .created_at = std::chrono::steady_clock::now(),
      .recorded_at = std::nullopt,
      .is_destroyed = false,
  };
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
  auto start_it = table.events.find(hStart);
  auto end_it = table.events.find(hEnd);
  if (start_it == table.events.end() ||
      end_it == table.events.end()) {
    return CUDA_ERROR_INVALID_HANDLE;
  }

  auto diff_us = std::chrono::duration_cast<std::chrono::microseconds>(
      end_it->second.created_at - start_it->second.created_at);
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