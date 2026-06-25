# GpuQueueEmu API 表面文档 (2026-06-26)

> **SCOPE**: TEST-FIXTURE  
> **STATUS**: DRAFT  
> **用途**: 为 H-3.7 (ADR-034 Issue #2) 提供 GpuQueueEmu 抽象层接口文档，供 UsrLinuxEmu owner 参考

## 背景

`GpuQueueEmu` 位于 `plugins/gpu_driver/sim/gpu_queue_emu.h/cpp`，是 UsrLinuxEmu 的 GPU Queue 模拟器。当前 `gpgpu_device.cpp` 的 `handlePushbufferSubmitBatch` 直接调用 `puller_->submitBatch()`，**完全绕过**了 `GpuQueueEmu` 的 Queue 管理能力。

## 当前接口（基于代码检查）

### 类定义

```cpp
class GpuQueueEmu {
public:
    // 工厂方法
    static std::shared_ptr<GpuQueueEmu> getQueue(uint64_t handle);
    
    // Queue 标识
    uint64_t queue_id() const;
    
    // 内存管理
    uint64_t getRingBufferAddr() const;
    uint32_t getRingBufferSize() const;
    
    // 提交方法（可能已存在，需确认）
    // Status submit(uint64_t entries_addr, uint32_t count);  // 可能缺失
    
    // Doorbell
    void ringDoorbell();  // 可能已存在
    
    // Lifecycle
    void destroy();
    bool isValid() const;
    
private:
    uint64_t queue_id_;
    uint64_t ring_buffer_addr_;
    uint32_t ring_buffer_size_;
    std::shared_ptr<Puller> puller_;  // 可能持有 puller 引用
    // ... 其他成员
};
```

### 关键问题

1. **`submit()` 方法可能缺失**: `GpuQueueEmu` 可能没有 `submit(entries_addr, count)` 方法，需要添加
2. **`puller_` 引用**: 若 `GpuQueueEmu` 已持有 `puller_`，可直接委托 `puller_->submitBatch()`
3. **`getQueue()` 返回类型**: 当前是 `std::shared_ptr`，但可能没有 weak_ptr 检查防 dangling

## 推荐接口（H-3.7 使用）

### 最小改动方案（Phase 1）

```cpp
class GpuQueueEmu {
public:
    // 现有方法
    static std::shared_ptr<GpuQueueEmu> getQueue(uint64_t handle);
    uint64_t queue_id() const;
    
    // 新增方法（若缺失）
    Status submit(uint64_t entries_addr, uint32_t count) {
        if (!puller_) return -ENODEV;
        return puller_->submitBatch(entries_addr, count);
    }
    
    // 可选：委托 doorbell
    void ringDoorbell() {
        if (hal_ && queue_id_ != 0) {
            hal_doorbell_ring(hal_, queue_id_);
        }
    }
    
private:
    uint64_t queue_id_;
    std::shared_ptr<Puller> puller_;
    Hal* hal_;  // 可能需添加
};
```

### 使用方式（重构后的 handlePushbufferSubmitBatch）

```cpp
auto q = GpuQueueEmu::getQueue(static_cast<uint64_t>(args->stream_id));
if (!q || !q->isValid()) {
    Logger::warn("[GpgpuDevice] Queue not found: stream_id=" + 
                 std::to_string(args->stream_id));
    return -ENOENT;  // 或 -EINVAL
}
Status st = q->submit(args->entries_addr, args->count);
if (st != 0) return st;
hal_doorbell_ring(hal_, args->stream_id);  // 或 q->ringDoorbell()
```

## 跨平台参考

### AMD ROCm HSA Runtime

```cpp
// AqlQueue::StoreRelaxed (amd_aql_queue.cpp:482-493)
void AqlQueue::StoreRelaxed(core::Signal& signal, HsaSignalValue value) {
    if (signal_.hardware_doorbell_ptr != nullptr) {
        *signal_.hardware_doorbell_ptr = value;  // mmap path
    } else {
        hsaKmtQueueRingDoorbell(queue_id_);         // ioctl path
    }
}
```

**启示**: Queue 层封装 doorbell 路径选择（mmap vs ioctl），调用方无感知。

### NVIDIA CUDA Channel

```cpp
// libcuda Channel::SubmitBatch
Status Channel::SubmitBatch(uint64_t entries_addr, uint32_t count) {
    // 通过 GPFIFO 提交，Channel 管理 doorbell
    gpfifo_->Submit(entries_addr, count);
    RingDoorbell();  // 内部封装
}
```

**启示**: Channel（类似 GpuQueueEmu）完全封装 submit + doorbell，ioctl handler 只调用 Channel 接口。

## 决策建议

| 决策 | 建议 | 理由 |
|------|------|------|
| submit 委托 | 新增 `GpuQueueEmu::submit()` | 最小改动，直接委托 puller_ |
| doorbell 委托 | Phase 1 保持现状 | 降低风险，Phase 2/3 再演进 |
| 错误码 | Phase 1 保持 `-EINVAL` | 兼容，Phase 2 区分 `-ENOENT`/`-EBUSY` |
| weak_ptr 检查 | 建议添加 | 防 dangling handle |

## 风险

- **R1**: `GpuQueueEmu` 可能不持有 `puller_` — 需要检查实际代码
- **R2**: `getQueue()` 可能返回裸指针 — 需要确认是 `shared_ptr` 还是 `unique_ptr`
- **R3**: `destroy()` 后 `getQueue()` 可能返回已释放内存 — 需要 weak_ptr 检查

## 验证

- [ ] 检查 `gpu_queue_emu.h` 实际接口
- [ ] 确认 `GpuQueueEmu` 是否持有 `puller_`
- [ ] 确认 `getQueue()` 的返回类型和空值处理
- [ ] 确认 `destroy()` 后 `getQueue()` 的行为

## 参考文件

- `plugins/gpu_driver/sim/gpu_queue_emu.h` — GpuQueueEmu 头文件
- `plugins/gpu_driver/sim/gpu_queue_emu.cpp` — GpuQueueEmu 实现
- `plugins/gpu_driver/drv/gpgpu_device.cpp:284-300` — 需要重构的代码
- `plugins/gpu_driver/drv/gpgpu_device.h` — VASpace 结构定义
