---
SCOPE: test-fixture
STATUS: PROPOSED
---

# Tasks: g-gpu-client-meyers-singleton-fallback

> **Goal**: Add Meyers-singleton fallback in UMD shim files so `cuStreamSynchronize` / `cuGraphLaunch` / `cuMemPoolCreate` work without explicit `g_gpu_client = &g_mock;` setup. Does NOT mutate global `g_gpu_client` (preserves `init_gpu_client()` semantics).
> **Risk**: low (additive fallback, no global mutation, no API changes).
> **Estimated effort**: 0.5-1 d.

## 1. Pre-flight

- [ ] 1.1 Verify baseline: all 318 tests pass on current main.
  ```bash
  cd /workspace/project/UsrLinuxEmu/external/TaskRunner
  cmake -B build_baseline
  cmake --build build_baseline -j4
  ctest --test-dir build_baseline  # expect 318/318
  ```

- [ ] 1.2 Confirm v1 artifacts remain (for traceability, do NOT delete):
  ```bash
  ls openspec/changes/g-gpu-client-default-stub-init/  # should exist
  ```

- [ ] 1.3 Locate shim files to modify (verify line numbers):
  ```bash
  grep -n "get_driver_or_log\|g_gpu_client\|NOT_INITIALIZED" src/umd/libcuda_shim/cu_stream.cpp
  grep -n "get_driver_or_log\|g_gpu_client\|NOT_INITIALIZED" src/umd/libcuda_shim/cu_graph.cpp
  grep -n "get_driver_or_log\|g_gpu_client\|NOT_INITIALIZED" src/umd/libcuda_shim/cu_mem_pool.cpp
  ```
  Expected: each file has a null guard around `g_gpu_client` that returns `CUDA_ERROR_NOT_INITIALIZED`.

## 2. Apply change

### Task 2.1: Create shared accessor header

- [ ] 2.1.1 Create `src/umd/libcuda_shim/cuda_driver_accessor.hpp`:
  ```cpp
  // SCOPE: UMD-EVOLUTION
  /**
   * cuda_driver_accessor.hpp - Meyers-singleton fallback accessor
   *
   * Provides get_driver_or_default() which returns g_gpu_client if non-null,
   * otherwise a Meyers-singleton CudaStub. Does NOT mutate global state.
   *
   * Created: 2026-07-11 (g-gpu-client-meyers-singleton-fallback)
   */

  #pragma once
  #include "test_fixture/cuda_stub.hpp"
  #include "test_fixture/gpu_driver_client.h"

  namespace async_task::umd::shim {

  inline async_task::gpu::IGpuDriver* get_driver_or_default() {
    if (async_task::gpu::g_gpu_client != nullptr) {
      return async_task::gpu::g_gpu_client;
    }
    // Meyers singleton fallback — constructed on first call, never destroyed
    static async_task::gpu::CudaStub default_stub;
    return &default_stub;
  }

  }  // namespace async_task::umd::shim
  ```

### Task 2.2: Modify cu_stream.cpp

- [ ] 2.2.1 Add `#include "cuda_driver_accessor.hpp"` after existing includes.
- [ ] 2.2.2 Locate the `cuStreamSynchronize` function body. Replace the null guard pattern:
  ```cpp
  // OLD (around line 55):
  auto* driver = async_task::gpu::g_gpu_client;
  if (!driver) {
    return CUDA_ERROR_NOT_INITIALIZED;
  }
  ```
  with:
  ```cpp
  // NEW:
  auto* driver = async_task::umd::shim::get_driver_or_default();
  ```
- [ ] 2.2.3 Verify all other functions in `cu_stream.cpp` that use `g_gpu_client` (e.g., `cuStreamWaitEvent`) also use the accessor. Update each.

### Task 2.3: Modify cu_graph.cpp

- [ ] 2.3.1 Add `#include "cuda_driver_accessor.hpp"`.
- [ ] 2.3.2 Locate `cuGraphLaunch` (around line 48). Replace null guard with `get_driver_or_default()`.
- [ ] 2.3.3 Verify all other functions using `g_gpu_client` are updated.

### Task 2.4: Modify cu_mem_pool.cpp

- [ ] 2.4.1 Add `#include "cuda_driver_accessor.hpp"`.
- [ ] 2.4.2 Locate `cuMemPoolCreate` and related functions (around line 42). Replace null guard with `get_driver_or_default()`.
- [ ] 2.4.3 Verify all other functions using `g_gpu_client` are updated.

### Task 2.5: Add regression test for fallback

- [ ] 2.5.1 Create `tests/umd/test_shim_default_init.cpp`:
  ```cpp
  // SCOPE: UMD-EVOLUTION
  /**
   * test_shim_default_init.cpp - Regression tests for g_gpu_client default fallback
   *
   * Verifies:
   *   T1: cuStreamSynchronize works with g_gpu_client == nullptr (Meyers fallback)
   *   T2: cuGraphLaunch works with g_gpu_client == nullptr
   *   T3: Fallback does NOT mutate g_gpu_client (verifies design contract)
   */

  #define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
  #include "doctest.h"

  #include "cuda.h"
  #include "test_fixture/gpu_driver_client.h"

  TEST_CASE("cuStreamSynchronize works with g_gpu_client nullptr") {
    async_task::gpu::g_gpu_client = nullptr;  // explicit clear
    CUstream s;
    REQUIRE(cuStreamCreate(&s, 0) == CUDA_SUCCESS);
    CHECK(cuStreamSynchronize(s) == CUDA_SUCCESS);  // via Meyers fallback
    CHECK(async_task::gpu::g_gpu_client == nullptr);  // not mutated
  }

  TEST_CASE("cuGraphLaunch works with g_gpu_client nullptr") {
    async_task::gpu::g_gpu_client = nullptr;
    // ... create graph via fallback, launch, verify
    // (Details depend on existing cuGraphLaunch signature in test_cu_graph.cpp)
  }
  ```

- [ ] 2.5.2 Add test target to `cmake/UMDEvolution.cmake` (or wherever umd tests are registered):
  ```cmake
  # In cmake/UMDEvolution.cmake (or tests/umd/CMakeLists.txt)
  add_executable(test_shim_default_init tests/umd/test_shim_default_init.cpp)
  target_link_libraries(test_shim_default_init PRIVATE taskrunner_umd doctest_with_main)
  add_test(NAME test_shim_default_init COMMAND test_shim_default_init)
  ```

## 3. Verification

- [ ] 3.1 Default build (umd-evolution):
  ```bash
  cd /workspace/project/UsrLinuxEmu/external/TaskRunner
  cmake -B build
  cmake --build build -j4
  ctest --test-dir build  # expect 318 + new tests (T1-T3) = 320-321 PASS
  ```

- [ ] 3.2 test-fixture opt-out (verifies fallback doesn't break test-fixture-only builds):
  ```bash
  rm -rf build_tf
  TASKRUNNER_BUILD_MODE=test-fixture cmake -B build_tf
  cmake --build build_tf -j4
  ctest --test-dir build_tf  # expect 318/318 (libcuda_shim not built, no fallback tests)
  ```

- [ ] 3.3 CLI smoke test (verify init_gpu_client() still creates GpuDriverClient):
  ```bash
  ./build/taskrunner --help 2>&1 | head -20
  # Expect: normal CLI help output, no crash, no "CudaStub" mentions in logs
  ```

- [ ] 3.4 docs-audit.sh passes:
  ```bash
  ./tools/docs-audit.sh  # expect exit 0
  ```

- [ ] 3.5 Verify fallback does NOT mutate global:
  ```bash
  ctest --test-dir build -R "test_shim_default_init" --output-on-failure
  # T1, T2, T3 should all PASS
  ```

## 4. Commit + push

- [ ] 4.1 commit (atomic):
  ```bash
  cd /workspace/project/UsrLinuxEmu/external/TaskRunner
  git add openspec/changes/g-gpu-client-meyers-singleton-fallback/
  git add src/umd/libcuda_shim/cuda_driver_accessor.hpp
  git add src/umd/libcuda_shim/cu_stream.cpp
  git add src/umd/libcuda_shim/cu_graph.cpp
  git add src/umd/libcuda_shim/cu_mem_pool.cpp
  git add tests/umd/test_shim_default_init.cpp
  git add cmake/UMDEvolution.cmake  # or wherever test target registered
  git commit -m "feat(umd): Meyers-singleton fallback for g_gpu_client (v2 supersedes v1)

  - Add cuda_driver_accessor.hpp with get_driver_or_default() helper
  - 3 shim files (cu_stream, cu_graph, cu_mem_pool) use accessor instead of null guard
  - UMD shim functions now work without explicit g_gpu_client = &g_mock; setup
  - Does NOT mutate global g_gpu_client — init_gpu_client() still creates GpuDriverClient for CLI
  - 318+ tests pass; new test_shim_default_init adds 2-3 fallback tests
  - Supersedes g-gpu-client-default-stub-init (v1 baseline was factually wrong)

  Refs: g-gpu-client-meyers-singleton-fallback (v2 supersedes v1)"
  ```

- [ ] 4.2 push:
  ```bash
  git push origin main
  ```

## Acceptance Criteria

- 318+ existing tests pass (no regression)
- New `test_shim_default_init` tests T1-T3 pass
- CLI smoke test passes (init_gpu_client still creates GpuDriverClient)
- `tools/docs-audit.sh` passes
- v1 artifacts remain in `openspec/changes/g-gpu-client-default-stub-init/` (NOT deleted, for traceability)
- `g_gpu_client` global pointer is NEVER mutated by the fallback path (verified by T1's final assertion)
- 3 shim files (`cu_stream.cpp`, `cu_graph.cpp`, `cu_mem_pool.cpp`) all use the shared accessor (grep verifies)