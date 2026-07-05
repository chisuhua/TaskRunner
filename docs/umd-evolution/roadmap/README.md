---
SCOPE: UMD-EVOLUTION
STATUS: ACTIVE
LAST_UPDATED: 2026-07-05
---

# UMD-EVOLUTION Roadmap Status

This directory tracks the current state of the UMD-EVOLUTION redesign project,
so future sessions can pick up exactly where previous sessions ended.

## Current Snapshot (2026-07-05)

**Status**: Phase 0 + 1 + 1.5 + 1.6 + 1.7 + 2 COMPLETE. **Phase 3 ACTIVE** (trigger condition met 2026-07-04).

**Tests**: 142/142 pass (across 5 binaries) · **docs-audit**: 53/54 checks + 1 false-positive FAIL · **cu\* symbols exported**: 98

| Phase | Description | Status | Documentation |
|-------|-------------|--------|---------------|
| Phase 0 | Doc fix + architecture/ + Q4 motivation | ✅ COMPLETE | `roadmap/phase-0-complete.md` |
| Phase 1 | CudaRuntimeApi + 8 tests + 4 CLI commands | ✅ COMPLETE | `roadmap/phase-1-complete.md` |
| Phase 2 | libcuda_taskrunner.so LD_PRELOAD shim | ✅ COMPLETE | `roadmap/phase-2-complete.md` |
| Phase 1.5/1.6/1.7 | dynamic_cast fix + 15 cu\* REAL_IMPL + 103 tests | ✅ COMPLETE | `roadmap/phase-1-6-7-extensions-complete.md` |
| **Phase 3** | **API extension (Stream/Event/Memory pool/Texture)** | **🚀 ACTIVE** | **`roadmap/phase-3-deferred.md` (historical) + `docs/superpowers/plans/2026-07-02-phase3-prep-design-notes.md` (ACTIVE) + `docs/superpowers/plans/2026-07-05-umd-phase3.3-event-texture.md` (3.2 实施)** |

## How to Use This Roadmap

For a new session after this one:

1. Read `roadmap/current-status.md` first — overview + handoff
2. Read `roadmap/phase-1-6-7-extensions-complete.md` for the post-Phase 2 follow-ups
3. Read `docs/superpowers/plans/2026-07-02-phase3-prep-design-notes.md` for Phase 3 design (ACTIVE)
4. Read `docs/superpowers/plans/2026-07-05-umd-phase3.3-event-texture.md` for the immediate next work
5. Phase 3.1 (Stream/Memory pool) needs cross-repo coordination with UsrLinuxEmu sim layer

## Files in This Directory

| File | Purpose |
|------|---------|
| `README.md` (this) | Roadmap index |
| `current-status.md` | Master snapshot — most important to read first |
| `phase-0-complete.md` | Phase 0 documentation fix summary |
| `phase-1-complete.md` | Phase 1 Runtime PoC summary |
| `phase-2-complete.md` | Phase 2 LD_PRELOAD shim summary (baseline) |
| `phase-1-6-7-extensions-complete.md` | **Phase 1.5/1.6/1.7 follow-ups (NEW)** |
| `phase-3-deferred.md` | Phase 3 historical deferred status (superseded by ACTIVE prep notes) |

## Authoritative References

| Doc | Path | Use |
|-----|------|-----|
| Design spec | `docs/superpowers/specs/2026-06-30-umd-evolution-redesign.md` | Architecture decisions |
| Phase 1 plan | `docs/superpowers/plans/2026-06-30-umd-evolution-redesign.md` | B.1-B.7 detail |
| Phase 2 plan v2 | `docs/superpowers/plans/2026-07-01-umd-phase2-ld-preload.md` | C.1-C.9 detail |
| Phase 1.5 plan | `docs/superpowers/plans/2026-07-02-phase1.5-cudastub-dynamic-cast-fix.md` | dynamic_cast fix |
| Phase 3 prep design (ACTIVE) | `docs/superpowers/plans/2026-07-02-phase3-prep-design-notes.md` | Phase 3 architecture |
| **Phase 3.3 plan (NEW)** | **`docs/superpowers/plans/2026-07-05-umd-phase3.3-event-texture.md`** | **Phase 3.2 Event timing + Texture/Surface (immediate)** |
| Architecture | `docs/umd-evolution/architecture/runtime-layering.md` | Handle lifecycle + limitations |
