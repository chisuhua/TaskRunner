---
SCOPE: UMD-EVOLUTION
STATUS: PROPOSED
IMPLEMENTED: NO
---

# umd-evolution Scope

This directory contains **umd-evolution scope** vision documents. **No production implementation.**

## In Scope (Vision Only)

- Minimal CUDA Runtime API surface (cudaMalloc / cudaMemcpy / cudaLaunchKernel PoC)
- Doorbell mmap bypass (PoC, see AMD ROCm `amd_aql_queue.cpp:482-493`)
- Ring buffer self-management (PoC)
- ELF + CUBIN parser (PoC)
- Stream object model (vision)
- Context (CUcontext) model (vision)

## Status Rules

**`STATUS: ACCEPTED` is FORBIDDEN** for unimplemented features. Use `STATUS: PROPOSED` or `STATUS: DRAFT`.

## Subdirectories

- `vision-source.md` — Original `plan.md` v0.1 content (preserved as historical reference)
- `vision.md` — Curated UMD vision extracted from `vision-source.md`
- `gap-analysis.md` — Gap analysis vs AMD ROCm / NVIDIA CUDA UMDs (2026-06-24 research)
- `architecture/` — UMD architecture vision (future Phase D)
- `roadmap/` — UMD PoC roadmap (deferred until Phase D)
- `adr/` — UMD scope TADRs (tadr-201~205 + redirect files for old tadr-001~003)

## Cross-Scope References

- For test-fixture (current main): see `../test-fixture/README.md`
- For shared infrastructure: see `../shared/README.md`
