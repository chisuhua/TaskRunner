# Capability Spec: umd-shim-coverage-hardening

> **Capability**: `umd-shim-coverage-hardening`
> **Change**: `openspec/changes/2026-07-02-umd-shim-coverage-hardening/`
> **Spec**: `external/TaskRunner/docs/superpowers/specs/2026-07-02-umd-shim-coverage-hardening.md`
> **Status**: PROPOSED (2026-07-02)

---

## Purpose

Promote backend-independent cu\* API stubs in `libcuda_taskrunner.so` to real implementations, expand E2E test coverage from 37 to ≥50 cases, resolve 4 H-3 follow-up documentation fixes, and prepare Phase 3 skeleton — all without breaking the 76 existing test cases or the 79-symbol ABI.

---

## Requirements

### Requirement: SHIM-COVERAGE-STUB-IMPL

The system SHALL promote 10 backend-independent cu\* API stubs from `CUDA_ERROR_NOT_IMPLEMENTED` return to real implementations.

#### Scenario: cuDeviceComputeCapability returns valid compute capability

- **WHEN** application calls `cuDeviceComputeCapability(&major, &minor, dev=0)`
- **THEN** system SHALL return `CUDA_SUCCESS` with `major ≥ 2` and `minor ≥ 0`
- **AND** the function SHALL be marked `REAL_IMPL in cu_query.cpp` in `cu_stub_table.inc`

#### Scenario: cuDeviceGetAttribute returns MAX_BLOCK_DIM_X

- **WHEN** application calls `cuDeviceGetAttribute(&value, CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_X, dev=0)`
- **THEN** system SHALL return `CUDA_SUCCESS` with `value ≥ 1024`

#### Scenario: cuCtxGetCacheConfig returns default

- **WHEN** application calls `cuCtxGetCacheConfig(&config)` after `cuCtxCreate`
- **THEN** system SHALL return `CUDA_SUCCESS` with `config = CU_FUNC_CACHE_PREFER_NONE`

#### Scenario: cuCtxSetCacheConfig persists

- **WHEN** application calls `cuCtxSetCacheConfig(CU_FUNC_CACHE_PREFER_L1)` then `cuCtxGetCacheConfig(&config)`
- **THEN** system SHALL return `CUDA_SUCCESS` with `config = CU_FUNC_CACHE_PREFER_L1`

#### Scenario: cuGetErrorName maps SUCCESS to string

- **WHEN** application calls `cuGetErrorName(CUDA_SUCCESS, &name)`
- **THEN** system SHALL return `CUDA_SUCCESS` with `name = "cudaSuccess"`

#### Scenario: cuGetErrorName maps OUT_OF_MEMORY to string

- **WHEN** application calls `cuGetErrorName(CUDA_ERROR_OUT_OF_MEMORY, &name)`
- **THEN** system SHALL return `CUDA_SUCCESS` with `name = "cudaErrorOutOfMemory"`

#### Scenario: cuGetErrorName rejects unknown error

- **WHEN** application calls `cuGetErrorName((CUresult)99999, &name)`
- **THEN** system SHALL return `CUDA_ERROR_INVALID_VALUE`

#### Scenario: cuMemGetInfo returns total and free

- **WHEN** application calls `cuMemGetInfo(&free, &total)` after init
- **THEN** system SHALL return `CUDA_SUCCESS` with `total ≥ free`
- **AND** `total` SHALL match `TASKRUNNER_GPU_MEM_SIZE` env var (default 8 GB)

#### Scenario: cuMemGetInfo reflects allocation

- **WHEN** application calls `cuMemAlloc(&ptr, 4096)` then `cuMemGetInfo(&free2, &total)`
- **THEN** system SHALL return `CUDA_SUCCESS` with `free2 ≤ free - 4096` (aligned to 4 KB page)

#### Scenario: cuFuncGetAttribute returns MAX_THREADS_PER_BLOCK

- **WHEN** application calls `cuFuncGetAttribute(&value, CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK, func)`
- **THEN** system SHALL return `CUDA_SUCCESS` with `value = 1024`

#### Scenario: cuPointerGetAttribute returns context

- **WHEN** application calls `cuPointerGetAttribute(&ctx, CU_POINTER_ATTRIBUTE_CONTEXT, allocated_ptr)`
- **THEN** system SHALL return `CUDA_SUCCESS` with `ctx != NULL`

---

### Requirement: SHIM-COVERAGE-TEST-EXPANSION

The system SHALL expand `tests/umd/test_cuda_shim.cpp` from 37 to ≥50 E2E test cases.

#### Scenario: threading test for cuCtx isolation

- **WHEN** 2 threads each call `cuCtxCreate` then `cuCtxDestroy`
- **THEN** no cross-thread context contamination SHALL occur
- **AND** both threads SHALL complete without deadlock

#### Scenario: threading test for cuMemAlloc safety

- **WHEN** 4 threads concurrently call `cuMemAlloc(&ptr, 4096)`
- **THEN** all 4 allocations SHALL succeed with distinct pointers

#### Scenario: error path test for cuMemAlloc NULL

- **WHEN** application calls `cuMemAlloc(NULL, 4096)`
- **THEN** system SHALL return `CUDA_ERROR_INVALID_VALUE`

#### Scenario: error path test for cuMemFree invalid

- **WHEN** application calls `cuMemFree(0xDEADBEEF)` (unallocated ptr)
- **THEN** system SHALL return a non-success error code

#### Scenario: error path test for cuModuleLoad missing file

- **WHEN** application calls `cuModuleLoad(&mod, "/nonexistent.cubin")`
- **THEN** system SHALL return `CUDA_ERROR_FILE_NOT_FOUND` or equivalent

---

### Requirement: H3-FOLLOWUP-FIXES

The system SHALL resolve 4 H-3 follow-up fixes per `UsrLinuxEmu/docs/07-integration/h3-activation-followup.md`.

#### Scenario: F1 README.md consistent ACTIVE status

- **WHEN** reviewer reads `openspec/changes/h3-phase2-management/README.md`
- **THEN** no occurrences of "DRAFT", "plans/2026-06-19-h3", "待建", or "激活流程" SHALL exist outside line 1 "✅ ACTIVE" header

#### Scenario: F2 tasks.md test count updated

- **WHEN** reviewer reads `openspec/changes/h3-phase2-management/tasks.md`
- **THEN** no occurrences of "10 tests" or "10/10" SHALL exist (replaced with 12)

#### Scenario: F3 design.md/spec.md log descriptions aligned

- **WHEN** reviewer diffs log-related sections in design.md vs spec.md
- **THEN** no semantic conflict SHALL exist (spec.md is authoritative)

#### Scenario: F4 design.md:277 date prefix present

- **WHEN** reviewer reads line 277 of `openspec/changes/h3-phase2-management/design.md`
- **THEN** the line SHALL contain `2026-06-22` date prefix per N4 fix style

---

### Requirement: PHASE3-SKELETON-DOC

The system SHALL create Phase 3 skeleton plan document for future kickoff acceleration.

#### Scenario: skeleton document exists

- **WHEN** reviewer runs `ls docs/superpowers/plans/2026-07-02-umd-phase3-skeleton.md`
- **THEN** the file SHALL exist
- **AND** it SHALL contain Phase 3 priority matrix copied from `phase-3-deferred.md`
- **AND** it SHALL contain 4 Trigger Conditions (UsrLinuxEmu Stage 1.4, external demand, CI gap, time pressure)
- **AND** it SHALL be marked DRAFT (no implementation)

#### Scenario: roadmap README references skeleton

- **WHEN** reviewer reads `docs/umd-evolution/roadmap/README.md`
- **THEN** a `phase-3-skeleton` entry SHALL appear in the Current Snapshot table

---

### Requirement: DOCS-AUDIT-COVERAGE-TRACKING

The system SHALL add 2 new checks to `tools/docs-audit.sh` for ongoing stub reduction tracking.

#### Scenario: critical API count check passes

- **WHEN** `tools/docs-audit.sh` runs against `build/libcuda_taskrunner.so` with ≥41 critical APIs exported
- **THEN** the new "Phase 1.6 critical API count" check SHALL pass

#### Scenario: backend-independent stub count check passes

- **WHEN** `cu_stub_table.inc` contains ≤28 `// STUB` markers
- **THEN** the new "Phase 1.6 backend-independent stub count" check SHALL pass

---

### Requirement: NON-REGRESSION

The system SHALL NOT regress existing functionality.

#### Scenario: existing 76 tests still pass

- **WHEN** `for t in test_cuda_scheduler test_gpu_architecture test_gpu_phase2 test_cuda_runtime_api test_cuda_shim; do ./build/$t; done` runs
- **THEN** all 76 existing test cases SHALL pass (37 shim tests + 39 others)

#### Scenario: 79 cu\* symbols preserved

- **WHEN** `nm -D --defined-only build/libcuda_taskrunner.so | grep -c " cu[A-Z]"` runs
- **THEN** output SHALL be exactly 79 (no ABI break)

#### Scenario: docs-audit total check count increased

- **WHEN** `tools/docs-audit.sh` runs
- **THEN** total check count SHALL be ≥58 (was 54)

---

## Cross-References

- Spec: [`docs/superpowers/specs/2026-07-02-umd-shim-coverage-hardening.md`](../../../../docs/superpowers/specs/2026-07-02-umd-shim-coverage-hardening.md)
- Plan: [`docs/superpowers/plans/2026-07-02-umd-shim-coverage-hardening.md`](../../../../docs/superpowers/plans/2026-07-02-umd-shim-coverage-hardening.md)
- Proposal: [`../proposal.md`](../proposal.md)
- Design: [`../design.md`](../design.md)
- Tasks: [`../tasks.md`](../tasks.md)
- Prerequisite: [`../../archive/2026-07-02-taskrunner-umd-backend-enable/`](../../archive/2026-07-02-taskrunner-umd-backend-enable/)
- Phase 2: [`../../../../docs/umd-evolution/roadmap/phase-2-complete.md`](../../../../docs/umd-evolution/roadmap/phase-2-complete.md)
- Phase 3: [`../../../../docs/umd-evolution/roadmap/phase-3-deferred.md`](../../../../docs/umd-evolution/roadmap/phase-3-deferred.md)
- H-3 follow-up: [`../../../../../UsrLinuxEmu/docs/07-integration/h3-activation-followup.md`](../../../../../UsrLinuxEmu/docs/07-integration/h3-activation-followup.md)
- Stage 1 plan: [`../../../../../UsrLinuxEmu/docs/roadmap/stage-1-kernel-emu.md`](../../../../../UsrLinuxEmu/docs/roadmap/stage-1-kernel-emu.md)

---

**Capability Status**: PROPOSED
**Next State**: ACCEPTED (after all Sub-plans A-E complete + tests pass + cross-repo sync)
**Capability Owner**: TaskRunner owner (Sisyphus)