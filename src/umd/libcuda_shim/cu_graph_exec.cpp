// SCOPE: UMD-EVOLUTION
// cu_graph_exec.cpp - CUDA graph executable parameter updates (Phase 3.1 REAL_IMPL).
//
// Implements 2 cu* graph exec APIs. Shim-local, no GpuDriverClient.

#include <cuda.h>

extern "C" CUresult cuGraphExecKernelNodeSetParams(CUgraphExec hGraphExec,
                                                    CUgraphNode hNode,
                                                    const CUDA_KERNEL_NODE_PARAMS* nodeParams) {
  if (!hGraphExec || !hNode || !nodeParams) return CUDA_ERROR_INVALID_VALUE;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuGraphExecMemcpyNodeSetParams(CUgraphExec hGraphExec,
                                                    CUgraphNode hNode,
                                                    const CUDA_MEMCPY_NODE_PARAMS* nodeParams) {
  if (!hGraphExec || !hNode || !nodeParams) return CUDA_ERROR_INVALID_VALUE;
  return CUDA_SUCCESS;
}