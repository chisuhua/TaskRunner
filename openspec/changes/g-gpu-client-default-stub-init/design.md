---
SCOPE: test-fixture
STATUS: PROPOSED
---

## Context

`include/test_fixture/gpu_driver_client.h` declares:
```cpp
extern IGpuDriver* g_gpu_client;  // Phase 4 (M1)
```

`src/test_fixture/cuda_stub.cpp` defines a static `CudaStub g_cuda_stub;` instance.

The current default value of `g_gpu_client` is `nullptr` (zero-initialized BSS). Every test that wants shim functions to work must explicitly assign `g_gpu_client = &g_mock;` or `g_gpu_client = &g_cuda_stub;`.

This change makes `g_gpu_client` default to `&g_cuda_stub` in test builds.

## Goals / Non-Goals

**Goals:**
- `g_gpu_client` defaults to `&g_cuda_stub` in test builds
- Shim functions work out of the box (no explicit setup required for new shim consumer tests)
- Existing tests that explicitly override `g_gpu_client` are unaffected
- All 318 existing tests still pass

**Non-Goals:**
- Removing the null check in shim functions (kept as belt-and-suspenders for edge cases like process startup before init)
- Defaulting to MockGpuDriver (that would defeat the purpose of integration tests)
- Changing the production-build behavior (this is test-fixture scope only)

## Decisions

### Decision 1: Function-local static initialization (Meyers singleton)

Use a function that returns the `g_gpu_client` pointer, with the value initialized in a function-local static:

```cpp
// In src/test_fixture/gpu_driver_client_init.cpp (new)
#include "test_fixture/cuda_stub.h"
#include "test_fixture/gpu_driver_client.h"

namespace async_task::gpu {
IGpuDriver* get_default_gpu_client() {
  static CudaStub default_stub;  // Meyers singleton
  static IGpuDriver* default_ptr = &default_stub;
  return default_ptr;
}
}  // namespace async_task::gpu
```

Then in `gpu_driver_client.h`:
```cpp
extern IGpuDriver* g_gpu_client;
IGpuDriver* get_default_gpu_client();  // declared

// At the bottom of the header (file-static initializer trick):
// No, this is hard. Better: initialize g_gpu_client in cuda_stub.cpp directly.
```

**Rationale**: Meyers singleton guarantees construction happens at first call, not at static init time. This sidesteps the static init order fiasco.

### Decision 2: Initialize in `cuda_stub.cpp` directly (chosen)

Simpler approach: add to the same TU as `CudaStub g_cuda_stub;`:

```cpp
// In src/test_fixture/cuda_stub.cpp
#include "test_fixture/gpu_driver_client.h"

namespace async_task::gpu {
// existing
CudaStub g_cuda_stub;
}  // namespace async_task::gpu

// NEW: default g_gpu_client (only if user hasn't set it)
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

**Rationale**: 
- Same TU as `g_cuda_stub` → static init order is well-defined within TU
- The "init only if null" pattern allows tests to override before our init runs (e.g., via a static `g_gpu_client = &g_mock;` in their TU)
- Single source of truth: all test-fixture init lives in `cuda_stub.cpp`

### Decision 3: Keep the null check in shim functions

The shim functions (e.g., `cuStreamSynchronize` in `cu_stream.cpp`) still check `if (g_gpu_client == nullptr) return NOT_INITIALIZED`. This is a safety net for:
- Process startup before static init completes (theoretical, but C++ guarantees static init before main)
- Edge cases where the linker drops the test-fixture library (impossible in test builds)

**Rationale**: Defense in depth. Cost is negligible (one branch).

## Risks / Trade-offs

| # | Risk | Mitigation |
|---|------|------------|
| 1 | Static init order: g_cuda_stub not yet constructed when default-init runs | Both live in the same TU (cuda_stub.cpp); within-TU order is top-to-bottom in declaration order. Place CudaStub g_cuda_stub; before the default-init helper. |
| 2 | Test wants to start with null g_gpu_client (to test NOT_INITIALIZED path) | Existing tests already set g_gpu_client explicitly. The new code initializes only if null. So tests that set BEFORE the init helper runs win; tests that want null must set it in their fixture (or in TEST_CASE setup). |
| 3 | Production build pulls in this default-init | Production build doesn't link test_fixture; default-init lives in test-fixture TU. No impact. |

## Verification

```bash
# Default build (test-fixture, no env var)
cmake -B build_default
cmake --build build_default -j4
ctest --test-dir build_default  # expect 318/318 PASS

# New test (no explicit g_gpu_client set) compiles and runs:
# Add a new TEST_CASE in tests/umd/test_cu_graph.cpp that omits g_gpu_client set
# Expected: cuStreamSynchronize works (returns CUDA_SUCCESS via CudaStub)
```

## Open Questions

- **Q**: Should the default be `&g_cuda_stub` or a fresh `CudaStub` instance per test?  
  **A**: `&g_cuda_stub` (the existing static). Per-test fresh instance complicates state cleanup and isn't needed for shim tests.
