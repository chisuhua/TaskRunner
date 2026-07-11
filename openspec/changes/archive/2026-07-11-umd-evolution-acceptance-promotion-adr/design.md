---
SCOPE: umd-evolution
STATUS: PROPOSED
---

## Context

`docs/umd-evolution/README.md` STATUS: PROPOSED + the hard rule "ACCEPTED FORBIDDEN for unimplemented features" makes the promotion a **governance event**, not an automatic transition. The 4 prerequisite changes (3 in TaskRunner + 1 in UsrLinuxEmu) need to land first, then a stability window elapses, then a dual sign-off, then the STATUS field is changed.

This ADR is the artifact that:
- Documents the promotion criteria
- Acts as a checklist
- Forces explicit dual sign-off
- Reminds future maintainers of the cross-repo mirror requirement

## Goals / Non-Goals

**Goals:**
- ADR document exists and is in PROPOSED state initially
- Promotion Checklist covers all 5 entry conditions
- Cross-repo mirror requirement is explicit
- The ADR links to all 3 prerequisite openspec changes in TaskRunner
- (Future) At actual promotion time, the STATUS field of `docs/umd-evolution/README.md` is changed and the cross-repo mirror in `UsrLinuxEmu/docs/00_adr/README.md` is updated

**Non-Goals:**
- Actually changing STATUS in this change (deferred to a future commit after checklist completion)
- Implementing any of the prerequisite changes
- Forcing 1-2 w CI stability window (the change proposes the criterion, not enforces time)

## Decisions

### Decision 1: ADR lives in `docs/umd-evolution/adr/`

This is consistent with existing TADR convention. The ADR is a TaskRunner TADR (`tadr-4XX`) because the umd-evolution scope is TaskRunner-owned.

### Decision 2: TADR number `tadr-401`

The existing umd-evolution TADRs are `tadr-201~205` (initial vision) + `tadr-301~305` (Phase 3 series). The 400-series is reserved for meta-governance ADRs. `tadr-401` is the natural choice.

### Decision 3: STATUS stays PROPOSED in the ADR document until checklist is met

The ADR document itself has a `STATUS: PROPOSED` field. This is updated to `STATUS: ACCEPTED` only when the checklist is complete AND the dual sign-off is obtained. Until then, the ADR serves as a checklist + criterion document.

### Decision 4: Cross-repo mirror is part of the actual promotion commit (not the ADR change)

When the actual STATUS change happens (e.g., in a future commit after all 3 prerequisite changes are merged and 1-2 w has passed), the commit must also update `UsrLinuxEmu/docs/00_adr/README.md` to add a mirror entry for `tadr-401`. This is enforced by the ADR's "Checklist item 6" which lists it as a required step.

## Promotion Checklist (ADR body)

The ADR document includes this checklist:

```markdown
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
       `cuGraphLaunch` + `cuStreamSynchronize` go through real GpuDriverClient → real plugin → Puller → handleComplete → fence signaled)

- [ ] **Entry 4/5**: 1-2 weeks of CI clean (no critical bugs introduced by the 3 TaskRunner changes)
      (Verify: CI green for ≥ 10 working days; no SIGSEGV / sanitizer failures in UMD shim)

- [ ] **Entry 5/5**: Dual sign-off
      (Verify: TaskRunner owner + UsrLinuxEmu owner both approve; sign-off recorded in PR comments)

- [ ] **Entry 5b/5** (cross-repo): Mirror entry in `UsrLinuxEmu/docs/00_adr/README.md` table
      (Verify: Table contains `tadr-401 | promote-umd-evolution-to-accepted | ACCEPTED | ...`)

- [ ] **STATUS change**: Update `docs/umd-evolution/README.md` STATUS from PROPOSED to ACCEPTED
      (Verify: `git grep "STATUS: PROPOSED" docs/umd-evolution/README.md` returns nothing)
```

## Risks / Trade-offs

| # | Risk | Mitigation |
|---|------|------------|
| 1 | TADR number collision with existing tadr-4XX | Verified no existing 4XX exists; tadr-401 is available |
| 2 | Cross-repo mirror entry forgotten at actual promotion | ADR's Entry 5b/5 explicitly lists it; dual reviewer (UsrLinuxEmu owner) will catch it |
| 3 | 1-2 w CI stability window is hard to enforce | Documented in ADR; CI operator must monitor; not enforced by tooling |

## Verification

```bash
# Verify ADR exists and is well-formed
cat docs/umd-evolution/adr/tadr-401-promote-umd-evolution-to-accepted.md

# Verify cross-references to prerequisite changes
grep -l "umd-evolution-build-default-on" docs/umd-evolution/adr/tadr-401*
grep -l "g-gpu-client-default-stub-init" docs/umd-evolution/adr/tadr-401*
grep -l "l1-l2-bridge-e2e-test-skeleton" docs/umd-evolution/adr/tadr-401*

# docs-audit
tools/docs-audit.sh  # expect: no new violations
```

## Open Questions

- **Q**: Should the ADR be in PROPOSED state initially, or directly in DRAFT?  
  **A**: PROPOSED. The ADR's "Decision" is the actual promotion criteria; PROPOSED is the correct initial state until the criteria are met.

- **Q**: When does the actual STATUS change happen?  
  **A**: After all 5 entries are checked off and dual sign-off is obtained. This is a future commit, not part of this change.
