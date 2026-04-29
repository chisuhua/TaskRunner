# CUDA/Vulkan Runtime 架构决策框架

> **目的**：为 CTO + 架构委员会提供清晰的选择题，便于快速决策  
> **状态**：✅ 已批准（2026-04-07）→ 进入详细设计阶段  
> **最后更新**：2026-04-07  
> **详细设计**: `DDS-CUDA-Vulkan-Runtime-v1.2-final.md`

---

## 📋 决策清单概览

| 决策 ID | 问题描述 | 选项数 | 推荐指数 |
|---------|---------|--------|---------|
| **D1** | 集成路径选择 | A/B/C 3 种 | ⭐⭐⭐⭐⭐ |
| **D2** | UsrLinuxEmu 接口扩展策略 | A/B/C 3 种 | ⭐⭐⭐⭐⭐ |
| **D3** | Barrier/Event 同步模型统一策略 | A/B/C 3 种 | ⭐⭐⭐⭐⭐ |
| **D4** | 资源管理器层级划分 | A/B/C 3 种 | ⭐⭐⭐⭐ |

---

## D1. 集成路径选择（核心决策）

### 问题描述

如何在 TaskRunner + UsrLinuxEmu 基座上构建 CUDA/Vulkan API 兼容层？

### 选项对比

| 维度 | **A. 纯转发层** | **B. 统一调度器** ⭐ | **C. 驱动级替换** |
|------|---------------|------------------|-----------------|
| **工作量估算** | 2-3 周 MVP | 8-10 周完整版本 | 6 月+ |
| **代码复用率** | 30% | 70%+ | 90% (但初始成本高) |
| **跨 API 互操作** | ❌ 困难 | ✅ 原生支持 | ✅ 容易 |
| **性能开销** | 高（多次拷贝） | 中（1-2x） | 低（接近原生） |
| **维护成本** | 低（逻辑分散） | 中（需理解统一层） | 高（跟进官方 API） |
| **技术风险** | 低 | 中 | 高（法律/专利） |
| **适用场景** | MVP 验证 | 长期产品规划 | 商业化生态 |

### 推荐答案：**B. 统一调度器模式**

**理由**：
1. 平衡了开发效率与长期可维护性
2. 天然支持双 API 互操作（老板 Q3 明确要求"独立系统"）
3. 符合 TaskRunner 原有设计理念（多队列调度 + work-stealing）

### 决策结果标记

```
[ ] 选项 A - 纯转发层（最小改动，快速 MVP）
[X] 选项 B - 统一调度器（推荐方案，平衡性能与维护）
[ ] 选项 C - 驱动级替换（激进方案，适合商业化）
```

**CTO 批准**: 2026-04-07 ✅

---

## D2. UsrLinuxEmu 接口扩展策略

### 问题描述

GpuCommandPacket 当前仅支持 KERNEL/DMA_COPY 两种命令类型，是否需要扩展以支持 Vulkan Compute 的高级语义（如 semaphore/fence/pipeline bind）？

### 选项对比

| 维度 | **A. 立即扩展 enum** | **B. 保持简化模型** | **C. 分层设计** ⭐ |
|------|--------------------|-------------------|-----------------|
| **UsrLinuxEmu 复杂度** | 高（20+ 命令类型） | 低（保持 2 种） | 中（5-6 种基础命令） |
| **TaskRunner 灵活性** | 低（受限于下层） | 中（需组合模拟） | 高（丰富命令集） |
| **转译成本** | 无 | 高（每次组合模拟） | 中（一次性映射规则） |
| **未来扩展性** | 差（enum 膨胀） | 差（无法表达新语义） | 好（插件化底层驱动） |
| **调试友好度** | 中（扁平结构） | 高（简单直接） | 中（多层间接） |

### 推荐答案：**C. 分层设计**

**理由**：
1. 符合"关注点分离"原则（TaskRunner 负责高级语义，UsrLinuxEmu 负责执行）
2. 可插拔底层驱动（未来可替换为真实 GPU 驱动或不同仿真器）
3. 避免重复编码（TASKRUNNER 的命令枚举无需与底层一一对应）

### 具体实施方案

```cpp
// TaskRunner 层（丰富）
enum class TaskCommand {
    // CUDA
    CUDA_ALLOC, CUDA_FREE, CUDA_COPY, CUDA_LAUNCH, 
    CUDA_EVENT_RECORD, CUDA_EVENT_WAIT, CUDA_STREAM_SYNC,
    // Vulkan
    VK_ALLOC_MEMORY, VK_FREE_MEMORY, VK_DISPATCH_COMPUTE,
    VK_BIND_PIPELINE, VK_SET_BUFFER, VK_SIGNAL_SEMAPHORE, VK_WAIT_SEMAPHORE,
    // Generic
    BARRIER_SYNC, FENCE_SIGNAL, MEMBARRIER
};

// UsrLinuxEmu 层（精简）
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

### 决策结果标记

```
[ ] 选项 A - 立即扩展 enum（简单粗暴，不推荐）
[ ] 选项 B - 保持简化模型（适合学习原型）
[X] 选项 C - 分层设计（推荐方案，职责清晰）
```

**CTO 批准**: 2026-04-07 ✅

---

## D3. Barrier/Event 同步模型统一策略

### 问题描述

CUDA Event 与 Vulkan Semaphore/Fence 语义差异较大，如何选择统一内部表示？

### 语义差异对照表

| 特性 | CUDA Event | Vulkan Semaphore | Internal Barrier |
|------|-----------|------------------|------------------|
| **可见性** | host-visible by default | device-only (除非显式声明) | device-only |
| **触发机制** | timestamp-based | fence-like (signal → wait) | countdown/future |
| **复用性** | 可多次 record/wait | 单次 signal + 多次 wait | 单次 reset |
| **延迟敏感度** | 低（host poll 为主） | 高（device queue 依赖） | 高（需要同步保障） |

### 选项对比

| 维度 | **A. 统一内部表示** ⭐ | **B. 分别追踪** | **C. 降级方案** |
|------|--------------------|---------------|---------------|
| **实现复杂度** | 中（需处理语义转换） | 高（两套机制交叉引用） | 低（强制 host_visible） |
| **性能影响** | 低（内部统一优化） | 中（双重检查） | 高（丧失异步优势） |
| **跨 API 互操作** | ✅ 自然支持 | ❌ 困难（需额外桥接） | ✅ 容易（均转为 barrier） |
| **调试友好度** | 中（抽象层增加难度） | 高（保留原生语义） | 高（统一简化模型） |
| **可扩展性** | 高（新增 API 只需注册） | 低（每加一个 API 都要维护两套） | 中（受限于降级假设） |

### 推荐答案：**A. 统一内部表示**

**理由**：
1. 最大化代码复用（SyncManager 统一管理所有同步源）
2. 天然支持跨 API 互操作（Vulkan Semaphore → CUDA Event 映射无需特殊处理）
3. 符合"单一 truth source"设计原则

### 关键设计要点

```cpp
class SyncSource {
public:
    enum class Type { CUDA_EVENT, VK_SEMAPHORE, VK_FENCE, INTERNAL_BARRIER };
    
    Type source_type;
    union {
        struct { uint64_t event_handle; cudaStream_t stream; } cuda_event;
        struct { uint64_t semaphore_handle; VkQueue queue; } vk_semaphore;
        struct { std::shared_ptr<std::promise<void>> promise; } internal_barrier;
    };
    
    std::atomic<int> pending_wait_count{0};
    void signal();          // 原子信号发布
    bool await_ready() {};  // 非阻塞查询
};

class SyncManager {
public:
    uint64_t register_cuda_event(cudaStream_t s, uint64_t handle);
    uint64_t register_vk_semaphore(VkQueue q, uint64_t handle);
    
    // 创建依赖关系（将 CommandBuffer 绑定到 SyncSource）
    void add_dependency(CommandBuffer& cb, const SyncSource& src);
    
    // 全局同步点（用于 stream synchronize 等）
    void synchronize_all();
};
```

### 决策结果标记

```
[X] 选项 A - 统一内部表示（推荐方案，兼顾灵活性与性能）
[ ] 选项 B - 分别追踪（保留原生语义，但复杂度高）
[ ] 选项 C - 降级方案（牺牲异步换取简单，不推荐）
```

**CTO 批准**: 2026-04-07 ✅

---

## D4. 资源管理器层级划分

### 问题描述

虚拟地址空间管理（Host 侧 tracker ↔ Device 侧物理地址分配）应该由哪一层负责？

### 选项对比

| 维度 | **A. TaskRunner 层统一** | **B. Runtime Stub 层独立** ⭐ | **C. UsrLinuxEmu 底层托管** |
|------|----------------------|---------------------------|-------------------------|
| **职责清晰度** | ❌ TaskRunner 侵入 GPU 领域知识 | ✅ 各司其职（Stub 管生命周期，TaskRunner 管调度） | ✅ 贴近硬件真实行为 |
| **调试友好度** | ✅ 单一 truth source | ⚠️ 需协调两套 tracker | ❌ Host 侧无法预知地址布局 |
| **跨 API 互操作** | ✅ 容易（统一映射） | ⚠️ 需显式导入接口 | ❌ 困难（物理地址 opaque） |
| **单元测试独立性** | ❌ 强耦合 | ✅ 可独立测试每个 Stub | ✅ 可模拟设备分配器 |
| **内存开销** | 低（单一 tracker） | 中（Double-tracking） | 低（无额外 tracker） |

### 推荐答案：**B. Runtime Stub 层独立追踪**

**理由**：
1. 符合各 API 原生生命周期管理语义（CUDA MemoryTracker 有自己的一套 refcount/logic）
2. 便于单元测试（Mock 底层分配器，专注验证 Stub 逻辑）
3. 违反少（TaskRunner 无需了解 GPU 领域知识）

### 具体实现示例

```cpp
// cuda_stub/src/memory_tracker.cpp
class CudaMemoryTracker {
public:
    void* allocate(size_t size) {
        auto handle = next_handle_.fetch_add(1);
        auto ptr = std::make_unique<char[]>(size);
        allocations_[handle] = std::move(ptr);
        
        // 通过 ioctl 告知 UsrLinuxEmu 分配请求
        GpuMemoryRequest req;
        req.size = size;
        req.space_type = ADDRESS_SPACE_DEVICE;
        gpu_ioctl(GPGPU_ALLOC_MEM, &req);
        
        return allocations_[handle].get();
    }
    
    void deallocate(uint64_t handle) {
        allocations_.erase(handle);
        gpu_ioctl(GPGPU_FREE_MEM, &handle);
    }
    
private:
    std::unordered_map<uint64_t, std::unique_ptr<char[]>> allocations_;
    std::atomic<uint64_t> next_handle_{1};
};
```

### 决策结果标记

```
[ ] 选项 A - TaskRunner 层统一（违反关注点分离）
[X] 选项 B - Runtime Stub 层独立（推荐方案，职责清晰）
[ ] 选项 C - UsrLinuxEmu 底层托管（适合最终生产环境）
```

**CTO 批准**: 2026-04-07 ✅

---

## 📝 决策记录（已批准）

### D1. 集成路径选择

```
☑ B - 统一调度器（批准）
```

### D2. UsrLinuxEmu 接口扩展策略

```
☑ C - 分层设计（批准）
```

### D3. Barrier/Event 同步模型统一策略

```
☑ A - 统一内部表示（批准）
```

### D4. 资源管理器层级划分

```
☑ B - Runtime Stub 层独立（批准）
```

**CTO 签字**: 老板  
**批准日期**: 2026-04-07  
**备注**: 其他部分批准通过，更新 TaskRunner 架构文档为 DDS-v1.1-final

---

## ✅ 已完成行动

1. **详细设计文档（DDS）**：`DDS-CUDA-Vulkan-Runtime-v1.1-final.md` ✅
2. **实施计划**：Phase 0-3 时间表已制定 ✅
3. **下一步**: 启动 Phase 0 环境准备

---

**END OF DECISION FRAMEWORK**
