---
SCOPE: UMD-EVOLUTION
STATUS: DRAFT
DESIGN_DATE: 2026-07-02
DESIGN_AUTHOR: Sisyphus
RELATED: ../specs/2026-06-30-umd-evolution-redesign.md
RELATED: ../specs/2026-07-02-phase3-mempool-design.md
RELATED: ../specs/2026-07-02-phase3-stream-capture-design.md
RELATED_DIR: src/umd/libcuda_shim/
RELATED_TOOL: tools/generate_cu_stubs.py
TARGET_AUDIENCE: TaskRunner 与 UsrLinuxEmu 跨仓开发者
---

# Shim Layer Architecture (libcuda_taskrunner.so)

> **Status**: DRAFT — 反映 Phase 2 完成后的当前实现,作为 Phase 3 设计与新人 onboarding 的总览。
> **本文目的**:用一个文档说清 `libcuda_taskrunner.so` 是怎么把 CUDA Driver API (`cu*`) 串到 `CudaRuntimeApi` → `CudaScheduler` → `IGpuDriver` 的。

## 1. 位置与职责

- **源目录**:`src/umd/libcuda_shim/`(UMD-EVOLUTION scope)。
- **构建产物**:`build/libcuda_taskrunner.so`(Phase 2 LD_PRELOAD 库)。
- **核心职责**:
  - 提供 ~200 个 `cu*` 符号,使得未修改的 CUDA 程序可通过 `LD_PRELOAD` 在 TaskRunner + UsrLinuxEmu 上运行。
  - 把 `cu*` 调用翻译成 `CudaRuntimeApi` 的高层 API(malloc / memcpy / launch_kernel)。
  - 不实现真 GPU 执行,沿用 `CudaStub` / `MockGpuDriver` / `GpuDriverClient` 三种后端中的可用者。

## 2. 整体架构图

```
 ┌─────────────────────────────────────────────────────────────────┐
 │   Real CUDA Program (e.g. NVIDIA CUDA Samples vectorAdd)       │
 │   链接 libcudart.so + 显式调用 cu*                              │
 └───────────────────────────┬─────────────────────────────────────┘
                             │ LD_PRELOAD=libcuda_taskrunner.so
                             ▼
 ┌─────────────────────────────────────────────────────────────────┐
 │   libcuda_taskrunner.so                                          │
 │                                                                  │
 │   ┌─────────────────────────────────────────────────────────┐   │
 │   │  cu_*.cpp (REAL_IMPL)                                   │   │
 │   │   cu_init.cpp    cu_ctx.cpp    cu_device.cpp             │   │
 │   │   cu_query.cpp   cu_module.cpp cu_mem.cpp                │   │
 │   │   cu_stream.cpp  cu_event.cpp  cu_launch.cpp             │   │
 │   └─────────────────────┬───────────────────────────────────┘   │
 │                         │ 强符号(覆盖 weak)                     │
 │   ┌─────────────────────▼───────────────────────────────────┐   │
 │   │  cu_stub_table.inc (~150 weak stubs)                    │   │
 │   │  __attribute__((weak, visibility("default")))           │   │
 │   │  → 返回 CUDA_ERROR_NOT_IMPLEMENTED                       │   │
 │   └─────────────────────────────────────────────────────────┘   │
 │                                                                  │
 │   内部全局状态:                                                  │
 │   - g_streams (StreamTable, mutex + atomic id)                  │
 │   - g_events  (EventTable,  chrono timestamps)                  │
 │   - g_ctx     (ContextState, thread-local stack)                │
 │   - g_runtime (CudaRuntimeApi*, lazy singleton)                 │
 └───────────────────────────┬─────────────────────────────────────┘
                             │ C++ API 调用
                             ▼
 ┌─────────────────────────────────────────────────────────────────┐
 │   async_task::umd::CudaRuntimeApi (Phase 1, DI)                  │
 │   - malloc / memcpy / launch_kernel / get_total_memory          │
 │   - 持有 CudaScheduler*,kernel_registry_,allocations_           │
 │   - mutex mu_ 保护所有可变状态                                    │
 └───────────────────────────┬─────────────────────────────────────┘
                             │ C++ API 调用
                             ▼
 ┌─────────────────────────────────────────────────────────────────┐
 │   taskrunner::CudaScheduler (test-fixture, H-3/H-5 verified)   │
 │   - submit_mem_alloc / submit_memcpy_h2d / submit_memcpy_d2h    │
 │   - submit_launch (LaunchParams)                                │
 │   - wait_fence (fence_id)                                       │
 │   - MemoryManager + fence 同步机制                              │
 └───────────────────────────┬─────────────────────────────────────┘
                             │ C++ API 调用
                             ▼
 ┌─────────────────────────────────────────────────────────────────┐
 │   async_task::gpu::IGpuDriver (shared, 31 methods)              │
 │   ├─ taskrunner::GpuDriverClient  (真实 ioctl → UsrLinuxEmu)    │
 │   ├─ taskrunner::CudaStub         (Phase 1 默认,内存返回固定值)│
 │   └─ MockGpuDriver                (单元测试)                    │
 └─────────────────────────────────────────────────────────────────┘
```

## 3. 符号导出机制

### 3.1 总体思路

LD_PRELOAD 的核心 trick:被 preload 的 .so 提供比 `libcuda.so` **更强**(或同等)的符号,优先解析到这个 .so。

CUDA Driver API 有 ~200 个符号(`cuInit` 到 `cuPointerGetAttribute`),TaskRunner 不可能也不需要全部实现。设计如下:

- **强符号(REAL_IMPL)**:由 `cu_*.cpp` 文件用正常 C 函数定义(带完整参数列表),自动获得强符号语义。
- **弱符号(STUB)**:由 `cu_stub_table.inc` 用 `__attribute__((weak, visibility("default")))` 声明。链接器若找到同名强符号就丢弃弱符号,否则保留弱符号作为 fallback。
- 强弱同时存在 → 链接器解析到强符号 → `cu_stub_table.inc` 中的弱版本成为"后备"。

### 3.2 `tools/generate_cu_stubs.py`

`tools/generate_cu_stubs.py` 是 stub 表的**唯一真源**,所有 `cu_stub_table.inc` 都是它的产物。流程:

1. **CRITICAL_APIS_IMPL_REQUIRED** 字典(行 22-112):列出**必须**在 `cu_*.cpp` 中真实实现的 API,以及它们所在的源文件。
2. **CUDA_DRIVER_APIS** 列表(行 116-193):列出 ~200 个 CUDA Driver API 符号(去重顺序保留)。
3. 对每个 API:
   - 若在 `CRITICAL_APIS_IMPL_REQUIRED` 中 → 写入 `// REAL_IMPL in <file>` 注释 + weak 声明。
   - 否则 → 写入 `// STUB` 注释 + weak 声明。
4. 输出到 `src/umd/libcuda_shim/cu_stub_table.inc`,自动生成,**禁止手编**。

注意:**所有弱符号都只有 `(void)` 参数列表**(`CUresult fn_name(void);`),因为它们只作为链接器的占位,不会被真调用。真正的 REAL_IMPL 函数在 .cpp 中用真实签名定义,**与弱声明的类型不冲突**(C 链接 + weak 解析规则)。

### 3.3 符号冲突的规避

`cu_init.cpp:8-10` 注释明确警告:

```
NOTE: does NOT include cu_stub_table.inc, because we define the strong symbol
here with full parameter list, and the .inc declares it as (void) — which
would cause conflicting types in C linkage.
```

`cu_init.cpp` 是唯一的强符号入口,它**不** include `cu_stub_table.inc`,避免 `cuInit(unsigned int)` 与 `cuInit(void)` 在同一翻译单元里冲突。

## 4. 文件组织与 REAL_IMPL 划分

`src/umd/libcuda_shim/` 下的文件按 CUDA Driver API 子域切分:

| 文件 | REAL_IMPL 函数 | STUB 函数 | 内部状态 |
|---|---|---|---|
| `cu_init.cpp` | `cuInit` | — | `g_stub` / `g_scheduler` / `g_runtime` 懒初始 singleton |
| `cu_device.cpp` | `cuDeviceGetCount` / `cuDeviceGet` / `cuDeviceComputeCapability` | — | — |
| `cu_query.cpp` | `cuDriverGetVersion` / `cuDeviceGetName` / `cuDeviceGetAttribute` / `cuDeviceTotalMem` / `cuCtxGetApiVersion` / `cuDevicePrimaryCtx*` / `cuGetErrorName` / `cuGetErrorString` | — | — |
| `cu_ctx.cpp` | `cuCtxCreate` / `cuCtxDestroy` / `cuCtxSetCurrent` / `cuCtxGetCurrent` / `cuCtxPushCurrent` / `cuCtxPopCurrent` / `cuCtxSynchronize` / `cuCtxGetDevice` / `cuCtxGetFlags` / `cuCtxGetCacheConfig` / `cuCtxSetCacheConfig` / `cuCtxGetSharedMemConfig` / `cuCtxSetSharedMemConfig` / `cuCtxGetLimit` / `cuCtxSetLimit` | — | `g_ctx`(ContextState:全局 mutex + thread-local stack) |
| `cu_module.cpp` | `cuModuleLoad` / `cuModuleUnload` / `cuModuleGetFunction` / `cuModuleGetTexRef` / `cuModuleGetSurfRef` / `cuModuleGetGlobal` | `cuModuleLoadData` / `cuModuleLoadDataEx` / `cuModuleLoadFatBinary` (内部返回 `NOT_IMPLEMENTED`) | — |
| `cu_mem.cpp` | `cuMemAlloc` / `cuMemFree` / `cuMemcpyHtoD` / `cuMemcpyDtoH` / `cuMemcpyDtoD` / `cuMemcpy` / `cuMemcpyAsync` / `cuMemsetD32` / `cuMemsetD8` / `cuMemAllocHost` / `cuMemFreeHost` / `cuMemAllocManaged` / `cuMemAllocPitch` / `cuMemGetInfo` / `cuMemGetAddressRange` | — | — |
| `cu_stream.cpp` | `cuStreamCreate` / `cuStreamDestroy` / `cuStreamSynchronize` / `cuStreamQuery` / `cuStreamWaitEvent` / `cuStreamWaitValue32` / `cuStreamWriteValue32` / `cuStreamAddCallback` / `cuStreamCreateWithPriority` / `cuStreamGetPriority` / `cuStreamGetFlags` / `cuStreamBeginCapture` / `cuStreamEndCapture` | `cuStreamGetCaptureInfo` | `g_streams`(StreamTable:atomic id + unordered_map + mutex) |
| `cu_event.cpp` | `cuEventCreate` / `cuEventDestroy` / `cuEventRecord` / `cuEventSynchronize` / `cuEventElapsedTime` / `cuEventQuery` | `cuEventCreateWithFlags` | `g_events`(EventTable:chrono time_point) |
| `cu_launch.cpp` | `cuLaunchKernel` / `cuLaunchKernelEx` / `cuLaunchCooperativeKernel` / `cuLaunchHostFunc` | — | — |

> `cu_stub_table.inc` 之外的所有 cu* 函数都是 `// STUB`,在 .inc 里以 weak 形式声明,由 `libcuda.so` 的标准实现兜底(若系统装了 CUDA Toolkit)。

## 5. 线程安全模型

shim 层采用**按资源类型一把全局锁**的简单模型:

| 资源类型 | 锁 | 持有者 | 文件 |
|---|---|---|---|
| Stream handles | `g_streams.mu` | `std::mutex` + `std::atomic<uint64_t> next_id` | `cu_stream.cpp` |
| Event handles + timestamps | `g_events.mu` | `std::mutex` + `std::atomic<uint64_t> next_id` | `cu_event.cpp` |
| Context handles | `g_ctx.mu` | `std::mutex` + `std::atomic<uint64_t> next_id` + **thread_local stack** | `cu_ctx.cpp` |
| 运行时入口(singleton) | `std::once_flag g_init_flag` | 一次性初始化 | `cu_init.cpp` |
| CudaRuntimeApi 内部状态 | `CudaRuntimeApi::mu_` | 上游 `CudaRuntimeApi` 自身管理 | `include/umd/cuda_runtime_api.hpp` |

**设计取舍**:

- 锁粒度粗 → 实现简单,不会有死锁风险,但高并发下吞吐受限。
- `std::atomic<uint64_t> next_id` 提供无需锁的 ID 分配,只对 `unordered_map` 操作加锁。
- `cuCtx*` 系列额外用 thread_local 栈模拟真 CUDA 的"每线程当前 context"语义(`cu_ctx.cpp:38`),这是 Phase 2 的 Oracle critical fix。
- 跨文件全局状态(例如 stream 在 capture 时引用 graph)目前没有共享锁,Phase 3.1 引入 `CaptureTable` 时需要单独设计锁(参考 [`../specs/2026-07-02-phase3-stream-capture-design.md`](../specs/2026-07-02-phase3-stream-capture-design.md) §3.1)。

## 6. 错误处理模式

shim 层错误处理遵循三条原则:

### 6.1 NULL 参数 → `CUDA_ERROR_INVALID_VALUE`

所有"输出参数"(`CUdeviceptr*` / `CUcontext*` / `CUstream*` / `CUevent*` / `CUgraph*` / size_t* / float* 等)在函数体第一行做 null check:

```cpp
if (!dptr) return CUDA_ERROR_INVALID_VALUE;
```

例: `cu_mem.cpp:21`、`cu_mem.cpp:44`、`cu_mem.cpp:54`、`cu_stream.cpp:28`、`cu_event.cpp:30`。

### 6.2 表中找不到 handle → `CUDA_ERROR_INVALID_HANDLE`

对于需要先在表中查表的 API(`cuCtxDestroy` / `cuEventRecord` / `cuEventElapsedTime`),找不到时返回 `CUDA_ERROR_INVALID_HANDLE`:

```cpp
auto it = table.created.find(hEvent);
if (it == table.created.end()) return CUDA_ERROR_INVALID_HANDLE;
```

例: `cu_ctx.cpp:78`、`cu_ctx.cpp:94`、`cu_event.cpp:54`、`cu_event.cpp:80-82`。

### 6.3 未实现功能 → `CUDA_ERROR_NOT_IMPLEMENTED`

STUB 函数或带"Phase X limitation"的函数返回 `NOT_IMPLEMENTED`:

- shim 内部 stub:`src/umd/libcuda_shim/cu_stub_table.inc` 的 weak 默认实现。
- `.cpp` 内显式:`cu_mem.cpp:83 (cuMemcpyAsync)` / `cu_mem.cpp:89 (cuMemsetD32)` / `cu_mem.cpp:101 (cuMemAllocHost)` / `cu_stream.cpp:99 (cuStreamBeginCapture)` / `cu_stream.cpp:105 (cuStreamEndCapture)`。

### 6.4 Phase 1 同步语义不支持 → `CUDA_ERROR_NOT_SUPPORTED`

表示"接口存在,但当前 backend 不支持",区别于 `NOT_IMPLEMENTED`:

- `cu_mem.cpp:68 (cuMemcpyDtoD)`:Phase 1 `CudaRuntimeApi::memcpy` 对 D2D 返回 `CudaError::NotSupported`。
- `cu_mem.cpp:77 (cuMemcpy)`:Phase 2 限制,等 `cuPointerGetAttribute` 实现后才能区分方向。

### 6.5 错误码与 `CudaRuntimeApi` 的映射

shim 把 `CudaError` 枚举映射到 `CUresult` 常量:

| `CudaError` | `CUresult` |
|---|---|
| `Success` | `CUDA_SUCCESS` |
| `InvalidValue` | `CUDA_ERROR_INVALID_VALUE` |
| `NotSupported` | `CUDA_ERROR_NOT_SUPPORTED` |
| `OutOfMemory` | `CUDA_ERROR_OUT_OF_MEMORY` |
| `Unknown` | `CUDA_ERROR_UNKNOWN` |

例: `cu_mem.cpp:27-29`、`cu_mem.cpp:48`、`cu_mem.cpp:58`。

## 7. 已知限制(Known Limitations)

### 7.1 无真 GPU 执行

- `cuLaunchKernel` 通过 `CudaRuntimeApi::launch_kernel` → `CudaScheduler::submit_launch`,最终调用 `IGpuDriver::submit_launch`。
- 真实计算发生在 `CudaStub`(mock,返回固定值)或 `GpuDriverClient`(ioctl 到 UsrLinuxEmu 的模拟 GPU)。
- **不是真 GPU 加速**;仅做接口连通性验证。

### 7.2 同步语义

- 所有 `Async` 后缀的 API 在 shim 里走同步路径(`wait_fence` 等待),不真异步。
- `cuStreamSynchronize` / `cuStreamQuery` / `cuEventSynchronize` / `cuEventQuery` 全是 no-op。
- 例: `cu_stream.cpp:46-54`、`cu_stream.cpp:60-62`、`cu_event.cpp:60-68`。

### 7.3 内存管理(no-op free)

- `cuMemFree` 是 no-op(`cu_mem.cpp:34-39`):Phase 1 限制,`CudaRuntimeApi::malloc` 没有对应 free。
- `cuMemAllocHost` / `cuMemFreeHost` 返回 `NOT_IMPLEMENTED`。
- 后果:进程长时间运行会累积 device memory(本质是进程退出 OS 回收)。

### 7.4 D2D / H2H memcpy 不支持

- `cuMemcpyDtoD` 返回 `NOT_SUPPORTED`(`cu_mem.cpp:67-68`)。
- `cuMemcpy` 通用版本也返回 `NOT_SUPPORTED`(`cu_mem.cpp:76-77`)。

### 7.5 单设备

- `cuDeviceGetCount` 返回 1。
- `cuDeviceGetAttribute` 只暴露固定的 80+ 属性中已实现的部分。
- 多设备 API 在 Phase 3.5 范围之外。

### 7.6 Stream Capture / Graph 未实现

- `cuStreamBeginCapture` / `cuStreamEndCapture` 返回 `NOT_IMPLEMENTED`(`cu_stream.cpp:99`、`cu_stream.cpp:105`)。
- 所有 `cuGraph*` API 在 `cu_stub_table.inc` 中是 STUB。
- Phase 3.1 会基于本文设计 capture tree 表示(参考 [`../specs/2026-07-02-phase3-stream-capture-design.md`](../specs/2026-07-02-phase3-stream-capture-design.md))。

### 7.7 Module 数据加载未实现

- `cuModuleLoadData` / `cuModuleLoadDataEx` / `cuModuleLoadFatBinary` 内部返回 `NOT_IMPLEMENTED`(其 .cpp 文件中)。真 ELF/CUBIN 解析在 D-3 范围(无限期延后)。

### 7.8 Kernel ABI 限制

- `register_kernel(name, index)` 由 `CudaRuntimeApi` 维护手动 name→index 映射;无自动 ELF 解析。
- 真 CUDA 程序必须先在 TaskRunner 端注册才能 launch。

### 7.9 真实设备后端的 dynamic_cast 抽象泄漏

- `CudaScheduler` 早期版本依赖 `dynamic_cast<CudaStub*>`,导致 `GpuDriverClient` 后端某些操作返回 `CudaError::Unknown`。
- 已通过 [`../specs/2026-07-02-phase1.5-cudastub-dynamic-cast-fix.md`](../specs/2026-07-02-phase1.5-cudastub-dynamic-cast-fix.md) 部分修复。

## 8. 新增 cu* 函数的流程(给 Phase 3 实施者)

如果想在 shim 层加一个 cu* 函数,按以下步骤:

1. **把函数名加入 `tools/generate_cu_stubs.py`**:
   - 加到 `CUDA_DRIVER_APIS` 列表(若尚未存在)。
   - 若有 REAL_IMPL,加到 `CRITICAL_APIS_IMPL_REQUIRED`,值是目标 `.cpp` 文件名。
2. **重新生成 stub 表**:
   ```
   python3 tools/generate_cu_stubs.py
   ```
3. **实现 REAL_IMPL**(若选 REAL_IMPL):
   - 在指定 `.cpp` 中用 `extern "C" CUresult xxx(...) { ... }` 写完整签名。
   - 文件头保留 `// SCOPE: UMD-EVOLUTION`。
   - 函数第一行做 NULL check,返回 `CUDA_ERROR_INVALID_VALUE`。
   - 复杂逻辑参考 §5 锁模型与 §6 错误码模式。
4. **添加测试**:
   - 在 `tests/umd/libcuda_shim/` 下加对应 `test_xxx.cpp`,使用 doctest。
   - 至少覆盖:成功路径、NULL 参数、未实现路径(若 STUB)。
5. **更新文档**:
   - `docs-audit.sh` 自动识别新增 .cpp/.hpp/.h/.md。
   - 若引入新 ADR,在 `docs/superpowers/specs/` 加 `tadr-NNN-...md`。
6. **同步 UsrLinuxEmu**:
   - 若是 Phase 3 触发的新 API,同步协议见 `AGENTS.md` "跨仓工作原则"。
   - `docs/00_adr/README.md` 的 "TaskRunner TADR mirror" 段需要追加新 TADR 引用。

## 9. 调试与验证

### 9.1 LD_PRELOAD 用法

```bash
LD_PRELOAD=$(pwd)/build/libcuda_taskrunner.so \
  ./your_cuda_program
```

可用 `LD_DEBUG=symbols` 查看符号解析是否走到 shim:

```bash
LD_PRELOAD=... LD_DEBUG=symbols ./your_cuda_program 2>&1 | grep -E 'cuInit|cuMemAlloc'
```

### 9.2 验证后端选择

`cu_init.cpp:34` 默认构造 `CudaStub`,Phase 2 PoC 用的是 stub 模式(无需 UsrLinuxEmu 进程)。要切到 `GpuDriverClient`,需要:

- UsrLinuxEmu 进程在跑(参见 `docs/07-integration/`)。
- `CudaScheduler` 构造时传入 `GpuDriverClient*` 而非 `CudaStub*`。

Phase 2.5 / Phase 3 才有 CLI 切后端的能力(`taskrunner --backend=gpu_driver`)。当前 `cu_init.cpp` 没有暴露这个开关。

### 9.3 单元测试

```bash
cd build && ctest -V -R shim
```

测试使用 doctest,覆盖 `cu*` 函数的行为而非接口连通性。

## 10. 跨仓关系

- **上游**:`UsrLinuxEmu/plugins/gpu_driver/shared/gpu_ioctl.h`(canonical GPU ioctl 定义)。
- **符号链接**:`UsrLinuxEmu → ../../`,在 `AGENTS.md` §构建命令解释。
- **TaskRunner 是 UsrLinuxEmu 的 git submodule**(`external/TaskRunner`)。改动后需按 `AGENTS.md` §跨仓工作原则 4 步同步协议走。

## 11. 参考

- [`../specs/2026-06-30-umd-evolution-redesign.md`](../specs/2026-06-30-umd-evolution-redesign.md) — UMD-EVOLUTION 整体设计。
- [`../specs/2026-07-02-phase3-mempool-design.md`](../specs/2026-07-02-phase3-mempool-design.md) — Phase 3.2 内存池设计。
- [`../specs/2026-07-02-phase3-stream-capture-design.md`](../specs/2026-07-02-phase3-stream-capture-design.md) — Phase 3.1 capture 设计。
- [`../../plans/2026-07-02-phase3-prep-design-notes.md`](../../plans/2026-07-02-phase3-prep-design-notes.md) — Phase 3 kickoff 设计备注。
- `src/umd/libcuda_shim/` — 本文档覆盖的所有源文件。
- `tools/generate_cu_stubs.py` — stub 表生成器。
- `include/umd/cuda_runtime_api.hpp` — Phase 1 `CudaRuntimeApi` 接口。
- `include/shared/igpu_driver.hpp` — IGpuDriver 抽象(31 方法契约)。
- `include/test_fixture/cuda_scheduler.hpp` — `CudaScheduler` 接口。
- `AGENTS.md` — 跨仓工作原则与构建命令。

---

**Status**: DRAFT — Phase 3 kickoff 时按需刷新。
**Last Updated**: 2026-07-02
**Next Action**: Phase 3.1 / 3.2 启动后,把新增 cu* 函数补充到 §4 表格。