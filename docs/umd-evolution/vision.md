---
SCOPE: UMD-EVOLUTION
STATUS: PROPOSED
IMPLEMENTED: NO
SOURCE: docs/umd-evolution/vision-source.md
---

# UMD Vision

> **Note:** This document describes a future evolution path. **No code is currently implemented.**

## Goals

1. Provide a minimal CUDA Runtime API surface (cudaMalloc / cudaMemcpy / cudaLaunchKernel) as PoC
2. Implement doorbell mmap bypass for low-latency command submission (following AMD ROCm pattern)
3. Support ring buffer self-management with explicit lifecycle
4. Enable ELF + CUBIN parser for kernel module loading (PoC)

## Non-Goals

- Full CUDA Runtime API coverage (Phase 1: 3 APIs only)
- Production-quality performance optimization
- Multi-GPU support (deferred to later)
- CUDA Graph API
- C++ template support (HIP-style)
- Cross-platform support (Linux x86_64 only)

## API Surface (PoC Target)

| API | Status | Notes |
|-----|--------|-------|
| `cudaMalloc` / `cudaFree` | PoC target | Basic device memory allocation |
| `cudaMemcpy` (H2D / D2H / D2D) | PoC target | Synchronous copy via ioctl |
| `cudaLaunchKernel` | PoC target | Launch by kernel name (not CUfunction handle) |

## Phasing

- **Phase D-1** (1-2 weeks): Doorbell mmap bypass + Ring buffer skeleton
- **Phase D-2** (2-4 weeks): Minimal CUDA Runtime API surface
- **Phase D-3** (4-8 weeks): ELF + CUBIN parser + Kernel launch by name

## Risks

- **Performance**: PoC will not match real NVIDIA libcuda.so performance
- **Scope creep**: Tempting to expand scope; resist
- **Maintenance burden**: Experimental code can rot if not regularly validated

## References

- AMD ROCm UMD: `amd_aql_queue.cpp:482-493` (doorbell bypass)
- NVIDIA CUDA: libcuda.so architecture (cu* Driver API)
- UsrLinuxEmu roadmap: `docs/roadmap/stage-2-multi-device.md`
