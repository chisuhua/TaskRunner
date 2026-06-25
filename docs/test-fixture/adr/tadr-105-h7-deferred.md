---
SCOPE: TEST-FIXTURE
STATUS: ACCEPTED
REPLACES: tadr-008
---

# TADR-008: H-7 Deferred Registry TaskRunner 侧注册点

**状态**: ✅ Partially Resolved (Issue #3/#2 已修复，Issue #1 待 H-3.8)
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
| #2 ioctl path 绕过 GpuQueueEmu 抽象层 | 行为分歧（ioctl vs mmap）难调试 | **✅ 已修复** (392a496: `getQueue()` + `q->submit()` 委托 + `test_gpu_plugin` 50 cases + `test_gpu_fence_return` 28 cases) |
| #3 attached_queues 弱校验 | 仅存在性检查，无 lifecycle/type/binding 断言 | **✅ 已修复** (bf8192f + 09ae1b0: PUSHBUFFER_SUBMIT_BATCH 强校验 + 4 cases 测试覆盖) |

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
- **UsrLinuxEmu 端 (外部仓)**: 实际代码修改 (DONE 2026-06-25, commits bf8192f + 09ae1b0)

**实际修复** (commits bf8192f + 09ae1b0):
- `bf8192f`: PUSHBUFFER_SUBMIT_BATCH handler 添加 va_space_handle 验证 + attached_queues 成员检查（`std::find` 验证 stream_id 是否存在）
- `09ae1b0`: test_gpu_pushbuffer_validation 4 cases 测试（Case A: 成功, Case B: invalid va_space_handle, Case C: stream_id 不在 attached_queues, Case D: va_space_handle=0 向后兼容）
- 行为：从"仅存在性检查"升级为"完整 stream_id ∈ attached_queues 强校验"，失败返回 -EINVAL 并记录 warn 日志

**验证状态**: ✅ 功能修复完成 (H-3.6 协调阶段完成)

**备注**: `std::vector` → `std::unordered_set` 性能优化未实施，但 O(n) 在典型 n<10 场景下可接受。若后续 n 增长，可单独提性能优化 PR。

**跨仓协议**: 按 [ADR-035 §Rule 5.1](../../../../docs/00_adr/adr-035-governance-policy.md) 4 步流程，详见跨仓 PR 模板。

**预期时间表**:
- Day 1-3: TaskRunner 端协调文档 (DONE 2026-06-26)
- Day 4-7: UsrLinuxEmu owner 评估 + 实施 PR 1 (DONE 2026-06-25, bf8192f)
- Day 8-10: 跨仓 submodule bump + tadr-105 状态更新 (DONE 2026-06-25, 522e671)

**预期产出**:
- ✅ Issue #3 修复完成，状态从 ⏸️ → ✅ Accepted
- ✅ 触发 H-3.7 (Issue #2 协调) 启动 — 见 §H-3.7 段

---

### H-3.7: Issue #2 主动修复协调（2026-06-26 启动）

**触发原因**: H-3.6 完成后，按优先级顺序 #3 → #2 → #1，Issue #2 (ioctl path 绕过 GpuQueueEmu 抽象层) 进入协调阶段。

**工作范围**:
- openspec change: [`2026-06-26-h3-7-issue-2-coordination`](../../../../openspec/changes/2026-06-26-h3-7-issue-2-coordination/) (5 文件完整)
- GitHub issue 草稿: `docs/test-fixture/coordination/h7-issue-2-github-issue-draft-2026-06-26.md`
- 跨仓 PR 模板: [`docs/07-integration/cross-repo-h7-template.md`](../../07-integration/cross-repo-h7-template.md)
- 调研文档: `docs/test-fixture/research/gpu-queue-emu-api-2026-06-26.md`
- 测试设计文档: `docs/test-fixture/research/ioctl-mmap-equivalence-2026-06-26.md`

**提议方案**（待 UsrLinuxEmu owner 评估）:

PR 1 (最小化):
```diff
// gpgpu_device.cpp:284-300 (handlePushbufferSubmitBatch)
- // [current] puller path bypasses GpuQueueEmu
- u64 gpfifo_addr = GPFIFO_BASE;
- puller_->submitBatch(gpfifo_addr, args->count);
- hal_doorbell_ring(hal_, args->stream_id);
+ // [fixed] route through GpuQueueEmu abstraction
+ auto q = getQueue(static_cast<uint64_t>(args->stream_id));  // O(1) lookup
+ if (!q) return -ENOENT;
+ q->submit(args->entries_addr, args->count);  // GpuQueueEmu 内部提交
+ hal_doorbell_ring(hal_, q->queue_id());        // 委托给 q
```

PR 2 (可选，错误码语义化):
- 区分 `-EINVAL` / `-ENOENT` / `-EBUSY`
- GpuQueueEmu 内部 lifecycle 状态管理

**实施分工**:
- **TaskRunner 端 (本仓)**: 协调 + 文档 + 测试设计 (DONE 2026-06-26)
- **UsrLinuxEmu 端 (外部仓)**: 实际代码修改 (DONE 2026-06-25, commit 392a496)

**实际修复** (commit 392a496):
- `gpgpu_device.cpp:283-300`: 添加 `getQueue(static_cast<uint64_t>(args->stream_id))` O(1) 查询
- 若 queue 不存在返回 `-ENOENT`（含 warn 日志）
- `q->submit(gpfifo_addr, args->count)` 委托 GpuQueueEmu 内部提交（替代 `puller_->submitBatch()`）
- `gpu_queue_emu.h/cpp`: 新增 `submit()` 和 `setPuller()` 方法
- `handleCreateQueue`: 注册 queue 时 `queue->setPuller(puller_.get())`
- 测试: `test_gpu_plugin` +50 cases，`test_gpu_fence_return` +28 cases
- 行为：从"直接调用 puller"升级为"通过 GpuQueueEmu 抽象层委托"，Queue lifecycle 管理可复用

**验证状态**: ✅ 功能修复完成 (H-3.7 协调阶段完成)
- 34/34 ctest cases pass (含 test_gpu_plugin, test_gpu_fence_return, test_queue_puller_integration)
- CLI `cuda_queue` 功能正常
- 行为等价性：重构前后 fence_id 序列一致

**备注**: doorbell 委托给 `q->ringDoorbell()` 未实施（Phase 1 保持 `hal_doorbell_ring(hal_, args->stream_id)`），可在 Phase 3 中演进。

**跨仓协议**: 按 [ADR-035 §Rule 5.1](../../../../docs/00_adr/adr-035-governance-policy.md) 4 步流程，详见跨仓 PR 模板。

**预期时间表**:
- Day 1-3: TaskRunner 端协调文档 (DONE 2026-06-26)
- Day 4-7: UsrLinuxEmu owner 评估 + 实施 PR 1 (DONE 2026-06-25, 392a496)
- Day 8-10: 跨仓 submodule bump + tadr-105 状态更新 (IN PROGRESS)
- Day 11+: 可选 PR 2 (错误码语义化，TBD)

**预期产出**:
- ✅ Issue #2 修复完成，状态从 ⏸️ → ✅ Accepted
- ✅ 触发 H-3.8 (Issue #1 协调) 启动 — 见 §H-3.8 段

---

### H-3.8: Issue #1 主动修复协调（2026-06-26 启动）

**触发原因**: H-3.6/H-3.7 完成后，按优先级顺序 #3 → #2 → #1，Issue #1 (stream_id u32 ABI 类型不匹配) 进入协调阶段。这是 H-7 deferred 跟踪的最后一个未解决 issue。

**工作范围**:
- openspec change: [`2026-06-26-h3-8-issue-1-coordination`](../../../../openspec/changes/2026-06-26-h3-8-issue-1-coordination/) (6 文件完整)
- GitHub issue 草稿: [`docs/test-fixture/coordination/h7-issue-1-github-issue-draft-2026-06-26.md`](../coordination/h7-issue-1-github-issue-draft-2026-06-26.md)
- 跨仓 PR 模板: [`docs/07-integration/cross-repo-h7-template.md`](../../07-integration/cross-repo-h7-template.md) (已更新 ABI 拓宽专章)
- 调研文档: [`docs/shared/research/gpu-queue-id-patterns-2026-06-26.md`](../../shared/research/gpu-queue-id-patterns-2026-06-26.md)
- 测试设计文档:
  - [`docs/test-fixture/research/u64-boundary-test-design-2026-06-26.md`](../research/u64-boundary-test-design-2026-06-26.md)
  - [`docs/test-fixture/research/abi-backward-compat-test-design-2026-06-26.md`](../research/abi-backward-compat-test-design-2026-06-26.md)

**提议方案**（待 UsrLinuxEmu owner 评估）:

PR 1 (最小化 ABI 拓宽 + 向后兼容):
```diff
// gpu_ioctl.h:43 (gpu_pushbuffer_args 结构体)
struct gpu_pushbuffer_args {
    __u64 entries_addr;
    __u32 count;
-   __u32 stream_id;              // ❌ u32（截断）
+   __u64 stream_id;              // ✅ u64（完整 handle）
+   __u32 stream_id_compat;       // ⚠️ deprecated alias（旧调用方使用）
+   __u32 flags_extended;         // ✅ 新 flag 空间（reserved）
    __u64 va_space_handle;
    __u64 reserved[2];
};

// gpgpu_device.cpp:262
- uint64_t effective_id = static_cast<uint64_t>(args->stream_id);  // R2 mapping 截断
+ uint64_t effective_id = args->stream_id;                        // 直接 u64
+ if (effective_id == 0 && args->stream_id_compat != 0) {
+     // 向后兼容：旧调用方未设置 stream_id，但设置了 stream_id_compat
+     effective_id = static_cast<uint64_t>(args->stream_id_compat);
+ }
```

PR 2 (6 月后，2026-12-26+，废弃 alias 字段清理):
- 移除 `stream_id_compat` 字段
- 移除 `gpgpu_device.cpp` 中 backward compat fallback 逻辑
- 更新所有 caller（kernel module + userspace helper）

**实施分工**:
- **TaskRunner 端 (本仓)**: 协调 + 文档 + 测试设计 (DONE 2026-06-26)
- **UsrLinuxEmu 端 (外部仓)**: 实际代码修改 (📋 待 owner 启动)

**调研结论** (bg_5826c044):
- AMD ROCm KFD: `HSA_QUEUEID` = `struct queue*` (u64 指针，无截断)
- AMD ROCm HSA Runtime: `hsa_queue_t*` (u64 指针，无截断)
- NVIDIA CUDA: `CUstream` = `CUstream_st*` (u64 指针，无截断)
- NVIDIA UVM: `uvm_va_block_region_t` (u64 packed handle，无截断)
- **TaskRunner 当前**: `__u32` ABI + `u64` 内部 = **反模式**
- **推荐**: `__u64` ABI + `deprecated alias` = 与 AMD/NVIDIA 生态一致

**验证状态**: ⏸️ 功能修复待 UsrLinuxEmu owner 实施 (H-3.8 协调阶段)

**备注**: 
- `flags_extended` 字段不定义 flag 语义（reserved for future use，PR 3 范围）
- 6 月过渡期从 PR 1 merged 开始计算（2026-06-26 ~ 2026-12-26）
- 生产硬上限从 ~40 亿提升到 2^64（理论无限）

**跨仓协议**: 按 [ADR-035 §Rule 5.1](../../../../docs/00_adr/adr-035-governance-policy.md) 4 步流程，详见跨仓 PR 模板。

**预期时间表**:
- Day 1-3: TaskRunner 端协调文档 (DONE 2026-06-26)
- Day 4-7: UsrLinuxEmu owner 评估 + 实施 PR 1 (📋 待启动)
- Day 8-10: 跨仓 submodule bump + tadr-105 状态更新 (⏸️ 待 PR 1 merged)

**预期产出**:
- ⏸️ Issue #1 修复完成，状态从 ⏸️ → ✅ Accepted
- ⏸️ H-7 deferred 3 issues 全部解决
- ⏸️ 生产硬上限消除（u64 handle 支持）

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

### Current (H-3.8 协调阶段，H-3/3.5/3.6/3.7 已 shippable)

- ✅ TaskRunner 5 Phase 2 方法正常工作（12/12 doctest cases pass）
- ✅ CLI cuda_va_space / cuda_queue 工作
- ✅ Issue #3 已修复（bf8192f + 09ae1b0）
- ✅ Issue #2 已修复（392a496: GpuQueueEmu 抽象层委托）
- ⏸️ Issue #1 协调阶段（H-3.8 2026-06-26 启动）

### Deferred (H-3.8 协调阶段，PR 1 实施中)

- ⏸️ Issue #1 ABI 拓宽需要 UsrLinuxEmu owner 实际代码修改（u32 → u64 + deprecated alias）— H-3.8 协调阶段，PR 1 待 owner 评估

### Resolved (H-3.6/H-3.7 已完成)

- ✅ Issue #2 修复已完成（GpuQueueEmu 抽象层委托，392a496）
- ✅ Issue #3 修复已完成（PUSHBUFFER_SUBMIT_BATCH 强校验 + 4 cases 测试，bf8192f + 09ae1b0）

### Mitigation Until Fix

- 📚 **Issue #1**: 监控 `next_queue_handle_`，接近 `UINT32_MAX` 时强制重启 service（H-3.8 协调阶段，临时措施）
- ✅ **Issue #2**: 已修复（392a496），无需 mitigation
- ✅ **Issue #3**: 已修复（bf8192f + 09ae1b0），无需 mitigation

## 跨引用

- **上游 UsrLinuxEmu ADR-034**: §Deferred Issues Registry（3 issues + Phase 3 Trigger Conditions）
- **关联 TADR**: TADR-006 (H-3 Phase 2 lifecycle), TADR-007 (R2 mapping)
- **关联 File**: `plans/sync-plan.md` §5.3 "H-7 ADR" 候选行

---

**最后更新**: 2026-06-26（H-3.6 完成 §Issue #3 → Accepted，H-3.7 完成 §Issue #2 → Accepted，H-3.8 启动 §Issue #1 协调阶段）
