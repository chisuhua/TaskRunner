## Context

Phase 4 shim real-bridge (TaskRunner commit `fbcbe44`, PR #8) shipped a hybrid
mode where shim-side cu* APIs delegate to `GpuDriverClient` IOCTLs when
`g_gpu_client != nullptr`. Of the 5 APIs in the Phase 4 scope (per
`tadr-301` §Stability rule 5), 4 have been bridged end-to-end:

| API | Shim | GpuDriverClient forwarder | UsrLinuxEmu IOCTL |
|---|---|---|---|
| `cuGraphLaunch` | real bridge (Phase 4) | `submit_graph` → 0x59 | `sim_graph_launch` |
| `cuMemPoolAlloc` | real bridge (Phase 4) | `mem_pool_alloc` → 0x62 | `sim_mem_pool_alloc` |
| `cuMemPoolAllocAsync` | real bridge (Phase 4) | `mem_pool_alloc_async` → 0x63 | `sim_mem_pool_alloc_async` |
| `cuMemPoolFreeAsync` | real bridge (Phase 4) | `mem_pool_free_async` → 0x64 | `sim_mem_pool_free_async` |
| `cuMemPoolExportToShareableHandle` | real bridge (Phase 4) | **`mem_pool_export_shareable` → 0x68** ❌ TODO stub | `sim_mem_pool_export` (PR #27) |

The gap is in **GpuDriverClient::mem_pool_export_shareable** which was
left as a `return -1; (void)pool_handle; ...` TODO stub in PR #8 (commit
`fbcbe44`). The shim and UsrLinuxEmu sides are both production-ready; only
the client-side forwarder is missing.

Separately, the ADR numbering has a name collision:
- `docs/shared/adr/tadr-302-mempool-export-shareable.md` — mempool contract
- `docs/shared/adr/tadr-302-sync-primitives.md` — sync primitives (H-5 NEW)

Both files exist with the same number; this causes confusion in the
mirror table at `docs/shared/adr/README.md` and breaks cross-repo
references in UsrLinuxEmu `docs/00_adr/README.md` (which already routes
`mempool-export-shareable` → `tadr-305` after PR #8). Renumbering the
mempool ADR to `tadr-305` resolves the collision and aligns with the
mirror state.

## Goals / Non-Goals

**Goals:**
- Replace TODO stub in `GpuDriverClient::mem_pool_export_shareable` with
  real `ioctl(fd_, GPU_IOCTL_MEM_POOL_EXPORT, &args)` forwarder that
  returns the kernel-allocated FD.
- Bring `mem_pool_export_shareable` inline with the other 4 Phase 4
  forwarders in pattern, error reporting, and fence/return semantics.
- Renumber the mempool ADR file from `tadr-302` → `tadr-305`, update
  internal references in `tadr-301` and the shared ADR `README.md`.

**Non-Goals:**
- Adding new test cases (Phase 1.7 backlog covers `test_cu_mem_pool_export`
  extension as a separate change).
- Changing the public `IGpuDriver` signature for `mem_pool_export_shareable`
  (already finalized at 47 methods in commit `fbcbe44`).
- Renaming `tadr-302-sync-primitives.md` — that number is the legitimate
  owner per `tadr-102` lineage; the mempool ADR is the intruder.
- Touching UsrLinuxEmu code (already shipped via PR #27 + mirror).

## Decisions

### D1: Forwarder pattern — reuse `ioctl(fd_, ...)` style (no helper extraction)

**Choice**: Implement the forwarder inline with `ioctl(fd_,
GPU_IOCTL_MEM_POOL_EXPORT, &args)`, matching the existing `free_bo`,
`wait_fence`, and Phase 3.1+3.2 forwarding overrides from PR #7.

**Rationale**: Consistency over DRY. The pattern is the established
convention in this file; deviating would create reviewer friction and
asymmetric code paths. A `submit_ioctl()` helper was explicitly
**removed** during the 2026-07-06 self-review revision per
`openspec/changes/archive/2026-07-06-phase3-step3-shim-and-forwarding/`.

**Alternatives considered**:
- Wrap all forwarders in `submit_ioctl(fd_, cmd, args)` helper →
  rejected by self-review; increases indirection without clarity gain.
- Move the forwarder to `GpuDriverClient.cpp` (out-of-line) →
  rejected; all 15 Phase 3.1+3.2 forwarders are inline header methods.

### D2: Args struct — use `gpu_mem_pool_export_args` (not the lower-level 0x60 range)

**Choice**: Use the dedicated `gpu_mem_pool_export_args` struct (already
defined in UsrLinuxEmu `plugins/gpu_driver/shared/gpu_ioctl.h`) with
fields `pool_handle`, `handle_type`, `flags`, `fd_out`. Read `fd_out`
into `*fd_out` parameter of `mem_pool_export_shareable`.

**Rationale**: The kernel-side IOCTL handler in UsrLinuxEmu PR #27
expects this exact struct shape. Using a different or smaller struct
would cause silent truncation.

### D3: Return value semantics — `0` on success, `-errno` on failure

**Choice**: Return Linux-style negative errno on ioctl failure
(matching `free_bo`, `wait_fence`, `submit_batch`).

**Rationale**: Aligns with `tadr-301` §Stability rules 7-8 (error
handling). No fence_id propagation here (this is a synchronous FD
export; no async path needed at this layer — the FD itself is the
synchronization primitive for FD-passing IPC).

### D4: Stash contents — reuse existing untracked worktree file

**Choice**: Pick up the untracked `tadr-305-mempool-export-shareable.md`
from the existing worktree stash, polish its content if needed, and
commit through this change.

**Rationale**: The file was authored in a prior in-progress session;
re-writing would duplicate effort. The content matches the proposal.

### D5: TADR-301 reference update — change `tadr-302` to `tadr-305` in Phase 4 row only

**Choice**: Only the Phase 4 row of the Method Count Evolution table
references the renamed ADR; the Sync Primitives `tadr-302` reference is
already correct (sync primitives file stays as `tadr-302`). Update
**only** the mempool reference.

**Rationale**: Surgical change; minimize churn.

## Risks / Trade-offs

- **[Risk]** Pre-commit hook `docs-audit.sh` in main repo runs against
  main repo's working tree, not the worktree where this change is
  implemented. If `kNumIoctls` expectation was changed in this change,
  pre-commit hook could fail with `expected 31 (actual 16)` until PR
  #26 merges. → **Mitigation**: This change does not modify
  `gpgpu_device.h` (kNumIoctls). The forwarder is in
  `gpu_driver_client.h` only. Pre-commit hook unaffected.
- **[Risk]** Breaking existing test annotations → **Mitigation**: Only
  1 test annotation update in `test_cu_graph.cpp` (`PoC no-op` →
  `real bridge`); test still passes because behavior matches.
- **[Risk]** Rename could orphan inbound links to
  `tadr-302-mempool-export-shareable.md`. → **Mitigation**: Git rename
  detection (when `git mv` is used) preserves history. New file
  `tadr-305-mempool-export-shareable.md` already authored in worktree
  stash; mirror at UsrLinuxEmu `docs/00_adr/README.md` already updated.
- **[Risk]** `gpu_mem_pool_export_args` struct shape may evolve in
  UsrLinuxEmu → **Mitigation**: Struct is now stable per UsrLinuxEmu
  commit `f315c3e` (PR #27 merged). No further changes expected for
  Phase 4.

## Migration Plan

Single atomic commit on `main` (TaskRunner). No UsrLinuxEmu side
changes required (already shipped). Steps:

1. Apply forwarder implementation to
   `include/test_fixture/gpu_driver_client.h`
2. Rename `tadr-302-mempool-export-shareable.md` → `tadr-305-...`
   via `git mv` (preserves history)
3. Pick up existing untracked `tadr-305-mempool-export-shareable.md`
   from worktree stash (commit it)
4. Update 2 internal references in `tadr-301-igpu-driver-contract.md`
5. Update mirror row in `docs/shared/adr/README.md`
6. Update 1 test annotation in `tests/umd/test_cu_graph.cpp`
7. Commit + push
8. Verify: build (default + ASan/UBSan/TSan) + `ctest` (8 binaries,
   225 cases baseline, no regressions expected)

Rollback: `git revert <commit>` — no DB migration, no shared state,
no protocol change. Single file revert restores previous state.

## Open Questions

None at this time. The mirror state in UsrLinuxEmu already aligns
with the post-rename target, so no cross-repo coordination needed.