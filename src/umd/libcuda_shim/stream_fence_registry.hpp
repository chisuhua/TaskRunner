// SCOPE: UMD-EVOLUTION
// stream_fence_registry.hpp - Internal TU bridge for per-stream latest fence.
//
// cu_graph.cpp writes the fence (after submit_graph success); cu_stream.cpp
// reads it (in cuStreamSynchronize). This header is NOT in include/ because
// it is not part of any cross-scope public API.
//
// Thread safety: internal std::mutex on the unordered_map.

#pragma once

#include <cstdint>

namespace async_task::umd::shim {

// After cuGraphLaunch's submit_graph succeeds, record the returned fence_id
// as the stream's most-recent fence. Called from cu_graph.cpp.
void record_stream_fence(void* stream, uint64_t fence_id);

// cuStreamSynchronize queries the most-recent fence for the stream.
// Returns 0 if the stream has no recorded fence (no pending work).
uint64_t get_stream_last_fence(void* stream);

}  // namespace async_task::umd::shim
