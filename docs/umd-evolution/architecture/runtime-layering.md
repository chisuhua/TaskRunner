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

## Phase 2 Extension (Status: ✅ Implemented, 2026-07-01)

Phase 2 was implemented via 7 commits (C.1-C.7):
- C.1: stub generator + 143 cu* declarations
- C.2: cu_init + cu_module (Oracle cleanup fix)
- C.3: cuMem APIs
- C.4: cuLaunchKernel with handle→name resolution
- C.5: cuCtx + cuDevice (Oracle stack-tracked context)
- C.5b: cuQuery + cuStream + cuEvent
- C.6: libcuda_taskrunner.so built (79 cu* symbols exported)
- C.7: E2E tests (37/37 passing)

See `docs/superpowers/plans/2026-07-01-umd-phase2-ld-preload.md` for full details.

### Phase 2 Verification Results

- **Total cu* symbols exported**: 79
- **Critical API coverage**: 41/41 (100%)
- **E2E tests**: 37/37 pass (test_cuda_shim)
- **Phase 1 regression**: 39/39 pass (no impact)
- **Total runtime**: ~95ms for full shim test suite

### Phase 2 Known Limitations

1. **No real kernel execution**: cuLaunchKernel routes through CudaStub which is a fake. Real vectorAdd E2E requires D-3 (ELF parsing), explicitly deferred per gap-analysis.md.
2. **Stubbed APIs return CUDA_ERROR_NOT_IMPLEMENTED**: 79 exported, but only ~40 have real implementations. The remaining ~40 are functional placeholders.
3. **D2D memcpy unsupported**: cuMemcpyDtoD returns CUDA_ERROR_NOT_SUPPORTED (Phase 1 limitation).
4. **Async stream ops incomplete**: cuStreamBeginCapture/EndCapture return NOT_IMPLEMENTED (CUDA Graphs not supported).
5. **Single-device only**: cuDeviceGetCount returns 1 regardless of host configuration.

## Handle Lifecycle (Phase 2 Shim)

The shim implements handle tables for CUfunction, CUmodule, CUcontext, CUstream,
and CUevent. This section documents the lifecycle assumptions and known limitations.

### Handle ID Generation

- **Format**: 64-bit integer values, reinterpreted to opaque handle pointers
- **Counter**: Per-table atomic counter starting at 1 (or 2 for `CUcontext`)
- **Uniqueness**: Globally unique within the process until process exit
- **Reserved values**:
  - `nullptr` (0): Invalid handle (default for many pointer returns)
  - `0x1`: Primary context sentinel (reserved for `cuDevicePrimaryCtxRetain`)
  - `0x100`: Primary context actual value (avoids collision with 0x1 reserved)

### Cleanup Semantics

#### cuModuleUnload (Oracle Critical #1 fix applied)

When `cuModuleUnload(hmod)` is called, the shim:
1. Removes `hmod` from `mod_to_name` map
2. Iterates `mod_to_func[hmod]` and removes each `CUfunction` from `func_to_name`
3. Removes `mod_to_func[hmod]` entry

**Limitation**: CUfunction handles registered via `cuModuleGetFunction` for a module NOT loaded via `cuModuleLoad` (e.g., implicit primary context modules) will leak. In Phase 2, these are not encountered since `cuDevicePrimaryCtxRetain` returns 0x100 (a different sentinel).

#### cuCtxDestroy

Removes `ctx` from thread-local stack and marks it as destroyed in global map.
**Limitation**: Other thread stacks are NOT cleaned up. A multi-threaded application
may have orphan `CUcontext` references. Documented as known limitation.

#### cuStreamDestroy / cuEventDestroy

Removes handle from active set; std::unordered_map.erase is used.
**Limitation**: No deferred reclamation; underlying resources are not actually
released by the CudaStub backend (Phase 2 stub behavior).

### Thread Safety

- All handle table mutations are protected by `std::mutex`
- `cuCtxSetCurrent/GetCurrent/Push/Pop` use **thread_local** context stack
- `cuEventElapsedTime` reads from a global timestamp map (locked)
- `cuStream*` and `cuEvent*` operations are serialized via per-table mutex

**Limitation**: Fine-grained locking (per-handle) is not implemented. Contended
workloads may experience serialization. Acceptable for Phase 2 PoC.

### Multi-Context Support (Oracle Critical #4 fix applied)

**Before**: All contexts returned hardcoded `0x1` (breaks multi-context apps).

**After**: `cuCtxCreate` allocates new IDs from atomic counter. `cuCtxSetCurrent`/
`cuCtxPushCurrent` push to thread-local stack. `cuCtxGetCurrent` returns top-of-stack.

**Tested**: cuCtxCreate → SetCurrent → GetCurrent → Destroy cycle works correctly
across multiple creates (verified in `test_cuda_shim.cpp`).

### Known Lifecycle Limitations

1. **Memory grows unboundedly**: No LRU eviction. Phase 2 expects apps to call
   `cuModuleUnload`/`cuCtxDestroy` explicitly.
2. **Thread-local state is not shared**: Each thread has independent context
   stack. Cross-thread context propagation requires explicit `cuCtxSetCurrent`.
3. **Handle-32 stored in 64-bit pointer**: The 64-bit slot wastes 4 bytes/handle
   but avoids ABI compatibility issues with 32-bit-only clients.
4. **No reference counting on handle use**: Stale handles from unloaded modules
   return `INVALID_HANDLE` (CUDA_ERROR_INVALID_HANDLE) but the implementation
   is best-effort (lookup against `mod_to_func` list).

### Cleanup Guarantees on Process Exit

No atexit handler is registered. Process exit simply abandons all in-memory
state. This is the standard behavior for stub libraries and is acceptable.

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
