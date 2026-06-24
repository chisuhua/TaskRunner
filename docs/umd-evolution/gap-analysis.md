---
SCOPE: UMD-EVOLUTION
STATUS: PROPOSED
IMPLEMENTED: NO
---

# Gap Analysis vs Production UMDs

## vs AMD ROCm UMD (libhsa-runtime64.so + libamdhip64.so)

| Capability | ROCm | TaskRunner | Gap |
|------------|------|------------|-----|
| AQL packet construction | User-space | ❌ None | High (need new AQL packet builder) |
| Doorbell direct MMIO write | User-space (WC mmap) | ❌ None | High (need mmap exposure) |
| Ring buffer self-management | User-space | ❌ None | Medium |
| EOP buffer + CWSR allocation | User-space | ❌ None | Low (deferrable) |
| Built-in blit shader | User-space (compiled at runtime) | ❌ None | Medium |
| SDMA packet construction | User-space | ❌ None | Medium |
| Code object (.hsaco) loading | User-space | ❌ None | High |
| Kernel arg serialization | User-space | ❌ None | Medium |
| Stream object | libhsa stream | ❌ None (uses u32 id) | Medium |
| KFD ioctl communication | libhsakmt (thin wrapper) | ✅ Equivalent (GpuDriverClient) | None |

## vs NVIDIA CUDA UMD (libcuda.so)

| Capability | NVIDIA | TaskRunner | Gap |
|------------|--------|------------|-----|
| PTX JIT compilation | libnvidia-ptxjitcompiler | ❌ None | High (huge lib) |
| Fatbin parser | libnvidia-fatbinaryloader | ❌ None | Medium |
| libnvrtc | nvrtcCompileProgram | ❌ None | High |
| Module loading | cuModuleLoad* | ❌ None | High |
| Kernel launch | cuLaunchKernel (by CUfunction) | ❌ None (uses string name) | High |
| Memory allocation | cuMemAlloc | ⚠️ Partial (BO alloc) | Medium |
| Context (CUcontext) | libcuda state | ❌ None | Medium |
| Stream (CUstream) | libcuda state | ❌ None | Medium |
| UVM | libnvidia-uvm | ⚠️ Partial (VA Space) | Medium |
| MPS support | libcuda built-in | ❌ None | Low (out of scope) |

## Critical Missing Capabilities (Ranked)

1. **CUmodule loading** (libcuda + libamdhip equivalent): FATBIN/ELF parsing, symbol resolution
2. **AQL/Command packet construction**: Hardware-specific packet format
3. **Doorbell MMIO exposure**: mmap doorbell page to user-space
4. **Stream/Context object models**: Replace u32 handles with opaque pointers
5. **Kernel arg serialization**: Marshall args into kernarg buffer
6. **PTX JIT** (NVIDIA only): Requires libnvrtc + libnvidia-ptxjitcompiler integration

## Effort Estimates

- **Phase D-1** (Doorbell bypass + Ring skeleton): 2-4 weeks
- **Phase D-2** (Minimal CUDA API surface): 4-8 weeks
- **Phase D-3** (ELF parser + Kernel launch): 8-12 weeks
- **Total**: 14-24 weeks (~3-6 months)

## Recommendation

**Do NOT pursue UMD evolution as primary goal.** Reasons:
1. UsrLinuxEmu blueprint explicitly defers this work
2. Investment vs ROI is poor (3-6 months for PoC that won't match production libcuda.so)
3. Maintenance burden on small team

**Alternative:** Continue strengthening test-fixture scope (CudaStub + GpuDriverClient + CLI) as primary value-add.
