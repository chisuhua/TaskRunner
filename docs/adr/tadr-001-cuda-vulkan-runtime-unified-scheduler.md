# TADR-001: CUDA/Vulkan Runtime 集成路径 — 统一调度器 (B 方案)

**状态**: ✅ Accepted (retroactive)
**日期**: 2026-04-07 (决策) / 2026-06-23 (retroactive TADR 化)
**提案人**: DevMate
**评审者**: 老板 (CTO)
**关联 ADR (UsrLinuxEmu)**: —
**关联 Change**: —
**关联 Source**: [`docs/decision-frame-cuda-vulkan-runtime.md`](../../decision-frame-cuda-vulkan-runtime.md) §D1 (lines 21-58)

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

### 实施路径备注

**关键差异**：实际实施期（2026-04 → 2026-06），**未采用 `UnifiedScheduler` 类作为中央调度器**，而是采用 IGpuDriver 抽象 + 依赖注入模式（详见 TADR-005）。`UnifiedScheduler` 在原始决策中描述为"接收双 API 的 CmdBuffer 统一转化"，但实际代码中：

- `cuda_stub.cpp` 直接构造 `gpu_gpfifo_entry` 而不经过中间层
- `gpu_driver_client.cpp` 通过 `IGpuDriver*` 依赖注入与 `GpuDriverClient` / `CudaStub` / `MockGpuDriver` 三实现解耦

**本 TADR 的价值**：保留 v0.1 提案原貌（2026-04-07 决策时点），让历史决策可追溯。当前架构演进见 TADR-005 (IGpuDriver 抽象) + UsrLinuxEmu ADR-032 (H-2.5 跨仓架构)。

## 跨引用

- **关联 TADR**: TADR-002 (D2 分层设计), TADR-003 (D3 同步), TADR-004 (D4 Stub 追踪)
- **后续替代/演进**: TADR-005 (IGpuDriver 抽象层), UsrLinuxEmu [ADR-032](../../../docs/00_adr/adr-032-h2-5-igpu-driver-abstraction.md) (H-2.5)
- **关联 Source**: [`docs/decision-frame-cuda-vulkan-runtime.md`](../../decision-frame-cuda-vulkan-runtime.md):21-58
- **关联设计提案**: [`docs/cuda-vulkan-runtime-architecture.md`](../../cuda-vulkan-runtime-architecture.md) §4.2 (方案 B 详细)

---

**最后更新**: 2026-06-23（H-4.5 docs governance cleanup TADR 化）
