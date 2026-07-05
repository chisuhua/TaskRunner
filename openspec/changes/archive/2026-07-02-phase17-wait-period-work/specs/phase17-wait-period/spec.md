# Capability Spec: phase17-wait-period-work

> **Capability**: `phase17-wait-period-work`
> **Change**: `openspec/changes/2026-07-02-phase17-wait-period-work/`
> **Status**: PROPOSED (2026-07-02)

---

## Purpose

Complete Phase 1.7 wait-period work (independent of UsrLinuxEmu Stage 1.0-1.3): implement 14 remaining backend-independent cu\* STUB APIs, establish CI/build infrastructure, expand test coverage depth from 49 to 70+ cases, and prepare Phase 3 design sketches.

---

## Requirements

### Requirement: BACKEND-INDEPENDENT-STUB-IMPL

The system SHALL implement 14 backend-independent cu\* STUB APIs as REAL_IMPL.

#### Scenario: cuFuncGetAttribute returns MAX_THREADS_PER_BLOCK

- **WHEN** `cuFuncGetAttribute(&val, CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK, f)` called
- **THEN** return `CUDA_SUCCESS` with `val = 1024`

#### Scenario: cuFuncGetModule returns module for function

- **WHEN** `cuFuncGetModule(&mod, f)` called after `cuModuleGetFunction`
- **THEN** return `CUDA_SUCCESS` with `mod == module_handle`

#### Scenario: cuOccupancyMaxActiveBlocks returns heuristic count

- **WHEN** `cuOccupancyMaxActiveBlocksPerMultiprocessor(&blocks, f, 256, 0)` called
- **THEN** return `CUDA_SUCCESS` with `blocks >= 1`

#### Scenario: cuPointerGetAttribute returns context

- **WHEN** `cuPointerGetAttribute(&ctx, CU_POINTER_ATTRIBUTE_CONTEXT, ptr)` called after cuMemAlloc
- **THEN** return `CUDA_SUCCESS` with valid context

---

### Requirement: CI-INFRASTRUCTURE

The system SHALL provide CI/build infrastructure for automated testing and quality assurance.

#### Scenario: ASan build passes

- **WHEN** `cmake .. -DSANITIZER_ADDRESS=ON -DSANITIZER_UNDEFINED=ON && make -j4 && ./build/test_cuda_shim` runs
- **THEN** all tests pass with 0 sanitizer errors

#### Scenario: GitHub Actions workflow exists

- **WHEN** `.github/workflows/shim.yml` exists
- **THEN** it SHALL define at minimum: build+test job, docs-audit job

#### Scenario: pre-commit hook runs docs-audit

- **WHEN** `git commit` triggers `.githooks/pre-commit`
- **THEN** `tools/docs-audit.sh` SHALL run and fail if checks don't pass

---

### Requirement: TEST-DEPTH-EXPANSION

The system SHALL expand test coverage from 49 to ≥70 E2E cases.

#### Scenario: threading test for cuCtxCreate concurrency

- **WHEN** 4 threads concurrently call `cuCtxCreate`
- **THEN** all threads SHALL complete without deadlock or race

#### Scenario: cuDeviceGetAttribute returns WARP_SIZE

- **WHEN** `cuDeviceGetAttribute(&v, CU_DEVICE_ATTRIBUTE_WARP_SIZE, 0)` called
- **THEN** return `CUDA_SUCCESS` with `v == 32`

#### Scenario: cuMemAlloc with zero size returns error

- **WHEN** `cuMemAlloc(&ptr, 0)` called
- **THEN** return `CUDA_ERROR_INVALID_VALUE`

---

### Requirement: PHASE3-DESIGN-SKETCHES

The system SHALL create Phase 3 design sketch documents.

#### Scenario: cuMemPool design exists

- **WHEN** `docs/superpowers/specs/2026-07-02-phase3-mempool-design.md` exists
- **THEN** it SHALL cover: cuMemPoolCreate/Destroy, cuMemAllocFromPoolAsync, VA space integration

#### Scenario: cuStream capture design exists

- **WHEN** `docs/superpowers/specs/2026-07-02-phase3-stream-capture-design.md` exists
- **THEN** it SHALL cover: cuStreamBeginCapture/EndCapture, capture tree semantics

---

### Requirement: NON-REGRESSION

The system SHALL NOT regress existing functionality.

#### Scenario: 81+ existing tests pass

- **WHEN** all test binaries run
- **THEN** ≥81 cases SHALL pass (49 + 32)

#### Scenario: 79 cu\* symbols preserved

- **WHEN** `nm -D --defined-only build/libcuda_taskrunner.so | grep -c " cu[A-Z]"` runs
- **THEN** output SHALL be exactly 79

#### Scenario: docs-audit still passes

- **WHEN** `tools/docs-audit.sh` runs
- **THEN** SHALL report 53 PASS / 1 FAIL（实际基线）

---

## Cross-References

- Proposal: [`../proposal.md`](../proposal.md)
- Design: [`../design.md`](../design.md)
- Tasks: [`../tasks.md`](../tasks.md)
- Prerequisite: [`../../2026-07-02-phase16-shim-extension/`](../../2026-07-02-phase16-shim-extension/)
- Phase 2: [`../../../../docs/umd-evolution/roadmap/phase-2-complete.md`](../../../../docs/umd-evolution/roadmap/phase-2-complete.md)
- Phase 3 prep: [`../../../../docs/superpowers/plans/2026-07-02-phase3-prep-design-notes.md`](../../../../docs/superpowers/plans/2026-07-02-phase3-prep-design-notes.md)

---

**Capability Status**: PROPOSED
**Next State**: ACCEPTED (after all 4 workstreams complete + validation)