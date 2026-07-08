## Why

Phase 4 shim real-bridge (commit `fbcbe44`, PR #8) shipped 4 of 5 cu* API bridges to
GpuDriverClient IOCTLs — `cuMemPoolAllocAsync` / `cuMemPoolFreeAsync` /
`cuMemPoolExportToShareableHandle` were bridged but `mem_pool_export_shareable` in
GpuDriverClient still contains a TODO stub. Additionally, the original
`docs/shared/adr/tadr-302-mempool-export-shareable.md` ships under a misleading
number — `tadr-302` is already used by **Sync Primitives 抽象 (H-5 新增)** per
`docs/shared/adr/README.md`, causing confusion in mirror tables and cross-repo
references.

This change closes both gaps: (1) replaces the TODO stub with a real
`GPU_IOCTL_MEM_POOL_EXPORT` (0x68) forwarder added in UsrLinuxEmu PR #27
(commit `f315c3e`); (2) renumbers the mempool-export-shareable ADR from
`tadr-302` → `tadr-305` to eliminate the namespace collision.

## What Changes

- **GpuDriverClient::mem_pool_export_shareable real bridge**: Replace the
  current TODO stub (`return -1`) with a real `ioctl(fd_,
  GPU_IOCTL_MEM_POOL_EXPORT, &args)` forwarder returning `int fd_out`. Mirrors
  the pattern of `free_bo`, `wait_fence`, and the Phase 3.1+3.2 forwarding
  overrides added in PR #7.
- **TADR renumber `tadr-302-mempool-export-shareable` → `tadr-305-mempool-export-shareable`**:
  Update `docs/shared/adr/README.md` mirror row, internal reference in
  `tadr-301-igpu-driver-contract.md` (Phase 4 row), and the new
  `tadr-305` file content (currently exists as untracked from a prior
  in-progress session — to be committed through this change).
- **No behavior change to existing APIs**: This is purely a rename + TODO
  stub → real bridge upgrade. Sync Primitives (existing tadr-302) is
  untouched.

## Capabilities

### New Capabilities

- `mempool-export-shareable-bridge`: The contract between TaskRunner
  `cuMemPoolExportToShareableHandle` shim, `GpuDriverClient::mem_pool_export_shareable`
  client, and `UsrLinuxEmu GPU_IOCTL_MEM_POOL_EXPORT` (0x68) kernel
  handler. Covers: arg validation, fence_id propagation, error propagation,
  FD-out semantics, integration with `cuMemPoolCreate`/`Alloc` lifecycle.

### Modified Capabilities

- `igpu-driver-contract`: Existing capability (tadr-301) — its Phase 4 row
  currently references the wrong tadr-302 (Sync Primitives). Update reference
  to point to `tadr-305`. No other requirement changes.

## Impact

**Affected code (TaskRunner)**:
- `include/test_fixture/gpu_driver_client.h` — implement
  `mem_pool_export_shareable` (currently TODO stub, ~12 lines)
- `docs/shared/adr/README.md` — replace tadr-302 mirror row with tadr-305
- `docs/shared/adr/tadr-301-igpu-driver-contract.md` — 2 reference updates
  (Phase 4 Context row + bottom References line)
- `docs/shared/adr/tadr-302-mempool-export-shareable.md` — DELETE (renumbered)
- `docs/shared/adr/tadr-305-mempool-export-shareable.md` — NEW (~60 lines)

**Affected code (UsrLinuxEmu)**: None. `GPU_IOCTL_MEM_POOL_EXPORT` (0x68)
shipped in PR #27 (`f315c3e`); mirror row `tadr-305` already in
`docs/00_adr/README.md`. Coordination note in PR description.

**Affected tests**: 0 net change in count. `test_cu_graph` continues to
cover `cuGraphLaunch` real bridge path (annotation kept unchanged from
pre-change state; tightened to "real bridge" would be a separate
follow-up change).

**Test baseline impact**: 270 → 270 (no new cases — the export bridge path
was untested pre-change; new coverage tests are out of scope for this
incremental rename+upgrade change. Follow-up: `phase-1-7-test-coverage`
backlog item will add ~5 export cases per `test_cu_mem_pool_export`
extension).

**Risk assessment**: Low. No API signature changes. No new ioctl ranges.
The change unifies TADR numbering across shared adr directory and removes
a dead-code path.