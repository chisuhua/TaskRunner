---
SCOPE: TEST-FIXTURE
STATUS: ACCEPTED
REPLACES: tadr-008
---

# TADR-008: H-7 Deferred Registry TaskRunner 侧注册点

**状态**: ⏸️ Deferred (待 Phase 3 owner 触发)
**日期**: 2026-06-23
**提案人**: TaskRunner owner 协同 (H-3 review 阶段识别)
**评审者**: 待 Phase 3 owner 触发后由 owner 认领
**关联 ADR (UsrLinuxEmu)**: [ADR-034](../../../../docs/00_adr/adr-034-h7-deferred-registry.md) (canonical)
**关联 Source**: `openspec/changes/archive/2026-06-22-h3-phase2-management/design.md` §R4 + §R5 + tasks.md §7

---

## Context

H-3 review 阶段识别出 3 个 UsrLinuxEmu 侧 owner-flagged upstream issues。这些 issues 在 H-2 时期就存在，H-3 的 R2 mapping 契约**绕过**了它们的即时影响（TaskRunner 遵守 R2 mapping 即可工作），但生产 hardening 必须解决。

上游完整描述见 UsrLinuxEmu ADR-034（3 issues + 触发位置 + 修复路径）。本 TADR 记录 **TaskRunner 侧的注册点与应对**。

## Decision

### 3 issues 与 TaskRunner 侧影响

| Issue | 描述 | TaskRunner 侧当前状态 |
|-------|------|----------------------|
| #1 stream_id u32 ↔ queue_handle u64 类型不匹配 | `next_queue_handle_` 超过 `UINT32_MAX` 时 create_queue 失败 | **绕过**（R2 mapping LOW32 显式化见 TADR-007，单测环境不会触发）|
| #2 ioctl path 绕过 GpuQueueEmu 抽象层 | 行为分歧（ioctl vs mmap）难调试 | **不涉及**（TaskRunner 走 ioctl path only，spec.md R7 明确）|
| #3 attached_queues 弱校验 | 仅存在性检查，无 lifecycle/type/binding 断言 | **绕过**（happy path 单测覆盖，生产静默 `-EINVAL` 需 postmortem）|

### TaskRunner 侧应对策略

1. **遵守 R2 mapping**（TADR-007 显式化）— 解决 Issue #1 触发条件
2. **不实施 mmap 快速路径** — 避免 Issue #2
3. **错误日志含 errno**（TADR-006 §4 实施细节）— Issue #3 诊断辅助

### 触发条件（与 ADR-034 一致）

任一以下事件触发后，本 TADR 必须被重新打开并由 owner 填充修复章节：

1. `next_queue_handle_` 接近 `UINT32_MAX` (Issue #1 触发)
2. 生产环境首次 `submit_batch` 触发 `-EINVAL` 且 root cause 是 stream_id 截断 (Issue #1 触发)
3. mmap 快速路径 (`MAP_QUEUE_RING`) 被实施 (Issue #2 触发)
4. 第三方 GPU service 接入 UsrLinuxEmu
5. **H-3.6 协调启动** (2026-06-26 主动触发，Issue #3 修复协调) — 见 §H-3.6 段

### H-3.6: Issue #3 主动修复协调（2026-06-26 启动）

**触发原因**: Issue #3 (`attached_queues` 弱校验) **风险最低、收益最高**，主动从 deferred 状态启动修复协调。

**工作范围**:
- openspec change: [`2026-06-26-h3-6-issue-3-coordination`](../../../../openspec/changes/2026-06-26-h3-6-issue-3-coordination/) (5 文件完整)
- GitHub issue 草稿: [`docs/test-fixture/coordination/h7-issue-3-github-issue-draft-2026-06-26.md`](../coordination/h7-issue-3-github-issue-draft-2026-06-26.md)
- 跨仓 PR 模板: [`docs/07-integration/cross-repo-h7-template.md`](../../07-integration/cross-repo-h7-template.md)
- 测试设计文档: [`docs/test-fixture/research/h7-issue-3-test-design-2026-06-26.md`](../research/h7-issue-3-test-design-2026-06-26.md)

**提议方案**（待 UsrLinuxEmu owner 评估）:

PR 1 (最小化):
```diff
// gpgpu_device.h:77
- std::vector<uint64_t> attached_queues;  // O(n) std::find
+ std::unordered_set<uint64_t> attached_queues;  // O(1) avg

// gpgpu_device.cpp:261
- if (std::find(attached.begin(), attached.end(), ...) == attached.end()) {
+ if (attached.find(static_cast<uint64_t>(args->stream_id)) == attached.end()) {
```

PR 2 (可选，错误码语义化):
- 区分 `-EINVAL` / `-ENOENT` / `-EBUSY`
- 添加 `lifecycle_state` 字段
- race condition atomic check-and-set

**实施分工**:
- **TaskRunner 端 (本仓)**: 协调 + 文档 + 测试设计 (DONE 2026-06-26)
- **UsrLinuxEmu 端 (外部仓)**: 实际代码修改 (待 owner 启动)

**跨仓协议**: 按 [ADR-035 §Rule 5.1](../../../../docs/00_adr/adr-035-governance-policy.md) 4 步流程，详见跨仓 PR 模板。

**预期时间表**:
- Day 1-3: TaskRunner 端协调文档 (DONE 2026-06-26)
- Day 4-7: UsrLinuxEmu owner 评估 + 实施 PR 1 (TBD)
- Day 8-10: 跨仓 submodule bump + tadr-105 状态更新 (TBD)
- Day 11+: 可选 PR 2 (TBD)

**预期产出**:
- Issue #3 修复后，本 TADR §Issue #3 状态从 ⏸️ → ✅ Accepted
- 触发 H-3.7 (Issue #2 协调) + H-3.8 (Issue #1 协调) 启动

---

## Consumer-Lens

### TaskRunner 侧的注册点

- `plans/sync-plan.md` §5.3 "H-7 ADR" 候选行（含 TADR-008 引用作为前置）
- TADR-007 §Consequences 显式标注 Issue #1 风险
- TADR-006 §H-3.5 Follow-up 标注 MockGpuDriver 偏差（T6-T9）相关但不同问题

### 与 H-3.5 的关系

H-3.5（0.5 天，CudaStub guard verification）解决 **MockGpuDriver 偏差**（测试覆盖问题），与本 TADR 跟踪的 **3 owner-flagged issues**（生产 hardening）是独立维度。两者不冲突，可并行。

### 与 Phase 3 的关系

Phase 3（Multi-GPU / P2P）需先解决 Issue #1（u32 → u64 拓宽）和 Issue #2（GpuQueueEmu 抽象层），否则多 GPU 场景会触发生产 `-EINVAL`。

## Consequences

### Current (H-3 已 shippable，R2 mapping 工作)

- ✅ TaskRunner 5 Phase 2 方法正常工作（12/12 doctest cases pass）
- ✅ CLI cuda_va_space / cuda_queue 工作
- ⚠️ 3 issues 已知但未修复，记录在本 TADR

### Deferred (Phase 3 owner 触发时)

- ⏸️ Issue #1 修复需要 ABI 变更（u32 → u64 stream_id）
- ⏸️ Issue #2 修复需要新抽象层（GpuQueueEmu）
- ⏸️ Issue #3 修复需要错误码语义改进

### Mitigation Until Fix

- 📚 **Issue #1**: 监控 `next_queue_handle_`，接近 `UINT32_MAX` 时强制重启 service
- 📚 **Issue #2**: 严禁 `MAP_QUEUE_RING` 用于 Phase 2 路径
- 📚 **Issue #3**: 错误日志含 `errno`（TADR-006）+ 完整 attached_queues 状态

## 跨引用

- **上游 UsrLinuxEmu ADR-034**: §Deferred Issues Registry（3 issues + Phase 3 Trigger Conditions）
- **关联 TADR**: TADR-006 (H-3 Phase 2 lifecycle), TADR-007 (R2 mapping)
- **关联 File**: `plans/sync-plan.md` §5.3 "H-7 ADR" 候选行

---

**最后更新**: 2026-06-26（H-3.6 协调建立 §H-3.6 段，Issue #3 修复跟踪）
