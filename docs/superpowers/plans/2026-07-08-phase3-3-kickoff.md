# Phase 3.3 Event+Texture Implementation Kickoff Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 启动 Phase 3.3 (Event timing precision + Texture/Surface frontend) multi-week workstream: 创建 openspec change ACTIVE + 创建 worktree + 改 DRAFT plan 状态 DRAFT→ACCEPTED + commit。**不实施 1-3 周 work**（deferred 到后续 session）。

**Architecture:** main repo 注册 openspec change (3 files) + DRAFT plan status update + 1 commit。Worktree 创建于 `.rddf/wt/phase3-3-event-texture/` 用于后续 Phase 3.3 implementation 隔离。Worktree 推到 origin 作为 backup。

**Tech Stack:** Git worktree (per AGENTS.md .rddf/wt convention), OpenSpec workflow

---

## File Structure

| File | Status | Responsibility |
|---|---|---|
| `openspec/changes/phase3-3-event-texture-impl/proposal.md` | Created | Workstream proposal (3.3a + 3.3b scope) |
| `openspec/changes/phase3-3-event-texture-impl/tasks.md` | Created | High-level task list (15-20 tasks, 2 sub-plans) |
| `openspec/changes/phase3-3-event-texture-impl/design.md` | Created | 数据结构 + 决策 + 测试策略 |
| `docs/superpowers/plans/2026-07-05-umd-phase3.3-event-texture.md` | Modified | Status DRAFT → ACCEPTED + kickoff date |
| `.rddf/wt/phase3-3-event-texture/` | Created (git worktree) | 隔离 worktree for Phase 3.3 implementation |

---

## Task 1: Create worktree (per AGENTS.md convention)

**Files:**
- Create: `/workspace/project/UsrLinuxEmu/external/TaskRunner/.rddf/wt/phase3-3-event-texture/`

- [ ] **Step 1.1: Verify no existing worktree at target path**

```bash
ls /workspace/project/UsrLinuxEmu/external/TaskRunner/.rddf/wt/phase3-3-event-texture 2>/dev/null && echo "EXISTS" || echo "NOT_EXISTS"
```

Expected: `NOT_EXISTS`

- [ ] **Step 1.2: Create worktree from main**

```bash
cd /workspace/project/UsrLinuxEmu/external/TaskRunner
git worktree add .rddf/wt/phase3-3-event-texture -b phase3-3-event-texture main
```

Expected: worktree created, branch `phase3-3-event-texture` based on main @ 869bd25

- [ ] **Step 1.3: Verify worktree**

```bash
cd /workspace/project/UsrLinuxEmu/external/TaskRunner
git worktree list
ls .rddf/wt/phase3-3-event-texture/ | head -5
```

Expected: 2 worktrees (main + phase3-3-event-texture), worktree dir has openspec/ + src/ + ...

- [ ] **Step 1.4: Push branch to origin (backup)**

```bash
cd /workspace/project/UsrLinuxEmu/external/TaskRunner/.rddf/wt/phase3-3-event-texture
git push -u origin phase3-3-event-texture
```

Expected: `* [new branch] phase3-3-event-texture -> phase3-3-event-texture`

---

## Task 2: Update DRAFT plan status DRAFT → ACCEPTED

**Files:**
- Modify: `/workspace/project/UsrLinuxEmu/external/TaskRunner/docs/superpowers/plans/2026-07-05-umd-phase3.3-event-texture.md` (header section)

- [ ] **Step 2.1: Read DRAFT plan header**

```bash
head -20 /workspace/project/UsrLinuxEmu/external/TaskRunner/docs/superpowers/plans/2026-07-05-umd-phase3.3-event-texture.md
```

Expected: see `STATUS: DRAFT (active development, awaiting kickoff)` at line 4

- [ ] **Step 2.2: Edit DRAFT → ACCEPTED + kickoff date**

Use Edit tool:
- oldString: `> **STATUS**: DRAFT (active development, awaiting kickoff)`
- newString: `> **STATUS**: ✅ ACCEPTED (2026-07-08, kicked off via worktree phase3-3-event-texture)`

- [ ] **Step 2.3: Verify edit**

```bash
head -6 /workspace/project/UsrLinuxEmu/external/TaskRunner/docs/superpowers/plans/2026-07-05-umd-phase3.3-event-texture.md
```

Expected: `> **STATUS**: ✅ ACCEPTED (2026-07-08, kicked off via worktree phase3-3-event-texture)`

---

## Task 3: Create openspec change files (3 files in main repo)

**Files:**
- Create: `/workspace/project/UsrLinuxEmu/external/TaskRunner/openspec/changes/phase3-3-event-texture-impl/proposal.md`
- Create: `.../tasks.md`
- Create: `.../design.md`

- [ ] **Step 3.1: Create openspec change dir**

```bash
mkdir -p /workspace/project/UsrLinuxEmu/external/TaskRunner/openspec/changes/phase3-3-event-texture-impl
```

- [ ] **Step 3.2: Write proposal.md**

Use Write tool. Content:
- Status: 🔄 PROPOSED (awaiting kickoff)
- Why: DRAFT plan kickoff
- What: 2 sub-plans (3.3a Event 1w + 3.3b Texture 2w)
- Impact: src/umd/libcuda_shim/cu_event.cpp + cu_texref.cpp + cu_array.cpp (new) + tests/umd/test_event_timing.cpp (new) + test_texture_surface.cpp (new)

- [ ] **Step 3.3: Write tasks.md**

Use Write tool. Content:
- Phase 3.3a (5 tasks): EventTable refactor + cuEventCreate fix + cuEventRecord fix + cuEventElapsedTime fix + tests
- Phase 3.3b (5 tasks): ArrayTable + TexRefTable + cuArray* + cuTexRef* + tests

- [ ] **Step 3.4: Write design.md**

Use Write tool. Content:
- §1: Background (DRAFT plan)
- §2: Architecture (shim-only, no UsrLinuxEmu backend dep)
- §3: Phase 3.3a design (EventRecord struct, flag validation)
- §4: Phase 3.3b design (ArrayDescriptor, TexRefTable, virtual backing memory)
- §5: Test strategy (23 + 25 cases)
- §6: Files affected

- [ ] **Step 3.5: Verify files exist**

```bash
ls -la /workspace/project/UsrLinuxEmu/external/TaskRunner/openspec/changes/phase3-3-event-texture-impl/
wc -l /workspace/project/UsrLinuxEmu/external/TaskRunner/openspec/changes/phase3-3-event-texture-impl/*.md
```

Expected: 3 files, ~250+ lines total

---

## Task 4: Commit + push to main

**Files:**
- Stage: 4 files (3 new openspec + 1 modified DRAFT plan)
- Commit: 1 commit on main
- Push: 1 push to origin/main

- [ ] **Step 4.1: Stage changes**

```bash
cd /workspace/project/UsrLinuxEmu/external/TaskRunner
git add docs/superpowers/plans/2026-07-05-umd-phase3.3-event-texture.md
git add openspec/changes/phase3-3-event-texture-impl/
git status -s
```

Expected: 1 M (DRAFT plan) + 3 A (openspec files)

- [ ] **Step 4.2: Verify openspec list shows new change**

```bash
cd /workspace/project/UsrLinuxEmu/external/TaskRunner
openspec list --json
```

Expected: 1 change: `phase3-3-event-texture-impl`

- [ ] **Step 4.3: Commit**

```bash
cd /workspace/project/UsrLinuxEmu/external/TaskRunner
git commit -m "docs(openspec): kick off Phase 3.3 Event+Texture (worktree + openspec change)

Phase 3.3 multi-week workstream kickoff:
- 3.3a (1w): Event timing precision (cuEventCreate/Record/ElapsedTime fix)
- 3.3b (2w): Texture/Surface frontend (cuArray* + cuTexRef* 11 API REAL_IMPL)
- Total: 3 weeks, frontend-only, no UsrLinuxEmu backend dep

Infrastructure:
- openspec/changes/phase3-3-event-texture-impl/ (3 files: proposal/tasks/design)
- worktree .rddf/wt/phase3-3-event-texture (branch phase3-3-event-texture)
- DRAFT plan 2026-07-05-umd-phase3.3-event-texture.md status: DRAFT → ACCEPTED

Ref: docs/superpowers/plans/2026-07-05-umd-phase3.3-event-texture.md (547 lines)
Ref: AGENTS.md 2026-07-06 worktree convention"
```

Expected: `[main <hash>] docs(openspec): kick off Phase 3.3 Event+Texture (worktree + openspec change)`

- [ ] **Step 4.4: Push to origin/main**

```bash
cd /workspace/project/UsrLinuxEmu/external/TaskRunner
git push origin main
```

Expected: `X..Y  main -> main`

---

## Task 5: Final verification

- [ ] **Step 5.1: Verify commit on origin/main**

```bash
cd /workspace/project/UsrLinuxEmu/external/TaskRunner
git log --oneline -3 origin/main
```

Expected: top commit contains "Phase 3.3 Event+Texture"

- [ ] **Step 5.2: Verify worktree exists**

```bash
cd /workspace/project/UsrLinuxEmu/external/TaskRunner
git worktree list
```

Expected: 2 worktrees (main + phase3-3-event-texture)

- [ ] **Step 5.3: Verify openspec change active**

```bash
cd /workspace/project/UsrLinuxEmu/external/TaskRunner
openspec list --json
```

Expected: 1 change: `phase3-3-event-texture-impl` (status: in-progress)

- [ ] **Step 5.4: Verify DRAFT plan status**

```bash
grep "STATUS:" /workspace/project/UsrLinuxEmu/external/TaskRunner/docs/superpowers/plans/2026-07-05-umd-phase3.3-event-texture.md
```

Expected: `STATUS: ✅ ACCEPTED (2026-07-08, kicked off via worktree phase3-3-event-texture)`

---

## Definition of Done

- [ ] Worktree `.rddf/wt/phase3-3-event-texture/` exists, branch `phase3-3-event-texture` based on main @ 869bd25
- [ ] Worktree branch pushed to origin
- [ ] DRAFT plan 2026-07-05 status = ✅ ACCEPTED
- [ ] openspec/changes/phase3-3-event-texture-impl/ has 3 files (proposal, tasks, design)
- [ ] `openspec list` shows 1 active change (phase3-3-event-texture-impl)
- [ ] Commit hash visible in origin/main
- [ ] 5 files changed in commit (3 new openspec + 1 modified DRAFT plan + ...)

## Out of Scope (deferred to future sessions)

- 实际 1-3 周 Phase 3.3 implementation
- PR review + merge
- Cross-repo UsrLinuxEmu sync (no code change yet, only docs)
- Phase 3.3b (Texture) sub-plan execution (Phase 3.3a first)
- Subagent-driven implementation (next session can use subagent-driven-development skill)
