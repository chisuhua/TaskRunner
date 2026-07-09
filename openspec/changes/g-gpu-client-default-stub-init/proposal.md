---
SCOPE: test-fixture
STATUS: PROPOSED
---

## Why

`g_gpu_client` (declared in `include/test_fixture/gpu_driver_client.h:836` as `extern IGpuDriver* g_gpu_client;`) currently defaults to `nullptr`. This means:

- Every test that exercises UMD shim functions (`cuStreamSynchronize`, `cuGraphLaunch`, etc.) **must** explicitly set `g_gpu_client = &g_mock;` or `g_gpu_client = &g_cuda_stub;` before calling shim functions
- Forgetting this leads to `CUDA_ERROR_NOT_INITIALIZED` returns (recently formalized in `test-cu-graph-coverage-fixes` 4d266a8)
- New shim consumers must know the protocol

The static instance `CudaStub g_cuda_stub;` already exists in `src/test_fixture/cuda_stub.cpp` — it's just not wired to `g_gpu_client` by default. This change completes the wiring so:

- `g_gpu_client` defaults to `&g_cuda_stub` (in test builds)
- Shim functions work out of the box
- Existing tests that explicitly set `g_gpu_client` are unaffected (their override wins)
- The null check in shim functions is kept as a belt-and-suspenders safeguard

This is **entry condition 2/5** for UMD-EVOLUTION → ACCEPTED promotion. It also unblocks the L1↔L2 bridge test (change 3) which needs a working default `g_gpu_client`.

## What Changes

In `include/test_fixture/gpu_driver_client.h` (or a new file `src/test_fixture/gpu_client_init.cpp`):
- Add static initialization that sets `g_gpu_client = &g_cuda_stub;` for test builds
- The initialization runs before any test executes (static init order)
- Existing explicit `g_gpu_client = &g_mock;` assignments in test code override the default

**Note**: `CudaStub g_cuda_stub;` is a static instance in the test-fixture library. The default-init code must reference it (no new instance).

## Capabilities

### New Capabilities

(none — internal test-fixture change)

### Modified Capabilities

(none)

## Impact

- **Files affected**:
  - `include/test_fixture/gpu_driver_client.h` (declaration + default-init)
  - Possibly a new `src/test_fixture/gpu_client_init.cpp` (init code isolated)
- **No production code changes** (UMD shim null check preserved)
- **No API changes** (existing `g_gpu_client` external symbol unchanged)
- **No new tests required** (existing 318 tests will validate the change)

## Acceptance Criteria

- A new test consumer that does NOT call `g_gpu_client = &g_mock;` (or stub) compiles and runs successfully
- `cuStreamSynchronize(hStream)` called on default-init `g_gpu_client` returns `CUDA_SUCCESS` (via CudaStub's wait_fence which returns success)
- All 318 existing tests still pass
- Mock-based tests (which explicitly set `g_gpu_client = &g_mock;`) still work
- No new tests in `test_gpu_phase2` or `test_cuda_shim` regress

## Risk

- **Static init order**: C++ static initialization order across translation units is unspecified. If `g_cuda_stub` is in one TU and `g_gpu_client = &g_cuda_stub` is in another, the latter might run before `g_cuda_stub` is constructed. Mitigated by:
  - Putting the init in the same TU as `CudaStub::g_cuda_stub` (or a TU that includes cuda_stub.cpp)
  - OR using a function-local static (Meyers singleton pattern) for `g_gpu_client` initialization
- **Cross-DSO init**: The test-fixture library is linked into test binaries. If the init runs after the first test, it's too late. Mitigated by static init in linked TU.
