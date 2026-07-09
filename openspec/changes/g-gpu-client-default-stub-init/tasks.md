---
SCOPE: test-fixture
STATUS: PROPOSED
---

# Tasks: g-gpu-client-default-stub-init

> **Goal**: Default-initialize `g_gpu_client = &g_cuda_stub` so UMD shim functions work without explicit test setup.
> **Risk**: low (single-file change in test-fixture scope).
> **Estimated effort**: 0.5 d.

## 1. Pre-flight

- [ ] 1.1 Verify baseline: all 318 tests pass on current main.
  ```bash
  cd /workspace/project/UsrLinuxEmu/external/TaskRunner
  cmake -B build_baseline -DTASKRUNNER_BUILD_MODE=umd-evolution
  cmake --build build_baseline -j4
  ctest --test-dir build_baseline  # expect 318/318
  ```
- [ ] 1.2 Confirm `g_cuda_stub` is the only static CudaStub instance in `src/test_fixture/cuda_stub.cpp`:
  ```bash
  grep -n "CudaStub g_cuda_stub" src/test_fixture/cuda_stub.cpp
  ```

## 2. Apply change

- [ ] 2.1 Edit `src/test_fixture/cuda_stub.cpp`:
  - Add includes for `gpu_driver_client.h` (if not already included)
  - Add the default-init helper at the bottom of the file:
    ```cpp
    namespace {
    struct GpuClientDefaultInit {
      GpuClientDefaultInit() {
        if (async_task::gpu::g_gpu_client == nullptr) {
          async_task::gpu::g_gpu_client = &async_task::gpu::g_cuda_stub;
        }
      }
    };
    const GpuClientDefaultInit g_gpu_client_default_init;
    }  // namespace
    ```
  - Ensure `CudaStub g_cuda_stub;` is declared **before** the helper (within-TU static init order is declaration order).

- [ ] 2.2 (Optional) Add a static test in `tests/test_fixture/test_gpu_phase2.cpp`:
  ```cpp
  TEST_CASE("g_gpu_client defaults to CudaStub after static init") {
    REQUIRE(async_task::gpu::g_gpu_client != nullptr);
    REQUIRE(dynamic_cast<CudaStub*>(async_task::gpu::g_gpu_client) != nullptr);
  }
  ```
  This makes the new contract explicit and tested.

## 3. Verification

- [ ] 3.1 Default build (umd-evolution):
  ```bash
  cmake -B build
  cmake --build build -j4
  ctest --test-dir build  # expect 318/318 PASS
  ```
- [ ] 3.2 test-fixture opt-out (verify default init does NOT pull UMD shim):
  ```bash
  rm -rf build_tf
  TASKRUNNER_BUILD_MODE=test-fixture cmake -B build_tf
  cmake --build build_tf -j4
  ctest --test-dir build_tf
  ```
- [ ] 3.3 New shim consumer (no explicit g_gpu_client set):
  ```cpp
  // In a scratch test (commit-only, don't keep):
  TEST_CASE("shim works without explicit g_gpu_client setup") {
    // Note: NO g_gpu_client = &g_mock; here
    CUstream s;
    REQUIRE(cuStreamCreate(&s, 0) == CUDA_SUCCESS);
    CHECK(cuStreamSynchronize(s) == CUDA_SUCCESS);  // uses default CudaStub
  }
  ```
  Expect: compiles and passes.

## 4. Commit + push

- [ ] 4.1 commit (atomic):
  ```bash
  git add src/test_fixture/cuda_stub.cpp tests/test_fixture/test_gpu_phase2.cpp
  git commit -m "test(fixture): default-init g_gpu_client to CudaStub

  - g_gpu_client now points to &g_cuda_stub by default in test builds
  - Shim functions (cuStreamSynchronize, cuGraphLaunch, etc.) work without explicit setup
  - Existing tests that explicitly override g_gpu_client are unaffected
  - Static init in same TU as g_cuda_stub ensures correct construction order
  - 318/318 tests pass
  - Prerequisite for UMD-EVOLUTION → ACCEPTED promotion (entry 2/5)"
  ```
- [ ] 4.2 push:
  ```bash
  git push origin main
  ```

## Acceptance Criteria

- 318/318 tests pass (no regression)
- New shim consumer test (no `g_gpu_client = ...` set) compiles and runs
- Optional: `g_gpu_client defaults to CudaStub` test case added
- `tools/docs-audit.sh` passes
