# Capability Spec: phase16-shim-extension

> **Capability**: `phase16-shim-extension`
> **Change**: `openspec/changes/2026-07-02-phase16-shim-extension/`
> **Status**: PROPOSED (2026-07-02)

---

## Purpose

Resolve semantic-layer gaps revealed by hotfix d8ca3d3 (Phase 2 drift hotfix), expand E2E test coverage for newly-registered APIs, prepare H-3 cross-repo PR description for UsrLinuxEmu owner, and create Phase 3 prep design notes — all based on accurate post-hotfix baseline (STUB=65, REAL_IMPL=79).

---

## Requirements

### Requirement: SHIM-MEMINFO-REAL-DATA

The system SHALL provide accurate memory information via `cuMemGetInfo` rather than hardcoded fake values.

#### Scenario: cuMemGetInfo pulls from IGpuDriver when backend available

- **WHEN** application calls `cuMemGetInfo(&free, &total)` with GpuDriverClient backend injected
- **THEN** system SHALL return `CUDA_SUCCESS` with `total` matching `gpu_device_info.total_memory`
- **AND** `free` SHALL be ≤ `total`

#### Scenario: cuMemGetInfo uses env var fallback

- **WHEN** `TASKRUNNER_GPU_MEM_SIZE` env var is set and no backend available
- **THEN** system SHALL return `CUDA_SUCCESS` with `total = env value`

#### Scenario: cuMemGetInfo uses default 8GB when no backend and no env

- **WHEN** neither backend nor env var available
- **THEN** system SHALL return `CUDA_SUCCESS` with `total = 8 GB`

---

### Requirement: SHIM-MODULE-LOAD-HONEST-MARKING

The system SHALL accurately mark `cuModuleLoadData/Ex/FatBinary` as STUB rather than fake REAL_IMPL.

#### Scenario: cuModuleLoadData returns CUDA_ERROR_NOT_IMPLEMENTED

- **WHEN** application calls `cuModuleLoadData(&module, image)` with no ELF parser backend
- **THEN** system SHALL return `CUDA_ERROR_NOT_IMPLEMENTED`
- **AND** `cu_stub_table.inc` SHALL mark it as `// STUB`

#### Scenario: docs-audit reflects demoted APIs

- **WHEN** `tools/docs-audit.sh` runs after A.2 demotion
- **THEN** Critical APIs count SHALL be 76 (was 79 before demotion)

---

### Requirement: SHIM-TEST-COVERAGE-EXPANSION

The system SHALL expand `tests/umd/test_cuda_shim.cpp` from 37 to ≥47 E2E test cases.

#### Scenario: cuEventCreate returns valid handle

- **WHEN** application calls `cuEventCreate(&event, 0)`
- **THEN** system SHALL return `CUDA_SUCCESS` with `event != NULL`

#### Scenario: cuEventSynchronize waits for completion

- **WHEN** application calls `cuEventRecord(event, 0)` then `cuEventSynchronize(event)`
- **THEN** system SHALL return `CUDA_SUCCESS`

#### Scenario: cuStreamCreate returns valid handle

- **WHEN** application calls `cuStreamCreate(&stream, 0)`
- **THEN** system SHALL return `CUDA_SUCCESS` with `stream != NULL`

#### Scenario: cuStreamSynchronize waits for completion

- **WHEN** application calls `cuStreamSynchronize(stream)`
- **THEN** system SHALL return `CUDA_SUCCESS`

#### Scenario: cuCtxGetCacheConfig returns default

- **WHEN** application calls `cuCtxGetCacheConfig(&config)` after init
- **THEN** system SHALL return `CUDA_SUCCESS` with `config = CU_FUNC_CACHE_PREFER_NONE`

#### Scenario: cuCtxGetLimit returns non-zero

- **WHEN** application calls `cuCtxGetLimit(&value, CU_LIMIT_STACK_SIZE)`
- **THEN** system SHALL return `CUDA_SUCCESS` with `value > 0`

#### Scenario: cuLaunchCooperativeKernel returns NOT_SUPPORTED

- **WHEN** application calls `cuLaunchCooperativeKernel(...)` with no cooperative hardware
- **THEN** system SHALL return `CUDA_ERROR_NOT_SUPPORTED` (not silently delegate to cuLaunchKernel)

---

### Requirement: CROSS-REPO-H3-FOLLOWUP-PR-DESCRIPTION

The system SHALL prepare a PR description for UsrLinuxEmu owner to resolve 4 H-3 follow-up fixes.

#### Scenario: PR description covers F1-F4

- **WHEN** UsrLinuxEmu owner reads `docs/superpowers/cross-repo-prs/2026-07-02-h3-followup-fixes.md`
- **THEN** the document SHALL contain sections for F1, F2, F3, F4
- **AND** each section SHALL include exact file paths and proposed fixes

#### Scenario: PR description has no code changes

- **WHEN** reviewer inspects PR description
- **THEN** it SHALL affect only `.md` files (no .cpp/.hpp/.h)

---

### Requirement: PHASE3-PREP-DESIGN-NOTES

The system SHALL create a DRAFT design notes document for Phase 3 kickoff acceleration.

#### Scenario: Design notes exist with key sections

- **WHEN** reviewer reads `docs/superpowers/plans/2026-07-02-phase3-prep-design-notes.md`
- **THEN** document SHALL contain: Priority Matrix, Trigger Conditions (4), Open Decisions (Q1-Q5), Effort Estimates
- **AND** frontmatter SHALL mark `STATUS: DRAFT`

#### Scenario: Design notes reference trigger conditions

- **WHEN** reviewer reads Trigger Conditions section
- **THEN** it SHALL list 4 conditions: Stage 1.4 start, external demand, CI gap, time pressure (4+ weeks idle)

---

### Requirement: NON-REGRESSION

The system SHALL NOT regress existing functionality.

#### Scenario: 87+ existing tests still pass

- **WHEN** all 5 test binaries run
- **THEN** ≥87 cases SHALL pass (76 original + 10 new + 1 A.1)

#### Scenario: 79 cu\* symbols preserved

- **WHEN** `nm -D --defined-only build/libcuda_taskrunner.so | grep -c " cu[A-Z]"` runs
- **THEN** output SHALL be exactly 79 (no ABI break)

#### Scenario: docs-audit still passes

- **WHEN** `tools/docs-audit.sh` runs
- **THEN** total check count SHALL remain ≥54 with all PASS

---

## Cross-References

- Proposal: [`../proposal.md`](../proposal.md)
- Design: [`../design.md`](../design.md)
- Tasks: [`../tasks.md`](../tasks.md)
- Hotfix (prerequisite): [commit d8ca3d3](../../../../../../external/TaskRunner/) (searchable)
- Superseded: [`../../archive/2026-07-02-umd-shim-coverage-hardening/`](../../archive/2026-07-02-umd-shim-coverage-hardening/)
- Phase 1.5 Stretch: [`../../archive/2026-07-02-taskrunner-umd-backend-enable/`](../../archive/2026-07-02-taskrunner-umd-backend-enable/)
- H-3 follow-up: [`../../../../../UsrLinuxEmu/docs/07-integration/h3-activation-followup.md`](../../../../../UsrLinuxEmu/docs/07-integration/h3-activation-followup.md)
- Stage 1 plan: [`../../../../../UsrLinuxEmu/docs/roadmap/stage-1-kernel-emu.md`](../../../../../UsrLinuxEmu/docs/roadmap/stage-1-kernel-emu.md)

---

**Capability Status**: PROPOSED
**Next State**: ACCEPTED (after all sub-plans A.1-A.3 + H-3 PR + Phase 3 prep complete + tests pass + cross-repo sync)