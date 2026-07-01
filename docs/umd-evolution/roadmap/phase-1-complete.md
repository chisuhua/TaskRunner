---
SCOPE: UMD-EVOLUTION
STATUS: COMPLETE
PHASE: 1
COMPLETION_DATE: 2026-07-01
TESTS: 8/8 CudaRuntimeApi + 39/39 regression
---

# Phase 1 COMPLETE — Runtime PoC (CudaRuntimeApi)

7 commits implementing `CudaRuntimeApi` class:

- 3 CUDA Runtime API methods: `cudaMalloc`, `memcpy`, `launch_kernel`
- Built on existing CudaScheduler (H-5) with DI
- 8 doctest cases + 4 CLI commands
- Routes through proven IGpuDriver abstraction

## Outcome

`CudaRuntimeApi` class ready to be wrapped by LD_PRELOAD shim (Phase 2).

All Phase 1 deliverables verified:

- 8/8 doctest cases pass
- 39/39 existing tests unaffected
- `TASKRUNNER_BUILD_MODE=umd-evolution` builds clean
- CLI commands `cuda_runtime_alloc/memcpy/launch/register` functional

## Key Files

- `include/umd/cuda_runtime_api.hpp` — class declaration
- `src/umd/cuda_runtime_api.cpp` — implementation
- `tests/umd/test_cuda_runtime_api.cpp` — 8 doctest cases
- `src/test_fixture/cmd_cuda.cpp` — 4 CLI commands
- `include/test_fixture/TaskRunner.h` — `getScheduler()` accessor (B.1)

## Key Commits

| Commit | Description |
|--------|-------------|
| 6f7818d | getScheduler() accessor |
| 020814c | CudaRuntimeApi header |
| cb07353 | CudaRuntimeApi implementation |
| 8bc847a | 8 real test cases + skeleton removal |
| 4314dae | cuda_runtime_\* CLI commands |
| 9a50bd5 | Phase 1 marked implemented |
| e4f26f4 | Phase 1 final verification |

## Oracle Review Corrections

Oracle Phase 1 review identified:

1. **IGpuDriver method signatures don't exist as documented** (`allocateMemory`,
   `submitMemcpy(q, kind)` etc. don't exist; actual signatures use `alloc_bo`,
   `submit_memcpy(stream_id, src, dst, size, is_h2d)`)
2. **Implementer correctly adapted**: called CudaScheduler methods
   (`submit_mem_alloc`, `submit_memcpy_h2d/d2h`, `submit_launch`) instead of
   going directly to IGpuDriver

Detailed in plan v2 §Phase 1 Implementation.

## Trigger for Phase 2

Phase 2 was implemented next (LD_PRELOAD shim — see `phase-2-complete.md`).
