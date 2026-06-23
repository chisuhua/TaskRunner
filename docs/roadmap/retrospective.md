# Retrospective - 实际实施路径 vs v0.1 提案 Deviation

> **最后更新**: 2026-06-23（H-4.5 docs governance cleanup）

2026-04-07 v0.1 提案（[archive/2026-04-07-cuda-vulkan-runtime-architecture.md](../archive/2026-04-07-cuda-vulkan-runtime-architecture.md)）推荐 UnifiedScheduler 模式 + CommandTranslator 转译层。实际实施路径有显著偏差，本文档汇总。

## 核心偏差对比

### v0.1 提案 vs 实际架构

| 维度 | v0.1 提案 | 实际实施 | 偏差原因 |
|------|-----------|---------|---------|
| 中央调度器 | `UnifiedScheduler` 类 | `CudaScheduler` (单 GPU 专用) | ✅ Vulkan stub 未实施，简化设计 |
| 命令编码 | `CommandTranslator` 转译层 | 无中间层，`cuda_stub.cpp` 直接编码 `gpu_gpfifo_entry` | ✅ Phase 2 ioctl 编号扩展替代 enum 改造 |
| 抽象层 | UnifiedScheduler 持有资源 | `IGpuDriver` 抽象 + 3 实现 DI | ✅ 测试隔离 + 跨仓治理需要 |
| 同步原语 | `SyncSource` + `SyncManager` 完整设计 | `fence_id` 简化同步路径 | ✅ Phase 3+ 简化，coroutine await 推迟 |
| 资源管理 | `ResourceManager` 统一追踪 | `CudaStub` 独立 + `GpuDriverClient` 透传 | ✅ Stub 独立追踪决策保留 |
| 集成路径 | A/B/C 三方案对比 | B 方案的简化变体 | 见 [TADR-001 实施路径备注](../adr/tadr-001-cuda-vulkan-runtime-unified-scheduler.md#实施路径备注) |

### 决策保留

| 决策 | v0.1 提案 | 实际 | 状态 |
|------|-----------|------|------|
| D1 集成路径 | B 统一调度器 | B 简化（无 UnifiedScheduler 中央类） | ✅ 保留精神 |
| D2 UsrLinuxEmu 接口扩展 | C 分层设计 | UsrLinuxEmu 端保持 2 种基础命令 + TaskRunner 端 ioctl 编号扩展 | ⚠️ 推迟到 Phase 3 |
| D3 同步模型 | A 统一内部表示 | `fence_id` 简化路径 | ⚠️ 简化实施 |
| D4 资源管理 | B Stub 独立追踪 | `CudaStub` 独立 + atomic + map | ✅ 完全实施 |

## 偏差原因分析

### 1. 测试隔离需求驱动 IGpuDriver 抽象

v0.1 提案时代，`GpuDriverClient` 与 `CudaStub` 是两个独立类（无共同基类）。H-2 实施时发现：

- 单测难以 mock `GpuDriverClient`
- CLI 死调用（`g_gpu_client` 未初始化导致 silent fallback）

**修复方案**：H-2.5 引入 `IGpuDriver` 抽象，让 `CudaScheduler` 接受 `IGpuDriver*` 注入。这与 v0.1 UnifiedScheduler 不同（UnifiedScheduler 是 TaskRunner 中央类，IGpuDriver 是 GPU 驱动契约）。

### 2. Phase 2 ioctl 编号扩展替代 enum 改造

v0.1 提案 D2 推荐扩展 UsrLinuxEmu 端 `CommandType` enum（KERNEL/DMA_COPY → 5 种基础命令）。实际：

- UsrLinuxEmu 端保持 2 种基础命令（最小化改动）
- Phase 2 新增通过 `GPU_IOCTL_*` 编号扩展（IOCTL 编号无 enum 限制）

**优势**：避免 UsrLinuxEmu 端 enum 膨胀，Phase 2 ioctl 可独立演进。

### 3. SyncManager 设计简化

v0.1 提案 D3 推荐 `SyncSource` + `SyncManager` 完整设计（含 coroutine await）。实际：

- `fence_id` 直接通过 ioctl `GPU_IOCTL_WAIT_FENCE` 等待
- 无 coroutine 抽象（Phase 1 不需要）

**保留原因**：`fence_id` 已满足 Phase 1 需求，`SyncSource` 抽象推迟到 Phase 3+ 评估。

## 经验总结

1. **测试驱动架构演进**：H-2.5 IGpuDriver 抽象不是设计阶段预测的，而是 H-2 实施期被测试需求逼出来的
2. **ioctl 编号优于 enum**：跨仓演进时，ioctl 编号比 enum 更灵活
3. **v0.1 提案的价值**：保留作为决策历史快照（已迁移到 archive/）+ TADR-001~004 决策记录

---

**END OF RETROSPECTIVE**
