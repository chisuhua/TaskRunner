---
SCOPE: UMD-EVOLUTION
STATUS: DRAFT
DESIGN_DATE: 2026-07-02
DESIGN_AUTHOR: Sisyphus
RELATED: phase-3-deferred.md (supersedes as primary roadmap), phase-2-complete.md
RELATED_TADR: tadr-205, tadr-201/202/203 (all SUPERSEDED)
TRIGGER: External conditions met (see Trigger Conditions below)
---

# Phase 3 Kickoff — Design Notes

> **Status**: DRAFT (no implementation, design reservation only)
> **Purpose**: Pre-kickoff design notes that capture priority, effort, and decisions for Phase 3, complementing `phase-3-deferred.md` (which tracks the deferred status).
> **Naming**: Renamed from earlier "skeleton" draft to avoid confusion with existing `phase-3-deferred.md`.

## Context

Phase 0-2 of the umd-evolution roadmap are complete (commits `cd706fd` through `83ef131`). Phase 3 (API extension) is **deferred** per `phase-3-deferred.md`, pending external triggers. This document captures the design intent so that when Phase 3 kicks off, work can begin immediately without re-deriving architecture.

## Priority Matrix

| Priority | Category | Sub-effort | Sub-phase | Backend dependency |
|----------|----------|------------|-----------|---------------------|
| **P0** | Stream async ops (cuStreamBeginCapture/EndCapture, graphs) | 1-2 w | 3.1 | UsrLinuxEmu Stage 1.4 |
| **P0** | Memory pool (cuMemPool*, cuMemAllocFromPoolAsync) | 1-2 w | 3.2 | UsrLinuxEmu Stage 1.3 UVM |
| **P1** | Event timing precision (proper CUDA clock API integration) | 1 w | 3.2 | None (CudaStub clock) |
| **P1** | Texture/Surface (cuTexRefCreate/Destroy, cuArray*) | 2 w | 3.3 | None (frontend impl) |
| **P2** | YAML kernel registry (replace manual register_kernel with config) | 1 w | 3.4 | None |
| **P2** | cuDeviceGetAttribute expansion (cover all 80+ attributes) | 0.5 w | 3.4 | None |
| **P3** | Multi-device support (cuDeviceGetCount > 1) | 2-3 w | 3.5 | UsrLinuxEmu multi-device |
| **P3** | ELF/CUBIN parsing (D-3 lite, for real vectorAdd E2E) | 4-6 w | backlog | UsrLinuxEmu kernel ABI |

## Trigger Conditions

Phase 3 should start when **one of**:

1. **UsrLinuxEmu Stage 1.4 starts** requiring cuStream/cuMemPool APIs
2. **External demand**: Bug report or user request for specific cu* APIs
3. **CI gap**: tests requiring APIs that aren't currently stubbed
4. **Time pressure**: 4+ weeks idle (recurring review needed)

## Open Decisions

| Q# | Question | Default | Notes |
|----|----------|---------|-------|
| Q1 | Cubin parsing strategy | YAML config | ELF reserved for D-3 |
| Q2 | Scope | All P0+P1 (~6 w) | P2 optional |
| Q3 | Vulkan arch extension | Keep (no impl) | matches Phase 0 decision |
| Q4 | Q4 POA | ✅ Resolved | POA-1 (KFD Consumer) + POA-2 (CI Regression) |
| Q5 | Implementation team | Session-driven | works for Phase 2 |

## Effort Estimates

- **Sub-plan 3.1** (Stream async + Memory pool): 3-4 weeks
- **Sub-plan 3.2** (Event precision + Texture/Surface): 2-3 weeks
- **Sub-plan 3.3** (YAML registry + Multi-device + ELF parse): 6-9 weeks
- **Total Phase 3**: 11-16 weeks (3-4 months)
- **If only 3.1 + 3.2 (no 3.3)**: 5-7 weeks (1.5 months)

## Pre-Kickoff Checklist

When Phase 3 triggers, verify these are in place before starting:

- [ ] UsrLinuxEmu submodule pointer at the Stage 1.x completion commit
- [ ] docs-audit.sh reports ≥58/58 checks (current: 54 — see change 2026-07-02-phase16-shim-extension)
- [ ] All 49 E2E shim tests passing (current state after this change)
- [ ] IGpuDriver 31-method contract reviewed for Phase 3 additions (cuStream* / cuMemPool* / cuEvent*)
- [ ] Spec design doc reviewed and updated per Phase 3 scope

## Forbidden in Phase 3 (Maintain Scope)

- ❌ Vulkan Runtime API implementation (Q3 decision: no)
- ❌ Real kernel execution via ELF parsing (deferred to D-3)
- ❌ Replacing CudaStub with real device backend (CudaStub stays as test target)

## Related Backlog Items (NOT Phase 3)

- **D-1**: Doorbell mmap bypass (requires UsrLinuxEmu ADR-024 implementation)
- **D-3**: ELF/CUBIN parser + real kernel execution (requires UsrLinuxEmu BasicGpuSimulator kernel ABI)

These remain deferred indefinitely per `gap-analysis.md`.

## Recommended Workflow When Triggered

1. User says "let's start Phase 3.1" (or whichever sub-plan is relevant)
2. New session reads this roadmap (phase-3-deferred.md + this doc)
3. Creates detailed plan in `docs/superpowers/plans/2026-XX-XX-umd-phase3-N-N.md`
4. Dispatches implementer subagents (parallel where possible)
5. Pushes commits to `main` with `feat(shim): ... (Phase 3.X)` prefix
6. Updates `phase-3-deferred.md` status when sub-plan completes

## References

- [`../roadmap/phase-3-deferred.md`](../roadmap/phase-3-deferred.md) — primary deferred roadmap
- [`../roadmap/current-status.md`](../roadmap/current-status.md) — master snapshot
- [`../roadmap/phase-2-complete.md`](../roadmap/phase-2-complete.md) — Phase 2 deliverables
- [`../../../UmD-evolution-redesign.md`](../../../superpowers/specs/2026-06-30-umd-evolution-redesign.md) — overall design spec
- [`../../../UsrLinuxEmu/docs/roadmap/stage-1-kernel-emu.md`](../../../../../UsrLinuxEmu/docs/roadmap/stage-1-kernel-emu.md) — Stage 1 plan (Stage 1.4 is the primary trigger)
- [`../../../UsrLinuxEmu/docs/07-integration/h3-activation-followup.md`](../../../../../UsrLinuxEmu/docs/07-integration/h3-activation-followup.md) — H-3 follow-up (separate cleanup)

---

**Status**: DRAFT (will be promoted to ACTIVE when Phase 3 kickoff begins)
**Last Updated**: 2026-07-02
**Next Action**: Wait for trigger condition; revisit on UsrLinuxEmu Stage 1.4 completion or 4+ weeks idle