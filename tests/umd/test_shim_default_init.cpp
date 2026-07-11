// SCOPE: UMD-EVOLUTION
/**
 * test_shim_default_init.cpp - Regression tests for g_gpu_client default fallback
 *
 * Verifies:
 *   T1: cuStreamSynchronize returns SUCCESS when g_gpu_client == nullptr
 *       AND a fence is recorded (uses Meyers CudaStub fallback)
 *   T2: cuGraphLaunch returns non-NOT_INITIALIZED when g_gpu_client == nullptr
 *   T3: The fallback does NOT mutate the global g_gpu_client pointer (5 iterations)
 *   T4: Explicit g_gpu_client override still works (set to g_mock)
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cuda.h>

#include "test_fixture/cuda_stub.hpp"
#include "test_fixture/gpu_driver_client.h"

// stream_fence_registry.hpp is TU-internal (in src/umd/libcuda_shim/), not in
// include/. Forward-declare the 2 functions we need; they are exported from
// libcuda_taskrunner.so (extern linkage, not static).
namespace async_task::umd::shim {
void record_stream_fence(void* stream, uint64_t fence_id);
uint64_t get_stream_last_fence(void* stream);
}  // namespace async_task::umd::shim

namespace async_task::gpu {
// Test fixture: a TU-local CudaStub instance for use in T4.
inline CudaStub& local_stub() {
  static CudaStub stub;
  return stub;
}
}  // namespace async_task::gpu

TEST_CASE("T1: cuStreamSynchronize with null g_gpu_client returns SUCCESS via fallback") {
  // Reset to known state
  async_task::gpu::g_gpu_client = nullptr;

  // Create a stream
  CUstream s;
  REQUIRE(cuStreamCreate(&s, 0) == CUDA_SUCCESS);

  // Record a fence (so cuStreamSynchronize doesn't early-return SUCCESS without driver)
  async_task::umd::shim::record_stream_fence(s, /*fence_id=*/1);

  // Call cuStreamSynchronize — should succeed via Meyers CudaStub fallback
  // (CudaStub::wait_fence with status=1 returns SUCCESS).
  CUresult ret = cuStreamSynchronize(s);
  CHECK(ret == CUDA_SUCCESS);

  // CRITICAL: fallback must NOT mutate global g_gpu_client
  CHECK(async_task::gpu::g_gpu_client == nullptr);
}

TEST_CASE("T2: cuGraphLaunch with null g_gpu_client does not return NOT_INITIALIZED") {
  async_task::gpu::g_gpu_client = nullptr;

  CUgraph graph;
  REQUIRE(cuGraphCreate(&graph, 0) == CUDA_SUCCESS);

  CUgraphExec exec;
  REQUIRE(cuGraphInstantiate(&exec, graph, nullptr, nullptr, 0) == CUDA_SUCCESS);

  CUstream s;
  REQUIRE(cuStreamCreate(&s, 0) == CUDA_SUCCESS);

  CUresult ret = cuGraphLaunch(exec, s);
  // CudaStub does not override submit_graph (default returns -1 → CUDA_ERROR_UNKNOWN).
  // The key assertion: NOT_INITIALIZED is no longer returned.
  CHECK(ret != CUDA_ERROR_NOT_INITIALIZED);
  CHECK(async_task::gpu::g_gpu_client == nullptr);

  // Cleanup
  cuGraphExecDestroy(exec);
  cuGraphDestroy(graph);
}

TEST_CASE("T3: fallback does not mutate g_gpu_client across multiple calls") {
  async_task::gpu::g_gpu_client = nullptr;

  CUstream s1, s2;
  REQUIRE(cuStreamCreate(&s1, 0) == CUDA_SUCCESS);
  REQUIRE(cuStreamCreate(&s2, 0) == CUDA_SUCCESS);

  for (int i = 0; i < 5; ++i) {
    CHECK(cuStreamSynchronize(s1) == CUDA_SUCCESS);
    CHECK(cuStreamSynchronize(s2) == CUDA_SUCCESS);
    CHECK(async_task::gpu::g_gpu_client == nullptr);
  }
}

TEST_CASE("T4: explicit g_gpu_client override takes precedence over fallback") {
  // Override with our local stub
  async_task::gpu::g_gpu_client = &async_task::gpu::local_stub();

  CUstream s;
  REQUIRE(cuStreamCreate(&s, 0) == CUDA_SUCCESS);
  async_task::umd::shim::record_stream_fence(s, 42);

  CHECK(cuStreamSynchronize(s) == CUDA_SUCCESS);
  CHECK(async_task::gpu::g_gpu_client == &async_task::gpu::local_stub());

  // Cleanup
  async_task::gpu::g_gpu_client = nullptr;
}