// SCOPE: UMD-EVOLUTION
// cu_graph.cpp - CUDA Graph lifecycle + add-node + instantiate + launch (Phase 3.1 REAL_IMPL).
//
// Implements 7 cu* graph APIs with self-contained atomic + map + mutex state
// tables. Matches the shim-local pattern used by cu_stream.cpp / cu_event.cpp /
// cu_mem.cpp / cu_stream_capture.cpp.
//
// Shim does NOT call GpuDriverClient (D-S3-1 decision). Graph nodes are
// stored in-memory only; cuGraphLaunch is a PoC no-op (returns SUCCESS
// without dispatching work). Phase 4+ can bridge shim -> GpuDriverClient
// for actual graph submission.
//
// Handle convention:
//   CUgraph     = void*  (atomic counter)
//   CUgraphExec = void*  (atomic counter)
//   node id     = uint64 (monotonic)
//
// F-3: cuGraphAddKernelNode passes kernargs_bo_handle through unchanged
// (0 = no kernargs BO; client does not validate against BO table).

#include <cuda.h>

#include <atomic>
#include <cstdint>
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

// PoC: cuGraphLaunch is a no-op. Phase 4+ will bridge shim -> GpuDriverClient
// submit_graph (F-4 fence_id >= 1<<32) to actually dispatch the captured
// graph to the simulator.
extern "C" CUresult cuGraphLaunch(CUgraphExec hGraphExec, CUstream hStream) {
  if (!hGraphExec) return CUDA_ERROR_INVALID_VALUE;
  (void)hStream;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuGraphExecDestroy(CUgraphExec hGraphExec) {
  if (!hGraphExec) return CUDA_ERROR_INVALID_VALUE;
  (void)hGraphExec;
  return CUDA_SUCCESS;
}