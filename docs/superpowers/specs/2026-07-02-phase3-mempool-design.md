---
SCOPE: UMD-EVOLUTION
STATUS: DRAFT
DESIGN_DATE: 2026-07-02
DESIGN_AUTHOR: Sisyphus
RELATED: ../../plans/2026-07-02-phase3-prep-design-notes.md
RELATED_TADR: tadr-205 (Phase 3 deferred)
RELATED_API: CUDA Driver API Memory Management §5 (cuMemPool*)
PHASE: 3.2 (Memory Pool)
PRIORITY: P0
BACKEND_DEP: UsrLinuxEmu Stage 1.3 UVM
---

# Phase 3.2 — cuMemPool* 内存池接口设计

> **Status**: DRAFT — 设计稿,未实现。本文仅做接口层面与 shim 适配设计,不包含真实代码。
> **触发依赖**: UsrLinuxEmu Stage 1.3 UVM 后端;参考 [`../plans/2026-07-02-phase3-prep-design-notes.md`](../../plans/2026-07-02-phase3-prep-design-notes.md) 中 P0 子项 3.2。

## 1. 背景与目标

CUDA Driver API 在 11.2 之后引入了异步内存池(`CUmemPool`)机制,允许用户:

- 把一块大的虚拟地址范围预留为池,在池内做细粒度异步分配/释放。
- 跨进程共享内存池(`cuMemPoolExportToShareableHandle` / `cuMemPoolImportFromShareableHandle`)。
- 在释放时复用底层内存,降低 `cuMemAlloc` 的延迟。

当前 shim 层(`cu_mem.cpp`)只暴露了同步的 `cuMemAlloc` / `cuMemFree`(后者为 no-op,见 `src/umd/libcuda_shim/cu_mem.cpp:34-39`),且所有分配走 `CudaRuntimeApi::malloc`,没有池化、异步或跨进程能力。

Phase 3.2 的目标:

1. 让 shim 端暴露完整的 `cuMemPool*` 接口,真实 CUDA 程序可以编译链接通过并以 stub 形式返回。
2. 设计分配/回收策略,使异步语义与现有 `CudaRuntimeApi` / `CudaScheduler` 解耦。
3. 在 `IGpuDriver` 上预留 pool 相关的 5 个方法扩展点(参考 Phase 2 的 31 方法契约)。

## 2. API 表面(全部为 DRAFT)

下表列出 Phase 3.2 计划实现的 cuMemPool* 接口、当前 stub 状态,以及 Phase 3 计划的 shim 行为。

| CUDA Driver API | 当前 stub 状态 | Phase 3.2 设计行为 |
|---|---|---|
| `cuMemPoolCreate(CUmemoryPool* pool, const CUmemPoolProps* props)` | STUB(`NOT_IMPLEMENTED`) | 分配 `CUmemoryPool` handle,记录池属性,挂到全局 `PoolTable`。**不真正预留 VA**,仅保留 handle 跟踪。 |
| `cuMemPoolDestroy(CUmemoryPool pool)` | STUB | 从 `PoolTable` 移除。池内分配保留为孤儿(`cuMemFree` 仍为 no-op,Phase 1 限制)。 |
| `cuMemAllocFromPoolAsync(CUdeviceptr* dptr, size_t size, CUmemoryPool pool, CUstream stream)` | STUB | 同 `cuMemAlloc` 走 `CudaRuntimeApi::malloc`,但额外记录来源 pool。`stream` 暂时忽略(同步语义)。 |
| `cuMemFreeAsync(CUdeviceptr dptr, CUstream stream)` | STUB | 同 `cuMemFree`,no-op。 |
| `cuMemPoolExportToShareableHandle(void* handle, CUmemoryPool pool, CUmemAllocationHandleType handleType, unsigned long long flags)` | STUB | 标记 `CUDA_ERROR_NOT_SUPPORTED`(`handleType` 不是 `CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR` 时也返回 `INVALID_VALUE`)。 |
| `cuMemPoolImportFromShareableHandle(CUmemoryPool* pool, void* handle, CUmemAllocationHandleType handleType, unsigned long long flags)` | STUB | 同上,`NOT_SUPPORTED`。 |
| `cuMemPoolSetAttribute(CUmemoryPool pool, CUmemPoolAttribute attr, void* value)` | STUB | 属性白名单(`CU_MEMPOOL_ATTR_RELEASE_THRESHOLD` / `CU_MEMPOOL_ATTR_REUSE_FOLLOW_EVENT_DEPENDENCIES` 等)接受,其他返回 `INVALID_VALUE`;仅保存值不生效。 |
| `cuMemPoolGetAttribute(CUmemoryPool pool, CUmemPoolAttribute attr, void* value)` | STUB | 返回保存的属性值;未设置时返回默认值。 |
| `cuMemPoolSetAccess(CUmemoryPool pool, const CUmemAccessDesc* desc, size_t count)` | STUB | `NOT_IMPLEMENTED`,多设备不在 Phase 3.5 范围。 |
| `cuMemPoolGetAccess(CUmemAccessFlags* flags, CUmemoryPool pool, CUmemLocation* location)` | STUB | 同上。 |
| `cuMemPoolTrimTo(CUmemoryPool pool, size_t minBytesToKeep)` | STUB | 接受调用,返回 `Success`,trim 留待后端实现。 |

> **设计原则**:仅暴露 handle 与属性跟踪,不真的做 VA 预留和池化。Phase 3.2 的内存模型仍是"扁平分配 + no free",与现有 Phase 1 兼容。

## 3. 数据结构(DRAFT)

```
namespace async_task::umd::shim {

struct PoolTable {
  std::atomic<std::uint64_t> next_id{1};
  // pool handle -> 属性副本
  std::unordered_map<CUmemoryPool, PoolRecord> pools;
  // device pointer -> 所属 pool(用于 cuMemFreeAsync 行为对齐)
  std::unordered_map<CUdeviceptr, CUmemoryPool> allocation_source;
  std::mutex mu;
};

struct PoolRecord {
  CUmemPoolProps props;        // 副本:类型/release threshold 等
  std::unordered_map<CUmemPoolAttribute, std::vector<std::uint8_t>> attrs;
  std::size_t reserved_bytes{0};   // 占位,Phase 3.2 不真的预留
  std::size_t used_bytes{0};       // 占位
};

}
```

设计点:

- `next_id` 从 1 起;`0` 视作 `CU_MEMPOOL_INVALID`(对齐现有 stream/event 风格)。
- 属性存储用 `vector<uint8_t>` 序列化,避免 union 模板膨胀。
- `allocation_source` 是 Phase 3.2 新增;为未来 `cuMemFreeAsync` 走 pool 路径做准备。

## 4. 关键设计决策

### 4.1 池大小追踪与 VA 范围 — UsrLinuxEmu B-2 决策（2026-07-05）

**问题**:真 CUDA 池会跟踪 reserved / used / peak。shim 没有真实后端,无法维护真实水位。

**UsrLinuxEmu 决策**（**Option B — VA 子范围预留**,**强制采用**）:

- `cuMemPoolCreate(props, &handle)` 时,**必须**从 `va_space_handle` 预留 `props.maxSize` 大小的 VA 子范围。
- pool 的 `va_base` 和 `va_limit` 由 UsrLinuxEmu 侧在 `sim_mem_pool_props_t` 中填充(承诺字段由 UsrLinuxEmu 提供)。
- `cuMemAllocFromPoolAsync(handle, size, &dptr)` 返回 pool 预留子范围内的 VA。
- `cuMemPoolTrim(handle, minBytesToKeep)` 释放 pool 的**上半部分**子范围回 VA Space(保留 `minBytesToKeep` 在低端)。
- **禁止 Option A**（pool 作为 `gpu_buddy` 的薄包装）— UsrLinuxEmu 反馈 B-2：Option A 无法强制 `maxSize` 上限（Scenario 3.6 "alloc exceeding pool size → -ENOSPC" 不可实现）。

**TaskRunner shim 要求**:

- 在 GpuDriverClient shim 中,**将 `props.maxSize` 视为 pool 容量上限**（**不**假设 buddy heap 全局限制）。
- 接收 `sim_mem_pool_props_t.va_base` / `va_limit` 字段（由 UsrLinuxEmu 提供），用于：
  - 客户端可见性（用户可查询 pool 边界）
  - 后续 `cuMemPoolTrim` 决策（保留低端 VA 子范围）

**测试样例要求**（新增 — UsrLinuxEmu B-2）:

- 两个 pool 同一 VA Space 不重叠子范围
- Pool A alloc + Pool B alloc → 都成功，VA 不同
- Pool A alloc 超过 `maxSize` → 返回 `CUDA_ERROR_OUT_OF_MEMORY`（**不**是 `INVALID_VALUE`）

**决策编号 D-MP-1**（**修订**）。

**辅助决策**:

- `used_bytes` 在 `cuMemAllocFromPoolAsync` 成功时累加,在 `cuMemFreeAsync` 时递减(即便实际 no-op,保持账面一致)。
- `reserved_bytes = props.maxSize`(在 pool 创建时记录)。
- 不暴露 `cuMemPoolTrimTo` 的真实 trim 效果(但 trim 调用本身可接受返回 `Success`,仅更新账面 `reserved_bytes = minBytesToKeep`)。

### 4.2 异步分配策略

**问题**:`cuMemAllocFromPoolAsync` 名字暗示异步,当前 `CudaRuntimeApi::malloc` 是同步(`wait_fence` 路径)。

**决策**:

- shim 层仍然同步返回(保留 Phase 1 同步语义),`stream` 参数忽略。
- 行为不变:`CudaRuntimeApi::malloc` 内部已经 `submit_mem_alloc` + `wait_fence`,等价于"阻塞直到 fence"。
- 不引入真异步队列;Phase 3.2 仅在接口签名上对齐,实现保持同步。
- 决策编号 **D-MP-2**。

### 4.3 与 `IGpuDriver` 的 VA Space 集成

**问题**:`cuMemAlloc` 底层走 `CudaScheduler.submit_mem_alloc`,该方法最终落到 `IGpuDriver` 的 `create_va_space` / `va_space_alloc`(`tadr-301` 31 方法)。pool 是否需要独立 VA Space?

**决策**:

- **不引入新 VA Space**。每个 pool 仅是 shim 层的 handle/属性跟踪,实际分配仍走共享的 `CudaRuntimeApi` 持有的 `va_space_handle_`。
- 在 `IGpuDriver` 接口层,仅**预留**以下 5 个方法扩展点(本 Phase 不实现):
  - `create_memory_pool(props) -> handle`
  - `destroy_memory_pool(handle)`
  - `pool_alloc(handle, size) -> addr`
  - `pool_free(addr)`
  - `export_pool(handle) -> fd`
- 决策编号 **D-MP-3**。

### 4.4 跨进程共享(Export/Import)

**问题**:`cuMemPoolExportToShareableHandle` 真 CUDA 需要 fd 或 `HANDLE`(Windows)。当前 Phase 2 + UsrLinuxEmu 没有 IPC 路径。

**决策**:

- 全部返回 `CUDA_ERROR_NOT_SUPPORTED`,即使属性是 `CU_MEM_HANDLE_TYPE_NONE` 也拒绝。
- **不**用 `CUDA_ERROR_INVALID_VALUE` 替代——区分"参数错"和"功能不支持"对调用方排错更重要。
- 决策编号 **D-MP-4**。

### 4.5 属性语义 — UsrLinuxEmu F-2 决策（2026-07-05）

**问题**:Pool 有 10+ 属性(`CU_MEMPOOL_ATTR_*`),其中部分与硬件/驱动行为强耦合。

**决策**:

- 仅 `CU_MEMPOOL_ATTR_RELEASE_THRESHOLD` 接受数值(0 ~ props.maxSize),保存不生效。
- `CU_MEMPOOL_ATTR_REUSE_FOLLOW_EVENT_DEPENDENCIES` 接受 0/1。
- 其余属性返回 `CUDA_ERROR_INVALID_VALUE`,避免假装支持。
- 决策编号 **D-MP-5**。

**IOCTL attr value blob 布局**（**UsrLinuxEmu F-2 — 必须遵循**）:

```c
struct gpu_mem_pool_attr_args {
  uint64_t pool_handle;
  uint32_t attr;
  uint32_t _reserved;      /* padding, must be 0 */
  uint64_t value[4];       /* 32-byte in/out blob */
};
```

| attr | value_size | value[0] | value[1..3] |
|------|------------|----------|-------------|
| `RELEASE_THRESHOLD (1)` | 8 | `uint64_t release_threshold` (bytes) | must be 0 |
| `REUSE_FOLLOW_EVENT_DEPS (2)` | 4 | `uint32_t enable` (0/1) | must be 0 |

**TaskRunner GpuDriverClient 实现要求**:

- `mem_pool_set_attr` / `mem_pool_get_attr` forwarding **必须**将 typed value pointer 序列化到 `value[0]` slot + 设 `_reserved=0` + 其余 `value[1..3]` 填 0。
- 不遵守布局会导致 sim 层反序列化错误。

## 5. 与 `cu_stub_table.inc` 的协同

`tools/generate_cu_stubs.py` 中 `CRITICAL_APIS_IMPL_REQUIRED` 字典需要在 Phase 3.2 开始时扩展,把以下条目从 `STUB` 改为 `REAL_IMPL in cu_mem_pool.cpp`:

```
"cuMemPoolCreate": "cu_mem_pool.cpp",
"cuMemPoolDestroy": "cu_mem_pool.cpp",
"cuMemAllocFromPoolAsync": "cu_mem_pool.cpp",
"cuMemFreeAsync": "cu_mem_pool.cpp",
"cuMemPoolExportToShareableHandle": "cu_mem_pool.cpp",
"cuMemPoolImportFromShareableHandle": "cu_mem_pool.cpp",
"cuMemPoolSetAttribute": "cu_mem_pool.cpp",
"cuMemPoolGetAttribute": "cu_mem_pool.cpp",
"cuMemPoolSetAccess": "cu_mem_pool.cpp",
"cuMemPoolGetAccess": "cu_mem_pool.cpp",
"cuMemPoolTrimTo": "cu_mem_pool.cpp",
```

新文件 `src/umd/libcuda_shim/cu_mem_pool.cpp` 是本子项新增,沿用现有文件头注释风格(`// SCOPE: UMD-EVOLUTION`)。

## 6. 错误码对照表

| 场景 | 返回码 |
|---|---|
| 任一 out-pointer 为 null(`dptr`/`pool`/`handle`/`value`) | `CUDA_ERROR_INVALID_VALUE` |
| `pool` handle 在 `PoolTable` 中找不到 | `CUDA_ERROR_INVALID_HANDLE` |
| 属性 id 超出白名单 | `CUDA_ERROR_INVALID_VALUE` |
| 属性值越界(`RELEASE_THRESHOLD > maxSize`) | `CUDA_ERROR_INVALID_VALUE` |
| `handleType` 不是 `CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR` | `CUDA_ERROR_INVALID_VALUE` |
| Export/Import | `CUDA_ERROR_NOT_SUPPORTED` |
| `cuMemPoolSetAccess`/`GetAccess` | `CUDA_ERROR_NOT_IMPLEMENTED` |

## 7. 实现顺序

1. **扩展 `generate_cu_stubs.py`**:把 11 个 cuMemPool* 加入 `CRITICAL_APIS_IMPL_REQUIRED`,重新生成 `cu_stub_table.inc`(机械动作,无设计风险)。
2. **新增 `cu_mem_pool.cpp`** 与配套匿名命名空间 `PoolTable` / `PoolRecord`。
3. **实现 handle 分配 / 释放**:`cuMemPoolCreate` / `cuMemPoolDestroy`(无属性写入)。
4. **实现 `cuMemAllocFromPoolAsync`**:复用 `runtime()->malloc`,在 `allocation_source` 中记录 pool。
5. **实现 `cuMemFreeAsync`**:no-op,与 `cuMemFree` 一致。
6. **实现属性 Set/Get**:白名单内可读可写,白名单外拒绝。
7. **Export/Import**:统一返回 `NOT_SUPPORTED`。
8. **SetAccess/GetAccess/TrimTo**:`NOT_IMPLEMENTED` 或 accept-noop(已决策)。
9. **测试**:在 `tests/umd/libcuda_shim/` 加 `test_cu_mempool.cpp`,覆盖创建/属性/分配/拒绝路径。
10. **文档**:`docs-audit.sh` 应识别新文件,无新增 ADR(TADR 仍为 DRAFT)。

## 8. 设计问题(待 Phase 3 kickoff 时回答)

| Q# | 问题 | 倾向 | 阻塞? |
|---|---|---|---|
| Q-MP-1 | 是否引入独立 `CUDA_ERROR_NOT_INITIALIZED` 处理 `cuMemPoolCreate` 早于 `cuInit`? | 不引入,与现有 shim 一致(其他 cu* 早于 `cuInit` 仅返回 `UNKNOWN`)。 | 否 |
| Q-MP-2 | 属性写操作要不要返回 `CUDA_ERROR_NOT_PERMITTED` 表示 "saved but not applied"? | 不引入新码,统一 `SUCCESS`,文档注释说明语义。 | 否 |
| Q-MP-3 | 是否需要在 `PoolTable` 上提供 `set_global_pool_default()` 给 `cuMemAllocAsync` 后续扩展? | 预留接口但不实现。 | 否 |
| Q-MP-4 | `cuMemAllocFromPoolAsync` 在 `pool == nullptr` 时是默认池还是错误? | 错误,`INVALID_HANDLE`。 | 否 |
| Q-MP-5 | 是否要记录每个 pool 的 `stream` 亲和性(`pools_per_stream`)? | 否,Phase 3.2 同步语义不区分。 | 否 |

## 9. 已知 Open Issues

- **O-MP-1**:`cuMemFreeAsync` 是 no-op,但 `allocation_source` 表会累积孤儿条目。无影响(进程退出清空),但严格审计下需要文档化。
- **O-MP-2**:`PoolRecord.props` 在 Phase 3.2 不真的预留 VA,任何依赖 `reserved_bytes > 0` 的客户端会读到 0。这与 Q1(池大小追踪)决策一致。
- **O-MP-3**:UsrLinuxEmu Stage 1.3 UVM 未启动前,即使 IGpuDriver 5 个预留方法也不会被任何 .cpp 调用(空转)。
- **O-MP-4**:`cuMemPoolExportToShareableHandle` 的 `flags` 当前忽略(`0` 即不接受),与决策 D-MP-4 一致。

## 10. 范围禁令(从 prep notes 继承)

- ❌ Vulkan 内存池(沿用 Q3:no Vulkan)。
- ❌ 真 ELF/CUBIN 解析(D-3)。
- ❌ 替换 `CudaStub` 为真设备后端。
- ❌ 真多设备支持(`SetAccess`/`GetAccess` 留 `NOT_IMPLEMENTED`)。

## 11. 参考

- [`../plans/2026-07-02-phase3-prep-design-notes.md`](../../plans/2026-07-02-phase3-prep-design-notes.md) §Priority Matrix / Trigger Conditions / Effort Estimates。
- [`../specs/2026-06-30-umd-evolution-redesign.md`](../specs/2026-06-30-umd-evolution-redesign.md) Phase 3 范围。
- [`../../../UmD-evolution-redesign.md`](../../../../docs/00_adr/umd-evolution-redesign.md) — 上游 UsrLinuxEmu 端设计意图(若存在)。
- `src/umd/libcuda_shim/cu_mem.cpp` — Phase 1 / 2 内存 API 实现,Phase 3.2 在此基础上扩展。
- `include/umd/cuda_runtime_api.hpp` — `CudaRuntimeApi` 现状(`malloc` 同步语义)。
- `tools/generate_cu_stubs.py` — stub 表生成机制。

---

**Status**: DRAFT(Phase 3.2 kickoff 时晋升为 ACCEPTED)。
**Last Updated**: 2026-07-02
**Next Action**: 等 UsrLinuxEmu Stage 1.3 UVM 启动或显式触发。