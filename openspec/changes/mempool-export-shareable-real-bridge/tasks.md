# Tasks: mempool-export-shareable-real-bridge

> **Status**: 🔄 PROPOSED（2026-07-08, change proposal accepted; awaiting apply）
> **Scope**: 5 files (1 modified `.h`, 1 mirror row, 2 reference updates, 1 file rename + 1 new file)
> **Source**: in-progress stash `stash@{0}` of TaskRunner main repo (carry forward)
> **Cross-repo note**: UsrLinuxEmu side already shipped via PR #27 (`f315c3e`); mirror row already updated.

## 1. Pick up stash + apply forwarder (Workstream 1)

> **Goal**: Replace TODO stub in `mem_pool_export_shareable` with real IOCTL forwarder. Pull in the stashed in-progress work.

- [ ] 1.1 Pop stash `stash@{0}` from TaskRunner main worktree (carry forward `tadr-302 → tadr-305` rename work + `mem_pool_export_shareable` forwarder). Resolve any conflicts with current main (commit `80d4596` is the latest).
- [ ] 1.2 Verify `include/test_fixture/gpu_driver_client.h::mem_pool_export_shareable` body matches design §D1-D3 (inline `ioctl(fd_, GPU_IOCTL_MEM_POOL_EXPORT, &args)`, `is_open()` + `fd_out` guards, stderr log on ioctl failure, `args.fd_out` → `*fd_out`, return 0 / -1).
- [ ] 1.3 Run `cmake --build build -j4` to verify forwarder compiles (expect 0 warnings, 0 errors). Compare against the existing 4 forwarders (e.g., `mem_pool_alloc`) for pattern consistency.

## 2. TADR renumber (Workstream 2)

> **Goal**: Rename `tadr-302-mempool-export-shareable.md` → `tadr-305-...` and update references. Resolves namespace collision with `tadr-302-sync-primitives.md`.

- [ ] 2.1 `git mv docs/shared/adr/tadr-302-mempool-export-shareable.md docs/shared/adr/tadr-305-mempool-export-shareable.md` (preserve history via git rename detection).
- [ ] 2.2 Verify the new `tadr-305-mempool-export-shareable.md` content has the renumbered references inside (already drafted in stash; sanity check `RELATED:` line and any in-text `tadr-302` references).
- [ ] 2.3 Update `docs/shared/adr/README.md` — change the tadr-302 row for mempool to tadr-305 (description: `IGpuDriver::memPoolExportShareable 契约 (Phase 4 新增 47th 方法)`, related: `tadr-301`). Confirm no row remains for `tadr-302-mempool-export-shareable`.
- [ ] 2.4 Update `docs/shared/adr/tadr-301-igpu-driver-contract.md` — change 2 references from `tadr-302` → `tadr-305`:
  - The Phase 4 row in Method Count Evolution table: `per tadr-302` → `per tadr-305`
  - The References section at the bottom: `Phase 4 tadr-302 (mem_pool_export_shareable contract, 47-method expansion)` → `Phase 4 tadr-305 (mem_pool_export_shareable contract, 47-method expansion)`

## 3. Verification (Workstream 3)

> **Goal**: Prove the change works end-to-end before commit/push.

- [ ] 3.1 `cmake --build build -j4` — expect 0 warnings, 0 errors.
- [ ] 3.2 `cd build && ctest -j4` — expect 8/8 test binaries pass, 270/270 cases pass (no regressions, no new cases).
- [ ] 3.3 `cmake --build build_asan -j4 && ctest --test-dir build_asan -j4` — expect 8/8 pass, 0 ASan errors.
- [ ] 3.4 `cmake --build build_ubsan -j4 && ctest --test-dir build_ubsan -j4` — expect 8/8 pass, 0 UBSan errors.
- [ ] 3.5 `cmake --build build_tsan -j4 && ctest --test-dir build_tsan -j4` — expect 8/8 pass, 0 TSan errors.
- [ ] 3.6 `bash tools/docs-audit.sh --strict` (with `SKIP_DOCS_AUDIT=1` if main repo `kNumIoctls = 16` triggers pre-existing false-positive) — expect PASS in worktree.
- [ ] 3.7 `git diff --cached --check` — verify rename detection shows 100% similarity on `tadr-302 → tadr-305`.

## 4. Commit + push (Workstream 4)

> **Goal**: Single atomic commit on `main` (TaskRunner).

- [ ] 4.1 `git status` — expect 3 modified + 1 new file + 1 deleted file (rename):
  - `M  include/test_fixture/gpu_driver_client.h`
  - `M  docs/shared/adr/README.md`
  - `M  docs/shared/adr/tadr-301-igpu-driver-contract.md`
  - `D  docs/shared/adr/tadr-302-mempool-export-shareable.md`
  - `?? docs/shared/adr/tadr-305-mempool-export-shareable.md`
- [ ] 4.2 Stage all changes; `git status -s` clean except those 5 lines.
- [ ] 4.3 `git commit -m "feat(gpu-driver-client): implement mem_pool_export_shareable real bridge + rename tadr-302 → tadr-305

Phase 4 shim real-bridge (commit fbcbe44, PR #8) shipped 4 of 5 cu* API bridges.
The 5th — cuMemPoolExportToShareableHandle → mem_pool_export_shareable — was
left as a TODO stub because UsrLinuxEmu's GPU_IOCTL_MEM_POOL_EXPORT (0x68)
hadn't merged yet. Now that UsrLinuxEmu PR #27 (commit f315c3e) has merged,
this commit closes the final bridge.

Also renumbers docs/shared/adr/tadr-302-mempool-export-shareable.md →
tadr-305-mempool-export-shareable.md to resolve a number collision with
tadr-302-sync-primitives.md. Updates the mirror row in docs/shared/adr/README.md
and 2 internal references in tadr-301-igpu-driver-contract.md.

Changes (5 files, 1 atomic commit):
- include/test_fixture/gpu_driver_client.h: real ioctl(fd_,
  GPU_IOCTL_MEM_POOL_EXPORT, &args) forwarder with is_open() + fd_out guards
  + stderr log on failure (replaces TODO stub returning -1)
- docs/shared/adr/tadr-302-mempool-export-shareable.md → tadr-305 (git mv)
- docs/shared/adr/README.md: mirror row updated
- docs/shared/adr/tadr-301-igpu-driver-contract.md: 2 references updated

Refs: chisuhua/UsrLinuxEmu#27 (GPU_IOCTL_MEM_POOL_EXPORT added, MERGED 2026-07-07)
Refs: TaskRunner commit fbcbe44 (Phase 4 base, 4 of 5 bridges shipped)
Refs: openspec/changes/mempool-export-shareable-real-bridge/ (this change)"`
- [ ] 4.4 `git push origin main` (or `git push origin <feature-branch>` if user wants a PR workflow).
- [ ] 4.5 Verify commit on `origin/main` via `git log --oneline origin/main -3`.

## 5. Post-commit verification (Workstream 5)

- [ ] 5.1 Check TaskRunner CI: `gh run list --workflow shim.yml --limit 3` — expect SUCCESS (CI was previously broken by missing UsrLinuxEmu symlink; the 3de3f01 commit `ci(shim): fix dangling UsrLinuxEmu symlink` was the fix).
- [ ] 5.2 Confirm no cross-repo bump needed in UsrLinuxEmu — this change does not modify `external/TaskRunner` pointer-relevant content (no `UsrLinuxEmu` IOCTL struct changes, no `IGpuDriver` signature changes). The mirror row in UsrLinuxEmu `docs/00_adr/README.md` already lists `tadr-305` (post-PR #8).
- [ ] 5.3 Optional: Notify UsrLinuxEmu owner (no action required; informational).

## Acceptance Criteria (Definition of Done)

- [ ] Workstream 1-4 atomic steps completed; 5 files changed in one commit
- [ ] All 4 sanitizers (default + ASan + UBSan + TSan) pass 8/8 tests each
- [ ] 270/270 test cases pass (no regression, no new cases — this is a rename + stub upgrade, not new functionality)
- [ ] `git diff HEAD~1 --stat` shows: 3 modified + 1 renamed (100%) + 1 new file
- [ ] `git log --oneline -1` shows the new commit with the message above
- [ ] TaskRunner CI passes (4 sanitizers + build-and-test)
- [ ] UsrLinuxEmu side untouched (no submodule bump needed)

## Timeline

```
Hour 0-0.5:   Workstream 1 (stash pop + forwarder sanity)
Hour 0.5-1:   Workstream 2 (TADR renumber)
Hour 1-1.5:   Workstream 3 (full verification with 4 sanitizers)
Hour 1.5-2:   Workstream 4 (commit + push)
Hour 2-2.5:   Workstream 5 (post-commit CI verification)
```

**Total estimated effort**: 2.5 hours (including CI wait)
