// SCOPE: UMD-EVOLUTION
// cu_stream_capture.cpp - Stream capture state machine (Phase 3.1 REAL_IMPL).
//
// Implements cuStreamBeginCapture / cuStreamEndCapture / cuStreamIsCapturing
// with a self-contained atomic + map + mutex state table. Matches the
// shim-local pattern used by cu_stream.cpp / cu_event.cpp / cu_mem.cpp.
//
// Shim does NOT call GpuDriverClient (D-S3-1 decision). The shim-only
// design keeps these functions usable from tests that link against
// libcuda_taskrunner.so without requiring a running UsrLinuxEmu instance.
//
// Handle convention:
//   CUstream = void*  (used directly as CaptureTable state key)
//   state: 0=NONE, 1=ACTIVE, 2=INVALID (latched on illegal state transition)
//
// F-1: cuStreamBeginCapture rejects non-GLOBAL capture mode.

#include <cuda.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace async_task::umd::shim {
namespace {

struct CaptureTable {
  std::atomic<std::uint64_t> next_graph_id{1};
  std::unordered_map<CUstream, std::uint32_t> state;
  std::mutex mu;
};
CaptureTable g_captures;

}  // namespace
}  // namespace async_task::umd::shim

extern "C" CUresult cuStreamBeginCapture(CUstream hStream,
                                         CUstreamCaptureMode mode) {
  if (mode != CU_STREAM_CAPTURE_MODE_GLOBAL) return CUDA_ERROR_NOT_SUPPORTED;
  if (!hStream) return CUDA_ERROR_INVALID_VALUE;
  auto& table = async_task::umd::shim::g_captures;
  std::lock_guard<std::mutex> lock(table.mu);
  if (table.state[hStream] == 1) {
    table.state[hStream] = 2;
    return CUDA_ERROR_ILLEGAL_STATE;
  }
  table.state[hStream] = 1;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuStreamEndCapture(CUstream hStream, CUgraph* phGraph) {
  if (!hStream) return CUDA_ERROR_INVALID_VALUE;
  if (!phGraph) return CUDA_ERROR_INVALID_VALUE;
  auto& table = async_task::umd::shim::g_captures;
  std::lock_guard<std::mutex> lock(table.mu);
  if (table.state[hStream] != 1) {
    table.state[hStream] = 2;
    return CUDA_ERROR_ILLEGAL_STATE;
  }
  table.state[hStream] = 0;
  // Delegate to cuGraphCreate so the graph is registered in cu_graph.cpp's
  // GraphTable (otherwise cuGraphInstantiate would reject as INVALID_HANDLE).
  // cuGraphCreate acquires its own mutex on the GraphTable, no deadlock.
  return cuGraphCreate(phGraph, 0);
}

extern "C" CUresult cuStreamIsCapturing(CUstream hStream,
                                        CUstreamCaptureStatus* captureStatus) {
  if (!captureStatus) return CUDA_ERROR_INVALID_VALUE;
  auto& table = async_task::umd::shim::g_captures;
  std::lock_guard<std::mutex> lock(table.mu);
  auto it = table.state.find(hStream);
  uint32_t s = (it == table.state.end()) ? 0u : it->second;
  switch (s) {
    case 0: *captureStatus = CU_STREAM_CAPTURE_STATUS_NONE; break;
    case 1: *captureStatus = CU_STREAM_CAPTURE_STATUS_ACTIVE; break;
    case 2: *captureStatus = CU_STREAM_CAPTURE_STATUS_INVALIDATED; break;
    default: *captureStatus = CU_STREAM_CAPTURE_STATUS_NONE; break;
  }
  return CUDA_SUCCESS;
}