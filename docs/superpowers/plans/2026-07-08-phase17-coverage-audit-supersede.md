# Phase 1.7 Close Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close Phase 1.7 (test coverage completion) openspec change as superseded by Phase 3.1+3.2+4 work, via documentation housekeeping: tick tasks, edit sync-plan, archive, commit, push.

**Architecture:** 1 new openspec change directory (3 files) → modify sync-plan.md (2 line changes) → mv to archive → git commit + push. No code changes, no new tests, no UsrLinuxEmu cross-repo impact.

**Tech Stack:** Git, OpenSpec workflow (housekeeping pattern), sync-plan.md v2.4.1

---

## File Structure

| File | Status | Responsibility |
|---|---|---|
| `openspec/changes/phase17-coverage-audit-supersede/proposal.md` | Created | Audit report + Why/What/Impact |
| `openspec/changes/phase17-coverage-audit-supersede/tasks.md` | Created | Task breakdown with all boxes ticked |
| `openspec/changes/phase17-coverage-audit-supersede/design.md` | Created | Design rationale + verification steps |
| `plans/sync-plan.md` | Modified | §5.3 row 259 status + footer last_updated |
| `openspec/changes/archive/2026-07-08-phase17-coverage-audit-supersede/` | Created (via mv) | Archived location post-apply |

---

## Task 1: Edit sync-plan.md (status row + footer)

**Files:**
- Modify: `/workspace/project/UsrLinuxEmu/external/TaskRunner/plans/sync-plan.md` (line 259 + line 325)

- [ ] **Step 1.1: Read current sync-plan.md around line 259**

```bash
sed -n '257,261p' /workspace/project/UsrLinuxEmu/external/TaskRunner/plans/sync-plan.md
```

Expected: see `| **Phase 1.7 test coverage** | 25-30 E2E tests (REAL_IMPL 50.5%→≥85%) | 🟢 可立即开始 (独立) | — | PROPOSED |`

- [ ] **Step 1.2: Edit row 259**

Use Edit tool:
- oldString: `| **Phase 1.7 test coverage** | 25-30 E2E tests (REAL_IMPL 50.5%→≥85%) | 🟢 可立即开始 (独立) | — | PROPOSED |`
- newString: `| **Phase 1.7 test coverage** | (superseded by Phase 3.1+3.2+4) | ✅ Done 2026-07-08 | — | Done |`

- [ ] **Step 1.3: Read current sync-plan.md footer (line 325)**

```bash
sed -n '323,327p' /workspace/project/UsrLinuxEmu/external/TaskRunner/plans/sync-plan.md
```

Expected: see `**最后更新**: 2026-07-07（Phase 4 real-impl-bridge, v2.4 新增 §1.5 + tadr-301/302）`

- [ ] **Step 1.4: Edit footer line 325**

Use Edit tool:
- oldString: `**最后更新**: 2026-07-07（Phase 4 real-impl-bridge, v2.4 新增 §1.5 + tadr-301/302）`
- newString: `**最后更新**: 2026-07-08（Phase 1.7 close via audit-supersede; v2.4.1 housekeeping）`

- [ ] **Step 1.5: Verify edits**

```bash
grep -n "Phase 1.7 test coverage" /workspace/project/UsrLinuxEmu/external/TaskRunner/plans/sync-plan.md
grep -n "最后更新" /workspace/project/UsrLinuxEmu/external/TaskRunner/plans/sync-plan.md
```

Expected:
- Row 259 contains `✅ Done 2026-07-08`
- Footer contains `2026-07-08`

---

## Task 2: Tick all 12 tasks in tasks.md (project convention)

**Files:**
- Modify: `/workspace/project/UsrLinuxEmu/external/TaskRunner/openspec/changes/phase17-coverage-audit-supersede/tasks.md`

- [ ] **Step 2.1: Verify task count**

```bash
grep -c "^- \[ \]" /workspace/project/UsrLinuxEmu/external/TaskRunner/openspec/changes/phase17-coverage-audit-supersede/tasks.md
grep -c "^- \[x\]" /workspace/project/UsrLinuxEmu/external/TaskRunner/openspec/changes/phase17-coverage-audit-supersede/tasks.md
```

Expected: unchecked=12, checked=0

- [ ] **Step 2.2: Replace all `- [ ] ` with `- [x] `**

Use Edit tool with replaceAll=true:
- oldString: `- [ ] `
- newString: `- [x] `

- [ ] **Step 2.3: Verify tick**

```bash
grep -c "^- \[ \]" /workspace/project/UsrLinuxEmu/external/TaskRunner/openspec/changes/phase17-coverage-audit-supersede/tasks.md
grep -c "^- \[x\]" /workspace/project/UsrLinuxEmu/external/TaskRunner/openspec/changes/phase17-coverage-audit-supersede/tasks.md
```

Expected: unchecked=0, checked=12

---

## Task 3: Archive move + git commit + push

**Files:**
- Move: `openspec/changes/phase17-coverage-audit-supersede/` → `openspec/changes/archive/2026-07-08-phase17-coverage-audit-supersede/`
- Stage: 4 files (3 R100 from rename + 1 modified sync-plan.md)
- Commit: 1 new commit on main
- Push: 1 push to origin/main

- [ ] **Step 3.1: Ensure archive dir exists + check target doesn't exist**

```bash
cd /workspace/project/UsrLinuxEmu/external/TaskRunner
mkdir -p openspec/changes/archive
ls openspec/changes/archive/2026-07-08-phase17-coverage-audit-supersede/ 2>/dev/null && echo "TARGET_EXISTS" || echo "TARGET_NOT_EXIST"
```

Expected: `TARGET_NOT_EXIST`

- [ ] **Step 3.2: mv changeRoot to archive**

```bash
cd /workspace/project/UsrLinuxEmu/external/TaskRunner
mv openspec/changes/phase17-coverage-audit-supersede \
   openspec/changes/archive/2026-07-08-phase17-coverage-audit-supersede
echo "mv_exit=$?"
```

Expected: `mv_exit=0`

- [ ] **Step 3.3: Stage + verify rename detection**

```bash
cd /workspace/project/UsrLinuxEmu/external/TaskRunner
git add -A
git diff --staged -M --name-status
```

Expected: 3 R100 (proposal.md, tasks.md, design.md) + 1 M (sync-plan.md)

- [ ] **Step 3.4: Verify no other active changes**

```bash
cd /workspace/project/UsrLinuxEmu/external/TaskRunner
openspec list --json
```

Expected: `{"changes":[]}`

- [ ] **Step 3.5: Commit**

```bash
cd /workspace/project/UsrLinuxEmu/external/TaskRunner
git commit -m "docs(sync-plan): mark Phase 1.7 superseded by Phase 3.x (2026-07-08 audit)

Phase 1.7 (test coverage completion) openspec change archived via audit-supersede.
The 26 test cases planned in archive/2026-07-02-phase17-test-coverage-completion/
were actually delivered via Phase 3.1+3.2 (PR #7) + Phase 4 (PR #8 + 2595f16) work:
- test_cuda_shim.cpp: 69 → 103 TEST_CASE (+34, 旧 plan 目标 95+ 已超越)
- 全测试套件: 134 → 270/270 pass (+136)
- REAL_IMPL: 91 → 113 (+22)
- STUB: 53 → 45 (-8)

sync-plan §5.3 row 259 status: 🟢 PROPOSED → ✅ Done (superseded)
sync-plan footer last_updated: 2026-07-07 → 2026-07-08

Ref: openspec/changes/phase17-coverage-audit-supersede/
Ref: archive/2026-07-02-phase17-test-coverage-completion/ (superseded)"
```

Expected: commit created, `[main <hash>] docs(sync-plan): mark Phase 1.7 superseded...`

- [ ] **Step 3.6: Push to origin/main**

```bash
cd /workspace/project/UsrLinuxEmu/external/TaskRunner
git push origin main
```

Expected: `X..Y  main -> main` (no errors)

---

## Task 4: Final verification (post-push)

- [ ] **Step 4.1: Verify commit on origin/main**

```bash
cd /workspace/project/UsrLinuxEmu/external/TaskRunner
git log --oneline -3 origin/main
```

Expected: top commit contains "Phase 1.7 superseded"

- [ ] **Step 4.2: Verify remote HEAD matches**

```bash
cd /workspace/project/UsrLinuxEmu/external/TaskRunner
git ls-remote origin main
```

Expected: hash matches local `git rev-parse origin/main`

- [ ] **Step 4.3: Verify sync-plan.md changes are live**

```bash
curl -s https://raw.githubusercontent.com/chisuhua/TaskRunner/main/plans/sync-plan.md | grep -A 0 "Phase 1.7 test coverage" | head -1
```

Expected: `| **Phase 1.7 test coverage** | (superseded by Phase 3.1+3.2+4) | ✅ Done 2026-07-08 | — | Done |`

---

## Definition of Done

- [ ] sync-plan.md §5.3 row 259 状态 = ✅ Done
- [ ] sync-plan.md footer last_updated = 2026-07-08
- [ ] openspec/changes/phase17-coverage-audit-supersede/ 不在 active list
- [ ] openspec/changes/archive/2026-07-08-phase17-coverage-audit-supersede/ 存在 (3 files)
- [ ] Commit hash visible in origin/main
- [ ] `openspec list --json` returns `{"changes":[]}`
- [ ] No UsrLinuxEmu cross-repo changes (TaskRunner 本地 housekeeping)
- [ ] No code changes (仅 docs housekeeping)
- [ ] No test changes
