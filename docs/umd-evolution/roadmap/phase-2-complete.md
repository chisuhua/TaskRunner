---
SCOPE: UMD-EVOLUTION
STATUS: COMPLETE
PHASE: 2
COMPLETION_DATE: 2026-07-01
HEAD_COMMIT_AT_COMPLETION: 83ef131
TESTS: 37/37 (Phase 2) + 39/39 (Phase 1)
---

# Phase 2 COMPLETE — LD_PRELOAD Driver API shim

**Goal**: Build `libcuda_taskrunner.so` that exports ~200 cu\* symbols so
unmodified CUDA programs run on TaskRunner + UsrLinuxEmu via LD_PRELOAD.

**Status**: ✅ ALL 12 SUB-TASKS (C.1-C.9 plus C.5b, C.8b) COMPLETE.

## Deliverables

| Task | Description | Commit |
|------|-------------|--------|
| C.1 | `tools/generate_cu_stubs.py` + 143 cu\* declarations + 41 critical API list | a2cfe36 |
| C.2 | `cu_init.cpp` + `cu_module.cpp` (Oracle cleanup fix applied) | e7741ec |
| C.3 | `cu_mem.cpp` — cuMemAlloc/Free/cuMemcpyHtoD/DtoH; cuMemcpyDtoD NOT_SUPPORTED | a12e9e9 |
| C.4 | `cu_launch.cpp` — cuLaunchKernel with handle→name resolution | a7beaac |
| C.5 | `cu_ctx.cpp` + `cu_device.cpp` — stack-tracked context (Oracle Critical #4 fix) | e2fdf51 |
| C.5b | `cu_query.cpp` + `cu_stream.cpp` + `cu_event.cpp` | 12e69a4 |
| C.6 | `libcuda_taskrunner.so` linked (79 cu\* symbols exported) | 07cbb50 |
| C.7 | `test_cuda_shim.cpp` — 37 E2E test cases (all pass) | afb00f1 |
| C.8 | stub completeness check (added to `tools/docs-audit.sh`) + status docs | 6cbcd56 |
| C.8b | Handle Lifecycle documentation (architecture/runtime-layering.md) | 17de516 |
| C.9 | Final verification + design spec marked ACCEPTED | 83ef131 |

## Test Results

- **Phase 1 tests**: 39/39 pass (no regression)
- **Phase 2 shim tests**: 37/37 pass
- **Total**: 76/76
- **docs-audit.sh**: 54/54 checks
- **cu\* symbols exported by `libcuda_taskrunner.so`**: 79

## Oracle Review Resolutions

The Phase 2 plan v1 was reviewed by Oracle (architectural review) which identified
5 critical findings. All applied in plan v2 (`88603ed`):

| Critical | Resolution | Implemented in |
|----------|------------|----------------|
| #1 cuModuleUnload leaks function handles | Track module→function map, clean up on unload | C.2 (e7741ec) |
| #2 ~200 stubs insufficient for libcudart.so | 41 critical APIs whitelisted; 79 total exported | C.5b (12e69a4) |
| #3 CUcontext hardcoded 0x1 breaks multi-context | Stack-tracked thread-local context | C.5 (e2fdf51) |
| #4 Validation gaps in tests | 37 E2E test cases added (context lifecycle, version, module unload) | C.7 (afb00f1) |
| #5 Handle lifecycle documentation | Full `Handle Lifecycle` section added | C.8b (17de516) |

## Phase 2 Architecture

```
┌────────────────────────────────────────────────────────────────┐
│ CUDA Application (uses cu* via libcudart.so)                    │
└──────────────────┬─────────────────────────────────────────────┘
                   │ LD_PRELOAD hook
┌──────────────────▼─────────────────────────────────────────────┐
│ libcuda_taskrunner.so (79 exported symbols)                   │
│                                                                │
│ cuInit                  cuDeviceGetCount        cuCtxCreate    │
│ cuDriverGetVersion      cuDeviceGet             cuCtxDestroy   │
│ cuModuleLoad            cuModuleGetFunction     cuCtxSync       │
│ cuMemAlloc              cuMemcpyHtoD/DtoH        cuMemFree       │
│ cuLaunchKernel          cuStreamCreate          cuEventCreate   │
│ cuGetErrorName          cuLaunchCooperativeKernel ... (79 total)│
└──────────────────┬─────────────────────────────────────────────┘
                   │ routes to
┌──────────────────▼─────────────────────────────────────────────┐
│ Phase 1: CudaRuntimeApi (8 tests)                              │
│   cudaMalloc → submit_mem_alloc → CudaStub (real impl)         │
│   cudaMemcpy → submit_memcpy_h2d/d2h → CudaStub                │
│   launchKernel → submit_launch → CudaStub (no real execution)  │
└──────────────────┬─────────────────────────────────────────────┘
                   │
┌──────────────────▼─────────────────────────────────────────────┐
│ CudaScheduler (H-5) → IGpuDriver (H-2.5) → CudaStub backend  │
│                                                                │
│ (For non-CudaStub backend, dynamic_cast issue blocks real      │
│  alloc/memcpy; tracked as Phase 1.5 stretch.)                  │
└────────────────────────────────────────────────────────────────┘
```

## Phase 2 Usage

```bash
cd build
cmake .. -DTASKRUNNER_BUILD_MODE=umd-evolution
make -j4

# Run a CUDA program with our shim
LD_PRELOAD=./libcuda_taskrunner.so ./test_cuda_shim

# Verify exported symbols
nm -D --defined-only libcuda_taskrunner.so | grep " cu[A-Z]" | wc -l
# Output: 79
```

## Known Limitations (from runtime-layering.md)

1. No real kernel execution (CudaStub is fake)
2. ~38 cu\* APIs are functional placeholders (return NOT_IMPLEMENTED)
3. cuMemcpyDtoD NOT_SUPPORTED (would need GPU P2P support)
4. CUDA Graphs NOT_IMPLEMENTED
5. Single-device only (cuDeviceGetCount=1)
6. cuLaunchCooperativeKernel delegates to cuLaunchKernel (no cooperative enforcement)
7. cuDeviceGetAttribute returns plausible-but-stub values
8. Thread-local context state (no cross-thread ctx propagation)

## Phase 2 NOT Done (Phase 1.5 Stretch / Phase 3 / D-3)

- **Phase 1.5**: 5 `dynamic_cast<CudaStub*>` sites in CudaScheduler prevent
  GpuDriverClient backend from running `cuMemAlloc` etc. Fix tracked separately.
- **Phase 3**: Stream async, Memory pool, Event timing precision
- **D-3**: ELF/CUBIN parsing (deferred per gap-analysis.md)

## Tests to Add (Phase 3 / later work)

When Phase 3 starts, additional test cases:

- cuStreamBeginCapture / cuStreamEndCapture (CUDA Graphs)
- cuMemHostAlloc / cuMemHostGetDevicePointer
- cuDeviceGetAttribute for all attributes
- Threading tests (multi-ctx across threads)

## Code Locations

- shim sources: `src/umd/libcuda_shim/*.cpp` (9 files)
- shim header: `include/cuda.h` (compat layer)
- tests: `tests/umd/test_cuda_shim.cpp`
- CMake: `cmake/UMDEvolution.cmake` (cuda_taskrunner shared library + test_cuda_shim)
- Plan v2: `docs/superpowers/plans/2026-07-01-umd-phase2-ld-preload.md`
