---
SCOPE: shared
STATUS: ACCEPTED
DATE: 2026-07-08
CHANGE: mempool-export-shareable-real-bridge
RELATED: tadr-301-igpu-driver-contract.md
RELATED: UsrLinuxEmu adr-039-mem-pool-export-ioctl.md
RELATED: UsrLinuxEmu PR #27 (merged 2026-07-07, commit f315c3e)
---

# TADR-305: IGpuDriver::mem_pool_export_shareable Contract

## Context

Phase 4 shim real-bridge required `cuMemPoolExportToShareableHandle` to forward
to sim. Prior to this change, IGpuDriver had 46 methods and no export facility,
forcing shim to no-op. UsrLinuxEmu GPU IOCTL range 0x60-0x67 also lacked any
export IOCTL (verified 2026-07-07).

The PR #27 (commit `f315c3e`, merged 2026-07-07) added `GPU_IOCTL_MEM_POOL_EXPORT`
(0x68) on the UsrLinuxEmu side, enabling this contract to be backfilled end-to-end
on the TaskRunner side via `GpuDriverClient::mem_pool_export_shareable`.

## Decision

The 47th virtual method on `IGpuDriver`:

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

**Forwarder behaviour** (`GpuDriverClient::mem_pool_export_shareable`):
- Pre-check `is_open()` → return -1 if device not open
- Pre-check `fd_out != nullptr` → return -EINVAL if null
- Build `gpu_mem_pool_export_args { pool_handle, handle_type, flags, fd_out=0, _pad=0 }`
- Dispatch `ioctl(fd_, GPU_IOCTL_MEM_POOL_EXPORT, &args)`
- On `ioctl < 0`: log `GpuDriverClient: GPU_IOCTL_MEM_POOL_EXPORT failed (errno=...)` to stderr, return -1
- On success: `*fd_out = static_cast<int>(args.fd_out)` and return 0
- `pool_handle == 0` is **not** rejected at this layer; the kernel-side IOCTL handler validates semantics itself (deliberate asymmetry with `mem_pool_create` B-2 sentinel guard).

## Consequences

- All IGpuDriver implementations override this method (GpuDriverClient / CudaStub / MockGpuDriver)
- UsrLinuxEmu sim provides `GPU_IOCTL_MEM_POOL_EXPORT` (0x68) IOCTL (merged via PR #27)
- ABI range expands: 0x60-0x67 → 0x60-0x68
- Phase 5+ Win32/Fabric export deferred (returns NOT_SUPPORTED at shim level)
- Backward compatibility: default IGpuDriver implementation returns -1 (NOT_SUPPORTED),
  so stub-mode callers fail gracefully

## Cross-Repo

This TADR depends on UsrLinuxEmu `adr-039-mem-pool-export-ioctl`, merged via
PR #27 (commit `f315c3e`, 2026-07-07). Both contracts are now in place to
maintain ABI consistency end-to-end.

## References

- openspec/changes/mempool-export-shareable-real-bridge/proposal.md
- openspec/changes/mempool-export-shareable-real-bridge/design.md
- openspec/changes/mempool-export-shareable-real-bridge/specs/mempool-export-shareable-bridge/spec.md
- openspec/changes/mempool-export-shareable-real-bridge/tasks.md
- UsrLinuxEmu f315c3e (PR #27 — adds GPU_IOCTL_MEM_POOL_EXPORT 0x68)
- UsrLinuxEmu 91ea76c (predecessor work — earlier UsrLinuxEmu state before merge)
