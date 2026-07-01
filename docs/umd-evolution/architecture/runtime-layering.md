---
SCOPE: UMD-EVOLUTION
STATUS: ACCEPTED
DATE: 2026-06-30
RELATED_DESIGN: ../../superpowers/specs/2026-06-30-umd-evolution-redesign.md
RELATED_ADR: ../adr/tadr-201, ../adr/tadr-202, ../adr/tadr-203
---

# Runtime Layering Design (Phase 1 + Phase 2)

## Rationale (per Oracle architectural review 2026-06-30)

The original tadr-201/202/203 proposed a `CudaRuntimeApi → IGpuDriver` direct path. **Oracle review identified that this duplicates logic already provided by CudaScheduler** (memory allocation, fence creation, kernel arg serialization via `LaunchParams`, etc.).

**Corrected architecture**: `CudaRuntimeApi → CudaScheduler → IGpuDriver → implementations`.

## Layer Responsibilities

| Layer | Responsibility | Status |
|-------|----------------|--------|
| **CudaRuntimeApi** | Provide 3 CUDA Runtime API surface (cudaMalloc/Memcpy/LaunchKernel), kernel name→index registry, RAII handle lifecycle, thread safety mutex | Phase 1 ✅ Implemented (commit 8bc847a, 2026-07-01) |
| **CudaScheduler** | Submit commands via existing DI infra (`submit_mem_alloc`, `submit_memcpy_h2d/d2h`, `submit_launch`), wait_fence synchronization | Existing (H-3/H-5) |
| **IGpuDriver** | 31 low-level methods (alloc_bo, submit_memcpy, submit_launch, create_va_space, create_queue, etc.) | Existing (H-2.5) |
| **GpuDriverClient** | Translate IGpuDriver calls to ioctl commands for UsrLinuxEmu | Existing (H-3) |
| **CudaStub** | In-memory fake returning placeholder values; **only fully-wired backend in Phase 1** | Existing (H-2.5) |
| **MockGpuDriver** | Test mock with strict call recording | Existing (test-fixture) |

## API-to-method mapping

| CUDA Runtime API | CudaScheduler call | Verified since |
|-----------------|--------------------|--------------------|
| cudaMalloc(size) | `scheduler_->submit_mem_alloc(size)` → `fence_id` | H-3 (Phase 1) |
| cudaMemcpy(H2D) | `scheduler_->submit_memcpy_h2d(src, dst, n)` | H-3 |
| cudaMemcpy(D2H) | `scheduler_->submit_memcpy_d2h(src, dst, n)` | H-3 |
| cudaLaunchKernel(name, g, b, args, shmem) | `scheduler_->submit_launch(LaunchParams)` | H-3 + H-3.5 |
| Synchronous wait (default) | `scheduler_->wait_fence(fence_id)` | H-3.5 fence_id |

## Known Limitations (Phase 1)

- **GpuDriverClient backend**: `dynamic_cast<CudaStub*>` at 5 sites in CudaScheduler means `submit_mem_alloc`-type operations return `-ENOSYS` via this backend. **Phase 1 only verifies via CudaStub mode.**
- **D2D/H2H memcpy**: Not supported (gpu_ioctl.h limitation). Returns `cudaErrorNotSupported`.
- **Single stream**: No `cuStreamCreate` in Phase 1.
- **Kernel names**: Manually registered; no ELF parsing.

## Phase 2 Extension (forward reference)

Phase 2 adds `libcuda_taskrunner.so` (LD_PRELOAD) wrapping `CudaRuntimeApi`. Adds CUfunction→name handle table in the shim layer; existing Phase 1 path is reused.

## Phase 1 Implementation Status (Updated 2026-07-01)

**Status: ✅ Phase 1 COMPLETE on `main` branch.**

### Deliverables

| # | Component | Commit | Notes |
|---|-----------|--------|-------|
| 1 | `TaskRunner::getScheduler()` accessor + initialize scheduler | `6f7818d` | B.1 + deviation fix |
| 2 | `CudaRuntimeApi` header | `020814c` | B.2 (replaces skeleton) |
| 3 | `CudaRuntimeApi` implementation | `cb07353` | B.3 (5 methods + RAII) |
| 4 | 8 real test cases | `8bc847a` | B.4 (replaces skeleton tests) |
| 5 | 4 CLI commands | `4314dae` | B.5 (cuda_runtime_register/alloc/memcpy/launch) |
| 6 | Phase 1 docs marked implemented | `9a50bd5` | B.6 (this status row) |
| 7 | **Phase 1 final verification** | B.7 (this commit) | ✅ |

### Test Results (39 cases, 0 failures)

- `test_cuda_scheduler`: 8/8 PASS
- `test_gpu_architecture`: 11/11 PASS
- `test_gpu_phase2`: 12/12 PASS
- `test_cuda_runtime_api`: 8/8 PASS

### Phase 2 Readiness

All Phase 2 prerequisites met:
- `CudaRuntimeApi` class ready for LD_PRELOAD shim wrapping
- `getScheduler()` (in test_fixture scope) OR standalone CudaRuntimeApi instance (CLI pattern)
- 12 cu* APIs identified for Phase 2
- 200 cu* stubs required for completeness
- CudaStub backend fully wired (other backends have known limitations)

### POA-1+POA-2 Achievement (Q4 Resolution)

- **POA-1 (UsrLinuxEmu Stage 1.4 KFD Consumer)**: `CudaRuntimeApi` provides the API consumer layer; Phase 2 LD_PRELOAD will let real CUDA programs use this layer.
- **POA-2 (CI Regression Test Baseline)**: 8 test cases + 4 CLI commands form an executable baseline. Adding new tests to `test_cuda_runtime_api.cpp` extends regression coverage.

### Known Limitations (Phase 1)

1. **GpuDriverClient backend `-ENOSYS`**: CudaScheduler has `dynamic_cast<CudaStub*>` at 5 sites. Phase 1 only verifies via CudaStub mode.
2. **D2D/H2H memcpy unsupported**: `gpu_ioctl.h` only defines H2D/D2H. Returns `cudaErrorNotSupported`.
3. **Single stream**: No `cuStreamCreate` in Phase 1.
4. **No ELF parsing**: Kernel names manually registered.
5. **CLI scheduler is standalone**: `cmd_cuda.cpp` creates its own CudaStub + CudaScheduler (not via TaskRunner::getScheduler()) to avoid doctest.h dependency.

These limitations are intentional per Phase 1 scope; documented in design spec §Known Limitations (Phase 1).
