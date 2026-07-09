---
SCOPE: umd-evolution
STATUS: PROPOSED
---

## Why

After `test-cu-graph-coverage-fixes` (4d266a8) closed 3 cuGraph coverage gaps in TaskRunner's UMD shim, and `mem-pool-async-fence-coverage` (UsrLinuxEmu a035e7b) added fence signal verification for MEM_POOL async ops, the remaining **structural gap** is:

> **No test exercises the L1 (UMD shim, MockGpuDriver) ↔ L2 (real plugin IOCTL) bridge.**

Concretely:
- `tests/umd/test_cu_graph.cpp:376` ("Launch + cuStreamSynchronize polls fence") validates the UMD shim's `cuGraphLaunch + cuStreamSynchronize` chain using **MockGpuDriver** — a L1 unit test
- `UsrLinuxEmu/tests/test_gpu_plugin.cpp:495` validates the **raw IOCTL** path: `GPU_IOCTL_GRAPH_LAUNCH → Puller handleComplete → GPU_IOCTL_WAIT_FENCE signaled` — a L2 integration test
- **There is no test where `cuGraphLaunch` goes through the real `GpuDriverClient` to the real plugin, and `cuStreamSynchronize` waits on the real fence**

This bridge is the "ACCEPTED" test for UMD-EVOLUTION: it proves the shim's user-facing API actually works end-to-end with the real backend. Without it, the UMD shim is "compiles but we don't know if it really works in production".

This change ships the **TaskRunner-side skeleton** for that bridge test:
- New `tests/umd/test_cu_graph_e2e_standalone.cpp` with `RealGpuFixture` + 1 placeholder test
- CMake registration in `cmake/UMDEvolution.cmake`
- The placeholder test uses real `GpuDriverClient` and gracefully SKIPs if `/dev/gpgpu0` is unavailable
- Full test assertions (where the actual fence signal verification happens) live in **UsrLinuxEmu side** (`tests/test_cu_graph_e2e_standalone.cpp` for that repo), where ModuleLoader / VFS / plugin runtime infrastructure lives

This is **entry condition 3/5** for UMD-EVOLUTION → ACCEPTED promotion.

## What Changes

In TaskRunner:
- **New file**: `tests/umd/test_cu_graph_e2e_standalone.cpp` (skeleton)
- **Modified**: `cmake/UMDEvolution.cmake` (register the new test target)

The skeleton:
- Defines `RealGpuFixture` with documented setup/teardown contract
- Has 1 `TEST_CASE` that calls `cuGraphLaunch` via the real `GpuDriverClient`
- Skips gracefully if `/dev/gpgpu0` is not present (so CI on machines without the plugin still passes)
- Comments mark where the UsrLinuxEmu-side implementation will fill in the real assertions

In UsrLinuxEmu (separate change, not part of this skeleton):
- Real test code in `tests/test_cu_graph_e2e_standalone.cpp` for the UsrLinuxEmu repo
- Loads `plugin_gpu_driver.so` via `ModuleLoader`
- Opens `/dev/gpgpu0`
- Fills in the placeholder's assertions

## Capabilities

### New Capabilities

- `l1-l2-bridge-e2e-test`: Skeleton E2E test for `cuGraphLaunch + cuStreamSynchronize` via real `GpuDriverClient`. (Full implementation requires UsrLinuxEmu-side cooperation.)

### Modified Capabilities

(none)

## Impact

- **Files affected (TaskRunner side)**:
  - `tests/umd/test_cu_graph_e2e_standalone.cpp` (new, ~80-150 lines)
  - `cmake/UMDEvolution.cmake` (add new test target, ~5 lines)
- **No production code changes**
- **No existing test changes** (L1 mock-based tests in `test_cu_graph.cpp` stay)
- **Cross-repo follow-up required** (UsrLinuxEmu side)

## Acceptance Criteria

- New `test_cu_graph_e2e_standalone` compiles in umd-evolution mode
- `RealGpuFixture` is defined with documented contract
- 1 placeholder `TEST_CASE` exists and compiles
- Placeholder test gracefully SKIPs when `/dev/gpgpu0` is not available
- Existing 318 tests still pass (no regression)
- `tools/docs-audit.sh` passes

## Risk

- **Build infra complexity**: linking `cuda_taskrunner` + real plugin + the test binary requires careful CMake. The placeholder may need to be a no-op for now (link only, not run) to avoid build complications.
- **CI behavior**: CI machines without `/dev/gpgpu0` will SKIP the new test. This is acceptable but should be documented in CI logs.
- **Forward-compat**: The skeleton must be designed so UsrLinuxEmu side can fill in real assertions without re-touching TaskRunner.
