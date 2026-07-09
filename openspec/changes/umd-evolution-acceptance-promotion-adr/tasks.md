---
SCOPE: umd-evolution
STATUS: PROPOSED
---

# Tasks: umd-evolution-acceptance-promotion-adr

> **Goal**: Create the ADR document `tadr-401-promote-umd-evolution-to-accepted.md` that documents the UMD-EVOLUTION → ACCEPTED promotion criteria.
> **Risk**: low (documentation only, no code changes).
> **Estimated effort**: 0.5 d.

## 1. Pre-flight

- [ ] 1.1 Verify existing TADR numbering scheme in `docs/umd-evolution/adr/`:
  ```bash
  ls docs/umd-evolution/adr/
  ```
  Expected: `tadr-201` ~ `tadr-205` + `tadr-301` ~ `tadr-305` (or similar). Verify `tadr-401` is not yet taken.

- [ ] 1.2 Read one existing umd-evolution TADR (e.g., `tadr-301` or similar) to follow the same format.

- [ ] 1.3 Verify the 3 prerequisite openspec changes exist (PROPOSED or merged):
  ```bash
  ls openspec/changes/umd-evolution-build-default-on/
  ls openspec/changes/g-gpu-client-default-stub-init/
  ls openspec/changes/l1-l2-bridge-e2e-test-skeleton/
  ```

## 2. Write the ADR

- [ ] 2.1 Create `docs/umd-evolution/adr/tadr-401-promote-umd-evolution-to-accepted.md` with these sections:

  ```markdown
  ---
  SCOPE: umd-evolution
  STATUS: PROPOSED
  TADR: 401
  TITLE: Promote UMD-EVOLUTION from PROPOSED to ACCEPTED
  ---

  # TADR-401: Promote UMD-EVOLUTION from PROPOSED to ACCEPTED

  ## Context

  `docs/umd-evolution/README.md` currently has STATUS: PROPOSED with the hard rule:
  > `STATUS: ACCEPTED` is FORBIDDEN for unimplemented features.

  The 3 prerequisite changes (build-default-on, g-gpu-client default init, L1↔L2 bridge) plus
  a 1-2 w CI stability window plus dual sign-off are the gate to promotion. This ADR documents
  the criteria and the actual checklist.

  ## Status

  This ADR is in PROPOSED state. It transitions to ACCEPTED only when the Promotion Checklist
  is complete and dual sign-off is obtained.

  ## Decision

  UMD-EVOLUTION scope can be promoted from PROPOSED to ACCEPTED when **all 5 entry conditions**
  in the Promotion Checklist below are met. The actual STATUS field change in
  `docs/umd-evolution/README.md` is the final step, performed in a separate commit after the
  checklist is complete.

  ## Promotion Checklist

  Mark each item as DONE before changing STATUS to ACCEPTED.

  - [ ] **Entry 1/5**: `umd-evolution-build-default-on` merged to TaskRunner main
        (Verify: `cmake -B build` (no env var) compiles UMD code; 318/318 tests pass)

  - [ ] **Entry 2/5**: `g-gpu-client-default-stub-init` merged to TaskRunner main
        (Verify: New shim consumer test (no explicit `g_gpu_client = ...`) works)

  - [ ] **Entry 3/5**: `l1-l2-bridge-e2e-test-skeleton` merged to TaskRunner main
        (Verify: `test_cu_graph_e2e_standalone` compiles + SKIPs gracefully)

  - [ ] **Entry 3b/5** (cross-repo follow-up): UsrLinuxEmu side real L1↔L2 test implemented
        (Verify: `UsrLinuxEmu/tests/test_cu_graph_e2e_standalone.cpp` exists and passes;
         cuGraphLaunch + cuStreamSynchronize go through real GpuDriverClient → real plugin
         → Puller → handleComplete → fence signaled)

  - [ ] **Entry 4/5**: 1-2 weeks of CI clean (no critical bugs introduced by the 3 TaskRunner changes)
        (Verify: CI green for ≥ 10 working days; no SIGSEGV / sanitizer failures in UMD shim)

  - [ ] **Entry 5/5**: Dual sign-off
        (Verify: TaskRunner owner + UsrLinuxEmu owner both approve; sign-off recorded in PR comments)

  - [ ] **Entry 5b/5** (cross-repo): Mirror entry in `UsrLinuxEmu/docs/00_adr/README.md` table
        (Verify: Table contains `tadr-401 | promote-umd-evolution-to-accepted | ACCEPTED | ...`)

  - [ ] **STATUS change**: Update `docs/umd-evolution/README.md` STATUS from PROPOSED to ACCEPTED
        (Verify: `git grep "STATUS: PROPOSED" docs/umd-evolution/README.md` returns nothing)

  ## Consequences

  - UMD code is built by default (no `TASKRUNNER_BUILD_MODE=umd-evolution` required)
  - New cu* APIs added to UMD are no longer "experimental vision" but production code
  - L1↔L2 bridge test is the regression gate for future UMD changes
  - Cross-repo coordination overhead: UsrLinuxEmu side has a mirror ADR entry; promotion
    requires both owners' sign-off

  ## Cross-References

  Prerequisite changes (all in TaskRunner openspec/changes/):
  - `umd-evolution-build-default-on/`
  - `g-gpu-client-default-stub-init/`
  - `l1-l2-bridge-e2e-test-skeleton/`

  Cross-repo follow-up (in UsrLinuxEmu):
  - `tests/test_cu_graph_e2e_standalone.cpp` (real L1↔L2 test implementation)

  ## Verification (at actual promotion time)

  ```bash
  # 1. Verify all 5 entries checked
  # (visual review of the checklist above)

  # 2. Verify CI green for ≥ 10 working days
  gh run list --workflow=ci.yml --status=success --limit=50

  # 3. Update STATUS field
  # In docs/umd-evolution/README.md, change "STATUS: PROPOSED" to "STATUS: ACCEPTED"
  # This is the final commit. Combined with the UsrLinuxEmu mirror update.
  ```
  ```

## 3. Verification

- [ ] 3.1 ADR exists and has all required sections:
  ```bash
  ls docs/umd-evolution/adr/tadr-401*
  head -10 docs/umd-evolution/adr/tadr-401-promote-umd-evolution-to-accepted.md
  ```
- [ ] 3.2 Cross-references to all 3 prerequisite changes are present:
  ```bash
  grep "umd-evolution-build-default-on" docs/umd-evolution/adr/tadr-401*
  grep "g-gpu-client-default-stub-init" docs/umd-evolution/adr/tadr-401*
  grep "l1-l2-bridge-e2e-test-skeleton" docs/umd-evolution/adr/tadr-401*
  ```
- [ ] 3.3 docs-audit passes:
  ```bash
  tools/docs-audit.sh
  ```

## 4. Commit + push

- [ ] 4.1 commit (atomic):
  ```bash
  git add docs/umd-evolution/adr/tadr-401-promote-umd-evolution-to-accepted.md
  git commit -m "docs(umd): add TADR-401 UMD-EVOLUTION → ACCEPTED promotion criteria

  - Documents the 5 entry conditions for promotion
  - Includes Promotion Checklist (build-default-on, g-gpu-client default, L1↔L2 bridge, CI stability, dual sign-off)
  - Includes cross-repo mirror requirement (UsrLinuxEmu docs/00_adr/README.md)
  - STATUS: PROPOSED initially; transitions to ACCEPTED only when checklist is complete
  - Prerequisite for UMD-EVOLUTION → ACCEPTED promotion (entry 5/5)"
  ```
- [ ] 4.2 push:
  ```bash
  git push origin main
  ```

## Acceptance Criteria

- ADR `tadr-401-promote-umd-evolution-to-accepted.md` exists
- All 5 entry conditions + 5b (cross-repo mirror) + STATUS change are in the Promotion Checklist
- Cross-references to all 3 prerequisite openspec changes exist
- docs-audit.sh passes
- The ADR's own STATUS field is PROPOSED (will be updated at actual promotion time)
- The actual STATUS change of `docs/umd-evolution/README.md` is NOT done in this change
