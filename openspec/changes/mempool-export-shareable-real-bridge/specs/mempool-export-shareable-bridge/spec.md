---
SCOPE: shared + test-fixture
STATUS: PROPOSED
DATE: 2026-07-08
CHANGE: mempool-export-shareable-real-bridge
RELATED: tadr-301-igpu-driver-contract.md, tadr-305-mempool-export-shareable.md
RELATED_UPSTREAM: chisuhua/UsrLinuxEmu PR #27 (f315c3e, MERGED 2026-07-07) — added GPU_IOCTL_MEM_POOL_EXPORT (0x68)
RELATED: openspec/changes/archive/2026-07-07-phase3-real-impl-bridge/ (Phase 4 base, commit fbcbe44)
---

# Capability: mempool-export-shareable-bridge

> End-to-end bridge contract for `cuMemPoolExportToShareableHandle`:
> TaskRunner shim → GpuDriverClient client → UsrLinuxEmu `GPU_IOCTL_MEM_POOL_EXPORT` (0x68) kernel handler.
> Completes the 5-API Phase 4 hybrid bridge set (per `tadr-301` Stability rule 5).

## ADDED Requirements

### Requirement: GpuDriverClient::mem_pool_export_shareable forwards to GPU_IOCTL_MEM_POOL_EXPORT

The system MUST provide `GpuDriverClient::mem_pool_export_shareable` as an
inline override of the `IGpuDriver::mem_pool_export_shareable` virtual method.
The implementation MUST dispatch `ioctl(fd_, GPU_IOCTL_MEM_POOL_EXPORT, &args)`
with a properly initialized `gpu_mem_pool_export_args` struct when `fd_` is
open and `fd_out` is non-null.

#### Scenario: Valid export with POSIX_FD handle type
- **WHEN** `mem_pool_export_shareable(pool_handle, CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR, flags, &fd_out)` is called
- **AND** `pool_handle != 0` and `fd_` is open
- **THEN** `mem_pool_export_shareable` MUST populate `args.pool_handle = pool_handle`, `args.handle_type = handle_type`, `args.flags = flags`
- **AND** MUST invoke `ioctl(fd_, GPU_IOCTL_MEM_POOL_EXPORT, &args)`
- **AND** on `ioctl` success (return 0), MUST write `args.fd_out` into `*fd_out` and return 0
- **AND** on `ioctl` failure (return < 0), MUST log to stderr (matching the
  `free_bo` / `wait_fence` / Phase 3.1+3.2 forwarding pattern) and return -1

#### Scenario: Device not open guard
- **WHEN** `mem_pool_export_shareable(pool_handle, handle_type, flags, &fd_out)` is called
- **AND** `GpuDriverClient::is_open()` returns false
- **THEN** the implementation MUST return -1 without invoking `ioctl`

#### Scenario: NULL fd_out pointer guard
- **WHEN** `mem_pool_export_shareable(pool_handle, handle_type, flags, nullptr)` is called
- **THEN** the implementation MUST return -EINVAL without invoking `ioctl`

### Requirement: No pool_handle sentinel guard

The system MUST NOT enforce a `pool_handle == 0` reject on
`mem_pool_export_shareable`, because `0` is a legitimate pool handle in
some pre-allocation flows and the IOCTL handler validates semantics
itself. This deviates from the `mem_pool_create` B-2 sentinel guard
(intentional, documented).

#### Scenario: pool_handle == 0 still attempts ioctl
- **WHEN** `mem_pool_export_shareable(0, handle_type, flags, &fd_out)` is called
- **AND** `fd_` is open
- **THEN** the implementation MUST invoke `ioctl(fd_, GPU_IOCTL_MEM_POOL_EXPORT, &args)` with `args.pool_handle = 0`
- **AND** MUST propagate the kernel-side `ioctl` return code (success → return 0 + write `*fd_out`; failure → return -1 + log)

### Requirement: TADR renumber tadr-302 → tadr-305 for mempool-export-shareable

The shared ADR file `docs/shared/adr/tadr-302-mempool-export-shareable.md`
MUST be renamed to `docs/shared/adr/tadr-305-mempool-export-shareable.md`
to resolve a number collision with the existing
`docs/shared/adr/tadr-302-sync-primitives.md`. The renumbering MUST
preserve file history via `git mv`.

#### Scenario: File renamed with git mv
- **WHEN** `git mv docs/shared/adr/tadr-302-mempool-export-shareable.md docs/shared/adr/tadr-305-mempool-export-shareable.md` is executed
- **THEN** git MUST record this as a 100% rename in the diff
- **AND** the new file content MUST be functionally equivalent to the original (renumber references inside)

#### Scenario: Mirror row updated in docs/shared/adr/README.md
- **WHEN** the renumbering is complete
- **THEN** `docs/shared/adr/README.md` MUST contain a row with `tadr-305`
  pointing to the new file
- **AND** MUST NOT contain a row for `tadr-302-mempool-export-shareable`

#### Scenario: tadr-301 internal reference updated
- **WHEN** the renumbering is complete
- **THEN** `docs/shared/adr/tadr-301-igpu-driver-contract.md` Phase 4 row
  in the Method Count Evolution table MUST reference `tadr-305` (not `tadr-302`)
- **AND** the bottom "Phase 4 tadr-305" reference line MUST use the new number

## REMOVED Requirements

### Requirement: GpuDriverClient::mem_pool_export_shareable TODO stub
**Reason**: The previous implementation returned -1 with `(void)` annotations on all parameters, with a TODO comment referencing "Phase 4: replace with sim_mem_pool_export_shareable call once UsrLinuxEmu 91ea76c is merged". UsrLinuxEmu 91ea76c has since merged via PR #27 (commit f315c3e), enabling real bridge.

**Migration**: This requirement is replaced by the new
"GpuDriverClient::mem_pool_export_shareable forwards to GPU_IOCTL_MEM_POOL_EXPORT"
requirement above. No downstream consumers need migration — the public
`IGpuDriver::mem_pool_export_shareable` signature is unchanged.