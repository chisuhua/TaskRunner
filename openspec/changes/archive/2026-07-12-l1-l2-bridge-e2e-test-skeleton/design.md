---
SCOPE: umd-evolution
STATUS: PROPOSED
---

## Context

After `test-cu-graph-coverage-fixes` (4d266a8) and `mem-pool-async-fence-coverage` (UsrLinuxEmu a035e7b), the test coverage looks like:

| Layer | Test | Status |
|---|---|---|
| L1 (UMD shim with MockGpuDriver) | `tests/umd/test_cu_graph.cpp:376` | ✅ 32/32 |
| L2 (raw IOCTL with real plugin) | `UsrLinuxEmu/tests/test_gpu_plugin.cpp:495` | ✅ |
| L1↔L2 (UMD shim with real GpuDriverClient) | **None** | ❌ |

The bridge test requires:
- A test binary that links `cuda_taskrunner` (the real shim) + `GpuDriverClient` (the real driver client)
- A test fixture that opens `/dev/gpgpu0` (registered by the UsrLinuxEmu plugin)
- A test that calls `cuGraphLaunch` through the shim and waits via `cuStreamSynchronize`
- Verification that the fence signal came from the real Puller's `handleComplete()`, not from MockGpuDriver

Where the test should live:
- **TaskRunner side** (`tests/umd/test_cu_graph_e2e_standalone.cpp`): the test binary + fixture skeleton + CMake target
- **UsrLinuxEmu side** (`tests/test_cu_graph_e2e_standalone.cpp` for that repo): the actual test code that loads `plugin_gpu_driver.so` and fills in the assertions

This change ships the TaskRunner-side skeleton only.

## Goals / Non-Goals

**Goals:**
- `RealGpuFixture` class defined in TaskRunner tests/umd/
- 1 placeholder `TEST_CASE` that compiles and runs (with SKIP if /dev/gpgpu0 unavailable)
- CMake target registered so the binary can be built
- Forward-compat: UsrLinuxEmu side can fill in real assertions without further TaskRunner changes

**Non-Goals:**
- Implementing the real test (lives in UsrLinuxEmu)
- Replacing the L1 (mock) test_cu_graph.cpp 32/32 tests
- Building real device file infrastructure (UsrLinuxEmu VFS work)

## Decisions

### Decision 1: Skeleton lives in TaskRunner; full test in UsrLinuxEmu

TaskRunner has the UMD shim + GpuDriverClient. UsrLinuxEmu has the plugin + ModuleLoader + VFS. The bridge test needs both. Splitting:
- TaskRunner: skeleton (RealGpuFixture, placeholder test, CMake target)
- UsrLinuxEmu: real test code (loads plugin, opens /dev/gpgpu0, fills in assertions)

**Rationale**: Each side owns the build infra for its own concerns. The skeleton in TaskRunner ensures the build target exists; UsrLinuxEmu side can re-use the test target or build its own against the same source.

### Decision 2: Graceful SKIP when /dev/gpgpu0 unavailable

The skeleton test uses Catch2/doctest's skip mechanism:

```cpp
TEST_CASE("L1↔L2 bridge: cuGraphLaunch via real GpuDriverClient") {
  if (!std::filesystem::exists("/dev/gpgpu0")) {
    SKIP("/dev/gpgpu0 not present; plugin not loaded");
  }
  // ... real test code
}
```

**Rationale**: CI machines without the plugin installed shouldn't fail. The test passes (= SKIP) by default, runs (= real assertions) when the plugin is available. This is forward-compat with UsrLinuxEmu's CI matrix.

### Decision 3: RealGpuFixture uses Meyers singleton for g_gpu_client

The fixture's setup creates a `GpuDriverClient` instance and assigns it to `g_gpu_client`. The teardown restores null. Other tests are unaffected because doctest runs each TEST_CASE in a fresh fixture instance.

```cpp
class RealGpuFixture {
public:
  RealGpuFixture() {
    if (!std::filesystem::exists("/dev/gpgpu0")) return;
    client_ = std::make_unique<GpuDriverClient>();
    REQUIRE(client_->open() == 0);
    g_gpu_client = client_.get();
  }
  ~RealGpuFixture() {
    if (client_) {
      g_gpu_client = nullptr;
      client_->close();
    }
  }
protected:
  std::unique_ptr<GpuDriverClient> client_;
};
```

**Rationale**: RAII pattern; consistent with `MockGpuDriver` usage in L1 tests.

### Decision 4: Placeholder test documents the assertion contract

The placeholder test calls `cuGraphLaunch` and verifies a stub assertion. It has `// TODO(UsrLinuxEmu): replace stub assertion with real fence_id + cuStreamSynchronize signaled check` comment, making the contract explicit for the future implementer.

## Risks / Trade-offs

| # | Risk | Mitigation |
|---|------|------------|
| 1 | Build infra complexity: linking real shim + plugin + test binary | Start with placeholder that links only `cuda_taskrunner` (no real plugin). UsrLinuxEmu side adds plugin linking when filling in real test. |
| 2 | CI machines without /dev/gpgpu0 will SKIP | Acceptable; documented in CI matrix. The test passes when the plugin is available. |
| 3 | Forward-compat: UsrLinuxEmu side may need to modify the skeleton | Skeleton is intentionally minimal (just fixture + placeholder). Real test code is in UsrLinuxEmu. |
| 4 | Static init of g_gpu_client may conflict with RealGpuFixture | `g-gpu-client-default-stub-init` change sets default to CudaStub; RealGpuFixture overrides with GpuDriverClient. Fixture's RAII restores null on teardown. |

## Verification

```bash
# Build the skeleton test
cmake -B build -DTASKRUNNER_BUILD_MODE=umd-evolution
cmake --build build -j4 --target test_cu_graph_e2e_standalone

# Run (should SKIP if /dev/gpgpu0 absent)
./build/test_cu_graph_e2e_standalone

# All existing tests still pass
ctest --test-dir build  # expect 318/318 PASS + the new test SKIP
```

## Open Questions

- **Q**: Should the skeleton link `libplugin_gpu_driver.so` (real plugin) or just `cuda_taskrunner`?  
  **A**: Just `cuda_taskrunner` for now. UsrLinuxEmu side adds plugin linking when implementing the real test.
- **Q**: Should the skeleton run as a standalone binary, or as a ctest entry?  
  **A**: Both. CMake registers it as an executable (for `ctest -R`), and the executable can be run directly.
