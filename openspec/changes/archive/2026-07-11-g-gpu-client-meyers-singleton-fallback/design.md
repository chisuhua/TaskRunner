---
SCOPE: test-fixture
STATUS: PROPOSED
---

## Context

`include/test_fixture/gpu_driver_client.h:836` declares:
```cpp
extern IGpuDriver* g_gpu_client;  // Phase 4 (M1): changed from GpuDriverClient* to allow MockGpuDriver injection
```

`src/test_fixture/gpu_driver_client.cpp:11` defines it as:
```cpp
IGpuDriver* g_gpu_client = nullptr;  // zero-initialized BSS
```

`init_gpu_client()` (called in `cli_main.cpp:35`) creates a real `GpuDriverClient` instance when `g_gpu_client` is null:
```cpp
int init_gpu_client() {
    if (g_gpu_client) { return 0; }  // already set, skip
    g_gpu_client = new GpuDriverClient();
    if (g_gpu_client->open() != 0) { return -1; }
    return 0;
}
```

UMD shim files (`src/umd/libcuda_shim/cu_stream.cpp:55`, `cu_graph.cpp:48`, `cu_mem_pool.cpp:42`) use `g_gpu_client` and return `CUDA_ERROR_NOT_INITIALIZED` if it's null.

**The current state**: shim functions only work if the caller (typically a test) explicitly assigns `g_gpu_client = &g_mock;` before invocation. Forgetting this causes silent test failures with NOT_INITIALIZED errors (formalized by recent fix in `test-cu-graph-coverage-fixes`, commit `4d266a8`).

## Goals / Non-Goals

**Goals:**
- UMD shim functions (`cuStreamSynchronize`, `cuGraphLaunch`, `cuMemPoolCreate`) work when `g_gpu_client` is null, falling back to a default `CudaStub`
- Existing tests that explicitly set `g_gpu_client` are unaffected
- `init_gpu_client()` (CLI/production path) is unaffected and continues to create `GpuDriverClient`
- All 318 existing tests still pass

**Non-Goals:**
- Creating a global static `CudaStub g_cuda_stub;` instance (supersedes v1's incorrect approach)
- Mutating the global `g_gpu_client` pointer (would break `init_gpu_client()` short-circuit, see Risk #1 below)
- Changing `CudaStub::wait_fence` semantics or any IGpuDriver method implementations
- Adding new test patterns (existing tests still use TU-local `g_mock` and explicit set)
- Production build changes (test-fixture scope only; CLI stays on `GpuDriverClient`)

## Decisions

### Decision 1: Meyers singleton fallback in shim accessor (chosen)

Extract a shared header `cuda_driver_accessor.hpp` with the fallback helper:

```cpp
// src/umd/libcuda_shim/cuda_driver_accessor.hpp
#pragma once
#include "test_fixture/cuda_stub.hpp"
#include "test_fixture/gpu_driver_client.h"

namespace async_task::umd::shim {

// Returns g_gpu_client if non-null, otherwise a Meyers-singleton CudaStub.
// Does NOT mutate the global g_gpu_client pointer.
inline async_task::gpu::IGpuDriver* get_driver_or_default() {
  if (async_task::gpu::g_gpu_client != nullptr) {
    return async_task::gpu::g_gpu_client;
  }
  static async_task::gpu::CudaStub default_stub;
  return &default_stub;
}

}  // namespace async_task::umd::shim
```

Each shim file replaces its local null guard with this accessor.

**Rationale**:
- **Meyers singleton guarantees first-call construction** → no static init order fiasco
- **Does not mutate `g_gpu_client`** → `init_gpu_client()` continues to work for CLI path (creates `GpuDriverClient` when null)
- **No cross-TU synchronization needed** → the singleton is per-call-site in inline header
- **Cheap fallback** → single pointer check on hot path

### Decision 2: Extract shared header (not 3 inline copies)

The same fallback pattern is needed in 3 shim files. Extract to `cuda_driver_accessor.hpp` to avoid drift.

**Rationale**: DRY principle. If fallback logic changes (e.g., add logging, change default), only one file to update.

### Decision 3: Keep `CUDA_ERROR_NOT_INITIALIZED` removed; rely on fallback

Old code returned `CUDA_ERROR_NOT_INITIALIZED` when `g_gpu_client == nullptr`. New code calls `get_driver_or_default()` which is guaranteed non-null. The error path is removed.

**Rationale**: The error was a defense against forgetting to set `g_gpu_client`. Now that we have a default, the error becomes dead code. Less code paths = fewer bugs.

**Note**: If true NOT_INITIALIZED semantics are needed (e.g., to test that path), tests can still set `g_gpu_client = nullptr` AND verify the shim returns success via fallback (not NOT_INITIALIZED). To test the "no fallback" case (which is now impossible), a build flag could be added later — out of scope for this change.

## Alternatives Considered

### Alt 1: Static init helper in `cuda_stub.cpp` (v1's approach)

```cpp
// v1's approach (REJECTED)
namespace {
struct GpuClientDefaultInit {
  GpuClientDefaultInit() {
    if (async_task::gpu::g_gpu_client == nullptr) {
      async_task::gpu::g_gpu_client = &async_task::gpu::g_cuda_stub;
    }
  }
};
const GpuClientDefaultInit g_gpu_client_default_init;
}
```

**Rejected because**:
1. `CudaStub g_cuda_stub;` doesn't exist; would need to add it (cross-scope addition to test-fixture)
2. **Static init order fiasco**: even within TU, `g_cuda_stub` and `g_gpu_client_default_init` need careful declaration order
3. **init_gpu_client() short-circuit**: `if (g_gpu_client) return 0;` would skip `GpuDriverClient` creation in CLI, breaking `cli_main.cpp` (CLI would silently use CudaStub mock instead of `/dev/gpgpu0`)
4. **Cross-build impact**: `cuda_stub.cpp` is linked into both `libcuda_shim` (umd-evolution build) and test binaries → static init runs everywhere, no way to opt out

### Alt 2: Meyer's singleton directly in `gpu_driver_client.cpp`

```cpp
// gpu_driver_client.cpp
IGpuDriver* g_gpu_client = nullptr;

IGpuDriver* get_default_gpu_client() {
  static CudaStub default_stub;
  return &default_stub;
}
```

**Rejected because**: requires changes to `init_gpu_client()` semantics (currently checks `g_gpu_client`, would need additional logic). The accessor helper approach is more localized.

### Alt 3: Mutate `g_gpu_client` in shim on first call

```cpp
// shim call:
if (!g_gpu_client) { g_gpu_client = new CudaStub(); }  // one-time init
```

**Rejected because**: requires synchronization for thread-safety in first-call init, and same short-circuit problem for `init_gpu_client()`.

## Risks / Trade-offs

| # | Risk | Mitigation |
|---|------|------------|
| 1 | Meyers singleton state leakage: `static CudaStub` accumulates state across TEST_CASEs (fence ids, mock state maps) | New fallback tests use `>= 1` assertions rather than `== 1`. Existing tests use `g_mock` (TU-local) so unaffected. |
| 2 | 3 shim files have local `get_driver_or_log()` helpers. Drift risk if one is updated but not others. | Extract shared `cuda_driver_accessor.hpp` header. CI verifies all 3 shim files include this header. |
| 3 | v1 `g-gpu-client-default-stub-init` artifacts remain in `openspec/changes/`. Future contributors may confuse with v2. | Add explicit supersession note in v2 `.openspec.yaml`. v1 directory renamed to `_superseded-g-gpu-client-default-stub-init`? (decision deferred to ship phase) |
| 4 | Test-fixture scope change to UMD-shim files: 3 umd-evolution scope files modified. Cross-scope concerns per H-5 3-scope rules. | Modifications are additive (new accessor, remove dead-code null guard). No new APIs. Existing SCOPE: UMD-EVOLUTION metadata preserved. |
| 5 | Build mode dependency: `cuda_stub.hpp` is test-fixture scope, included in umd-evolution scope shim. Cross-scope include (existing pattern from Phase 4 bridge). | Pattern already established in `cu_init.cpp:14`. No new cross-scope violations. |

## Verification

```bash
# Default build (umd-evolution, includes libcuda_shim)
cd /workspace/project/UsrLinuxEmu/external/TaskRunner
cmake -B build
cmake --build build -j4
ctest --test-dir build  # expect 318+ existing tests + 2-3 new fallback tests = 320-321 PASS

# test-fixture opt-out (verifies fallback also works without libcuda_shim)
TASKRUNNER_BUILD_MODE=test-fixture cmake -B build_tf
cmake --build build_tf -j4
ctest --test-dir build_tf  # expect 318 tests pass (shim not built)

# docs-audit.sh
./tools/docs-audit.sh  # exit 0

# CLI smoke (verify init_gpu_client() still creates GpuDriverClient, not CudaStub)
./build/taskrunner --help  # expect no SIGSEGV, normal CLI behavior
```

## Open Questions

- **Q**: Should v1 change `g-gpu-client-default-stub-init` directory be renamed to `_superseded-...` to prevent accidental ship?
  **A**: Deferred to ship phase. v2 change `supersedes:` field in `.openspec.yaml` is the primary mechanism. Directory rename is cosmetic.