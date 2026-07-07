---
SCOPE: shared
STATUS: ACCEPTED
DATE: 2026-07-07
CHANGE: phase3-real-impl-bridge
RELATED: tadr-301-igpu-driver-contract.md
RELATED: UsrLinuxEmu adr-039-mem-pool-export-ioctl.md
RELATED: UsrLinuxEmu PR phase4-mempool-export-ioctl
---

# TADR-302: IGpuDriver::mem_pool_export_shareable Contract

## Context

Phase 4 shim real-bridge requires `cuMemPoolExportToShareableHandle` to forward
to sim. Prior to this change, IGpuDriver had 46 methods and no export facility,
forcing shim to no-op. UsrLinuxEmu GPU IOCTL range 0x60-0x67 also lacked any
export IOCTL (verified 2026-07-07).

## Decision

Add the 47th virtual method to IGpuDriver:

```cpp
virtual int mem_pool_export_shareable(uint64_t pool_handle,
                                       uint32_t handle_type,
                                       uint32_t flags,
                                       int* fd_out) {
  return -1;  // default stub: NOT_SUPPORTED
}
```

**Semantics**:
- Returns `0` on success with `*fd_out` filled with valid POSIX FD (≥ 0)
- Returns `-1` on failure (invalid pool, unsupported handle_type, sim unavailable)
- `handle_type`: only `CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR=1` supported in Phase 4
- `flags`: must be 0 in Phase 4 (reserved)
- `fd_out`: pointer to int; must not be NULL (caller responsibility)

## Consequences

- All IGpuDriver implementations must override this method (GpuDriverClient / CudaStub / MockGpuDriver)
- UsrLinuxEmu sim must provide `GPU_IOCTL_MEM_POOL_EXPORT` (0x68) IOCTL
- ABI range expands: 0x60-0x67 → 0x60-0x68
- Phase 5+ Win32/Fabric export deferred (returns NOT_SUPPORTED at shim level)
- Backward compatibility: default IGpuDriver implementation returns -1 (NOT_SUPPORTED),
  so stub-mode callers fail gracefully

## Cross-Repo

This TADR depends on UsrLinuxEmu `adr-039-mem-pool-export-ioctl` (forthcoming in
PR `phase4-mempool-export-ioctl`). Both must be merged together to maintain ABI
consistency.

## References

- openspec/changes/phase3-real-impl-bridge/specs/phase3-real-impl-bridge/spec.md
- openspec/changes/phase3-real-impl-bridge/tasks.md §0.B
- UsrLinuxEmu 91ea76c (adds GPU_IOCTL_MEM_POOL_EXPORT 0x68)