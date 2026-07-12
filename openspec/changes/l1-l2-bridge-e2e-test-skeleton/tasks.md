---
SCOPE: umd-evolution
STATUS: PROPOSED
---

# Tasks: l1-l2-bridge-e2e-test-skeleton

> **Goal**: Ship TaskRunner-side skeleton for L1↔L2 bridge E2E test (`cuGraphLaunch + cuStreamSynchronize` via real `GpuDriverClient`).
> **Risk**: medium (new CMake target, fixture contract, forward-compat with UsrLinuxEmu).
> **Estimated effort**: 1-2 d.
> **Note**: Real test assertions live in UsrLinuxEmu (separate change). This is the TaskRunner skeleton.

## 1. Pre-flight

- [x] 1.1 Verify prerequisites:
  - `umd-evolution-build-default-on` change is done (or build mode is umd-evolution)
  - `g-gpu-client-default-stub-init` change is done (so default-init g_gpu_client = &g_cuda_stub)
  - All 318 existing tests pass

- [x] 1.2 Read `tests/test_fixture/test_gpu_phase2.cpp` to see how GpuDriverClient is constructed in existing tests:
  ```bash
  grep -n "GpuDriverClient" tests/test_fixture/test_gpu_phase2.cpp | head -5
  ```

- [x] 1.3 Read `include/test_fixture/gpu_driver_client.h` to understand the GpuDriverClient public API:
  - `open()`, `close()`, `submit_graph()`, `wait_fence()` etc.

## 2. Skeleton code

- [x] 2.1 Create `tests/umd/test_cu_graph_e2e_standalone.cpp`:

  ```cpp
  // SCOPE: UMD-EVOLUTION
  // test_cu_graph_e2e_standalone.cpp - L1↔L2 bridge E2E test skeleton.
  //
  // This skeleton provides:
  //   - RealGpuFixture: setup/teardown for real GpuDriverClient + /dev/gpgpu0
  //   - 1 placeholder TEST_CASE: cuGraphLaunch via real GpuDriverClient
  //
  // The full test (with real fence signal verification) lives in
  // UsrLinuxEmu/tests/test_cu_graph_e2e_standalone.cpp.
  //
  // SKIP behavior: if /dev/gpgpu0 is not present, the test SKIPs.
  // This allows CI on machines without the plugin to pass.

  #define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
  #include <doctest/doctest.h>

  #include <cuda.h>
  #include <filesystem>
  #include <memory>

  #include "test_fixture/gpu_driver_client.h"
  #include "test_fixture/cuda_stub.h"

  using async_task::gpu::g_gpu_client;
  using async_task::gpu::GpuDriverClient;
  using async_task::gpu::CudaStub;

  namespace {

  class RealGpuFixture {
  public:
    RealGpuFixture() {
      if (!std::filesystem::exists("/dev/gpgpu0")) {
        available_ = false;
        return;
      }
      client_ = std::make_unique<GpuDriverClient>();
      if (client_->open() != 0) {
        client_.reset();
        available_ = false;
        return;
      }
      g_gpu_client = client_.get();
      available_ = true;
    }
    ~RealGpuFixture() {
      if (client_) {
        g_gpu_client = nullptr;
        client_->close();
      }
    }
    bool is_available() const { return available_; }

  protected:
    std::unique_ptr<GpuDriverClient> client_;
    bool available_{false};
  };

  }  // namespace

  TEST_CASE_FIXTURE(RealGpuFixture, "L1↔L2 bridge: cuGraphLaunch via real GpuDriverClient") {
    if (!is_available()) {
      SKIP("/dev/gpgpu0 not present; plugin not loaded. "
           "UsrLinuxEmu side fills in real assertions.");
    }
    // TODO(UsrLinuxEmu): replace this stub with the real E2E assertions.
    // The real test code lives in UsrLinuxEmu/tests/test_cu_graph_e2e_standalone.cpp
    // and exercises:
    //   1. cuGraphCreate + cuGraphInstantiate
    //   2. cuGraphLaunch (real GpuDriverClient → IOCTL → Puller)
    //   3. cuStreamSynchronize (waits on real fence from Puller handleComplete)
    //   4. Verify: g_gpu_client->wait_fence was called with correct fence_id
    //   5. Verify: cuStreamSynchronize returned CUDA_SUCCESS
    SUCCEED("skeleton: real assertions are in UsrLinuxEmu side");
  }
  ```

- [x] 2.2 Verify the file compiles (without running):
  ```bash
  cmake -B build -DTASKRUNNER_BUILD_MODE=umd-evolution
  cmake --build build -j4 --target test_cu_graph_e2e_standalone
  ```

## 3. CMake registration

- [x] 3.1 Read current `cmake/UMDEvolution.cmake` to find where UMD tests are added (around line 81-85 for test_cu_graph).

- [x] 3.2 Add `test_cu_graph_e2e_standalone` to the UMD test list:
  ```cmake
  # In cmake/UMDEvolution.cmake, near where test_cu_graph is defined:
  add_executable(test_cu_graph_e2e_standalone
      tests/umd/test_cu_graph_e2e_standalone.cpp
  )
  target_link_libraries(test_cu_graph_e2e_standalone PRIVATE
      taskrunner_test_fixture    # for GpuDriverClient
      taskrunner_umd_stub        # for CudaStub
  )
  add_test(NAME test_cu_graph_e2e_standalone
           COMMAND test_cu_graph_e2e_standalone)
  ```

  Note: We link `taskrunner_umd_stub` for `CudaStub` (used in default-init), and `taskrunner_test_fixture` for `GpuDriverClient`. The real `plugin_gpu_driver.so` is linked by the UsrLinuxEmu-side test, not here.

## 4. Verification

- [x] 4.1 Build:
  ```bash
  cmake -B build -DTASKRUNNER_BUILD_MODE=umd-evolution
  cmake --build build -j4
  ```
- [x] 4.2 Run new test (should SKIP):
  ```bash
  ctest --test-dir build -R test_cu_graph_e2e_standalone --output-on-failure
  ```
  Expected: 1 test case, 1 SKIP (when /dev/gpgpu0 not present), 0 FAIL.
- [x] 4.3 Run full ctest (regression):
  ```bash
  ctest --test-dir build  # expect 318 PASS + 1 SKIP
  ```
- [x] 4.4 docs-audit:
  ```bash
  tools/docs-audit.sh
  ```
  Expected: no new violations.

## 5. Commit + push

- [x] 5.1 commit (atomic):
  ```bash
  git add tests/umd/test_cu_graph_e2e_standalone.cpp cmake/UMDEvolution.cmake
  git commit -m "test(umd): add L1↔L2 bridge E2E test skeleton

  - New tests/umd/test_cu_graph_e2e_standalone.cpp with RealGpuFixture
  - 1 placeholder TEST_CASE: cuGraphLaunch via real GpuDriverClient
  - Skeleton SKIPs gracefully if /dev/gpgpu0 is not present
  - Full real assertions deferred to UsrLinuxEmu/tests/test_cu_graph_e2e_standalone.cpp
  - CMake target registered in cmake/UMDEvolution.cmake
  - 318/318 existing tests pass + 1 SKIP (no regression)
  - Prerequisite for UMD-EVOLUTION → ACCEPTED promotion (entry 3/5)"
  ```
- [x] 5.2 push:
  ```bash
  git push origin main
  ```

## 6. Forward-compat handoff to UsrLinuxEmu

- [x] 6.1 Open GitHub issue or note in sync-plan §5.3:
  > Follow-up: UsrLinuxEmu side must implement the real L1↔L2 bridge E2E test in `UsrLinuxEmu/tests/test_cu_graph_e2e_standalone.cpp`, using `ModuleLoader` to load `plugin_gpu_driver.so` and filling in the placeholder assertions in TaskRunner `tests/umd/test_cu_graph_e2e_standalone.cpp`.

- [x] 6.2 (Optional) Add a section to `docs/umd-evolution/roadmap/current-status.md` documenting the bridge test status.

## Acceptance Criteria

- `test_cu_graph_e2e_standalone` compiles in umd-evolution mode
- `RealGpuFixture` defined with documented contract
- 1 placeholder TEST_CASE exists, SKIPs when /dev/gpgpu0 absent
- CMake target registered
- 318 existing tests pass + 1 SKIP
- Forward-compat handoff documented for UsrLinuxEmu
