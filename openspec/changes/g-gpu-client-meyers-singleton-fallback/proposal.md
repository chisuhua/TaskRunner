---
SCOPE: test-fixture
STATUS: PROPOSED
---

## Why

`g_gpu_client` (declared in `include/test_fixture/gpu_driver_client.h:836` as `extern IGpuDriver* g_gpu_client;`) currently defaults to `nullptr`. This means UMD shim functions (`cuStreamSynchronize`, `cuGraphLaunch`, `cuMemPoolCreate`, etc.) all return `CUDA_ERROR_NOT_INITIALIZED` unless the caller explicitly assigns `g_gpu_client = &g_mock;` (or `&g_cuda_stub`) before invocation.

**However, the previous attempt to fix this (`g-gpu-client-default-stub-init`, 2026-07-09) proposed a baseline that was factually wrong**: it claimed `CudaStub g_cuda_stub;` already exists as a static global instance in `src/test_fixture/cuda_stub.cpp`. Git archaeology confirms this instance **never existed** in any commit (`git log -S "CudaStub g_cuda_stub"` returns only the v1 spec commit itself). The v1 proposal also conflated three distinct CudaStub usage patterns:

1. `cmd_cuda.cpp:42` — TU-local `static std::unique_ptr<CudaStub> g_runtime_stub` (CLI runtime)
2. `cu_init.cpp:24` — TU-local `std::unique_ptr<CudaStub> g_stub` (shim lazy-init singleton)
3. Test fixtures like `tests/umd/test_cu_graph.cpp:24` — TU-local `static MockGpuDriver g_mock`

None of these are namespace-global `g_cuda_stub`, and none are reachable via `g_gpu_client`. **v1 is hereby superseded by this change.**

This change (v2) **preserves v1's goal** (UMD shim works without explicit setup) but uses the **Meyers singleton fallback pattern** instead of mutating global state:

- In each shim file's `get_driver_or_log()` helper, when `g_gpu_client == nullptr`, return a Meyers-singleton `static CudaStub` instead of `nullptr`
- This **does not modify the global `g_gpu_client` pointer**, so `init_gpu_client()` (which checks `if (g_gpu_client) return 0;`) is unaffected and continues to create `GpuDriverClient` for CLI/production paths
- All 318 existing tests pass (they explicitly set `g_gpu_client`, override is preserved)
- New shim consumer tests can call `cuStreamSynchronize` without explicit setup

This is **entry condition 2/5 for UMD-EVOLUTION → ACCEPTED promotion** (as v1 was). It unblocks the L1↔L2 bridge test skeleton (change `l1-l2-bridge-e2e-test-skeleton`) which needs a working default shim path.

## What Changes

In **3 shim files** (`umd-evolution` scope, currently shipping under default build mode):

| File | Change |
|---|---|
| `src/umd/libcuda_shim/cu_stream.cpp` (line ~55) | Add Meyers-singleton fallback in `get_driver_or_log()` |
| `src/umd/libcuda_shim/cu_graph.cpp` (line ~48) | Same fallback pattern |
| `src/umd/libcuda_shim/cu_mem_pool.cpp` (line ~42) | Same fallback pattern |

The fallback helper (extracted to a shared location to avoid duplication):

```cpp
// src/umd/libcuda_shim/cuda_driver_accessor.hpp (new)
#pragma once
#include "test_fixture/cuda_stub.hpp"
#include "test_fixture/gpu_driver_client.h"

namespace async_task::umd::shim {

inline async_task::gpu::IGpuDriver* get_driver_or_default() {
  if (async_task::gpu::g_gpu_client != nullptr) {
    return async_task::gpu::g_gpu_client;
  }
  // Meyers singleton fallback — does NOT modify global g_gpu_client
  static async_task::gpu::CudaStub default_stub;
  return &default_stub;
}

}  // namespace async_task::umd::shim
```

Each shim file includes this header and replaces:
```cpp
auto* driver = g_gpu_client;
if (!driver) {
  return CUDA_ERROR_NOT_INITIALIZED;  // OLD
}
```
with:
```cpp
auto* driver = async_task::umd::shim::get_driver_or_default();
// driver is guaranteed non-null now; previous null guard removed
```

**Not changed**:
- `src/test_fixture/cuda_stub.cpp` (no new static instance, no init helper)
- `src/test_fixture/gpu_driver_client.cpp` (`init_gpu_client()` unchanged — still creates `GpuDriverClient` when null)
- `include/test_fixture/cuda_stub.hpp` (no new `extern CudaStub g_cuda_stub;`)
- `include/test_fixture/gpu_driver_client.h` (no signature changes)

## Capabilities

### New Capabilities

- **`shim-works-without-explicit-setup`**: UMD shim functions (`cuStreamSynchronize`, `cuGraphLaunch`, `cuMemPoolCreate`) succeed when `g_gpu_client` is nullptr by falling back to a Meyers-singleton `CudaStub`. New shim consumer tests can call these functions without explicit `g_gpu_client = &g_mock;` setup.

### Modified Capabilities

(none — pure additive fallback layer)

## Impact

- **Files affected**:
  - `src/umd/libcuda_shim/cu_stream.cpp` (modify, ~5 lines)
  - `src/umd/libcuda_shim/cu_graph.cpp` (modify, ~5 lines)
  - `src/umd/libcuda_shim/cu_mem_pool.cpp` (modify, ~5 lines)
  - `src/umd/libcuda_shim/cuda_driver_accessor.hpp` (new, ~15 lines)
  - `tests/umd/test_shim_default_init.cpp` (new, ~40 lines, 2-3 test cases)
- **No production code changes** (UMD shim null guard removed but fallback guarantees driver non-null)
- **No API changes** (existing `g_gpu_client` external symbol unchanged)
- **No changes to existing test patterns** (existing 318 tests pass)
- **No cross-repo changes** (TaskRunner-only)
- **No submodule pointer bump required**

## Acceptance Criteria

- A new shim consumer test that does NOT call `g_gpu_client = &g_mock;` compiles and runs successfully
- `cuStreamSynchronize(hStream)` called with `g_gpu_client == nullptr` returns `CUDA_SUCCESS` (via CudaStub::wait_fence which returns success)
- `cuGraphLaunch(hGraphExec, hStream)` called with `g_gpu_client == nullptr` returns `CUDA_SUCCESS` (via CudaStub::submit_graph mock)
- All 318 existing tests still pass (no regression)
- Mock-based tests (which explicitly set `g_gpu_client = &g_mock;`) still work
- `init_gpu_client()` still creates `GpuDriverClient` for CLI path (verified by `cli_main.cpp:35` behavior)
- `tools/docs-audit.sh` passes
- Test `g_gpu_client` global pointer value is **never mutated by the fallback** (verified by `REQUIRE(g_gpu_client == nullptr)` at end of new test)

## Risk

- **Meyers singleton state leakage**: fallback `static CudaStub` accumulates state (`next_fence_id_` atomic counter, `queue_map_` mock state) across TEST_CASEs. Non-blocking: new fallback tests use `>= 1` assertions rather than `== 1`, and existing tests unaffected.
- **Cross-TU consistency**: 3 shim files have local `get_driver_or_log()` helpers. Risk of drift if one is updated but not the others. Mitigated by extracting shared `cuda_driver_accessor.hpp` header.
- **v1 supersession traceability**: v1 change `g-gpu-client-default-stub-init` artifacts remain in `openspec/changes/` directory but should not be shipped. Mitigated by explicit supersession note in v2 proposal and `.openspec.yaml` `supersedes:` field.

## Supersession

This change **supersedes** `g-gpu-client-default-stub-init` (2026-07-09). v1 artifacts remain in `openspec/changes/g-gpu-client-default-stub-init/` for traceability but should not be shipped. v1's baseline claim was factually wrong (Oracle analysis confirms `CudaStub g_cuda_stub;` static instance never existed). v1's Risk #3 mitigation ("production build doesn't link test_fixture") was also wrong (umd-evolution default build mode DOES link `cuda_stub.cpp` via `cu_init.cpp:14`).