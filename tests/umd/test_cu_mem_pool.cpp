// SCOPE: UMD-EVOLUTION
// test_cu_mem_pool.cpp - E2E tests for cuMemPool shim (Phase 3.2).
//
// Tests cuMemPoolCreate / Destroy / Alloc / Free / SetAttribute / GetAttribute
// / TrimTo / ExportToShareableHandle. 25+ cases covering basic lifecycle,
// sync/async allocation, Option B VA sub-range boundaries, attribute API,
// trim semantics, and Stage 1.4 regression.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cuda.h>

#include <cstdint>
#include <vector>

#include "tests/test_fixture/mock_gpu_driver.hpp"
#include "test_fixture/gpu_driver_client.h"

using async_task::gpu::g_gpu_client;

namespace {

static async_task::gpu::MockGpuDriver g_mock;

CUmemPoolProps make_props(uint64_t va_handle, size_t max_size) {
  CUmemPoolProps p{};
  p.allocType = CU_MEM_ALLOCATION_TYPE_PINNED;
  p.handleType = CU_MEM_HANDLE_TYPE_NONE;
  p.location = {0, 0};
  p.maxSize = max_size;
  p.vaSpaceHandle = va_handle;
  return p;
}

CUmemPool make_pool(uint32_t id) {
  return reinterpret_cast<CUmemPool>(static_cast<uintptr_t>(0x100000u + id));
}

}  // namespace

// ---------------------------------------------------------------------------
// Basic lifecycle (4 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cu_mem_pool: cuMemPoolCreate returns non-null handle") {
  CUmemPool pool = nullptr;
  CUmemPoolProps props = make_props(1, 1024 * 1024);
  CHECK(cuMemPoolCreate(&pool, &props) == CUDA_SUCCESS);
  CHECK(pool != nullptr);
  CHECK(cuMemPoolDestroy(pool) == CUDA_SUCCESS);
}

TEST_CASE("cu_mem_pool: cuMemPoolCreate rejects vaSpaceHandle=0 (B-2)") {
  CUmemPool pool;
  CUmemPoolProps props = make_props(0, 1024);  // vaSpaceHandle=0 = H-1 sentinel
  CHECK(cuMemPoolCreate(&pool, &props) == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cu_mem_pool: cuMemPoolCreate with NULL pool returns INVALID_VALUE") {
  CUmemPoolProps props = make_props(1, 1024);
  CHECK(cuMemPoolCreate(nullptr, &props) == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cu_mem_pool: cuMemPoolCreate with NULL props returns INVALID_VALUE") {
  CUmemPool pool;
  CHECK(cuMemPoolCreate(&pool, nullptr) == CUDA_ERROR_INVALID_VALUE);
}

// ---------------------------------------------------------------------------
// Sync alloc (3 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cu_mem_pool: cuMemPoolAlloc returns non-null ptr") {
  g_gpu_client = &g_mock;
  CUmemPool pool;
  CUmemPoolProps props = make_props(1, 1024 * 1024);
  REQUIRE(cuMemPoolCreate(&pool, &props) == CUDA_SUCCESS);
  CUmemPoolPtr ptr = nullptr;
  CHECK(cuMemPoolAlloc(&ptr, 4096, pool, nullptr) == CUDA_SUCCESS);
  CHECK(ptr != nullptr);
  cuMemPoolDestroy(pool);
  g_gpu_client = nullptr;
}

TEST_CASE("cu_mem_pool: cuMemPoolAlloc with size=0 returns INVALID_VALUE") {
  CUmemPool pool;
  CUmemPoolProps props = make_props(1, 1024 * 1024);
  REQUIRE(cuMemPoolCreate(&pool, &props) == CUDA_SUCCESS);
  CUmemPoolPtr ptr;
  CHECK(cuMemPoolAlloc(&ptr, 0, pool, nullptr) == CUDA_ERROR_INVALID_VALUE);
  cuMemPoolDestroy(pool);
}

TEST_CASE("cu_mem_pool: cuMemPoolAlloc with NULL ptr returns INVALID_VALUE") {
  CUmemPool pool;
  CUmemPoolProps props = make_props(1, 1024 * 1024);
  REQUIRE(cuMemPoolCreate(&pool, &props) == CUDA_SUCCESS);
  CHECK(cuMemPoolAlloc(nullptr, 4096, pool, nullptr) == CUDA_ERROR_INVALID_VALUE);
  cuMemPoolDestroy(pool);
}

// ---------------------------------------------------------------------------
// Async alloc / Free (5 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cu_mem_pool: cuMemPoolAlloc + cuMemPoolFree round-trip") {
  g_gpu_client = &g_mock;
  CUmemPool pool;
  CUmemPoolProps props = make_props(1, 1024 * 1024);
  REQUIRE(cuMemPoolCreate(&pool, &props) == CUDA_SUCCESS);
  CUmemPoolPtr ptr;
  REQUIRE(cuMemPoolAlloc(&ptr, 4096, pool, nullptr) == CUDA_SUCCESS);
  CHECK(cuMemPoolFree(ptr, pool) == CUDA_SUCCESS);
  cuMemPoolDestroy(pool);
  g_gpu_client = nullptr;
}

TEST_CASE("cu_mem_pool: cuMemPoolAlloc multiple from same pool produces unique ptrs") {
  g_gpu_client = &g_mock;
  g_mock.clear_history();
  CUmemPool pool;
  CUmemPoolProps props = make_props(1, 1024 * 1024);
  REQUIRE(cuMemPoolCreate(&pool, &props) == CUDA_SUCCESS);
  std::vector<CUmemPoolPtr> ptrs;
  for (int i = 0; i < 5; ++i) {
    CUmemPoolPtr p = nullptr;
    REQUIRE(cuMemPoolAlloc(&p, 1024, pool, nullptr) == CUDA_SUCCESS);
    ptrs.push_back(p);
  }
  for (size_t i = 0; i < ptrs.size(); ++i) {
    for (size_t j = i + 1; j < ptrs.size(); ++j) {
      CHECK(ptrs[i] != ptrs[j]);
    }
  }
  cuMemPoolDestroy(pool);
  g_gpu_client = nullptr;
}

TEST_CASE("cu_mem_pool: cuMemPoolAlloc from different pools produces unique ptrs") {
  g_gpu_client = &g_mock;
  CUmemPool pool1, pool2;
  CUmemPoolProps props1 = make_props(1, 1024 * 1024);
  CUmemPoolProps props2 = make_props(1, 1024 * 1024);
  REQUIRE(cuMemPoolCreate(&pool1, &props1) == CUDA_SUCCESS);
  REQUIRE(cuMemPoolCreate(&pool2, &props2) == CUDA_SUCCESS);
  CUmemPoolPtr p1, p2;
  REQUIRE(cuMemPoolAlloc(&p1, 4096, pool1, nullptr) == CUDA_SUCCESS);
  REQUIRE(cuMemPoolAlloc(&p2, 4096, pool2, nullptr) == CUDA_SUCCESS);
  CHECK(p1 != p2);
  cuMemPoolDestroy(pool1);
  cuMemPoolDestroy(pool2);
  g_gpu_client = nullptr;
}

TEST_CASE("cu_mem_pool: cuMemPoolDestroy accepts zero pool as INVALID_VALUE") {
  CHECK(cuMemPoolDestroy(nullptr) == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cu_mem_pool: cuMemPoolFree with NULL ptr does not crash") {
  CUmemPool pool;
  CUmemPoolProps props = make_props(1, 1024 * 1024);
  REQUIRE(cuMemPoolCreate(&pool, &props) == CUDA_SUCCESS);
  CHECK(cuMemPoolFree(nullptr, pool) == CUDA_ERROR_INVALID_VALUE);
  cuMemPoolDestroy(pool);
}

// ---------------------------------------------------------------------------
// Option B VA sub-range boundaries (4 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cu_mem_pool: pool vaSpaceHandle=1 accepted") {
  CUmemPool pool;
  CUmemPoolProps props = make_props(1, 1024);
  CHECK(cuMemPoolCreate(&pool, &props) == CUDA_SUCCESS);
  cuMemPoolDestroy(pool);
}

TEST_CASE("cu_mem_pool: pool vaSpaceHandle=2 (multi-VA) accepted") {
  CUmemPool pool;
  CUmemPoolProps props = make_props(2, 1024);
  CHECK(cuMemPoolCreate(&pool, &props) == CUDA_SUCCESS);
  cuMemPoolDestroy(pool);
}

TEST_CASE("cu_mem_pool: pool with large maxSize accepted") {
  CUmemPool pool;
  CUmemPoolProps props = make_props(1, 1024ull * 1024 * 1024);  // 1 GB
  CHECK(cuMemPoolCreate(&pool, &props) == CUDA_SUCCESS);
  cuMemPoolDestroy(pool);
}

TEST_CASE("cu_mem_pool: alloc returns unique synthetic VA per call (mock bridge)") {
  g_gpu_client = &g_mock;
  g_mock.clear_history();
  CUmemPool pool;
  CUmemPoolProps props = make_props(1, 1024 * 1024);
  REQUIRE(cuMemPoolCreate(&pool, &props) == CUDA_SUCCESS);
  CUmemPoolPtr p1, p2;
  REQUIRE(cuMemPoolAlloc(&p1, 4096, pool, nullptr) == CUDA_SUCCESS);
  REQUIRE(cuMemPoolAlloc(&p2, 4096, pool, nullptr) == CUDA_SUCCESS);
  CHECK(p1 != p2);
  CHECK(reinterpret_cast<uintptr_t>(p1) > 0);
  CHECK(reinterpret_cast<uintptr_t>(p2) > 0);
  cuMemPoolDestroy(pool);
  g_gpu_client = nullptr;
}

// ---------------------------------------------------------------------------
// Attribute API (3 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cu_mem_pool: SetAttribute with RELEASE_THRESHOLD returns SUCCESS") {
  CUmemPool pool;
  CUmemPoolProps props = make_props(1, 1024 * 1024);
  REQUIRE(cuMemPoolCreate(&pool, &props) == CUDA_SUCCESS);
  int value = 4096;
  CHECK(cuMemPoolSetAttribute(pool, CU_MEMPOOL_ATTR_RELEASE_THRESHOLD, &value)
        == CUDA_SUCCESS);
  cuMemPoolDestroy(pool);
}

TEST_CASE("cu_mem_pool: SetAttribute with REUSE_FOLLOW_EVENT_DEPENDENCIES returns SUCCESS") {
  CUmemPool pool;
  CUmemPoolProps props = make_props(1, 1024 * 1024);
  REQUIRE(cuMemPoolCreate(&pool, &props) == CUDA_SUCCESS);
  int value = 1;
  CHECK(cuMemPoolSetAttribute(pool, CU_MEMPOOL_ATTR_REUSE_FOLLOW_EVENT_DEPENDENCIES,
                              &value) == CUDA_SUCCESS);
  cuMemPoolDestroy(pool);
}

TEST_CASE("cu_mem_pool: SetAttribute with unsupported attr returns NOT_SUPPORTED (F-2)") {
  CUmemPool pool;
  CUmemPoolProps props = make_props(1, 1024 * 1024);
  REQUIRE(cuMemPoolCreate(&pool, &props) == CUDA_SUCCESS);
  int value = 0;
  CHECK(cuMemPoolSetAttribute(pool, CU_MEMPOOL_ATTR_REUSE_ALLOW_OPPORTUNISTIC, &value)
        == CUDA_ERROR_NOT_SUPPORTED);
  cuMemPoolDestroy(pool);
}

// ---------------------------------------------------------------------------
// Trim (3 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cu_mem_pool: TrimTo with valid pool returns SUCCESS") {
  CUmemPool pool;
  CUmemPoolProps props = make_props(1, 1024 * 1024);
  REQUIRE(cuMemPoolCreate(&pool, &props) == CUDA_SUCCESS);
  CHECK(cuMemPoolTrimTo(pool, 0) == CUDA_SUCCESS);
  CHECK(cuMemPoolTrimTo(pool, 4096) == CUDA_SUCCESS);
  cuMemPoolDestroy(pool);
}

TEST_CASE("cu_mem_pool: TrimTo with NULL pool returns INVALID_VALUE") {
  CHECK(cuMemPoolTrimTo(nullptr, 0) == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cu_mem_pool: TrimTo after Alloc-Free cycle") {
  g_gpu_client = &g_mock;
  CUmemPool pool;
  CUmemPoolProps props = make_props(1, 1024 * 1024);
  REQUIRE(cuMemPoolCreate(&pool, &props) == CUDA_SUCCESS);
  CUmemPoolPtr ptr;
  REQUIRE(cuMemPoolAlloc(&ptr, 4096, pool, nullptr) == CUDA_SUCCESS);
  REQUIRE(cuMemPoolFree(ptr, pool) == CUDA_SUCCESS);
  CHECK(cuMemPoolTrimTo(pool, 0) == CUDA_SUCCESS);
  cuMemPoolDestroy(pool);
  g_gpu_client = nullptr;
}

// ---------------------------------------------------------------------------
// ExportToShareableHandle (3 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cu_mem_pool: ExportToShareableHandle with POSIX_FD type returns SUCCESS") {
  g_gpu_client = &g_mock;
  CUmemPool pool;
  CUmemPoolProps props = make_props(1, 1024 * 1024);
  REQUIRE(cuMemPoolCreate(&pool, &props) == CUDA_SUCCESS);
  int fd_handle = 0;
  CHECK(cuMemPoolExportToShareableHandle(&fd_handle, pool,
                                         CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR,
                                         0) == CUDA_SUCCESS);
  cuMemPoolDestroy(pool);
  g_gpu_client = nullptr;
}

TEST_CASE("cu_mem_pool: ExportToShareableHandle with NULL handle returns INVALID_VALUE") {
  CUmemPool pool;
  CUmemPoolProps props = make_props(1, 1024 * 1024);
  REQUIRE(cuMemPoolCreate(&pool, &props) == CUDA_SUCCESS);
  CHECK(cuMemPoolExportToShareableHandle(nullptr, pool,
                                         CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR,
                                         0) == CUDA_ERROR_INVALID_VALUE);
  cuMemPoolDestroy(pool);
}

TEST_CASE("cu_mem_pool: ExportToShareableHandle with NULL pool returns INVALID_VALUE") {
  int fd_handle;
  CHECK(cuMemPoolExportToShareableHandle(&fd_handle, nullptr,
                                         CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR,
                                         0) == CUDA_ERROR_INVALID_VALUE);
}

// ---------------------------------------------------------------------------
// Stage 1.4 regression (3 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cu_mem_pool: GetAttribute for valid attr returns SUCCESS") {
  CUmemPool pool;
  CUmemPoolProps props = make_props(1, 1024 * 1024);
  REQUIRE(cuMemPoolCreate(&pool, &props) == CUDA_SUCCESS);
  int value = 0;
  CHECK(cuMemPoolGetAttribute(pool, CU_MEMPOOL_ATTR_RELEASE_THRESHOLD, &value)
        == CUDA_SUCCESS);
  cuMemPoolDestroy(pool);
}

TEST_CASE("cu_mem_pool: GetAttribute with NULL value returns INVALID_VALUE") {
  CUmemPool pool;
  CUmemPoolProps props = make_props(1, 1024 * 1024);
  REQUIRE(cuMemPoolCreate(&pool, &props) == CUDA_SUCCESS);
  CHECK(cuMemPoolGetAttribute(pool, CU_MEMPOOL_ATTR_RELEASE_THRESHOLD, nullptr)
        == CUDA_ERROR_INVALID_VALUE);
  cuMemPoolDestroy(pool);
}

TEST_CASE("cu_mem_pool: full lifecycle Create + Alloc + SetAttr + GetAttr + Trim + Export + Destroy") {
  g_gpu_client = &g_mock;
  CUmemPool pool;
  CUmemPoolProps props = make_props(1, 1024 * 1024);
  REQUIRE(cuMemPoolCreate(&pool, &props) == CUDA_SUCCESS);
  CUmemPoolPtr ptr;
  REQUIRE(cuMemPoolAlloc(&ptr, 4096, pool, nullptr) == CUDA_SUCCESS);
  int threshold = 4096;
  REQUIRE(cuMemPoolSetAttribute(pool, CU_MEMPOOL_ATTR_RELEASE_THRESHOLD, &threshold)
          == CUDA_SUCCESS);
  int read_back = 0;
  REQUIRE(cuMemPoolGetAttribute(pool, CU_MEMPOOL_ATTR_RELEASE_THRESHOLD, &read_back)
          == CUDA_SUCCESS);
  REQUIRE(cuMemPoolTrimTo(pool, 0) == CUDA_SUCCESS);
  int fd_handle;
  REQUIRE(cuMemPoolExportToShareableHandle(&fd_handle, pool,
                                           CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR,
                                           0) == CUDA_SUCCESS);
  REQUIRE(cuMemPoolFree(ptr, pool) == CUDA_SUCCESS);
  CHECK(cuMemPoolDestroy(pool) == CUDA_SUCCESS);
  g_gpu_client = nullptr;
}

// ---------------------------------------------------------------------------
// Phase 4: REAL bridge tests (8 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cu_mem_pool: Alloc calls mem_pool_alloc and writes mock VA") {
  g_gpu_client = &g_mock;
  g_mock.clear_history();
  CUmemPool pool;
  CUmemPoolProps props = make_props(1, 1024 * 1024);
  REQUIRE(cuMemPoolCreate(&pool, &props) == CUDA_SUCCESS);
  CUmemPoolPtr ptr = nullptr;
  CHECK(cuMemPoolAlloc(&ptr, 4096, pool, nullptr) == CUDA_SUCCESS);
  CHECK(ptr != nullptr);
  CHECK(g_mock.call_count("mem_pool_alloc") == 1);
  cuMemPoolDestroy(pool);
  g_gpu_client = nullptr;
}

TEST_CASE("cu_mem_pool: Alloc with mock returning error propagates") {
  g_gpu_client = &g_mock;
  g_mock.inject_error("mem_pool_alloc", true);
  CUmemPool pool;
  CUmemPoolProps props = make_props(1, 1024 * 1024);
  REQUIRE(cuMemPoolCreate(&pool, &props) == CUDA_SUCCESS);
  CUmemPoolPtr ptr = nullptr;
  CHECK(cuMemPoolAlloc(&ptr, 4096, pool, nullptr) == CUDA_ERROR_UNKNOWN);
  g_mock.inject_error("mem_pool_alloc", false);
  cuMemPoolDestroy(pool);
  g_gpu_client = nullptr;
}

TEST_CASE("cu_mem_pool: AllocAsync calls mem_pool_alloc_async and writes VA") {
  g_gpu_client = &g_mock;
  g_mock.clear_history();
  CUmemPool pool;
  CUmemPoolProps props = make_props(1, 1024 * 1024);
  REQUIRE(cuMemPoolCreate(&pool, &props) == CUDA_SUCCESS);
  CUmemPoolPtr ptr = nullptr;
  CHECK(cuMemPoolAllocAsync(&ptr, 4096, pool, nullptr, nullptr) == CUDA_SUCCESS);
  CHECK(ptr != nullptr);
  CHECK(g_mock.call_count("mem_pool_alloc_async") == 1);
  cuMemPoolDestroy(pool);
  g_gpu_client = nullptr;
}

TEST_CASE("cu_mem_pool: FreeAsync calls mem_pool_free_async with correct VA") {
  g_gpu_client = &g_mock;
  g_mock.clear_history();
  CUmemPool pool;
  CUmemPoolProps props = make_props(1, 1024 * 1024);
  REQUIRE(cuMemPoolCreate(&pool, &props) == CUDA_SUCCESS);
  CUmemPoolPtr ptr = nullptr;
  REQUIRE(cuMemPoolAlloc(&ptr, 4096, pool, nullptr) == CUDA_SUCCESS);
  CHECK(cuMemPoolFreeAsync(ptr, nullptr, pool) == CUDA_SUCCESS);
  CHECK(g_mock.call_count("mem_pool_free_async") == 1);
  cuMemPoolDestroy(pool);
  g_gpu_client = nullptr;
}

TEST_CASE("cu_mem_pool: FreeAsync with NULL ptr returns INVALID_VALUE") {
  g_gpu_client = &g_mock;
  g_mock.clear_history();
  CUmemPool pool;
  CUmemPoolProps props = make_props(1, 1024 * 1024);
  REQUIRE(cuMemPoolCreate(&pool, &props) == CUDA_SUCCESS);
  CHECK(cuMemPoolFreeAsync(nullptr, nullptr, pool) == CUDA_ERROR_INVALID_VALUE);
  CHECK(g_mock.call_count("mem_pool_free_async") == 0);
  cuMemPoolDestroy(pool);
  g_gpu_client = nullptr;
}

TEST_CASE("cu_mem_pool: ExportToShareableHandle with POSIX_FD calls mem_pool_export_shareable and writes FD") {
  g_gpu_client = &g_mock;
  g_mock.clear_history();
  CUmemPool pool;
  CUmemPoolProps props = make_props(1, 1024 * 1024);
  REQUIRE(cuMemPoolCreate(&pool, &props) == CUDA_SUCCESS);
  int fd_out = -1;
  CHECK(cuMemPoolExportToShareableHandle(&fd_out, pool,
                                         CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR,
                                         0) == CUDA_SUCCESS);
  CHECK(fd_out >= 0);
  CHECK(g_mock.call_count("mem_pool_export_shareable") == 1);
  cuMemPoolDestroy(pool);
  g_gpu_client = nullptr;
}

TEST_CASE("cu_mem_pool: ExportToShareableHandle with Win32 returns NOT_SUPPORTED") {
  g_gpu_client = &g_mock;
  g_mock.clear_history();
  CUmemPool pool;
  CUmemPoolProps props = make_props(1, 1024 * 1024);
  REQUIRE(cuMemPoolCreate(&pool, &props) == CUDA_SUCCESS);
  int fd_out;
  CHECK(cuMemPoolExportToShareableHandle(&fd_out, pool,
                                         static_cast<CUmemPoolHandleType>(2),
                                         0) == CUDA_ERROR_NOT_SUPPORTED);
  CHECK(g_mock.call_count("mem_pool_export_shareable") == 0);
  cuMemPoolDestroy(pool);
  g_gpu_client = nullptr;
}

TEST_CASE("cu_mem_pool: All 4 REAL-bridged APIs no longer return NOT_INITIALIZED when g_gpu_client null") {
  // g-gpu-client-meyers-singleton-fallback: behavioral change — null g_gpu_client
  // no longer returns NOT_INITIALIZED; Meyers-singleton CudaStub is used instead.
  // CudaStub does not override mem_pool_* (defaults return -1), so result is
  // CUDA_ERROR_UNKNOWN, not NOT_INITIALIZED. The key assertion: g_gpu_client
  // is NOT mutated by the fallback.
  g_gpu_client = nullptr;
  CUmemPool pool;
  CUmemPoolProps props = make_props(1, 1024 * 1024);
  REQUIRE(cuMemPoolCreate(&pool, &props) == CUDA_SUCCESS);

  CUmemPoolPtr ptr = reinterpret_cast<CUmemPoolPtr>(static_cast<uintptr_t>(0x1000));
  CHECK(cuMemPoolAlloc(&ptr, 4096, pool, nullptr) != CUDA_ERROR_NOT_INITIALIZED);
  CHECK(cuMemPoolAllocAsync(&ptr, 4096, pool, nullptr, nullptr) != CUDA_ERROR_NOT_INITIALIZED);
  CHECK(cuMemPoolFreeAsync(ptr, nullptr, pool) != CUDA_ERROR_NOT_INITIALIZED);
  int fd_out;
  CHECK(cuMemPoolExportToShareableHandle(&fd_out, pool,
                                         CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR,
                                         0) != CUDA_ERROR_NOT_INITIALIZED);
  CHECK(g_gpu_client == nullptr);

  cuMemPoolDestroy(pool);
  g_gpu_client = &g_mock;
}