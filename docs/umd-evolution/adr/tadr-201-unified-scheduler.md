---
SCOPE: UMD-EVOLUTION
STATUS: SUPERSEDED
SUPERSEDES: tadr-201 (originally Accepted retroactive 2026-06-23)
SUPERSEDED_BY: ../../test-fixture/adr/tadr-109-igpu-driver-uniform-scheduling.md [TEST-FIXTURE SCOPE], H-3.5 follow-up (2026-06-19)
SUPERSESSION_DATE: 2026-06-30
REPLACES: tadr-001
---

# TADR-201: CUDA/Vulkan Runtime 集成路径 — 统一调度器 (B 方案)

**状态**: ✅ Accepted (retroactive)
**日期**: 2026-04-07 (决策) / 2026-06-23 (retroactive TADR 化)
**提案人**: DevMate
**评审者**: 老板 (CTO)
**关联 ADR (UsrLinuxEmu)**: —
**关联 Change**: —
**关联 Source**: [`docs/decision-frame-cuda-vulkan-runtime.md`](../../archive/2026-04-07-decision-frame-cuda-vulkan-runtime.md) §D1 (lines 21-58)

---

## Context

TaskRunner + UsrLinuxEmu 基座之上构建 CUDA/Vulkan API 兼容层。三种候选集成路径：

- **A. 纯转发层**（最小改动，2-3 周 MVP）
- **B. 统一调度器**（8-10 周完整版本，⭐ 推荐）
- **C. 驱动级替换**（6 月+，长期商业化）

老板 Q3 明确要求"独立系统"，跨 API 互操作是硬约束。

## Decision

选择 **B. 统一调度器模式**。

**核心架构**：CUDA Stub + Vulkan Stub 共享一个 UnifiedScheduler，统一转化为 GPFIFO entry；通过 SyncManager 管理 Event/Semaphore 双向映射；通过 ResourceManager 跟踪虚拟地址空间。

**理由**：
1. 平衡了开发效率（70% 代码复用率）与长期可维护性
2. 天然支持双 API 互操作（CUDA Event ↔ Vulkan Semaphore）
3. 符合 TaskRunner 原有设计理念（多队列调度 + work-stealing）

## Consequences

### 正面

- ✅ 跨 API 互操作天然支持
- ✅ 性能开销中等（1-2x）
- ✅ 代码复用率 70%+

### 负面 / 风险

- ⚠️ 工作量中等（8-10 周完整版本）
- ⚠️ 架构复杂度增加（学习成本上升）

## SUPERSEDED Status (2026-06-30)

**原决策** (2026-04-07): B 方案 — 统一调度器 UnifiedScheduler 接收双 API CmdBuffer 统一转换为 GPFIFO entry。

**当前实现** (2026-06-30): 决策已被 **替代**。IGpuDriver 抽象 + DI 模式（详见 [`tadr-109`](../../test-fixture/adr/tadr-109-igpu-driver-uniform-scheduling.md) [TEST-FIXTURE SCOPE]）替代了 UnifiedScheduler 中央调度器角色：
- `cuda_stub.cpp` 直接构造 `gpu_gpfifo_entry`，不经中间层
- `gpu_driver_client.cpp` 通过 `IGpuDriver*` DI 与三种实现解耦
- 代码演化路径见 H-3.5 follow-up commit `5ff8c26`

**本 TADR 作为历史决策记录保留**，让 v0.1 提案可追溯。如需查阅原始动机，见 [`docs/decision-frame-cuda-vulkan-runtime.md`](../../archive/2026-04-07-decision-frame-cuda-vulkan-runtime.md) §D1。

**替代关系**:
- 本 TADR → [`tadr-109`](../../test-fixture/adr/tadr-109-igpu-driver-uniform-scheduling.md) [TEST-FIXTURE SCOPE] (IGpuDriver 31 方法扩展)
- 不再活跃更新；任何相关决策请在 test-fixture scope 通过 tadr-1xx 系列登记。

## 跨引用

- **关联 TADR**: TADR-002 (D2 分层设计), TADR-003 (D3 同步), TADR-004 (D4 Stub 追踪)
- **后续替代/演进**: TADR-005 (IGpuDriver 抽象层), UsrLinuxEmu [ADR-032](../../../../docs/00_adr/adr-032-h2-5-igpu-driver-abstraction.md) (H-2.5)
- **关联 Source**: [`docs/decision-frame-cuda-vulkan-runtime.md`](../../archive/2026-04-07-decision-frame-cuda-vulkan-runtime.md):21-58
- **关联设计提案**: [`docs/cuda-vulkan-runtime-architecture.md`](../../archive/2026-04-07-cuda-vulkan-runtime-architecture.md) §4.2 (方案 B 详细)

---

**最后更新**: 2026-06-30（SUPERSEDED status changed）
