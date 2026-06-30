---
SCOPE: UMD-EVOLUTION
STATUS: PROPOSED
DECISION_DATE: 2026-06-24
IMPLEMENTED: NO
---

# TADR-205: UMD Evolution PoC Roadmap (H-5)

## Context

If/when umd-evolution PoC work begins, it needs a clear phasing to avoid scope creep.

## Decision

Defer until all of the following are true:
1. H-5 cross-repo sync merged (this change)
2. H-3.5 follow-up work shipped
3. Explicit PoC requirement identified (currently none)

## Proposed Phasing (when initiated)

**Phase D-1** (2-4 weeks):
- Doorbell mmap bypass skeleton (following AMD ROCm `amd_aql_queue.cpp:482-493`)
- Ring buffer self-management skeleton
- Build verification under `TASKRUNNER_BUILD_MODE=umd-evolution`

**Phase D-2** (4-8 weeks):
- Minimal CUDA Runtime API surface (cudaMalloc / cudaMemcpy / cudaLaunchKernel)
- Kernel launch by name (no CUfunction handle yet)

**Phase D-3** (8-12 weeks):
- ELF + CUBIN parser
- Kernel arg serialization
- Real kernel execution via UsrLinuxEmu's BasicGpuSimulator

**Total**: 14-24 weeks (~3-6 months)

## Recommendation

**Do NOT pursue this work as primary goal.** See `gap-analysis.md` for ROI analysis.

## References

- `docs/umd-evolution/vision.md` — UMD complete vision
- `docs/umd-evolution/gap-analysis.md` — vs ROCm/CUDA gap analysis
- `docs/superpowers/specs/2026-06-30-umd-evolution-redesign.md` — redesign spec that generated this dependency analysis

## Dependency Analysis (Added 2026-06-30)

Per design doc [`docs/superpowers/specs/2026-06-30-umd-evolution-redesign.md`](../../superpowers/specs/2026-06-30-umd-evolution-redesign.md), the original D-phase roadmap has the following cross-repo dependencies:

| Original Phase | Requires UsrLinuxEmu change? | Blocking? |
|----------------|------------------------------|-----------|
| D-1 (Doorbell mmap) | Yes (ADR-024 implementation) | 🔴 Major |
| D-2 (Minimal CUDA API) | No (IGpuDriver 31 methods sufficient) | No |
| D-3 (ELF + CUBIN parser) | Yes (BasicGpuSimulator kernel execution) | 🔴 Major |

**Kernel launch dependency cycle** (critical):
- D-2 cudaLaunchKernel → relies on D-3 ELF parsing + Simulator execution
- Resolution: D-2 PoC **only runs via CudaStub mode**; real kernel execution requires D-3 prerequisite
- This means D-2 cannot deliver a complete PoC for real CUDA programs without D-3 first

**Why this matters for Phase 0-3 of the redesign spec**:
- Phase 1 (Runtime PoC) inherits the same constraint: only CudaStub backend is fully functional
- Phase 3.3 (YAML kernel registry) sidesteps ELF parsing by accepting user-provided name→index mapping
- D-1 (doorbell) and D-3 (ELF parsing) remain deferred indefinitely per `gap-analysis.md`

---
