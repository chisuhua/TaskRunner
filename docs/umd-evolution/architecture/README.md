---
SCOPE: UMD-EVOLUTION
STATUS: ACCEPTED
DATE: 2026-06-30
RELATED_DESIGN: ../../superpowers/specs/2026-06-30-umd-evolution-redesign.md
---

# UMD-EVOLUTION Architecture Overview

This directory holds the canonical architecture documentation for UMD-EVOLUTION, complementing `README.md` (scope rules) and `adr/` (decisions).

## Files

- `README.md` (this file) — Architecture overview + component diagram
- `runtime-layering.md` — Detailed Phase 1/2 layering design

## Component Stack (As Of 2026-06-30)

```
┌─────────────────────────────────────────────────────────┐
│  External Caller (CLI / Tests / Phase 2: LD_PRELOAD)    │
└──────────────────────┬──────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────┐
│  Phase 1: CudaRuntimeApi (NEW in Phase 1)              │
│  - 3 CUDA Runtime APIs (cudaMalloc/Memcpy/LaunchKern)  │
└──────────────────────┬──────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────┐
│  CudaScheduler (existing, H-3/H-5)                     │
│  - submit_mem_alloc, submit_memcpy_*, submit_launch,   │
│    wait_fence (DI through IGpuDriver*)                 │
└──────────────────────┬──────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────┐
│  IGpuDriver (31 methods, H-2.5/H-3.5)                  │
│  ├─ GpuDriverClient (real ioctl)                       │
│  ├─ CudaStub (fake; only Phase 1 fully wired)         │
│  └─ MockGpuDriver (test mock)                         │
└─────────────────────────────────────────────────────────┘
```

## Cross-Scope Layout

- `src/test_fixture/` — CudaScheduler + IGpuDriver implementations (test-fixture scope)
- `src/umd/` — CudaRuntimeApi + (Phase 2) libcuda shim (umd-evolution scope)
- `include/shared/igpu_driver.hpp` — IGpuDriver interface contract (shared scope)
- `UsrLinuxEmu plugins/gpu_driver/shared/` — ioctl canonical (cross-repo shared)

## Reading Order

1. This file (component orientation)
2. `runtime-layering.md` (Phase 1+ design rationale)
3. Design spec `2026-06-30-umd-evolution-redesign.md` (full context)
4. `../adr/tadr-204-umd-evolution-scope-clarification.md` (scope rules)
