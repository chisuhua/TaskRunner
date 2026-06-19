# H-1 Closeout Rebase 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把分支 `h1-pushbuffer-validation-closeout` 的唯一 commit (ff52e64) 干净地 rebase 到 `main` 之上，使 H-1 closeout 与 Phase 1.5 (S3.5 fence_id + 便捷方法扩展) 共存；同时修复两个分支都存在的 `UsrLinuxEmu` 符号链接断裂 bug。

**Architecture:**
- 拆成 3 个 commit：(1) 符号链接修复 (2) AGENTS.md Issue #13 标记 (3) rebase 后的 H-1 closeout
- Rebase 走 `git rebase main`，预期 0 冲突（基于 hunk 位置分析）
- Rebase 后做本地 build + 跑 `test_cuda_scheduler` 验证 ABI/行为
- 验证通过后再 `push --force-with-lease` 并创建 PR

**Tech Stack:** git 2.x · CMake · C++17 · doctest · UsrLinuxEmu (submodule-style 符号链接)

---

## 当前状态快照（pre-flight）

| 项 | 值 |
|---|---|
| 当前分支 | `h1-pushbuffer-validation-closeout` @ `ff52e64` |
| 远程同步 | `origin/h1-pushbuffer-validation-closeout` (已同步) |
| 主线 | `main` @ `a7f4463` (领先 H-1 的父 `f782535` 3 个 commit) |
| 工作区改动 | `AGENTS.md` (8 行) · `UsrLinuxEmu` 符号链接 (路径修复) |
| 已知风险 | 符号链接修复已在 main 也是坏的，需独立 commit |

## 风险登记 (Risk Register)

| ID | 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|---|
| R1 | Rebase 在 `include/gpu_driver_client.h` 出现 hunk 冲突 | 低 | 中 | 已分析 hunk 上下文在 main 中逐字保留，预期 0 冲突；若冲突，按本计划 §Conflict Resolution 处理 |
| R2 | Rebase 在 `plans/sync-plan.md` 冲突 | 极低 | 低 | H-1 改 §3.4 S3.1 (line 138) · main 改 §3.5 (line 223+)，物理隔离 |
| R3 | Build 失败（CMake 找不到 UsrLinuxEmu 头） | 低 | 高 | 符号链接修复在 rebase 之前提交，build 时符号链接已正确 |
| R4 | 测试失败 | 中 | 高 | Rebase 后跑 `test_cuda_scheduler`；若失败，立即 `git rebase --abort` 排查 |
| R5 | `--force-with-lease` 推送覆盖他人提交 | 极低 | 高 | 推送前再次 `git fetch` + 检查 `origin/h1-pushbuffer-validation-closeout` 未变 |
| R6 | 符号链接修复本身冲突（main 已有人修过） | 极低 | 低 | Rebase 时 Git 会自动处理；若冲突选 ours 即可 |

## 文件地图 (File Map)

| 文件 | 动作 | 所属 commit | 责任 |
|---|---|---|---|
| `UsrLinuxEmu` (symlink) | 修复目标 `../UsrLinuxEmu/` → `../../` | Commit 1 (独立) | 修复 main / H-1 共同 bug |
| `AGENTS.md` | 标记 Issue #13 已修复 | Commit 2 (独立) | 文档同步 |
| `include/gpu_driver_client.h` | 接受 rebase 后的合并 | Commit 3 (rebase) | H-1 closeout 主代码 |
| `plans/sync-plan.md` | 接受 rebase 后的合并 | Commit 3 (rebase) | H-1 同步点标记 |

---

## Task 1: Pre-flight 状态检查

**Files:** (无修改)

- [ ] **Step 1.1: 确认当前分支与上游同步**

```bash
cd /workspace/project/UsrLinuxEmu/external/TaskRunner
git rev-parse --abbrev-ref HEAD
git status --short
```

预期输出：
- 分支名: `h1-pushbuffer-validation-closeout`
- `git status --short` 仅显示 `M AGENTS.md` 和 `M UsrLinuxEmu`

- [ ] **Step 1.2: 确认 UsrLinuxEmu main 已就绪 H-1 ABI**

```bash
cd /workspace/project/UsrLinuxEmu
git rev-parse --abbrev-ref HEAD
git log --oneline -1
git log --oneline --grep="va_space_handle\|H-1\|h1-closeout" -5
```

预期输出：
- HEAD 在 `main` 分支
- 包含 `0272970 feat(gpu-ioctl): extend gpu_pushbuffer_args with va_space_handle field`

- [ ] **Step 1.3: 确认 GPU 头文件中存在 `va_space_handle` 字段**

```bash
grep -n "va_space_handle" /workspace/project/UsrLinuxEmu/plugins/gpu_driver/shared/gpu_ioctl.h | head -3
```

预期输出：至少 3 行匹配（line 61 在 `gpu_pushbuffer_args`，line 171/187/206 在其他 args）。

- [ ] **Step 1.4: 创建本地备份 ref（防 force-push 误操作）**

```bash
cd /workspace/project/UsrLinuxEmu/external/TaskRunner
git tag backup/h1-pre-rebase-$(date +%Y%m%d-%H%M) ff52e64
git tag -l "backup/h1-pre-rebase-*"
```

预期输出：刚创建的 tag 出现在列表中。

---

## Task 2: 提交符号链接修复（独立 commit）

**Files:**
- Modify: `UsrLinuxEmu` (symlink 目标: `../UsrLinuxEmu/` → `../../`)

- [ ] **Step 2.1: 验证工作区符号链接已正确**

```bash
readlink UsrLinuxEmu && readlink -f UsrLinuxEmu
```

预期输出：
- 第一行: `../../`
- 第二行: `/workspace/project/UsrLinuxEmu`

- [ ] **Step 2.2: 检查目标目录可访问**

```bash
ls -d /workspace/project/UsrLinuxEmu/plugins/gpu_driver/shared/
```

预期输出：目录存在，无错误。

- [ ] **Step 2.3: Stage 并提交符号链接修复**

```bash
cd /workspace/project/UsrLinuxEmu/external/TaskRunner
git add UsrLinuxEmu
git status --short
```

预期输出：仅 `M UsrLinuxEmu`（不再是未跟踪的 modified 状态）。

```bash
git commit -m "fix(repo): repair UsrLinuxEmu symlink target

符号链接 UsrLinuxEmu -> ../UsrLinuxEmu/ 在两个分支都是断的
(../UsrLinuxEmu/ 从 external/TaskRunner 解析为 external/UsrLinuxEmu/，
该目录不存在)。改为 ../../ 正确解析到 /workspace/project/UsrLinuxEmu/。

影响：
- 修复 CMake 头文件查找（gpu_ioctl.h 来自 UsrLinuxEmu）
- 消除 AGENTS.md '符号链接断裂时 CMake 报错退出' 这条防线失效的隐患

Refs: AGENTS.md §关键约束 / GPU 接口 (System C)
ABI: N/A (构建系统修复)"
```

预期输出：`[h1-pushbuffer-validation-closeout 1 commit ahead]` 状态更新。

- [ ] **Step 2.4: 验证 commit 落地**

```bash
git log --oneline -2
git show HEAD --stat
```

预期输出：
- HEAD commit message 匹配上面
- 改动文件列表: `UsrLinuxEmu`

---

## Task 3: 提交 AGENTS.md Issue #13 修复标记（独立 commit）

**Files:**
- Modify: `AGENTS.md` (line 77 区域)

- [ ] **Step 3.1: 确认 AGENTS.md 改动内容**

```bash
git diff AGENTS.md
```

预期输出（8 行变更）：把 `Issue #13 SIGSEGV` 从 "已知问题" 改为 "~~插件 teardown 时 SIGSEGV~~ - Issue #13 ✅ 已修复" 并附修复说明。

- [ ] **Step 3.2: Stage 并提交**

```bash
git add AGENTS.md
git commit -m "docs: mark Issue #13 teardown SIGSEGV as fixed (dd81e5c)

UsrLinuxEmu commit dd81e5c 修复了 plugin_fini_internal() 的销毁顺序
(stop puller → reset → unregister device)，所有测试均正常 teardown。

验证：test_gpu_plugin 等均无 SIGSEGV

Refs: UsrLinuxEmu Issue #13"
```

预期输出：新 commit 出现在 log 中。

- [ ] **Step 3.3: 验证工作区干净**

```bash
git status
```

预期输出：`nothing to commit, working tree clean`。

---

## Task 4: Rebase H-1 onto main

**Files:** (git 操作，不直接修改文件)

- [ ] **Step 4.1: 拉取 main 最新状态**

```bash
cd /workspace/project/UsrLinuxEmu/external/TaskRunner
git fetch origin main
git log origin/main --oneline -3
```

预期输出：3 个 main commit (a7f4463, 8469ad1, 10ec6b7)。

- [ ] **Step 4.2: 执行 rebase**

```bash
git rebase main
```

预期输出（无冲突情况）：
```
Successfully rebased and updated refs/heads/h1-pushbuffer-validation-closeout.
```

- [ ] **Step 4.3: 若出现冲突（见 §Conflict Resolution）**

如果出现冲突，git 会输出：
```
CONFLICT (content): Merge conflict in <file>
```

按下面 §Conflict Resolution 处理。

- [ ] **Step 4.4: 验证 rebase 结果**

```bash
git log --oneline -8
```

预期输出：5 个 commit (3 个 main 的 doc + 1 个 10ec6b7 便捷方法 + 2 个新加的修复 commit + 1 个 ff52e64 H-1)。

```bash
git log --oneline main..HEAD
```

预期输出：3 个 commit（commit 1 符号链接 + commit 2 AGENTS.md + ff52e64 H-1）。

---

## Task 5: 验证 rebase 后代码内容正确

**Files:**
- Inspect: `include/gpu_driver_client.h`
- Inspect: `plans/sync-plan.md`

- [ ] **Step 5.1: 验证 H-1 关键代码已合并**

```bash
grep -n "setCurrentVASpace\|current_va_space_handle_\|va_space_handle" include/gpu_driver_client.h
```

预期输出：3 处以上匹配（setCurrentVASpace 声明、current_va_space_handle_ 成员、submit_batch 内的赋值）。

- [ ] **Step 5.2: 验证 Phase 1.5 便捷方法保留**

```bash
grep -n "get_warp_size\|get_simd_count\|get_peak_fp32_gflops\|print_device_info" include/gpu_driver_client.h | head -5
```

预期输出：至少 4 个方法名匹配。

- [ ] **Step 5.3: 验证 `args.entries_addr` 重命名保留（H-1 不应回退此项）**

```bash
grep -n "args.entries_addr\|args.entries" include/gpu_driver_client.h
```

预期输出：仅 `args.entries_addr` 出现；**不应**有裸 `args.entries = entries;` 行。

- [ ] **Step 5.4: 验证 sync-plan.md 两处改动都在**

```bash
grep -n "S3.1 va_space_handle\|S3.5\|S3.5 已完成\|2026-05-13" plans/sync-plan.md | head -5
```

预期输出：S3.1 H-1 标记 + S3.5 完成标记均在。

- [ ] **Step 5.5: 验证符号链接在工作区仍正确**

```bash
readlink UsrLinuxEmu
```

预期输出：`../../`（rebase 不会触碰符号链接内容）。

---

## Task 6: 本地构建

**Files:** `build/` (新建)

- [ ] **Step 6.1: 清理旧 build 目录（可选）**

```bash
rm -rf build
mkdir -p build
cd build
cmake .. 2>&1 | tail -20
```

预期输出：CMake 配置成功，最后一行类似 `-- Build files have been written to: .../build`。

- [ ] **Step 6.2: 编译**

```bash
make -j4 2>&1 | tail -30
```

预期输出：
- exit code 0
- 无错误（warning 可接受）
- 产物: `test_cuda_scheduler`, `libtaskrunner.a` 等

- [ ] **Step 6.3: 若 CMake 报符号链接错（兜底）**

如果出现 `UsrLinuxEmu 符号链接不存在` 之类的错误：
```bash
cd /workspace/project/UsrLinuxEmu/external/TaskRunner
ls -la UsrLinuxEmu
readlink -f UsrLinuxEmu
```

如果解析结果不是 `/workspace/project/UsrLinuxEmu`：
```bash
git checkout HEAD -- UsrLinuxEmu
readlink UsrLinuxEmu
```

然后重新 cmake。

---

## Task 7: 跑测试

**Files:** (无修改)

- [ ] **Step 7.1: 运行 test_cuda_scheduler**

```bash
cd build
./test_cuda_scheduler 2>&1 | tail -30
```

预期输出：所有 doctest case 通过，类似 `[doctest] test cases: N | M passed | 0 failed`。

- [ ] **Step 7.2: 记录测试结果**

```bash
./test_cuda_scheduler 2>&1 | grep -E "test cases|passed|failed" | tail -5
```

把输出粘贴到 commit message 或 PR 描述中作为证据。

- [ ] **Step 7.3: 若测试失败**

立即停止推进，**不要 force-push**：
```bash
cd /workspace/project/UsrLinuxEmu/external/TaskRunner
git rebase --abort  # 或 git reset --hard backup/h1-pre-rebase-*
```

回到 Task 1 排查根因。

---

## Task 8: 推送分支

**Files:** (无修改，git push)

- [ ] **Step 8.1: 推送前最后确认**

```bash
cd /workspace/project/UsrLinuxEmu/external/TaskRunner
git log origin/h1-pushbuffer-validation-closeout..HEAD --oneline
```

预期输出：3 个 commit（与 backup tag 后新增的一致）。

- [ ] **Step 8.2: Force-with-lease 推送**

```bash
git push --force-with-lease origin h1-pushbuffer-validation-closeout
```

预期输出：`remote: Resolving deltas: .../... Done` 然后 `* [new branch] h1-pushbuffer-validation-closeout -> ...`（实质是 fast-forward 推送）。

- [ ] **Step 8.3: 验证推送**

```bash
git fetch origin
git log origin/h1-pushbuffer-validation-closeout --oneline -5
```

预期输出：3 个 commit + rebase 后的 ff52e64 都在 remote。

---

## Task 9: 创建 PR（可选，取决于用户决定）

**Files:** (GitHub UI / gh CLI)

- [ ] **Step 9.1: 用 gh CLI 创建 PR**

```bash
gh pr create \
  --base main \
  --head h1-pushbuffer-validation-closeout \
  --title "feat(client): H-1 closeout + 符号链接修复 + Issue #13 标记" \
  --body "$(cat <<'EOF'
## Summary
- **fix(repo)**: 修复 `UsrLinuxEmu` 符号链接断裂 bug（main + H-1 分支都受影响）
- **docs**: AGENTS.md 标记 Issue #13 teardown SIGSEGV 已修复
- **feat(client)**: H-1 closeout — `setCurrentVASpace()` + 自动透传到 `submit_batch()`

## Commits
1. `fix(repo): repair UsrLinuxEmu symlink target`
2. `docs: mark Issue #13 teardown SIGSEGV as fixed`
3. `feat(client): plumb va_space_handle through GpuDriverClient (H-1 closeout)`

## Test Evidence
- `test_cuda_scheduler`: [paste Step 7.2 output here]
- Build: `make -j4` exit 0

## ABI Compatibility
- 默认 `current_va_space_handle_ = 0` → 走 H-1 sentinel 跳过校验
- 旧调用方零行为变化；新调用方 opt-in 启用 H-1 校验

## UsrLinuxEmu 侧 H-1 依赖
- [x] `0272970` gpu_pushbuffer_args 增加 va_space_handle
- [x] `bf8192f` PUSHBUFFER_SUBMIT_BATCH handler 加校验
- [x] `09ae1b0` test_gpu_pushbuffer_validation (4 cases)
- [x] `028d50a` 跨仓库同步
- [x] `f223994` 归档

Closes: (无对应 issue，H-1 跟踪在 UsrLinuxEmu openspec)
EOF
)"
```

- [ ] **Step 9.2: 验证 PR 创建成功**

```bash
gh pr list --head h1-pushbuffer-validation-closeout
```

预期输出：PR 编号 + URL。

---

## Conflict Resolution（仅在 rebase 冲突时）

### 场景 A: `include/gpu_driver_client.h` 冲突

**Step A.1: 查看冲突标记**

```bash
grep -n "<<<<<<<\|=======\|>>>>>>>" include/gpu_driver_client.h
```

**Step A.2: 判断冲突位置**
- 若冲突在 `args.fence_id = 0;` 附近：保留 main 的 `args.entries_addr = reinterpret_cast<u64>(entries);` + H-1 的 `args.va_space_handle = current_va_space_handle_;`
- 若冲突在 `return wait_fence(...)` 附近：保留 main 的便利方法 + H-1 的 `setCurrentVASpace` 方法
- 若冲突在文件头部（include 块）：保留 main 的全部 include（含 `<cstring>`, `<cstdio>`）

**Step A.3: 解决后标记**

```bash
git add include/gpu_driver_client.h
git rebase --continue
```

### 场景 B: `plans/sync-plan.md` 冲突

**Step B.1: 保留 H-1 的 §3.4 S3.1 标记 + main 的 §3.5 S3.5 标记**

```bash
git add plans/sync-plan.md
git rebase --continue
```

### 场景 C: 符号链接修复 commit 冲突（main 已有人修）

**Step C.1: 选 ours（保留 H-1 的修复路径）**

```bash
git checkout --ours UsrLinuxEmu
git add UsrLinuxEmu
git rebase --continue
```

**Step C.2: 若 main 修复路径与 H-1 不同，事后核对**

```bash
readlink -f UsrLinuxEmu
# 必须解析到 /workspace/project/UsrLinuxEmu
```

---

## 回滚预案 (Rollback Procedures)

| 触发条件 | 操作 | 影响 |
|---|---|---|
| Rebase 中想放弃 | `git rebase --abort` | HEAD 回到 rebase 前状态 |
| Rebase 后想回退 | `git reset --hard backup/h1-pre-rebase-YYYYMMDD-HHMM` | HEAD 回到 rebase 前 |
| Push 后想回退 | `git push --force origin backup/h1-pre-rebase-YYYYMMDD-HHMM:h1-pushbuffer-validation-closeout` | remote 也回退（需协调团队） |
| 测试失败想全退 | `git reset --hard origin/h1-pushbuffer-validation-closeout` | 回到远程状态 |

---

## Self-Review

- [x] **Spec 覆盖**: 每个目标（rebase + 修复符号链接 + AGENTS.md 同步）都有专门 Task
- [x] **零占位符**: 所有命令、文件路径、预期输出都是可执行的具体内容
- [x] **类型一致性**: Task 5.1-5.4 引用的方法名、字段名与现有代码一致
- [x] **回滚完备**: 4 个不同时间点的回滚路径都有
- [x] **TDD 不适用**: 这是 git 运维 + 集成验证任务，不涉及新功能 TDD 循环；Task 7 的现有测试作为回归门

---

## 待用户确认

在执行前请回答：

1. **PR 是否需要创建？** (Task 9)
2. **是否要本地备份 ref？** (Task 1.4 - 我已默认要)
3. **是否需要 worktree 隔离？** 当前在 H-1 分支上工作，我倾向于不需要；但如果你希望隔离，我可以先 `git worktree add ../tr-h1-rebase h1-pushbuffer-validation-closeout`
4. **Force-push 风险接受度？** 当前 remote 没有任何人 fork 过 H-1 分支，force-with-lease 安全
