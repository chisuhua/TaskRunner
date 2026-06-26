---
SCOPE: TEST-FIXTURE
STATUS: ACTIVE
---

# 跨仓 PR 模板 — H-7 Issue 修复（4 步流程）

> **状态**: ✅ ACTIVE (2026-06-26, H-3.6 协调建立)
> **Owner**: TaskRunner 维护者 + UsrLinuxEmu 维护者
> **关联**: ADR-035 §Rule 5.1 4 步流程
> **适用**: 所有 H-7 Issue (#1 / #2 / #3) 修复的跨仓 PR 协调

本模板为 TaskRunner owner 在协调 UsrLinuxEmu 仓修复时的标准工作流。**不**替代 UsrLinuxEmu 仓的 PR 流程，**仅**作为跨仓 PR 的协调工具。

## 4 步流程（ADR-035 §Rule 5.1）

### 步骤 1：UsrLinuxEmu 仓 Commit + Push

**责任方**: UsrLinuxEmu owner

**Commit 格式**（Conventional Commits）：
```
<type>(<scope>): <subject>

<body>

Refs: <TaskRunner tadr-NNN reference>
Refs: <UsrLinuxEmu adr-NNN reference>
Cross-Repo: TaskRunner submodule bump required
```

**类型**:
- `fix`: Bug 修复
- `refactor`: 代码重构（不改变行为）
- `feat`: 新功能
- `perf`: 性能优化

**Scope**:
- `gpu`: GPU driver 相关
- `drv`: 驱动代码（gpgpu_device.cpp / h）
- `sim`: 仿真层
- `hal`: HAL 抽象层

**示例**（H-3.8 Issue #1 PR 1）:
```
feat(gpu): widen stream_id from u32 to u64 + add deprecated alias (H-7 Issue #1)

Widen gpu_pushbuffer_args.stream_id from __u32 to __u64 to support
unbounded next_queue_handle_ (internally uint64_t per tadr-006).

Changes:
- gpu_ioctl.h:43: stream_id u32 → u64 + add stream_id_compat (deprecated) + flags_extended (reserved)
- gpgpu_device.cpp:262: remove static_cast + add backward compat fallback logic
- gpgpu_device.cpp: error logs use effective_stream_id (no truncation)

Backward compatible: old callers using stream_id_compat continue to work.
Deprecation period: 2026-06-26 ~ 2026-12-26 (6 months).

Refs: tadr-105 (TaskRunner mirror)
Refs: adr-034 (UsrLinuxEmu canonical, Issue #1)
Refs: tadr-007 R2 mapping (deprecated workaround)
Cross-Repo: TaskRunner submodule bump required
```

**PR Description 模板**:
```markdown
## Summary

[1-2 句话描述本次 PR 修复的问题]

Fixes: #[GitHub issue 编号]
Refs: [TaskRunner tadr 链接]
Refs: [UsrLinuxEmu adr 链接]

## Changes

- [ ] [改动 1: 文件 + 行号 + 描述]
- [ ] [改动 2: 文件 + 行号 + 描述]

## Test Results

```
[UsrLinuxEmu 仓 build 输出]
[全部测试通过]
```

## Cross-Repo Impact

- [ ] TaskRunner 仓 submodule bump required
- [ ] TaskRunner 仓 tadr-NNN status update required
- [ ] UsrLinuxEmu 仓 ADR-034 status update required
- [ ] UsrLinuxEmu 仓 openspec change archive required

## Checklist

- [ ] CI passes
- [ ] 全部现有测试通过
- [ ] ABI compatibility verified (如适用)
- [ ] Code reviewed by UsrLinuxEmu owner
- [ ] Code reviewed by TaskRunner owner (跨仓 review)
```

### 步骤 2：TaskRunner 仓 Submodule Bump

**责任方**: TaskRunner owner

**操作**:
```bash
cd /workspace/project/UsrLinuxEmu/external/TaskRunner
git fetch origin main
git checkout main
git pull
cd /workspace/project/UsrLinuxEmu
git status -s external/TaskRunner  # 应显示 modified
git add external/TaskRunner
git commit -m "chore(submodule): bump TaskRunner to <hash> (H-3.X issue-N)"
git push origin main
```

**Commit 格式**:
```
chore(submodule): bump TaskRunner to <short-hash> (H-3.X issue-N)

Bump TaskRunner to <full-hash> to incorporate H-7 Issue #N fix from
UsrLinuxEmu.

Upstream: <UsrLinuxEmu PR 链接>
TaskRunner: <tadr-105 链接>
```

### 步骤 3：TaskRunner 仓 TADR 状态更新

**责任方**: TaskRunner owner

**修改**: `docs/test-fixture/adr/tadr-105-h7-deferred.md`（或对应 TADR）

**修改 1**: §Issue #N 状态更新
```diff
- ⏸️ Deferred (待 Phase 3 owner 触发)
+ ✅ Accepted (2026-MM-DD, H-3.X 完成)
```

**修改 2**: 添加 §H-3.X Implementation 段
```markdown
## H-3.X Implementation (2026-MM-DD)

**修复 commit**: [UsrLinuxEmu commit hash]
**TaskRunner bump**: [TaskRunner commit hash]
**跨仓 PR**: [UsrLinuxEmu PR 链接]

### 改动总结

[1-2 段描述实际修复内容]

### 测试验证

- [UsrLinuxEmu 仓全部测试通过]
- [TaskRunner 仓 submodule bump 后编译通过]
- [跨仓一致性验证]

### 跨引用

- UsrLinuxEmu adr-034: §Issue #N 状态更新
- openspec change: [archive 链接]
```

**修改 3**: §Deferred 段调整
```diff
- ⏸️ Issue #N 修复需要 ...
+ ✅ Issue #N 已修复（详见 §H-3.X Implementation）
```

**Commit 格式**:
```
docs(adr): tadr-105 §Issue #N → Accepted (H-3.X)

Update TADR-105 to reflect H-7 Issue #N fix completion via H-3.X
coordination. Issue #N is now accepted; remaining deferred issues
tracked separately.

Refs: [UsrLinuxEmu PR 链接]
Refs: [openspec change archive 链接]
```

### 步骤 4：UsrLinuxEmu 仓 Mirror + ADR 状态更新

**责任方**: UsrLinuxEmu owner

**修改 1**: `docs/00_adr/README.md` TaskRunner TADR mirror 段（如有变化）

**修改 2**: `docs/00_adr/adr-034-h7-deferred-registry.md` §Issue #N 状态
```diff
- ⏸️ Issue #N 修复需要 ...
+ ✅ Issue #N 已修复（2026-MM-DD，commit <hash>）

### 修复 commit

[commit hash] refactor(gpu): use unordered_set for VASpace::attached_queues

### 修复后状态

[1-2 段描述]
```

**修改 3**: Archive openspec change
```bash
cd /workspace/project/UsrLinuxEmu
openspec archive 2026-MM-DD-h3-X-issue-N-coordination -y
```

**修改 4**: Push
```bash
git add .
git commit -m "docs(adr): adr-034 §Issue #N → Accepted (H-3.X)

Mirror TaskRunner TADR-105 status update. H-7 Issue #N is now
resolved.

Refs: [TaskRunner bump commit 链接]"
git push origin main
```

## 验证清单

每次跨仓 PR 完成后验证：

- [ ] UsrLinuxEmu 仓 PR merged
- [ ] TaskRunner 仓 submodule bump commit 在 main
- [ ] TaskRunner 仓 TADR-105 状态更新 commit 在 main
- [ ] UsrLinuxEmu 仓 ADR-034 状态更新 commit 在 main
- [ ] UsrLinuxEmu 仓 openspec change archive 完成
- [ ] `docs/00_adr/README.md` mirror 表与 TaskRunner 端一致
- [ ] `git log --oneline` 4 个 commit 链路清晰

## 已知陷阱

### ABI 拓宽陷阱

**症状**: `gpu_ioctl.h` ABI 字段从 u32 拓宽到 u64，但下游 caller 仍按 u32 编译，导致内存布局错乱
**缓解**:
- 添加 `__u32 stream_id_compat` deprecated alias 字段（保持旧内存布局兼容）
- 使用 `__attribute__((packed))` 或显式 padding 控制结构体布局
- 提供 `CHANGELOG.md` 通知所有下游 caller 升级 ABI
- 6 月过渡期 + 双驱动版本共存期测试

### Trap 1: 跨仓 PR 冲突

**症状**: UsrLinuxEmu 仓 PR 与 TaskRunner 仓 submodule bump 冲突
**缓解**: 
- 严格按 4 步顺序执行
- TaskRunner bump 等 UsrLinuxEmu PR merge 后再触发
- 避免并发跨仓 PR

### Trap 2: TADR 状态提前更新

**症状**: TADR 状态在 UsrLinuxEmu PR merge 前更新
**缓解**:
- 步骤 3 严格在步骤 2 后执行
- 步骤 2 严格在步骤 1 后执行
- 自动化检查（可选）：CI 验证 TADR 状态与 submodule bump 一致性

### Trap 3: Mirror 表漂移

**症状**: TaskRunner TADR 状态更新，但 UsrLinuxEmu `docs/00_adr/README.md` mirror 表未更新
**缓解**:
- 步骤 4 包含 mirror 表更新
- 定期 `git diff main main -- docs/00_adr/README.md external/TaskRunner/docs/test-fixture/adr/` 验证一致性

## 历史 PR 范例

| H-# | Issue | UsrLinuxEmu Commit | TaskRunner bump | TADR 状态 | 归档时间 |
|-----|-------|-------------------|-----------------|-----------|---------|
| H-3.6 | #3 attached_queues 弱校验 | `bf8192f` | `f3f52d8` | tadr-105 §Issue #3 → Accepted | 2026-06-26 |
| H-3.7 | #2 ioctl 绕过 | `392a496` | `73390ae` | tadr-105 §Issue #2 → Accepted | 2026-06-26 |
| H-3.8 | #1 stream_id u32→u64 | `02ae421` | `9e3db2e` | tadr-105 §Issue #1 → Accepted | 2026-06-26 |

## 维护

- **Owner**: TaskRunner owner
- **Reviewers**: UsrLinuxEmu owner + TaskRunner owner
- **更新时机**: 每完成 1 个 H-7 Issue 修复后，更新"历史 PR 范例"表。当前所有 3 个 H-7 Issues 已全部修复归档 (2026-06-26)
- **关联文档**: ADR-035 §Rule 5.1, openspec change archive 文件
