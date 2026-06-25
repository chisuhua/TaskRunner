---
SCOPE: SHARED
STATUS: ACCEPTED
---

# GPU Queue ID 模式调研综合：AMD ROCm + NVIDIA CUDA

> **状态**: ✅ ACCEPTED (2026-06-26, H-3.8 协调建立)
> **作者**: TaskRunner owner (委托调研, bg_5826c044)
> **目标**: 为 H-3.8 Issue #1 (stream_id u32 → u64 ABI 拓宽) 提供跨平台参考
> **关联**: openspec change `2026-06-26-h3-8-issue-1-coordination` §调研支撑

## 1. AMD ROCm 模式

### 1.1 KFD (Kernel Fusion Driver)

**Handle 类型**: `HSA_QUEUEID` (typedef `HSAuint64`)

```cpp
// AMD ROCm KFD 内部实现
// HSA_QUEUEID 实际上是 `struct queue*` 的 u64 指针值

typedef uint64_t HSA_QUEUEID;  // 64-bit opaque handle

// KFD 内部管理
struct queue {
    uint64_t queue_id;        // 自增 u64，但通常从 1 开始
    void* ring_buffer;        // GPU 可见的环形缓冲区
    void* doorbell;           // 用户态 doorbell 指针
    ...
};

// 创建 queue 时返回指针值作为 handle
HSA_QUEUEID queue_create(...) {
    struct queue* q = allocate_queue(...);
    return (HSA_QUEUEID)q;    // 指针 → u64 handle
}
```

**关键特性**:
- **间接引用**: handle 是指针值，不是 packed index
- **无 u32 截断**: 完整 64-bit 指针值保存
- **无 deprecated alias**: 新版本直接升级，不保留旧字段
- **分配策略**: 指针值天然唯一，不需要 central allocator

### 1.2 HSA Runtime

**Handle 类型**: `hsa_queue_t*` (64-bit pointer)

```cpp
// AMD ROCm HSA Runtime API

typedef struct hsa_queue_s {  // 不透明结构体
    hsa_queue_type_t type;
    uint32_t size;             // 队列深度
    uint64_t doorbell_signal;  // doorbell 信号 handle
    ...
} hsa_queue_t;

// 用户态 API
hsa_status_t hsa_queue_create(..., hsa_queue_t** queue);  // 返回指针
// queue 是 hsa_queue_t* 的地址，handle 实际就是指针值
```

**关键特性**:
- **不透明指针**: `hsa_queue_t*` 是不透明结构体指针
- **用户态直接访问**: doorbell 通过 `queue->doorbell_signal` 直接写（mmap 路径）
- **ioctl 路径**: `hsaKmtQueueRingDoorbell` 用于后台 doorbell（fallback）

## 2. NVIDIA CUDA 模式

### 2.1 libcuda (CUDA Runtime)

**Handle 类型**: `CUstream` (typedef `CUstream_st*`)

```cpp
// NVIDIA CUDA 内部实现

typedef struct CUstream_st* CUstream;  // 不透明指针

// 创建 stream 时返回 opaque pointer
CUresult cuStreamCreate(CUstream* phStream, unsigned int Flags) {
    CUstream_st* stream = new CUstream_st(...);
    *phStream = stream;  // 返回指针值作为 handle
    return CUDA_SUCCESS;
}
```

**关键特性**:
- **不透明指针**: `CUstream` 是 `CUstream_st*` 的 typedef
- **用户态不可直接解引用**: 必须通过 CUDA API 访问
- **无 u32 截断**: 完整 64-bit 指针值
- **无 deprecated alias**: 新版本直接升级

### 2.2 UVM (Unified Virtual Memory)

**Handle 类型**: `uvm_va_block_region_t` (u64 packed handle)

```cpp
// NVIDIA UVM 驱动

typedef uint64_t uvm_va_block_region_t;  // packed handle

// 分配策略：bitmap + 递增计数器
static uint64_t next_region_id = 0;

uvm_va_block_region_t allocate_region(...) {
    return ++next_region_id;  // 自增 u64，无上限
}
```

**关键特性**:
- **packed handle**: 不是指针，是自增 u64 计数器
- **major version bump**: ABI 变更时通过 major version bump 通知 caller
- **无 deprecated alias**: 不保留旧字段

## 3. 对比分析

| 特性 | AMD ROCm KFD | AMD ROCm HSA | NVIDIA CUDA | NVIDIA UVM | TaskRunner 当前 | **推荐** |
|------|-------------|-------------|-------------|-----------|----------------|---------|
| **Handle 类型** | u64 pointer | u64 pointer | u64 pointer | u64 packed | u32 ABI + u64 内部 | **u64 packed** |
| **模式** | 间接引用 | 间接引用 | 间接引用 | packed index | R2 mapping 截断 | **packed index** |
| **u32 截断** | ❌ 无 | ❌ 无 | ❌ 无 | ❌ 无 | ✅ 有 | **❌ 无** |
| **deprecated alias** | ❌ 无 | ❌ 无 | ❌ 无 | ❌ 无 | N/A | **✅ 有 (6月过渡)** |
| **分配策略** | 指针值 | 指针值 | 指针值 | 自增计数器 | 自增计数器 | **自增计数器** |
| **生态地位** | 生产驱动 | 生产 Runtime | 生产 Runtime | 生产驱动 | 测试 emulator | **测试 emulator** |
| **向后兼容策略** | major version bump | major version bump | major version bump | major version bump | N/A | **deprecated alias + 6月过渡** |

## 4. 对 TaskRunner 的启示

### 4.1 为什么 TaskRunner 当前是反模式

1. **AMD/NVIDIA 都用 u64**: 所有主流 GPU 平台都用 u64 handle，没有 u32 截断
2. **R2 mapping 是 workaround**: TADR-007 的 R2 mapping 是为了解决 H-3 阶段 u32 限制，但长期看是技术债
3. **生产上限问题**: ~40 亿 queue 上限在真实场景可能不够（multi-tenant、长时服务）

### 4.2 为什么推荐 packed index + deprecated alias

1. **与 NVIDIA UVM 最接近**: TaskRunner 的 `next_queue_handle_` 自增计数器模式与 UVM 一致
2. **deprecated alias 适合 emulator**: 测试 emulator 的 caller 数量可控，6 月过渡期足够
3. **不破坏现有测试**: 旧测试代码传 u32 仍工作（通过 `stream_id_compat`）
4. **未来扩展**: `flags_extended` 为 Phase 3 预留 flag 空间

### 4.3 为什么不采用指针间接引用模式

1. **TaskRunner 是 emulator**: 不是真实 GPU 驱动，不需要指针解引用
2. **MockGpuDriver 需要 packed index**: 测试 fixture 用自增计数器更自然
3. **跨平台一致性**: packed index 模式在模拟器和测试 fixture 中更常见

## 5. 结论

**推荐方案**: `__u64 stream_id` + `__u32 stream_id_compat` deprecated alias + 6 月过渡期

- 与 AMD/NVIDIA 生态一致（u64 handle）
- 与 TaskRunner 内部 `next_queue_handle_` 自增计数器模式一致
- 向后兼容（deprecated alias 保证旧 caller 工作）
- 6 月过渡期足够覆盖所有 caller 升级
- 不引入新依赖

## 6. 关联文档

- **openspec change**: `2026-06-26-h3-8-issue-1-coordination/`
- **tadr-105**: `docs/test-fixture/adr/tadr-105-h7-deferred.md` §Issue #1
- **tadr-007 R2 mapping**: `docs/test-fixture/adr/tadr-007-r2-mapping.md`
- **ADR-034**: `UsrLinuxEmu/docs/00_adr/adr-034-h7-deferred-registry.md` §Issue #1

## 7. 维护

- **Owner**: TaskRunner owner
- **Reviewers**: UsrLinuxEmu owner（跨仓 review）
- **更新时机**: 
  - PR 1 merged 后：更新 §4 实施状态
  - PR 2 merged 后：更新 §5 结论
- **关联实施**: TaskRunner 测试套件 + UsrLinuxEmu 测试套件