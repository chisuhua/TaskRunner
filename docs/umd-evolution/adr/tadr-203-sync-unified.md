---
SCOPE: UMD-EVOLUTION
STATUS: SUPERSEDED
SUPERSEDES: tadr-203 (originally Accepted retroactive 2026-06-23)
SUPERSEDED_BY: H-3 S3.5 fence_id mechanism (commit a7f4463, 2026-05-13)
SUPERSESSION_DATE: 2026-06-30
REPLACES: tadr-003
---

# TADR-203: Barrier/Event 同步模型 — 统一内部表示 (A 方案)

**状态**: ✅ Accepted (retroactive)
**日期**: 2026-04-07 (决策) / 2026-06-23 (retroactive TADR 化)
**提案人**: DevMate
**评审者**: 老板 (CTO)
**关联 ADR (UsrLinuxEmu)**: —
**关联 Change**: —
**关联 Source**: [`docs/decision-frame-cuda-vulkan-runtime.md`](../../archive/2026-04-07-decision-frame-cuda-vulkan-runtime.md) §D3 (lines 123-198)

---

## Context

CUDA Event、Vulkan Semaphore、Vulkan Fence 语义差异较大（host-visible vs device-only、timestamp-based vs signal/wait），需选择统一的内部表示。三种候选：

- **A. 统一内部表示**（⭐ 推荐）：所有同步原语转为内部 `Barrier`
- **B. 分别追踪**（保留原生语义，两套机制交叉引用）
- **C. 降级方案**（强制 host_visible，牺牲并发性能）

## Decision

选择 **A. 统一内部表示**。

**核心抽象**：

```cpp
class SyncSource {
public:
    enum class Type { CUDA_EVENT, VK_SEMAPHORE, VK_FENCE, INTERNAL_BARRIER };
    Type source_type;
    std::atomic<int> pending_wait_count{0};
    void signal();
    bool await_ready() const;
    void await_suspend(std::coroutine_handle<> handle);
    void await_resume();
};

class SyncManager {
public:
    uint64_t register_cuda_event(cudaStream_t s, uint64_t handle);
    uint64_t register_vk_semaphore(VkQueue q, uint64_t handle);
    void add_dependency(CommandBuffer& cb, const SyncSource& src);
    void synchronize_all();
};
```

**理由**：
1. 最大化代码复用（SyncManager 统一管理所有同步源）
2. 天然支持跨 API 互操作（Vulkan Semaphore → CUDA Event 映射无需特殊处理）
3. 符合"单一 truth source"设计原则

## Consequences

### 正面

- ✅ 跨 API 同步天然支持
- ✅ 简化核心逻辑
- ✅ 新增 API 只需注册

### 负面 / 风险

- ⚠️ 丢失部分语义信息（如 CUDA Event timestamp）
- ⚠️ 需处理 host-visible 特殊场景

## SUPERSEDED Status (2026-06-30)

**原决策** (2026-04-07): A 方案 — 所有同步原语转为内部 `Barrier` / `SyncManager` 抽象 + coroutine await。

**当前实现**: 简化的 `fence_id` 机制（H-3 S3.5，2026-05-13 commit a7f4463）替代了完整 SyncManager。`gpu_driver_client.h:368-385` 的 `wait_fence()` 是简化同步路径。

**完整 SyncSource/SyncManager 的价值**（coroutine await + 跨 API 自动映射）**未实现**，留待 Phase 3+ 评估是否补全。当前已满足"跨 API 同步"需求（通过 ioctl `fence_id` 等待）。

**未来重启用 SyncSource 的触发条件**:
1. Phase 3 Stream API 实现需要 coroutine-style await
2. 用户 PoC 需求明确要求简化 Runtime API 调用链
3. 否则维持当前 fence_id 同步。

**替代关系**:
- 本 TADR → `fence_id` 简化同步（H-3 S3.5, commit a7f4463）
- 如需完整 SyncSource/SyncManager 设计，留待 Phase 3+ 评估

## 跨引用

- **关联 TADR**: TADR-201 (D1 统一调度器), TADR-202 (D2 分层设计)
- **关联 Source**: [`docs/decision-frame-cuda-vulkan-runtime.md`](../../archive/2026-04-07-decision-frame-cuda-vulkan-runtime.md):123-198

---

**最后更新**: 2026-06-30（SUPERSEDED status changed）
