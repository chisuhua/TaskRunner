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
| **CudaRuntimeApi** | Provide 3 CUDA Runtime API surface (cudaMalloc/Memcpy/LaunchKernel), kernel name→index registry, RAII handle lifecycle, thread safety mutex | Phase 1 [NEW] |
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
