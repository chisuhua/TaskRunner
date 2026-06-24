---
SCOPE: TEST-FIXTURE
STATUS: ACCEPTED
REPLACES: tadr-004
---

# TADR-004: 资源管理器层级 — Runtime Stub 层独立追踪 (B 方案)

**状态**: ✅ Accepted (retroactive)
**日期**: 2026-04-07 (决策) / 2026-06-23 (retroactive TADR 化)
**提案人**: DevMate
**评审者**: 老板 (CTO)
**关联 ADR (UsrLinuxEmu)**: —
**关联 Change**: —
**关联 Source**: [`docs/decision-frame-cuda-vulkan-runtime.md`](../../archive/2026-04-07-decision-frame-cuda-vulkan-runtime.md) §D4 (lines 199-262)

---

## Context

虚拟地址空间管理（Host 侧 tracker ↔ Device 侧物理地址分配）应归哪一层？三种候选：

- **A. TaskRunner 层统一**（UnifiedScheduler 持有 VA Space）
- **B. Runtime Stub 层独立**（⭐ 推荐）：CUDA Tracker + Vulkan Resource Manager 各自维护
- **C. UsrLinuxEmu 底层托管**（BuddyAllocator 完全委托底层）

## Decision

选择 **B. Runtime Stub 层独立追踪**。

**CUDA 侧实现**（决策时点的设计示例）：

```cpp
// cuda_stub/src/memory_tracker.cpp
class CudaMemoryTracker {
public:
    void* allocate(size_t size) {
        auto handle = next_handle_.fetch_add(1);
        auto ptr = std::make_unique<char[]>(size);
        allocations_[handle] = std::move(ptr);
        // 通过 ioctl 告知 UsrLinuxEmu 分配请求
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

**理由**：
1. 符合各 API 原生生命周期管理语义（CUDA MemoryTracker 有自己的 refcount/logic）
2. 便于单元测试（Mock 底层分配器，专注验证 Stub 逻辑）
3. 违反少（TaskRunner 无需了解 GPU 领域知识）

## Consequences

### 正面

- ✅ 职责清晰（Stub 管生命周期，TaskRunner 管调度）
- ✅ 可独立测试每个 Stub
- ✅ 符合各 API 原生语义

### 负面 / 风险

- ⚠️ 可能存在 double-tracking（Stub 侧 + 底层）
- ⚠️ 跨 API 互操作需额外协调

### 实施路径备注

H-2.5 引入的 `CudaStub` mock 实现（见 [`src/cuda_stub.cpp`](../../src/cuda_stub.cpp)）使用 atomic handle + `unordered_map` 跟踪 VA Space 和 Queue（与原决策示例的 `CudaMemoryTracker` 模式一致）。VA Space 生命周期管理由 `CudaStub` 独立维护，符合本 TADR 决策。

**关键一致性证据**（H-3 实施后）：
- `next_va_space_handle_` atomic 单调 from 1
- `next_queue_handle_` atomic 单调 from 1
- `va_space_map_` + `queue_map_` existence tracking
- `mock_state_mutex_` 保护 map

## 跨引用

- **关联 TADR**: TADR-001 (D1 统一调度器), TADR-006 (H-3 Phase 2 lifecycle)
- **关联 Source**: [`docs/decision-frame-cuda-vulkan-runtime.md`](../../archive/2026-04-07-decision-frame-cuda-vulkan-runtime.md):199-262

---

**最后更新**: 2026-06-23（H-4.5 docs governance cleanup TADR 化）
