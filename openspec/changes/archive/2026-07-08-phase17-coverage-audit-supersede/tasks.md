# Tasks: phase17-coverage-audit-supersede

> **状态**: 🔄 PROPOSED（2026-07-08, awaiting apply）
> **类型**: 文档 housekeeping (3 file changes: 1 new openspec change + 1 sync-plan edit + 1 mv)
> **Scope**: 4 files (3 in new openspec change + 1 sync-plan.md edit)

## 1. OpenSpec change 文件创建 (Workstream 1)

> **Goal**: 写 3 个文件记录 audit 报告

- [x] **1.1** 创建 `openspec/changes/phase17-coverage-audit-supersede/proposal.md` (audit 报告 + Why/What/Impact)
- [x] **1.2** 创建 `openspec/changes/phase17-coverage-audit-supersede/tasks.md` (本文档)
- [x] **1.3** 创建 `openspec/changes/phase17-coverage-audit-supersede/design.md` (旧 archive vs 当前 audit 详情)

## 2. sync-plan.md 状态更新 (Workstream 2)

> **Goal**: §5.3 row 259 status PROPOSED → Done (superseded)

- [x] **2.1** Edit `plans/sync-plan.md` line 259:
  - Before: `| **Phase 1.7 test coverage** | 25-30 E2E tests (REAL_IMPL 50.5%→≥85%) | 🟢 可立即开始 (独立) | — | PROPOSED |`
  - After:  `| **Phase 1.7 test coverage** | (superseded by Phase 3.1+3.2+4) | ✅ Done 2026-07-08 | — | Done |`
- [x] **2.2** Edit `plans/sync-plan.md` line 325 footer:
  - Before: `**最后更新**: 2026-07-07（Phase 4 real-impl-bridge, v2.4 新增 §1.5 + tadr-301/302）`
  - After:  `**最后更新**: 2026-07-08（Phase 1.7 close via audit-supersede; v2.4.1 housekeeping）`
- [x] **2.3** Edit `plans/sync-plan.md` line 326 next-review (optional):
  - Before: `**下次审查**: Phase 4 submodule bump 时`
  - After:  `**下次审查**: Phase 3.3 Event+Texture 启动时`

## 3. Archive move + commit + push (Workstream 3)

> **Goal**: mv to archive + commit + push

- [x] **3.1** `mv openspec/changes/phase17-coverage-audit-supersede openspec/changes/archive/2026-07-08-phase17-coverage-audit-supersede`
- [x] **3.2** Verify rename detection: `git add -A && git diff --staged -M --name-status`
  - Expected: 3 R100 (proposal/tasks/design) + sync-plan.md modified
- [x] **3.3** `git commit -m "docs(sync-plan): mark Phase 1.7 superseded by Phase 3.x (2026-07-08 audit)"`
- [x] **3.4** `git push origin main`
- [x] **3.5** Verify `openspec list --json` returns `{"changes":[]}`

## 验收准则 (Definition of Done)

- [x] sync-plan.md §5.3 row 259 状态 = ✅ Done
- [x] sync-plan.md footer last_updated = 2026-07-08
- [x] openspec/changes/phase17-coverage-audit-supersede/ 不在 active list
- [x] openspec/changes/archive/2026-07-08-phase17-coverage-audit-supersede/ 存在
- [x] Commit hash visible in origin/main
- [x] openspec list shows `{"changes":[]}`

## Refs

- Proposal: `proposal.md`
- Design: `design.md`
- 旧 archive (superseded): `archive/2026-07-02-phase17-test-coverage-completion/`
