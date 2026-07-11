// SCOPE: UMD-EVOLUTION
// cu_graph.cpp - CUDA Graph lifecycle + add-node + instantiate + launch (Phase 3.1+4 REAL bridge).
//
// Implements 7 cu* graph APIs with self-contained atomic + map + mutex state
// tables. Matches the shim-local pattern used by cu_stream.cpp / cu_event.cpp /
// cu_mem.cpp / cu_stream_capture.cpp.
//
// Phase 4: cuGraphLaunch bridges to g_gpu_client->submit_graph() for actual graph
// submission. LaunchTrace tracks fence_ids per exec handle. cuGraphExecDestroy
// cleans up the LaunchTrace entry (Metis S4).
//
// Handle convention:
//   CUgraph     = void*  (atomic counter)
//   CUgraphExec = void*  (atomic counter)
//   node id     = uint64 (monotonic)
//
// F-3: cuGraphAddKernelNode passes kernargs_bo_handle through unchanged
// (0 = no kernargs BO; client does not validate against BO table).

#include <cuda.h>

// Phase 4 (M2): expose g_gpu_client global + IGpuDriver interface
#include "test_fixture/gpu_driver_client.h"

// Phase 4: stream fence registry for cuStreamSynchronize bridge
#include "stream_fence_registry.hpp"

// g-gpu-client-meyers-singleton-fallback: Meyers fallback for null g_gpu_client
#include "cuda_driver_accessor.hpp"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace async_task::umd::shim {
namespace {

struct GraphTable {
  std::atomic<std::uint64_t> next_graph_id{1};
  std::atomic<std::uint64_t> next_exec_id{1};
  std::atomic<std::uint64_t> next_node_id{1};
  std::unordered_map<CUgraph, std::vector<std::uint64_t>> graph_nodes;
  std::mutex mu;
};
GraphTable g_graphs;

// Replaced by cuda_driver_accessor.hpp::get_driver_or_default() (Meyers fallback).
// The local helper that returned nullptr is gone; shim APIs now always get a
// non-null driver (via fallback CudaStub if user hasn't set g_gpu_client).

struct LaunchTrace {
  std::unordered_map<CUgraphExec, std::int64_t> fence_ids;
  std::mutex mu;
};
LaunchTrace g_launches;

}  // namespace
}  // namespace async_task::umd::shim

extern "C" CUresult cuGraphCreate(CUgraph* phGraph, unsigned int flags) {
  if (!phGraph) return CUDA_ERROR_INVALID_VALUE;
  (void)flags;
  auto& t = async_task::umd::shim::g_graphs;
  std::lock_guard<std::mutex> lock(t.mu);
  uint64_t id = t.next_graph_id.fetch_add(1);
  *phGraph = reinterpret_cast<CUgraph>(static_cast<std::uintptr_t>(id));
  t.graph_nodes[*phGraph] = {};
  return CUDA_SUCCESS;
}

extern "C" CUresult cuGraphDestroy(CUgraph hGraph) {
  if (!hGraph) return CUDA_ERROR_INVALID_VALUE;
  auto& t = async_task::umd::shim::g_graphs;
  std::lock_guard<std::mutex> lock(t.mu);
  t.graph_nodes.erase(hGraph);
  return CUDA_SUCCESS;
}

extern "C" CUresult cuGraphAddKernelNode(CUgraphNode* phGraphNode,
                                          CUgraph hGraph,
                                          CUgraphNode* dependencies,
                                          size_t numDependencies,
                                          const CUDA_KERNEL_NODE_PARAMS* nodeParams) {
  if (!phGraphNode || !hGraph || !nodeParams) return CUDA_ERROR_INVALID_VALUE;
  (void)dependencies;
  (void)numDependencies;
  // F-3: kernargs_bo_handle stored in extra field; for shim, we accept any value
  // (including 0 = no kernargs BO) without validating against BO table.
  auto& t = async_task::umd::shim::g_graphs;
  std::lock_guard<std::mutex> lock(t.mu);
  auto it = t.graph_nodes.find(hGraph);
  if (it == t.graph_nodes.end()) return CUDA_ERROR_INVALID_HANDLE;
  uint64_t nid = t.next_node_id.fetch_add(1);
  it->second.push_back(nid);
  *phGraphNode = reinterpret_cast<CUgraphNode>(static_cast<uintptr_t>(nid));
  return CUDA_SUCCESS;
}

extern "C" CUresult cuGraphAddMemcpyNode(CUgraphNode* phGraphNode,
                                          CUgraph hGraph,
                                          CUgraphNode* dependencies,
                                          size_t numDependencies,
                                          const CUDA_MEMCPY_NODE_PARAMS* nodeParams) {
  if (!phGraphNode || !hGraph || !nodeParams) return CUDA_ERROR_INVALID_VALUE;
  (void)dependencies;
  (void)numDependencies;
  auto& t = async_task::umd::shim::g_graphs;
  std::lock_guard<std::mutex> lock(t.mu);
  auto it = t.graph_nodes.find(hGraph);
  if (it == t.graph_nodes.end()) return CUDA_ERROR_INVALID_HANDLE;
  uint64_t nid = t.next_node_id.fetch_add(1);
  it->second.push_back(nid);
  *phGraphNode = reinterpret_cast<CUgraphNode>(static_cast<uintptr_t>(nid));
  return CUDA_SUCCESS;
}

extern "C" CUresult cuGraphInstantiate(CUgraphExec* phGraphExec,
                                        CUgraph hGraph,
                                        CUgraphNode* phErrorNode,
                                        char* logBuffer,
                                        size_t bufferSize) {
  if (!phGraphExec || !hGraph) return CUDA_ERROR_INVALID_VALUE;
  (void)phErrorNode;
  (void)logBuffer;
  (void)bufferSize;
  auto& t = async_task::umd::shim::g_graphs;
  std::lock_guard<std::mutex> lock(t.mu);
  auto it = t.graph_nodes.find(hGraph);
  if (it == t.graph_nodes.end()) return CUDA_ERROR_INVALID_HANDLE;
  uint64_t id = t.next_exec_id.fetch_add(1);
  *phGraphExec = reinterpret_cast<CUgraphExec>(static_cast<uintptr_t>(id));
  return CUDA_SUCCESS;
}

extern "C" CUresult cuGraphLaunch(CUgraphExec hGraphExec, CUstream hStream) {
  if (!hGraphExec) return CUDA_ERROR_INVALID_VALUE;
  async_task::gpu::IGpuDriver* driver = async_task::umd::shim::get_driver_or_default();
  std::int64_t fence = driver->submit_graph(
      reinterpret_cast<uint64_t>(hGraphExec),
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hStream)));
  if (fence < 0) return CUDA_ERROR_UNKNOWN;
  {
    std::lock_guard<std::mutex> lock(async_task::umd::shim::g_launches.mu);
    async_task::umd::shim::g_launches.fence_ids[hGraphExec] = fence;
  }
  // Phase 4: record fence for cuStreamSynchronize stream-lookup
  async_task::umd::shim::record_stream_fence(hStream, static_cast<uint64_t>(fence));
  return CUDA_SUCCESS;
}

extern "C" CUresult cuGraphExecDestroy(CUgraphExec hGraphExec) {
  if (!hGraphExec) return CUDA_ERROR_INVALID_VALUE;
  {
    std::lock_guard<std::mutex> lock(async_task::umd::shim::g_launches.mu);
    async_task::umd::shim::g_launches.fence_ids.erase(hGraphExec);
  }
  return CUDA_SUCCESS;
}