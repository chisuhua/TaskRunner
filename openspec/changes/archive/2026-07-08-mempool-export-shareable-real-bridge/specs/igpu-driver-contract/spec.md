---
SCOPE: shared
STATUS: PROPOSED
DATE: 2026-07-08
CHANGE: mempool-export-shareable-real-bridge
RELATED: tadr-301-igpu-driver-contract.md, tadr-305-mempool-export-shareable.md
---

# Capability: igpu-driver-contract (delta)

> Delta spec for `tadr-301-igpu-driver-contract.md` — only the Phase 4
> row references are affected by the TADR renumbering
> (`tadr-302-mempool-export-shareable` → `tadr-305-mempool-export-shareable`).
> No requirement semantics change.

## MODIFIED Requirements

### Requirement: Phase 4 Method Count Evolution table row references tadr-305 (was tadr-302)

The Phase 4 row in the Method Count Evolution table MUST reference the
correct mempool ADR. Previously referenced `tadr-302` (which was a
collision with sync primitives); now references `tadr-305` which is the
dedicated mempool-export-shareable ADR.

#### Scenario: Phase 4 row text updated
- **WHEN** reading `docs/shared/adr/tadr-301-igpu-driver-contract.md` after this change is applied
- **THEN** the Phase 4 row MUST read: `| Phase 4 真实化 (current) | **47 (+1)** | Phase 4 shim bridge + mem_pool_export_shareable 新增 |`
- **AND** the bottom References section MUST contain a line `Phase 4 tadr-305 (mem_pool_export_shareable contract, 47-method expansion)`
- **AND** MUST NOT contain `Phase 4 tadr-302 (mem_pool_export_shareable contract, ...)` anywhere in the file

## ADDED Requirements

None for this delta (the Phase 4 row was already present; only the ADR
reference number is corrected). The full Phase 4 requirement set lives in
`openspec/changes/archive/2026-07-07-phase3-real-impl-bridge/`.

## REMOVED Requirements

None. The Phase 4 row content (47 methods, +1, real bridge completion)
is preserved; only the ADR reference number changes.

## RENAMED Requirements

None. The Phase 4 row text is functionally identical; only its
cross-reference target (tadr-302 → tadr-305) changes.