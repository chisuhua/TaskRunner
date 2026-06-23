# TADR-002: UsrLinuxEmu 接口扩展策略 — 分层设计 (C 方案)

**状态**: ✅ Accepted (retroactive)
**日期**: 2026-04-07 (决策) / 2026-06-23 (retroactive TADR 化)
**提案人**: DevMate
**评审者**: 老板 (CTO)
**关联 ADR (UsrLinuxEmu)**: —
**关联 Change**: —
**关联 Source**: [`docs/decision-frame-cuda-vulkan-runtime.md`](../../decision-frame-cuda-vulkan-runtime.md) §D2 (lines 59-122)

---

## Context

`GpuCommandPacket` 当前仅支持 `KERNEL` / `DMA_COPY` 两种命令类型；Vulkan Compute 高级语义（semaphore/fence/pipeline bind）需要更丰富的命令类型。三种候选扩展策略：

- **A. 立即扩展 enum**（20+ 命令类型，UsrLinuxEmu 臃肿）
- **B. 保持简化模型**（组合 KERNEL/DMA 模拟 Vulkan）
- **C. 分层设计**（⭐ 推荐）：TaskRunner 层 20+ 命令，UsrLinuxEmu 层保留 5 种基础命令 + 转译层

## Decision

选择 **C. 分层设计**。

**核心结构**：

```cpp
// TaskRunner 层（丰富命令）
enum class TaskCommand {
    // CUDA
    CUDA_ALLOC, CUDA_FREE, CUDA_COPY, CUDA_LAUNCH,
    CUDA_EVENT_RECORD, CUDA_EVENT_WAIT, CUDA_STREAM_SYNC,
    // Vulkan Compute
    VK_ALLOC_MEMORY, VK_FREE_MEMORY, VK_DISPATCH_COMPUTE,
    VK_BIND_PIPELINE, VK_SET_BUFFER,
    VK_SIGNAL_SEMAPHORE, VK_WAIT_SEMAPHORE,
    // Generic
    BARRIER_SYNC, FENCE_SIGNAL, MEMBARRIER
};

// UsrLinuxEmu 层（精简命令）
enum class DeviceCommand {
    KERNEL,        // 对应 CUDA_LAUNCH / VK_DISPATCH_COMPUTE
    DMA_COPY,      // 对应 CUDA_COPY / VK_MEMORY_COPY
    MEMORY_ALLOC,  // 对应 CUDA_ALLOC / VK_ALLOC_MEMORY
    MEMORY_FREE,   // 对应 CUDA_FREE / VK_FREE_MEMORY
    SYNC_BARRIER   // 对应各类 sync primitive
};

// CommandTranslator（位于 UnifiedScheduler 内部）
class CommandTranslator {
    DeviceCommand translate(TaskCommand cmd);
    void inject_dependency(DeviceCommand cmd, const DependencyGraph& deps);
};
```

**理由**：
1. 符合"关注点分离"原则（TaskRunner 高级语义，UsrLinuxEmu 底层执行）
2. 可插拔底层驱动（未来可替换为真实 GPU 驱动或不同仿真器）
3. 避免重复编码（TaskRunner 命令枚举无需与底层一一对应）

## Consequences

### 正面

- ✅ 职责清晰，TaskRunner 与 UsrLinuxEmu 解耦
- ✅ 未来可插拔更多底层驱动
- ✅ UsrLinuxEmu 保持精简（5 种基础命令）

### 负面 / 风险

- ⚠️ 需设计 CommandTranslator 转译规则
- ⚠️ 增加一层间接性（调试时需穿透更多层）

### 实施路径备注

实际实施中，**CommandTranslator 类未单独实现**。H-2.5 引入的 `IGpuDriver` 抽象（见 TADR-005）替代了显式 CommandTranslator 角色：`CudaScheduler` 通过 DI 选择 `GpuDriverClient`（真 ioctl）或 `CudaStub`（mock），每种实现内部自行处理命令编码，无中间转译类。

**当前 UsrLinuxEmu 端** `CommandType` 仍保持 KERNEL/DMA_COPY 两种（未按 D2 扩展 5 种基础命令），原因是 Phase 1 + 1.5 + H-3 的 `GPU_IOCTL_*` 命令路径已经通过 ioctl 编号实现扩展，无需 enum 改造。

## 跨引用

- **关联 TADR**: TADR-001 (D1 统一调度器), TADR-005 (IGpuDriver 抽象)
- **关联 Source**: [`docs/decision-frame-cuda-vulkan-runtime.md`](../../decision-frame-cuda-vulkan-runtime.md):59-122

---

**最后更新**: 2026-06-23（H-4.5 docs governance cleanup TADR 化）
