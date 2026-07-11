---
SCOPE: UMD-EVOLUTION
STATUS: PROPOSED
TADR: 401
TITLE: Promote UMD-EVOLUTION from PROPOSED to ACCEPTED
---

# TADR-401: Promote UMD-EVOLUTION from PROPOSED to ACCEPTED

**状态**: PROPOSED (will transition to ACCEPTED only when all checklist entries are DONE and dual sign-off obtained)
**日期**: 2026-07-11 (initial creation)
**提案人**: TaskRunner owner
**评审者**: (pending — at actual promotion time: TaskRunner owner + UsrLinuxEmu owner)
**关联 ADR (UsrLinuxEmu)**: (mirror entry to be added at actual promotion time in `UsrLinuxEmu/docs/00_adr/README.md`)
**关联 Change**: `umd-evolution-acceptance-promotion-adr` (this change)
**关联 Source**: `openspec/changes/umd-evolution-acceptance-promotion-adr/{proposal,design,tasks}.md`

---

## Context

`docs/umd-evolution/README.md` currently declares STATUS: PROPOSED with the hard rule:

> `STATUS: ACCEPTED` is FORBIDDEN for unimplemented features.

The promotion of UMD-EVOLUTION from PROPOSED to ACCEPTED is therefore a **governance event**, not an automatic transition. It requires:

1. All 5 entry conditions in the Promotion Checklist below are met
2. Dual sign-off (TaskRunner owner + UsrLinuxEmu owner)
3. Cross-repo mirror entry in `UsrLinuxEmu/docs/00_adr/README.md` per AGENTS.md H-5 3-scope rules

This ADR documents the criteria. The actual STATUS field change in `docs/umd-evolution/README.md` is the **final step**, performed in a separate commit after the checklist is complete.

## Status

This ADR is in **PROPOSED** state. It transitions to ACCEPTED only when the Promotion Checklist is complete AND dual sign-off is obtained. Until then, this ADR serves as the formal criteria document + checklist + governance reminder for future maintainers.

## Decision

UMD-EVOLUTION scope can be promoted from PROPOSED to ACCEPTED when **all 5 entry conditions** in the Promotion Checklist below are met. The actual STATUS field change in `docs/umd-evolution/README.md` is the final step, performed in a separate commit after the checklist is complete. Dual sign-off (TaskRunner owner + UsrLinuxEmu owner) is required.

## Promotion Checklist

Mark each item as DONE before changing STATUS to ACCEPTED.

- [ ] **Entry 1/5**: `umd-evolution-build-default-on` merged to TaskRunner main
      (Verify: `cmake -B build` (no env var) compiles UMD code; full test suite passes)

- [ ] **Entry 2/5**: `g-gpu-client-default-stub-init` (or its v2 successor `g-gpu-client-meyers-singleton-fallback`) merged to TaskRunner main
      (Verify: New shim consumer test (no explicit `g_gpu_client = ...`) works)

- [ ] **Entry 3/5**: `l1-l2-bridge-e2e-test-skeleton` merged to TaskRunner main
      (Verify: `test_cu_graph_e2e_standalone` compiles + runs (SKIP is acceptable if UsrLinuxEmu side not ready))

- [ ] **Entry 3b/5** (cross-repo follow-up): UsrLinuxEmu side real L1↔L2 test implemented
      (Verify: `UsrLinuxEmu/tests/test_cu_graph_e2e_standalone.cpp` exists and passes; `cuGraphLaunch` + `cuStreamSynchronize` go through real `GpuDriverClient` → real plugin → Puller → handleComplete → fence signaled)

- [ ] **Entry 4/5**: 1-2 weeks of CI clean (no critical bugs introduced by the 3 TaskRunner changes)
      (Verify: CI green for ≥ 10 working days; no SIGSEGV / sanitizer failures in UMD shim)

- [ ] **Entry 5/5**: Dual sign-off
      (Verify: TaskRunner owner + UsrLinuxEmu owner both approve; sign-off recorded in PR comments)

- [ ] **Entry 5b/5** (cross-repo): Mirror entry in `UsrLinuxEmu/docs/00_adr/README.md` table
      (Verify: Table contains `tadr-401 | promote-umd-evolution-to-accepted | ACCEPTED | ...`)

- [ ] **STATUS change**: Update `docs/umd-evolution/README.md` STATUS from PROPOSED to ACCEPTED
      (Verify: `git grep "STATUS: PROPOSED" docs/umd-evolution/README.md` returns nothing)

## Consequences

### Positive

- UMD code is built by default (no `TASKRUNNER_BUILD_MODE=umd-evolution` required)
- New cu* APIs added to UMD are no longer "experimental vision" but production code
- L1↔L2 bridge test is the regression gate for future UMD changes
- Cross-repo coordination overhead: UsrLinuxEmu side has a mirror ADR entry; promotion requires both owners' sign-off

### Negative / Risks

- Promotion is irreversible without a new ADR (consider SUPERSEDED status if UMD-EVOLUTION later proves unviable)
- 1-2 w CI stability window is not enforced by tooling — relies on operator discipline
- Cross-repo mirror update is easy to forget; mitigated by Entry 5b/5 + UsrLinuxEmu owner sign-off

## Cross-References

Prerequisite changes (all in TaskRunner `openspec/changes/`):

- `umd-evolution-build-default-on/` (archived at `openspec/changes/archive/2026-07-11-umd-evolution-build-default-on`)
- `g-gpu-client-default-stub-init/` (v1, retained for traceability; superseded by v2 `g-gpu-client-meyers-singleton-fallback` which is merged)
- `l1-l2-bridge-e2e-test-skeleton/`

Cross-repo follow-up (in UsrLinuxEmu):

- `UsrLinuxEmu/tests/test_cu_graph_e2e_standalone.cpp` (real L1↔L2 test implementation, Entry 3b/5)
- `UsrLinuxEmu/docs/00_adr/README.md` (TaskRunner TADR mirror table, Entry 5b/5)

Related TaskRunner TADRs:

- `tadr-108-build-mode-selection.md` (shared; build mode default, now superseded by `umd-evolution-build-default-on`)

## Verification (at actual promotion time)

```bash
# 1. Verify all 5 entries checked (visual review of the checklist above)

# 2. Verify CI green for ≥ 10 working days
gh run list --workflow=ci.yml --status=success --limit=50

# 3. Verify docs-audit.sh passes (no scope violations)
tools/docs-audit.sh

# 4. Update STATUS field
# In docs/umd-evolution/adr/tadr-401-promote-umd-evolution-to-accepted.md:
#   change "STATUS: PROPOSED" to "STATUS: ACCEPTED"
# In docs/umd-evolution/README.md:
#   change "STATUS: PROPOSED" to "STATUS: ACCEPTED"

# 5. Update cross-repo mirror
# In UsrLinuxEmu/docs/00_adr/README.md:
#   add row: "| tadr-401 | promote-umd-evolution-to-accepted | ACCEPTED | ... |"

# 6. Commit (atomic, all 3 files together)
git add docs/umd-evolution/adr/tadr-401-promote-umd-evolution-to-accepted.md \
        docs/umd-evolution/README.md
git commit -m "feat(umd): promote UMD-EVOLUTION to ACCEPTED (tadr-401 complete)"
```

---

**最后更新**: 2026-07-11 (initial creation)