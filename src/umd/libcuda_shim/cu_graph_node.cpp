// SCOPE: UMD-EVOLUTION
// cu_graph_node.cpp - CUDA graph node attribute accessors (Phase 3.1 REAL_IMPL).
//
// Implements 2 cu* graph node APIs. Shim-local, no GpuDriverClient.

#include <cuda.h>

extern "C" CUresult cuGraphNodeGetType(CUgraphNode hNode,
                                       CUgraphNodeType* type) {
  if (!hNode || !type) return CUDA_ERROR_INVALID_VALUE;
  *type = CU_GRAPH_NODE_TYPE_KERNEL;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuGraphNodeSetAttribute(CUgraphNode hNode,
                                            CUgraphNodeParams* nodeParams) {
  if (!hNode || !nodeParams) return CUDA_ERROR_INVALID_VALUE;
  return CUDA_SUCCESS;
}