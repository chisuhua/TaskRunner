---
SCOPE: UMD-EVOLUTION
STATUS: PROPOSED
DECISION_DATE: 2026-06-24
IMPLEMENTED: NO
---

# ADR: UMD Evolution PoC Roadmap (H-5)

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
