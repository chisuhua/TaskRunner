---
SCOPE: UMD-EVOLUTION
STATUS: DRAFT
DESIGN_DATE: 2026-07-02
DESIGN_AUTHOR: Sisyphus
RELATED: ../../plans/2026-07-02-phase3-prep-design-notes.md
RELATED_TADR: tadr-205 (Phase 3 deferred)
RELATED_API: CUDA Driver API Stream Capture §22 (cuStreamBeginCapture/EndCapture) + CUDA Graph API §6
PHASE: 3.1 (Stream Capture + Graph)
PRIORITY: P0
BACKEND_DEP: UsrLinuxEmu Stage 1.4
---

# Phase 3.1 — Stream Capture / CUDA Graph 接口设计

> **Status**: DRAFT — 设计稿,未实现。本文仅做接口层面与 capture 生命周期设计,不包含真实代码。
> **触发依赖**: UsrLinuxEmu Stage 1.4 后端;参考 [`../plans/2026-07-02-phase3-prep-design-notes.md`](../../plans/2026-07-02-phase3-prep-design-notes.md) 中 P0 子项 3.1。

## 1. 背景与目标

CUDA Stream Capture 与 Graph API 是 CUDA 11 之后的高性能执行模型:用户把一段 stream 上的工作录制成 DAG(`CUgraph`),然后实例化(`CUgraphExec`)并反复 launch。它大幅降低了 per-launch CPU 开销,也是 cuBLASLt / cuDNN 推理等场景的推荐用法。

当前 shim 层(`src/umd/libcuda_shim/cu_stream.cpp`)的 capture 相关状态:

```
extern "C" CUresult cuStreamBeginCapture(CUstream hStream,
                                         CUstreamCaptureMode mode) {
  (void)hStream; (void)mode;
  return CUDA_ERROR_NOT_IMPLEMENTED;          // cu_stream.cpp:99
}

extern "C" CUresult cuStreamEndCapture(CUstream hStream, CUgraph* phGraph) {
  (void)hStream; (void)phGraph;
  return CUDA_ERROR_NOT_IMPLEMENTED;          // cu_stream.cpp:105
}
```

`cuGraph*` / `cuGraphExec*` / `cuGraphAdd*Node` 在 `cu_stub_table.inc` 中**全部为 STUB**。

Phase 3.1 的目标:

1. 暴露 capture lifecycle 端到端 handle(`CUstream` 绑定的 capture 状态 + `CUgraph` 实例)。
2. 设计 capture tree 表示与节点类型,记录 enough metadata,保证 `EndCapture` 能产出结构正确的 `CUgraph`。
3. 暴露最小可用的 graph instantiate/launch,但**不真的执行 DAG**(沿用 Phase 2 同步 no-op 语义)。
4. 在 `IGpuDriver` 上预留 1 个 `submit_graph` 方法扩展点。

## 2. API 表面(全部为 DRAFT)

### 2.1 Stream Capture 生命周期

| CUDA Driver API | 当前 stub 状态 | Phase 3.1 设计行为 |
|---|---|---|
| `cuStreamBeginCapture(CUstream hStream, CUstreamCaptureMode mode)` | `NOT_IMPLEMENTED` (Phase 2) | 若 stream 已在 capture 中返回 `CUDA_ERROR_ILLEGAL_STATE`;否则创建 `CaptureState` 挂到 `g_streams`。 |
| `cuStreamEndCapture(CUstream hStream, CUgraph* phGraph)` | `NOT_IMPLEMENTED` | 把 `CaptureState.nodes` 转成 `CUgraph` 返回,从 stream 解绑。 |
| `cuStreamIsCapturing(CUstream hStream, CUstreamCaptureStatus* status)` | **未注册**(`cu_stub_table.inc` 没有此条目,会被链接器解析为 `libcuda.so`) | 新增条目 + 实现,返回 `CU_STREAM_CAPTURE_STATUS_ACTIVE` / `NONE` / `INVALID`。 |
| `cuStreamGetCaptureInfo(CUstream hStream, CUstreamCaptureStatus* status, CUstreamCaptureMode* mode)` | STUB | 返回当前 capture 模式;未在 capture 时 `*status = NONE`、`*mode = 0`。 |

### 2.2 Graph CRUD

| API | 当前 stub | Phase 3.1 设计行为 |
|---|---|---|
| `cuGraphCreate(CUgraph* phGraph, unsigned int flags)` | STUB | 分配 `Graph` 对象,空节点列表。 |
| `cuGraphDestroy(CUgraph hGraph)` | STUB | 释放节点 vector;`null` 返回 `INVALID_HANDLE`。 |
| `cuGraphClone(CUgraph* newGraph, CUgraph originalGraph)` | **未注册** | 同 Create 后复制节点列表。 |
| `cuGraphAddKernelNode(CUgraphNode* phGraphNode, CUgraph hGraph, const CUgraphNode* deps, size_t numDeps, const CUDA_KERNEL_NODE_PARAMS* nodeParams)` | STUB | 分配 `KernelNode` 包装 `CUDA_KERNEL_NODE_PARAMS` 副本;不解析 `function`(Phase 3.1)。 |
| `cuGraphAddMemcpyNode(...)` | STUB | 同上,`MemcpyNode`。 |
| `cuGraphAddMemsetNode(...)` | STUB | 同上,`MemsetNode`。 |
| `cuGraphAddHostNode(...)` | STUB | `HostNode`,存回调 + userData。 |
| `cuGraphAddEmptyNode(...)` | STUB | 纯占位。 |
| `cuGraphAddEventRecordNode(...)` | **未注册** | `EventRecordNode`,存 `CUevent`。 |
| `cuGraphAddEventWaitNode(...)` | **未注册** | `EventWaitNode`。 |
| `cuGraphAddChildGraphNode(...)` | **未注册** | `ChildGraphNode`,存 `CUgraph` 引用。 |

### 2.3 Graph Instantiate + Launch

| API | 当前 stub | Phase 3.1 设计行为 |
|---|---|---|
| `cuGraphInstantiate(CUgraphExec* phGraphExec, CUgraph hGraph, CUgraphNode* phErrorNode, char* logBuffer, size_t bufferSize)` | STUB | 创建 `GraphExec` 包装 graph 引用;`phErrorNode`/`logBuffer` 暂时忽略。 |
| `cuGraphInstantiateWithFlags(CUgraphExec*, CUgraph, unsigned long long flags)` | STUB | 同上,接受 flags 不生效。 |
| `cuGraphLaunch(CUgraphExec hGraphExec, CUstream hStream)` | STUB | 同步语义:遍历节点,逐个调用对应底层 API(`cuLaunchKernel` / `cuMemcpy*` / `cuEventRecord`);保持 Phase 1 的同步 wait_fence 行为。**不真异步**。 |
| `cuGraphUpload(CUgraphExec hGraphExec, CUstream hStream)` | STUB | accept-noop(同步语义下无用)。 |
| `cuGraphExecDestroy(CUgraphExec hGraphExec)` | STUB | 释放 exec,减少 graph 引用计数。 |
| `cuGraphExecUpdate(CUgraphExec hGraphExec, CUgraph hGraph, CUgraphNode* phErrorNode, CUgraphExecUpdateResult* result)` | STUB | 返回 `CU_GRAPH_EXEC_UPDATE_ERROR_TOPOLOGY_CHANGED`(节点数变化即视为不兼容)。 |
| `cuGraphExecKernelNodeSetParams(CUgraphExec hGraphExec, CUgraphNode hNode, const CUDA_KERNEL_NODE_PARAMS* nodeParams)` | STUB | 仅当 `hNode` 是 `KernelNode` 时接受。 |

## 3. Capture Tree 表示(DRAFT)

### 3.1 整体结构

```
namespace async_task::umd::shim {

enum class CaptureStatus { None, Active, Invalid };

struct KernelNodePayload {
  CUfunction function{nullptr};
  Dim3 grid_dim, block_dim;
  std::vector<void*> kernel_args;   // 浅拷贝指针值(Phase 3.1 不深度 copy)
  std::size_t shared_mem{0};
  CUstream stream{nullptr};
};

struct MemcpyNodePayload {
  void* dst{nullptr};
  void* src{nullptr};
  std::size_t bytes{0};
  CUmemcpyKind kind;
};

struct MemsetNodePayload { /* ... */ };
struct HostNodePayload { CUhostFn fn; void* user_data; };
struct EventRecordNodePayload { CUevent event; };
struct EventWaitNodePayload { CUevent event; };
struct ChildGraphNodePayload { CUgraph child; };

using GraphNodePayload = std::variant<
    KernelNodePayload,
    MemcpyNodePayload,
    MemsetNodePayload,
    HostNodePayload,
    EventRecordNodePayload,
    EventWaitNodePayload,
    ChildGraphNodePayload,
    std::monostate>;     // Empty

struct GraphNode {
  std::uint64_t id;
  GraphNodePayload payload;
  std::vector<std::uint64_t> deps;   // 出/入边均可,统一存依赖
};

struct Graph {
  std::atomic<std::uint64_t> next_node_id{1};
  std::vector<GraphNode> nodes;
  std::mutex mu;
};

struct GraphExec {
  CUgraph graph{nullptr};      // 引用,不拥有
  std::uint64_t instance_id;
};

struct CaptureState {
  CUstream stream{nullptr};
  CUstreamCaptureMode mode{CU_STREAM_CAPTURE_MODE_THREAD_LOCAL};
  Graph graph;
  // 内存节点收集:EndCapture 时转成 ownership 引用
  std::unordered_map<CUdeviceptr, std::size_t> mem_nodes;
};

}
```

### 3.2 Capture 期间 API 行为分流

`cuStreamBeginCapture` 之后,所有对该 stream 的"可捕获"操作(`cuLaunchKernel` / `cuMemcpy*Async` / `cuEventRecord` / `cuStreamWaitEvent`)应改为**追加 graph 节点**,而非执行真操作。Phase 3.1 决策:

- **决策 D-SC-1**:在现有 `cu_launch.cpp` / `cu_mem.cpp` 的 *Async* 函数(`cuLaunchKernel` 不含 Async,但 `cuMemcpyAsync` / `cuMemsetD32Async` 是)中,先检查 `capture_state.find(stream)`,命中则追加节点并返回 `SUCCESS`,不调用 `runtime()`。
- **决策 D-SC-2**:`cuEventRecord(hEvent, hStream)` 进入 capture 模式后,只在 graph 里加 `EventRecordNode`,不实际更新时间戳(`cuEventElapsedTime` 在 capture 内的语义保留"创建时戳"行为)。
- **决策 D-SC-3**:`cuStreamWaitEvent` 进入 capture 模式后追加 `EventWaitNode`。

### 3.3 内存节点追踪

CUDA capture 规范要求跟踪 capture 期间分配的所有 device 内存(`memNode`),以便 `EndCapture` 后分配生命周期正确。Phase 3.1 实现:

- `cuMemAllocFromPoolAsync` 或 `cuMemAllocAsync`(后者尚未实现)在 capture 中成功时,记录 `(ptr, size)` 到 `CaptureState.mem_nodes`。
- `cuMemFreeAsync` 在 capture 中从 `mem_nodes` 移除对应条目。
- `EndCapture` 后,mem_nodes 转移到 graph 拥有,`cuGraphDestroy` 时一并清理(注意 Phase 1 `cuMemFree` 是 no-op,这里也是 no-op,只清理 map 条目)。

**决策 D-SC-4**:`CaptureState.mem_nodes` 仅为生命周期跟踪,**不**触发真 `cuMemFree`,保持 Phase 1 语义。

## 4. 关键设计决策

### 4.1 Capture 模式(`CUstreamCaptureMode`) — UsrLinuxEmu F-1 决策更新（2026-07-05）

CUDA 定义 4 种 mode:`GLOBAL` / `THREAD_LOCAL` / `RELAXED` / `DISALLOW`。当前 `cu_stream.cpp` 完全忽略。

**决策**（**修订 — UsrLinuxEmu 反馈 F-1**）:

- Phase 3.1 **仅接受**`CU_STREAM_CAPTURE_MODE_GLOBAL (0)`;其他 mode 返回 `cudaErrorNotSupported`(或 `cudaErrorInvalidValue`),**不**静默 fallback 到 GLOBAL。
- `CU_STREAM_CAPTURE_MODE_DISALLOW` 同样返回 `cudaErrorInvalidValue`(语义与原来一致)。
- 决策编号 **D-SC-5**（**修订**）。
- 决策原因:UsrLinuxEmu 简化实施路径,避免 `THREAD_LOCAL` / `RELAXED` 语义差异带来的额外测试矩阵(参见 UsrLinuxEmu 反馈 `taskrunner-feedback.md F-1`)。
- 后续 Phase 3.x 按需扩展支持(THREAD_LOCAL 需 per-thread capture state machine;RELAXED 放宽同步语义)。

### 4.2 嵌套 Capture

CUDA 允许 `fork/join` 嵌套(`cuStreamBeginCaptureToGraph`)。

**决策**:

- Phase 3.1 **不支持嵌套**:BeginCapture 时若 stream 已有 active capture 返回 `ILLEGAL_STATE`。
- 决策编号 **D-SC-6**。

### 4.3 Graph 实例化语义

CUDA 中 `cuGraphInstantiate` 会编译/校验 DAG;某些拓扑错误会导致失败。

**决策**:

- shim 端**永远返回 `Success`**:不解析拓扑、不检查循环依赖。
- 这是为了保持 Phase 2 的"接口暴露、行为简化"原则,与 `cu_stub_table.inc` 的 REAL_IMPL 约定一致。
- 决策编号 **D-SC-7**。

### 4.4 Graph Launch 同步 vs 异步

CUDA 中 `cuGraphLaunch` 是真异步到 stream。

**决策**:

- shim 端**保持同步语义**:遍历节点,每个节点同步执行对应的底层 cu* API(等价于直接写一遍代码)。
- 这与 `CudaRuntimeApi` 现有的同步语义兼容,不需要为 graph 单独引入异步队列。
- 决策编号 **D-SC-8**。

### 4.5 与 `IGpuDriver` 的集成 — UsrLinuxEmu B-1 决策（2026-07-05）

**问题**:`cuGraphLaunch` 是否需要后端新的"提交 DAG"能力?

**UsrLinuxEmu 现有 API**（**经 Architecture Team grep 验证**）:

```
$ grep "submit\|enqueue" plugins/gpu_driver/sim/gpu_queue_emu.h
71:  bool dequeue(gpu_gpfifo_entry* out_entry);
113: int submit(uint64_t gpfifo_addr, uint32_t entry_count);
```

实际只有 `GpuQueueEmu::submit(uint64_t gpfifo_addr, uint32_t entry_count)` — **不存在** `submit_batch` 或 `enqueue`。

**决策**（**修订 — UsrLinuxEmu B-1 反馈**）:

- `IGpuDriver` 新增 10 个 Phase 3.1 方法（参见 cross-repo PR §3.1.3），其中核心是 `submit_graph(graph_exec_handle, stream_id)`，返回 `int64_t` fence_id。
- shim 层 `cuGraphLaunch` 不直接调 `GpuQueueEmu`;通过 IOCTL（`GPU_IOCTL_GRAPH_LAUNCH`，0x58）经 GpuDriverClient forwarding 到 UsrLinuxEmu 的 `gpu_drm_driver.cpp` handler。
- driver-side handler 把 `exec_handle + stream_id` 转成 `gpfifo_addr + entry_count` 对，调 `GpuQueueEmu::submit(uint64_t, uint32_t)` 提交（**不是** `submit_batch`，**不是** `enqueue`）。
- GpuDriverClient forwarding 实现必须在 Step 3 PR 中**确认无 TaskRunner 代码假设 `submit_batch` 或 `enqueue` API**。
- 决策编号 **D-SC-9**（**修订 — 明确 GpuQueueEmu 实际 API 名称**）。
- 集成模式示例文档化在 UsrLinuxEmu `design.md §"与现有 sim 原语的集成"`（参见 UsrLinuxEmu fix-steps.md Fix-3）。

### 4.6 Graph Node 类型覆盖

CUDA 有 ~10 种节点类型。Phase 3.1 范围只覆盖上表列出的 7 种,其余返回 `NOT_IMPLEMENTED`:

- 拒绝:`cuGraphAddMemAllocNode` / `cuGraphAddMemFreeNode` / `cuGraphAddDependencies` / `cuGraphRemoveDependencies`(由 Phase 3.2 内存池子项接力)。
- 决策编号 **D-SC-10**。

### 4.7 fence_id 范围划分 — UsrLinuxEmu B-3 决策（2026-07-05）

**问题**:`cuGraphLaunch` 返回 fence 令牌,TaskRunner shim 必须正确处理 `int64_t` fence_id 不假设 `uint32_t`。UsrLinuxEmu 现有 fence_id 由 `hal_fence_create()` 分配（`plugins/gpu_driver/drv/gpu_drm_driver.cpp:212-218`），范围 `[1, ...)`，sim 层无 fence 概念。

**UsrLinuxEmu 决策**（**Option A — 最小侵入式**）：

| Layer | fence_id 范围 | 分配点 |
|-------|--------------|--------|
| HAL / driver（现有，不变） | `[1, (1 << 32) - 1]` | `hal_fence_create()`（保持现有 70+ 测试通过） |
| Sim（Phase 3.1/3.2 新增） | `[(1 << 32), INT64_MAX]` | 新增 `sim_fence_id_alloc()` |

**`wait_fence` handler 分发**：
- `fence_id < (1 << 32)` → 调用 `hal_fence_read()`（现有路径，不变）
- `fence_id >= (1 << 32)` → 调用 `sim_fence_id_check()`（新增路径）
- 任一路径返回 signaled=true 即返回 0；否则阻塞等待

**TaskRunner shim 要求**：

1. GpuDriverClient forwarding 的 `submit_graph` / `mem_pool_alloc_async` / `mem_pool_free_async` **必须**用 `int64_t` 接收 fence_id，**不** cast 到 `uint32_t`。
2. shim 层 `cuGraphLaunch` 返回 `CUgraphExec` launch token，**内部**包装 `int64_t` fence_id;CUDA 运行时 API 通过 `cudaStreamSynchronize` 轮询 `wait_fence` IOCTL。
3. 验证现有 `test_gpu_fence_return_standalone`（UsrLinuxEmu 侧）不被破坏。
4. 决策编号 **D-SC-11**（**新增** — UsrLinuxEmu B-3 反馈）。

**错误约定**（**UsrLinuxEmu F-4 决策**）：3 个返回 fence_id 的 sim 原语用 `int64_t`：
- `< 0` → 负 errno（如 `-EINVAL`、`-ENOSPC`）
- `>= (1 << 32)` → 有效 fence_id
- `0` → success 但无 fence（罕见）

shim **不** cast 到 `int`，负 errno 原样传递给 caller。

### 4.8 kernargs_bo_handle=0 语义 — UsrLinuxEmu F-3 决策（2026-07-05）

**问题**:TaskRunner shim 可能用 `kernelParams == nullptr` 表示"无参数 kernel"。

**UsrLinuxEmu 决策**:

- `kernargs_bo_handle == 0` 表示 **"无 kernargs BO"**，是**有效**值。
- driver-side handler (`gpu_drm_driver.cpp` 中的 `GRAPH_ADD_KERNEL_NODE`) **不**对 `kernargs_bo_handle == 0` 校验 BO 表存在性（避免 false EINVAL）。
- 非零 `kernargs_bo_handle` 仍按现有 BO handle 校验流程（验证 BO 存在性 + 读权限）。

**TaskRunner shim 要求**:

- `cuGraphAddKernelNode` shim 中,当 `kernelParams == nullptr` → 设置 `kernargs_bo_handle = 0`(显式语义,不是缺省)。
- 不需要为零 BO handle 做特殊 error handling。
- 决策编号 **D-SC-12**（**新增** — UsrLinuxEmu F-3 反馈）。

## 5. 生命周期状态机

```
   ┌─────────────┐
   │   None      │ ◄─────── 任何路径出错 → Invalid
   │             │                (后续 BeginCapture 返回 ILLEGAL_STATE)
   └──────┬──────┘
          │ cuStreamBeginCapture
          ▼
   ┌─────────────┐
   │   Active    │ ── 同 stream 再次 BeginCapture ──► ILLEGAL_STATE
   │  (per-stream)│
   └──────┬──────┘
          │ cuStreamEndCapture
          ▼
   ┌─────────────┐
   │  Closed     │  ── graph 转移到 *phGraph
   │  → None     │
   └─────────────┘
```

## 6. 错误码对照表

| 场景 | 返回码 |
|---|---|
| `phGraph` / `phGraphExec` / `phGraphNode` 任一为 null | `CUDA_ERROR_INVALID_VALUE` |
| BeginCapture 时 stream 已在 capture | `CUDA_ERROR_ILLEGAL_STATE` |
| EndCapture 时 stream 不在 capture | `CUDA_ERROR_ILLEGAL_STATE` |
| `hGraph` / `hGraphExec` 在表中找不到 | `CUDA_ERROR_INVALID_HANDLE` |
| `cuGraphExecUpdate` 拓扑变化 | 返回 `CUDA_ERROR_GRAPH_EXEC_UPDATE_FAILURE` + `result = CU_GRAPH_EXEC_UPDATE_ERROR_TOPOLOGY_CHANGED` |
| `cuGraphAddMemAllocNode` / `FreeNode` | `CUDA_ERROR_NOT_IMPLEMENTED` |
| `cuGraphAddDependencies` / `RemoveDependencies` | `CUDA_ERROR_NOT_IMPLEMENTED` |

## 7. 与 `cu_stub_table.inc` 的协同

`tools/generate_cu_stubs.py` 需要新增以下条目到 `CRITICAL_APIS_IMPL_REQUIRED`(REAL_IMPL):

```
"cuStreamIsCapturing":      "cu_stream.cpp",
"cuStreamGetCaptureInfo":   "cu_stream.cpp",   # 已在 STUB,改 REAL_IMPL
"cuGraphCreate":            "cu_graph.cpp",
"cuGraphDestroy":           "cu_graph.cpp",
"cuGraphClone":             "cu_graph.cpp",
"cuGraphInstantiate":       "cu_graph_exec.cpp",
"cuGraphInstantiateWithFlags": "cu_graph_exec.cpp",
"cuGraphLaunch":            "cu_graph_exec.cpp",
"cuGraphUpload":            "cu_graph_exec.cpp",
"cuGraphExecDestroy":       "cu_graph_exec.cpp",
"cuGraphExecUpdate":        "cu_graph_exec.cpp",
"cuGraphExecKernelNodeSetParams": "cu_graph_exec.cpp",
"cuGraphAddKernelNode":     "cu_graph_node.cpp",
"cuGraphAddMemcpyNode":     "cu_graph_node.cpp",
"cuGraphAddMemsetNode":     "cu_graph_node.cpp",
"cuGraphAddHostNode":       "cu_graph_node.cpp",
"cuGraphAddEmptyNode":      "cu_graph_node.cpp",
"cuGraphAddEventRecordNode":"cu_graph_node.cpp",
"cuGraphAddEventWaitNode":  "cu_graph_node.cpp",
"cuGraphAddChildGraphNode": "cu_graph_node.cpp",
```

新文件:`cu_graph.cpp` / `cu_graph_exec.cpp` / `cu_graph_node.cpp`,沿用 `// SCOPE: UMD-EVOLUTION` 文件头约定。

## 8. 实现顺序

1. **扩展 `generate_cu_stubs.py`**:加入新条目,重新生成 `cu_stub_table.inc`。
2. **新增 `cu_graph.cpp`** + `Graph` / `GraphNode` 数据结构,实现 `cuGraphCreate` / `Destroy` / `Clone`。
3. **新增 `cu_graph_node.cpp`**:实现 9 种 `Add*Node`。
4. **新增 `cu_graph_exec.cpp`**:实现 `Instantiate` / `Launch` / `Destroy` / `Update`。
5. **修改 `cu_stream.cpp`**:实现 `BeginCapture` / `EndCapture` / `IsCapturing` / `GetCaptureInfo`,加 `CaptureTable`。
6. **修改 `cu_mem.cpp`**:在 `cuMemcpyAsync` / `cuMemsetD32Async` 内增加 capture 检测分支(D-SC-1)。
7. **修改 `cu_event.cpp`**:`cuEventRecord` / `cuStreamWaitEvent` 增加 capture 分支(D-SC-2/3)。
8. **修改 `cu_launch.cpp`**:真 CUDA 语义下 `cuLaunchKernel` 也要 capture,但 Phase 3.1 暂不实现该路径,文档标记为后续;仍走原路径(不分支)。
9. **测试**:在 `tests/umd/libcuda_shim/` 加 `test_cu_capture.cpp` + `test_cu_graph.cpp`,覆盖 lifecycle / 节点类型 / 错误路径。
10. **文档**:`docs-audit.sh` 应识别新文件;无新增 ADR。

## 9. 设计问题(待 Phase 3 kickoff 时回答)

| Q# | 问题 | 倾向 | 阻塞? |
|---|---|---|---|
| Q-SC-1 | `cuStreamBeginCapture` 是否检查 stream 是否存在? | 不检查,与现有 stream API 一致(其他 stream 函数也不查 active 表,只查 handle 非 0)。 | 否 |
| Q-SC-2 | `KernelNodePayload.kernel_args` 的指针生命周期由谁负责? | 调用方拥有,shim 仅浅拷贝(与 Phase 1 `cudaLaunchKernel` 一致)。 | 否 |
| Q-SC-3 | `cuGraphAddChildGraphNode` 的子 graph 生命周期? | 引用,不深度 copy;`Destroy` 不级联。 | 否 |
| Q-SC-4 | `cuGraphLaunch` 时 stream 是否要在 capture 中? | 拒绝:返回 `ILLEGAL_STATE`(嵌套 capture 的一部分)。 | 否 |
| Q-SC-5 | 是否要支持 `cuStreamBeginCaptureToGraph`? | 不支持,Phase 3.1 仅有 `BeginCapture` 版本。 | 否 |
| Q-SC-6 | `cuEventRecord` 在 capture 中是否允许 `hStream == nullptr`(default stream)? | 允许,等同真 CUDA 语义。 | 否 |

## 10. 已知 Open Issues

- **O-SC-1**:`cuGraphLaunch` 是同步遍历,**不**真异步。这意味着 `cuGraphInstantiate` 后多次 Launch 会重新遍历节点,失去真 CUDA 的性能优势。可接受(Phase 3.1 范围),但需要在 commit message 与文档双重说明。
- **O-SC-2**:`ChildGraphNode` 是引用,`Destroy` 父 graph 时不级联销毁子 graph,与真 CUDA 略有不同(真 CUDA 是引用计数)。需要在 `cu_graph_node.cpp` 注释中显式声明。
- **O-SC-3**:`KernelNodePayload.function` 暂时不解析(`CUfunction` 是空指针不报错),Launch 时走 stub 路径返回 `SUCCESS`。这与 `cuLaunchKernel` 现有的 `cudaErrorNotSupported` 行为有差异,需要在测试中明确断言。
- **O-SC-4**:`EventRecordNodePayload` 不更新事件时间戳,`cuEventElapsedTime` 在 graph 内语义退化为"创建时刻"。**强烈不推荐**在 capture 后用 elapsed time 测量 graph 性能。
- **O-SC-5**:依赖 UsrLinuxEmu Stage 1.4 才有真后端 graph 执行能力,否则 Phase 3.1 仅停留在"接口对齐"。

## 11. 范围禁令(从 prep notes 继承)

- ❌ Vulkan Graph(沿用 Q3:no Vulkan)。
- ❌ 嵌套 capture(`cuStreamBeginCaptureToGraph`)。
- ❌ 真异步 DAG 执行(同步遍历)。
- ❌ 内存分配/释放节点(`AddMemAllocNode`/`AddMemFreeNode`,留 Phase 3.2)。
- ❌ 拓扑验证 / 循环依赖检查。
- ❌ 替换 `CudaStub` 为真设备后端。

## 12. 参考

- [`../plans/2026-07-02-phase3-prep-design-notes.md`](../../plans/2026-07-02-phase3-prep-design-notes.md) §Priority Matrix P0 / 3.1。
- [`../specs/2026-06-30-umd-evolution-redesign.md`](../specs/2026-06-30-umd-evolution-redesign.md) Phase 3 范围。
- `src/umd/libcuda_shim/cu_stream.cpp:95-106` — BeginCapture/EndCapture 当前 stub。
- `src/umd/libcuda_shim/cu_stub_table.inc:329-355, 449-507` — Graph API 全部为 STUB。
- `tools/generate_cu_stubs.py` — stub 表生成机制。

---

**Status**: DRAFT(Phase 3.1 kickoff 时晋升为 ACCEPTED)。
**Last Updated**: 2026-07-02
**Next Action**: 等 UsrLinuxEmu Stage 1.4 启动或显式触发。