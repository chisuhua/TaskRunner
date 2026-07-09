---
SCOPE: umd-evolution
STATUS: PROPOSED
---

## Why

`docs/umd-evolution/README.md` declares:

> **`STATUS: ACCEPTED` is FORBIDDEN** for unimplemented features.

UMD-EVOLUTION currently has STATUS: PROPOSED, which means it can never be `ACCEPTED` until the scope is explicitly promoted. The promotion is a governance event that requires:

1. All 5 entry conditions met (build-default-on, g-gpu-client default init, L1↔L2 bridge test, 1-2 w CI stability, dual sign-off)
2. An ADR documenting the criteria and the actual checklist completion
3. Cross-repo mirror entry in UsrLinuxEmu `docs/00_adr/README.md` per AGENTS.md H-5 3-scope rules
4. STATUS field in `docs/umd-evolution/README.md` changed from PROPOSED to ACCEPTED (this is the **only** file modified to reflect the promotion)

This change ships **only the ADR** that documents the criteria. It does NOT change the STATUS field (that happens in a follow-up commit after the checklist is complete). The ADR is the governance artifact that allows the future STATUS change to be made confidently.

This is **entry condition 5/5** for UMD-EVOLUTION → ACCEPTED promotion.

## What Changes

- **New file**: `docs/umd-evolution/adr/tadr-NNN-promote-umd-evolution-to-accepted.md` (the ADR)
- **No STATUS change** in this change (deferred to the actual promotion commit)
- **No code changes** (this is pure documentation)

## Capabilities

### New Capabilities

(none — governance document)

### Modified Capabilities

(none)

## Impact

- **Files affected**:
  - `docs/umd-evolution/adr/tadr-NNN-promote-umd-evolution-to-accepted.md` (new)
- **No production code changes**
- **No test changes**
- **No cross-repo changes** (the UsrLinuxEmu mirror entry is part of the actual promotion, not this ADR)

## Acceptance Criteria

- ADR document exists with these sections: Context, Status (PROPOSED initially), Decision, Consequences, Promotion Checklist
- The Promotion Checklist covers all 5 entry conditions:
  1. umd-evolution-build-default-on merged
  2. g-gpu-client-default-stub-init merged
  3. l1-l2-bridge-e2e-test-skeleton merged (and UsrLinuxEmu side real test)
  4. 1-2 w CI stability window with no critical bugs
  5. Dual reviewer sign-off (TaskRunner owner + UsrLinuxEmu owner)
- Cross-repo mirror requirement is explicit (UsrLinuxEmu `docs/00_adr/README.md` must be updated at actual promotion)
- The ADR links to all 3 prerequisite openspec changes
- `tools/docs-audit.sh` passes

## Risk

- **TADR number collision**: Need to pick a TADR number that doesn't conflict with existing `tadr-201~205` + `tadr-301~305`. Recommended: `tadr-401` (next 400-series) since this is a meta-ADR (governance promotion).
- **Forward-compat with cross-repo mirror**: The ADR should explicitly mention the cross-repo mirror update at actual promotion time, to avoid forgetting it.
- **Dual review requirement**: Per AGENTS.md H-5, shared scope changes need dual review. This ADR is umd-evolution scope only at the TaskRunner side, so single reviewer is sufficient at the TaskRunner side. But the cross-repo mirror in UsrLinuxEmu side requires UsrLinuxEmu owner approval — make this explicit in the ADR.
