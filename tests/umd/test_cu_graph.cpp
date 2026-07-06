// SCOPE: UMD-EVOLUTION
// test_cu_graph.cpp - E2E tests for cuGraph shim (Phase 3.1).
//
// Tests cuGraphCreate / Destroy / AddKernelNode / AddMemcpyNode /
// Instantiate / Launch / ExecDestroy + node accessor + exec param updates.
// 25+ cases covering basic lifecycle, kernel/memcpy nodes, instantiation,
// launch error paths, and node/exec param updates.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cuda.h>

#include <cstdint>
#include <vector>

namespace {

CUgraphNode make_kernel_params(CUDA_KERNEL_NODE_PARAMS* p) {
  p->func = nullptr;
  p->kernelParams = nullptr;
  p->extra = nullptr;
  p->gridDimX = 1; p->gridDimY = 1; p->gridDimZ = 1;
  p->blockDimX = 32; p->blockDimY = 1; p->blockDimZ = 1;
  p->sharedMemBytes = 0;
  return reinterpret_cast<CUgraphNode>(static_cast<uintptr_t>(0xAA00u));
}

CUgraphNode make_memcpy_params(CUDA_MEMCPY_NODE_PARAMS* p) {
  p->copyKind = 0;  // D2D
  p->src = 0x1000ull;
  p->dst = 0x2000ull;
  p->byteCount = 4096;
  return reinterpret_cast<CUgraphNode>(static_cast<uintptr_t>(0xBB00u));
}

}  // namespace

// ---------------------------------------------------------------------------
// Basic lifecycle (5 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cu_graph: cuGraphCreate returns non-null handle") {
  CUgraph g = nullptr;
  CHECK(cuGraphCreate(&g, 0) == CUDA_SUCCESS);
  CHECK(g != nullptr);
  CHECK(cuGraphDestroy(g) == CUDA_SUCCESS);
}

TEST_CASE("cu_graph: cuGraphCreate with NULL phGraph returns INVALID_VALUE") {
  CHECK(cuGraphCreate(nullptr, 0) == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cu_graph: cuGraphDestroy with NULL handle returns INVALID_VALUE") {
  CHECK(cuGraphDestroy(nullptr) == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cu_graph: sequential Create-Destroy cycles produce unique handles") {
  CUgraph g1, g2, g3;
  REQUIRE(cuGraphCreate(&g1, 0) == CUDA_SUCCESS);
  REQUIRE(cuGraphCreate(&g2, 0) == CUDA_SUCCESS);
  REQUIRE(cuGraphCreate(&g3, 0) == CUDA_SUCCESS);
  CHECK(g1 != g2);
  CHECK(g2 != g3);
  CHECK(g1 != g3);
  cuGraphDestroy(g1);
  cuGraphDestroy(g2);
  cuGraphDestroy(g3);
}

TEST_CASE("cu_graph: flags argument is accepted (ignored in shim)") {
  CUgraph g;
  CHECK(cuGraphCreate(&g, 1) == CUDA_SUCCESS);
  CHECK(cuGraphCreate(&g, 0xFF) == CUDA_SUCCESS);
  cuGraphDestroy(g);
}

// ---------------------------------------------------------------------------
// cuGraphAddKernelNode (5 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cu_graph: AddKernelNode with valid params returns SUCCESS") {
  CUgraph g;
  REQUIRE(cuGraphCreate(&g, 0) == CUDA_SUCCESS);
  CUDA_KERNEL_NODE_PARAMS params;
  make_kernel_params(&params);
  CUgraphNode node = nullptr;
  CHECK(cuGraphAddKernelNode(&node, g, nullptr, 0, &params) == CUDA_SUCCESS);
  CHECK(node != nullptr);
  cuGraphDestroy(g);
}

TEST_CASE("cu_graph: AddKernelNode with NULL params returns INVALID_VALUE") {
  CUgraph g;
  REQUIRE(cuGraphCreate(&g, 0) == CUDA_SUCCESS);
  CUgraphNode node;
  CHECK(cuGraphAddKernelNode(&node, g, nullptr, 0, nullptr) == CUDA_ERROR_INVALID_VALUE);
  cuGraphDestroy(g);
}

TEST_CASE("cu_graph: AddKernelNode with NULL graph returns INVALID_VALUE") {
  CUDA_KERNEL_NODE_PARAMS params;
  make_kernel_params(&params);
  CUgraphNode node;
  CHECK(cuGraphAddKernelNode(&node, nullptr, nullptr, 0, &params)
        == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cu_graph: AddKernelNode accepts kernargs_bo_handle=0 (F-3)") {
  CUgraph g;
  REQUIRE(cuGraphCreate(&g, 0) == CUDA_SUCCESS);
  CUDA_KERNEL_NODE_PARAMS params;
  make_kernel_params(&params);
  // Shim does not validate kernargs_bo_handle (F-3).
  CUgraphNode node = nullptr;
  CHECK(cuGraphAddKernelNode(&node, g, nullptr, 0, &params) == CUDA_SUCCESS);
  cuGraphDestroy(g);
}

TEST_CASE("cu_graph: AddKernelNode multiple times produces unique nodes") {
  CUgraph g;
  REQUIRE(cuGraphCreate(&g, 0) == CUDA_SUCCESS);
  CUDA_KERNEL_NODE_PARAMS params;
  make_kernel_params(&params);
  std::vector<CUgraphNode> nodes;
  for (int i = 0; i < 5; ++i) {
    CUgraphNode n = nullptr;
    REQUIRE(cuGraphAddKernelNode(&n, g, nullptr, 0, &params) == CUDA_SUCCESS);
    nodes.push_back(n);
  }
  for (size_t i = 0; i < nodes.size(); ++i) {
    for (size_t j = i + 1; j < nodes.size(); ++j) {
      CHECK(nodes[i] != nodes[j]);
    }
  }
  cuGraphDestroy(g);
}

// ---------------------------------------------------------------------------
// cuGraphAddMemcpyNode (3 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cu_graph: AddMemcpyNode with valid params returns SUCCESS") {
  CUgraph g;
  REQUIRE(cuGraphCreate(&g, 0) == CUDA_SUCCESS);
  CUDA_MEMCPY_NODE_PARAMS params;
  make_memcpy_params(&params);
  CUgraphNode node = nullptr;
  CHECK(cuGraphAddMemcpyNode(&node, g, nullptr, 0, &params) == CUDA_SUCCESS);
  CHECK(node != nullptr);
  cuGraphDestroy(g);
}

TEST_CASE("cu_graph: AddMemcpyNode with NULL params returns INVALID_VALUE") {
  CUgraph g;
  REQUIRE(cuGraphCreate(&g, 0) == CUDA_SUCCESS);
  CUgraphNode node;
  CHECK(cuGraphAddMemcpyNode(&node, g, nullptr, 0, nullptr)
        == CUDA_ERROR_INVALID_VALUE);
  cuGraphDestroy(g);
}

TEST_CASE("cu_graph: AddMemcpyNode + AddKernelNode mixed sequence") {
  CUgraph g;
  REQUIRE(cuGraphCreate(&g, 0) == CUDA_SUCCESS);
  CUDA_KERNEL_NODE_PARAMS kp;
  make_kernel_params(&kp);
  CUDA_MEMCPY_NODE_PARAMS mp;
  make_memcpy_params(&mp);
  CUgraphNode n1, n2, n3;
  REQUIRE(cuGraphAddKernelNode(&n1, g, nullptr, 0, &kp) == CUDA_SUCCESS);
  REQUIRE(cuGraphAddMemcpyNode(&n2, g, nullptr, 0, &mp) == CUDA_SUCCESS);
  REQUIRE(cuGraphAddKernelNode(&n3, g, nullptr, 0, &kp) == CUDA_SUCCESS);
  CHECK(n1 != n2);
  CHECK(n2 != n3);
  cuGraphDestroy(g);
}

// ---------------------------------------------------------------------------
// cuGraphInstantiate + Launch + Destroy (7 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cu_graph: Instantiate returns non-null exec handle") {
  CUgraph g;
  REQUIRE(cuGraphCreate(&g, 0) == CUDA_SUCCESS);
  CUgraphExec exec = nullptr;
  CHECK(cuGraphInstantiate(&exec, g, nullptr, nullptr, 0) == CUDA_SUCCESS);
  CHECK(exec != nullptr);
  CHECK(cuGraphExecDestroy(exec) == CUDA_SUCCESS);
  cuGraphDestroy(g);
}

TEST_CASE("cu_graph: Instantiate with NULL exec pointer returns INVALID_VALUE") {
  CUgraph g;
  REQUIRE(cuGraphCreate(&g, 0) == CUDA_SUCCESS);
  CHECK(cuGraphInstantiate(nullptr, g, nullptr, nullptr, 0)
        == CUDA_ERROR_INVALID_VALUE);
  cuGraphDestroy(g);
}

TEST_CASE("cu_graph: Instantiate with NULL graph returns INVALID_VALUE") {
  CUgraphExec exec;
  CHECK(cuGraphInstantiate(&exec, nullptr, nullptr, nullptr, 0)
        == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cu_graph: Launch with valid exec returns SUCCESS (PoC no-op)") {
  CUgraph g;
  REQUIRE(cuGraphCreate(&g, 0) == CUDA_SUCCESS);
  CUgraphExec exec;
  REQUIRE(cuGraphInstantiate(&exec, g, nullptr, nullptr, 0) == CUDA_SUCCESS);
  CHECK(cuGraphLaunch(exec, nullptr) == CUDA_SUCCESS);
  cuGraphExecDestroy(exec);
  cuGraphDestroy(g);
}

TEST_CASE("cu_graph: Launch with NULL exec returns INVALID_VALUE") {
  CHECK(cuGraphLaunch(nullptr, nullptr) == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cu_graph: ExecDestroy with NULL handle returns INVALID_VALUE") {
  CHECK(cuGraphExecDestroy(nullptr) == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cu_graph: full lifecycle Create + AddNode + Instantiate + Launch + Destroy") {
  CUgraph g;
  REQUIRE(cuGraphCreate(&g, 0) == CUDA_SUCCESS);
  CUDA_KERNEL_NODE_PARAMS params;
  make_kernel_params(&params);
  CUgraphNode node;
  REQUIRE(cuGraphAddKernelNode(&node, g, nullptr, 0, &params) == CUDA_SUCCESS);
  CUgraphExec exec;
  REQUIRE(cuGraphInstantiate(&exec, g, nullptr, nullptr, 0) == CUDA_SUCCESS);
  CHECK(cuGraphLaunch(exec, nullptr) == CUDA_SUCCESS);
  CHECK(cuGraphExecDestroy(exec) == CUDA_SUCCESS);
  CHECK(cuGraphDestroy(g) == CUDA_SUCCESS);
}

// ---------------------------------------------------------------------------
// cuGraphNodeGetType / cuGraphNodeSetAttribute (3 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cu_graph: NodeGetType returns KERNEL for any node") {
  CUgraphNodeType type = CU_GRAPH_NODE_TYPE_EMPTY;
  CUgraphNode node = reinterpret_cast<CUgraphNode>(static_cast<uintptr_t>(0x123u));
  CHECK(cuGraphNodeGetType(node, &type) == CUDA_SUCCESS);
  CHECK(type == CU_GRAPH_NODE_TYPE_KERNEL);
}

TEST_CASE("cu_graph: NodeGetType with NULL type returns INVALID_VALUE") {
  CUgraphNode node = reinterpret_cast<CUgraphNode>(static_cast<uintptr_t>(0x123u));
  CHECK(cuGraphNodeGetType(node, nullptr) == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cu_graph: NodeSetAttribute returns SUCCESS for valid params") {
  CUgraphNode node = reinterpret_cast<CUgraphNode>(static_cast<uintptr_t>(0x123u));
  CUgraphNodeParams params{};
  CHECK(cuGraphNodeSetAttribute(node, &params) == CUDA_SUCCESS);
}

// ---------------------------------------------------------------------------
// cuGraphExecKernelNodeSetParams / cuGraphExecMemcpyNodeSetParams (2 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cu_graph: ExecKernelNodeSetParams returns SUCCESS") {
  CUgraphExec exec = reinterpret_cast<CUgraphExec>(static_cast<uintptr_t>(0x1u));
  CUgraphNode node = reinterpret_cast<CUgraphNode>(static_cast<uintptr_t>(0x2u));
  CUDA_KERNEL_NODE_PARAMS params;
  make_kernel_params(&params);
  CHECK(cuGraphExecKernelNodeSetParams(exec, node, &params) == CUDA_SUCCESS);
}

TEST_CASE("cu_graph: ExecMemcpyNodeSetParams returns SUCCESS") {
  CUgraphExec exec = reinterpret_cast<CUgraphExec>(static_cast<uintptr_t>(0x1u));
  CUgraphNode node = reinterpret_cast<CUgraphNode>(static_cast<uintptr_t>(0x2u));
  CUDA_MEMCPY_NODE_PARAMS params;
  make_memcpy_params(&params);
  CHECK(cuGraphExecMemcpyNodeSetParams(exec, node, &params) == CUDA_SUCCESS);
}