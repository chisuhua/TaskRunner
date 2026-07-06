# Design: phase3-step3-shim-and-forwarding

> **状态**: 🔄 PROPOSED（2026-07-06，Step 1 + Step 2 已 merge）
> **架构**: 5 层（User → shim REAL_IMPL → GpuDriverClient override → UsrLinuxEmu IOCTL handler → sim primitive）
> **修正**: 2026-07-06 self-review 后适配现有代码模式（shim 自包含 + GpuDriverClient inline ioctl）
> **参考**: docs/superpowers/cross-repo-prs/2026-07-05-phase3-1-stream-mempool-coordination.md §3 + §5

## 1. Architecture Overview

```
┌──────────────────────────────────────────────────────────────────┐
│  Layer 1: User Application (cuStreamBeginCapture, cuMemPool*)     │
└──────────────────────────────────────────────────────────────────┘
                              ↓
┌──────────────────────────────────────────────────────────────────┐
│  Layer 2: TaskRunner Shim (NEW cu_stream_capture.cpp, cu_graph.cpp,│
│  cu_graph_node.cpp, cu_graph_exec.cpp, cu_mem_pool.cpp) — translate│
│  cu* API → self-contained atomic+map+mutex state machines          │
│  • cuStreamBeginCapture/EndCapture/IsCapturing → CaptureTable      │
│  • cuGraphCreate/AddKernelNode/Instantiate/Launch → GraphTable    │
│  • cuMemPoolCreate/Alloc/SetAttr → MemPoolTable                   │
│  • Error mapping: 内联 if/else (mimics cu_stream.cpp pattern)      │
│  Phase 3 PoC: 不调 GpuDriverClient（自包含）；Phase 4+ 可桥接      │
└──────────────────────────────────────────────────────────────────┘
                              ↓ (Phase 4+ 可选桥接)
┌──────────────────────────────────────────────────────────────────┐
│  Layer 3: GpuDriverClient override (gpu_driver_client.h/.cpp)      │
│  NEW: 15 方法添加（10 graph/capture + 5 mempool），每个 inline:    │
│  • ioctl(fd_, GPU_IOCTL_*, &args)  ← Step 2 引入的 0x50-0x67     │
│  • unmarshals OUT fields from struct                               │
│  • returns IGpuDriver int convention (0 success, <0 errno)        │
└──────────────────────────────────────────────────────────────────┘
                              ↓ (via symlink UsrLinuxEmu/)
┌──────────────────────────────────────────────────────────────────┐
│  Layer 4: UsrLinuxEmu IOCTL Handler (gpu_drm_driver.cpp)          │
│  • params validation → calls sim_* → returns Linux errno          │
│  (Step 2 implementation, MERGED PR #20)                           │
└──────────────────────────────────────────────────────────────────┘
                              ↓
┌──────────────────────────────────────────────────────────────────┐
│  Layer 5: Sim Primitives (plugins/gpu_driver/sim/)                 │
│  sim_stream_capture_* + sim_graph_* + sim_mem_pool_* + sim_fence_id│
│  (Step 2 implementation, MERGED PR #20)                           │
└──────────────────────────────────────────────────────────────────┘
```

**关键不变量**：
- **每个 cu* shim API 唯一对应 1 个 self-contained state table entry**
- **Phase 3 PoC**：shim 与 GpuDriverClient 解耦（不调 GpuDriverClient）
- **Phase 4+ 桥接**：可在 shim → GpuDriverClient 加桥接（参考 `cuda_runtime_api.cpp` 用 `scheduler_->submit_memcpy_h2d()` 的模式）
- **handle 类型稳定性**：`CUstream = void*` / `CUgraph = void*` / `CUmemPool = void*` / `CUevent = void*`（与 CUDA 规范一致）

### 1.1 Stage 1.4 Regression Risk Assessment

Modifications to `cu_stream.cpp` (lines 95-100, 102-106, 140-148) and `cu_mem.cpp` (line 258-261) are confined to:

1. Deleting 3 stub functions (whose current implementation returns `CUDA_ERROR_NOT_IMPLEMENTED`)
2. Modifying `cuStreamGetCaptureInfo` to delegate to `cuStreamIsCapturing` (new function)

A grep-based audit confirms:
- `grep -r "cuStreamBeginCapture\|cuStreamEndCapture\|cuStreamGetCaptureInfo\|cuGraphCreate" tests/` returns 0 matches.
- All Stage 1.4 tests (`test_cu_stream`, `test_cu_mem`, etc.) do not reference the modified functions.

**Risk verdict**: Stage 1.4 regressions are extremely unlikely. The 76+ baseline test count is expected to be preserved with zero regressions.

## 2. GpuDriverClient Override Design (Workstream 1)

> **关键事实（self-review 验证）**：
> - GpuDriverClient 类已存在（`include/test_fixture/gpu_driver_client.h`，577 行）
> - **没有** `submit_ioctl()` helper — 每个方法 inline `ioctl(fd_, GPU_IOCTL_*, &args)`
> - 全局变量名是 `g_gpu_client`（在 `async_task::gpu::` 命名空间）
> - Step 1 加的 15 个 IGpuDriver 虚函数目前在 GpuDriverClient **未实现**（仍是 no-op `return -1`/`-1LL`）
> - 本 Workstream 是在 GpuDriverClient 类内**新增 15 个 override 方法**

### 2.1 Method Categorization

| 类别 | 方法数 | 返回类型 | 注意事项 |
|------|--------|----------|----------|
| Status query | 1 (`stream_capture_status`) | int | `_IOWR` direction, status_out 必须 fill |
| Sync ops | 11 (graph create/destroy/add/instantiate/destroy_exec, mempool create/destroy/alloc) | int | 多数 `_IOW` 或 `_IOWR` |
| Async ops (return fence_id) | 3 (`submit_graph`, `mem_pool_alloc_async`, `mem_pool_free_async`) | int64_t | F-4 约定：`<0` = errno, `>=1<<32` = valid sim fence_id |

### 2.2 Override Pattern (synchronous) — INLINE ioctl

参考现有 GpuDriverClient 实现（如 `free_bo`, `wait_fence`）：

```cpp
// Pattern A: no OUT fields (_IOW direction)
int GpuDriverClient::graph_destroy(uint64_t graph_handle) {
  if (!is_open()) return -1;
  if (graph_handle == 0) return -1;
  gpu_graph_destroy_args args = {};
  args.graph_handle = graph_handle;
  if (ioctl(fd_, GPU_IOCTL_GRAPH_DESTROY, &args) < 0) {
    std::cerr << "GpuDriverClient: GPU_IOCTL_GRAPH_DESTROY failed"
              << " (errno=" << errno << ")\n";
    return -1;
  }
  return 0;
}

// Pattern B: with OUT fields (_IOWR direction)
int GpuDriverClient::graph_create(uint64_t* graph_handle_out) {
  if (!is_open()) return -1;
  if (!graph_handle_out) return -EINVAL;
  gpu_graph_create_args args = {};
  if (ioctl(fd_, GPU_IOCTL_GRAPH_CREATE, &args) < 0) {
    std::cerr << "GpuDriverClient: GPU_IOCTL_GRAPH_CREATE failed"
              << " (errno=" << errno << ")\n";
    return -1;
  }
  *graph_handle_out = args.graph_handle_out;
  return 0;
}
```

### 2.3 Override Pattern (asynchronous, return int64_t)

```cpp
// Pattern C: returns fence_id (B-3 + F-4)
int64_t GpuDriverClient::submit_graph(uint64_t graph_exec_handle, uint32_t stream_id) {
  if (!is_open()) return -1;
  if (graph_exec_handle == 0) return -1;
  gpu_graph_launch_args args = {};
  args.exec_handle = graph_exec_handle;  // 实际字段是 exec_handle（非 graph_exec_handle）
  args.stream_id = stream_id;
  if (ioctl(fd_, GPU_IOCTL_GRAPH_LAUNCH, &args) < 0) {
    std::cerr << "GpuDriverClient: GPU_IOCTL_GRAPH_LAUNCH failed"
              << " (errno=" << errno << ")\n";
    return -1;
  }
  return args.fence_id_out;  // s64, ≥1<<32 表示 sim fence
}
```

### 2.4 MemPool Special Pattern (B-2 + 嵌套 struct)

`gpu_mem_pool_create_args` 使用嵌套 `gpu_mem_pool_props` sub-struct（**修正**）：

```cpp
// Pattern D: 嵌套 struct 字段访问
int GpuDriverClient::mem_pool_create(uint64_t va_space_handle, uint64_t size,
                                      uint32_t flags, uint64_t* pool_handle_out) {
  if (!is_open()) return -1;
  if (!pool_handle_out) return -EINVAL;
  if (va_space_handle == 0) {
    std::cerr << "[GpuDriverClient] mem_pool_create: rejected H-1 sentinel"
              << " (va_space_handle=0)\n";
    return -1;
  }
  gpu_mem_pool_create_args args = {};
  args.props.va_space_handle = va_space_handle;  // ← args.props.X 而非 args.X
  args.props.size = size;
  args.props.flags = flags;
  if (ioctl(fd_, GPU_IOCTL_MEM_POOL_CREATE, &args) < 0) {
    std::cerr << "GpuDriverClient: GPU_IOCTL_MEM_POOL_CREATE failed"
              << " (errno=" << errno << ")\n";
    return -1;
  }
  *pool_handle_out = args.pool_handle_out;
  return 0;
}
```

### 2.5 全部 15 个 override 详细签名

| IGpuDriver 方法 | IOCTL | Struct | Pattern | 返回 |
|----------------|-------|--------|---------|------|
| `stream_capture_status` | 0x52 | `gpu_stream_capture_status_args` | B | int |
| `stream_capture_begin` | 0x50 | `gpu_stream_capture_args` | A | int |
| `stream_capture_end` | 0x51 | `gpu_stream_capture_args` | B | int |
| `graph_create` | 0x53 | `gpu_graph_create_args` | B | int |
| `graph_destroy` | 0x54 | `gpu_graph_destroy_args` | A | int |
| `graph_add_kernel_node` | 0x55 | `gpu_graph_add_kernel_node_args` | A | int |
| `graph_add_memcpy_node` | 0x56 | `gpu_graph_add_memcpy_node_args` | A | int |
| `graph_instantiate` | 0x57 | `gpu_graph_instantiate_args` | B | int |
| `submit_graph` | 0x58 | `gpu_graph_launch_args` | C | int64_t |
| `destroy_graph_exec` | 0x59 | `gpu_graph_destroy_exec_args` | A | int |
| `mem_pool_create` | 0x60 | `gpu_mem_pool_create_args` | D | int |
| `mem_pool_destroy` | 0x61 | `gpu_mem_pool_destroy_args` | A | int |
| `mem_pool_alloc` | 0x62 | `gpu_mem_pool_alloc_args` | B | int |
| `mem_pool_alloc_async` | 0x63 | `gpu_mem_pool_alloc_async_args` | E | int64_t |
| `mem_pool_free_async` | 0x64 | `gpu_mem_pool_free_async_args` | C | int64_t |

**注**：`mem_pool_set_attr/get_attr/trim`（0x65/0x66/0x67）暂不加 IGpuDriver 方法（Step 1 决议：Phase 3.2 初始 scope 不需要）。如未来需要可扩 IGpuDriver 至 49 方法。

### 2.6 Error Code Mapping

| IGpuDriver 返回 | Linux errno | CUDA Result | 说明 |
|----------------|------------|-------------|------|
| 0 | 0 (success) | CUDA_SUCCESS | 成功 |
| -EINVAL | -22 | CUDA_ERROR_INVALID_VALUE | 参数无效 |
| -ENOMEM | -12 | CUDA_ERROR_OUT_OF_MEMORY | 分配失败 |
| -ENOENT | -2 | CUDA_ERROR_NOT_FOUND | handle 不存在 |
| -EBUSY | -16 | CUDA_ERROR_ILLEGAL_STATE | state 错误 |
| -EOPNOTSUPP | -95 | CUDA_ERROR_NOT_SUPPORTED | 不支持的 mode |
| -EIO | -5 | CUDA_ERROR_UNKNOWN | sim 内部错误 |
| 任意负数 | 其他 | CUDA_ERROR_UNKNOWN | fallback |

**实现位置**：每个 shim 文件内联映射（如 `if (ret == -22) return CUDA_ERROR_INVALID_VALUE`），**不**提供跨 shim 的 `errno_to_cuda_error()` helper。

## 3. Shim Layer Design (Workstreams 2-3)

> **关键事实（self-review 验证）**：
> - 现有 shim（cu_stream.cpp / cu_event.cpp / cu_mem.cpp）**完全自包含**：每个 .cpp 在匿名 namespace 内维护自己的 atomic + map + mutex state table
> - shim **不引用** GpuDriverClient / `g_gpu_client` / `g_gpu_driver_client`（grep 验证 0 匹配）
> - 部分 shim（cu_mem.cpp）通过 `runtime()->malloc/memcpy()` 调 CudaRuntimeApi 完成实际工作
> - **本 change 沿用相同模式**：3 个新 shim .cpp 文件各自维护自己的 state table

### 3.0 通用模式（cu_stream.cpp / cu_event.cpp / cu_mem.cpp 现有）

每个 shim .cpp 文件结构：
```cpp
// SCOPE: UMD-EVOLUTION
// cu_stream_capture.cpp - Stream capture state machine (Phase 3.1 REAL_IMPL).
//
// F-1 enforced: only CU_STREAM_CAPTURE_MODE_GLOBAL accepted; others return NOT_SUPPORTED.

#include <cuda.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace async_task::umd::shim {
namespace {

struct CaptureTable {
  std::atomic<std::uint64_t> next_graph_id{1};
  std::unordered_map<CUstream, uint32_t> state;  // 0=NONE, 1=ACTIVE, 2=INVALID
  std::mutex mu;
};
CaptureTable g_captures;

}  // namespace
}  // namespace async_task::umd::shim

extern "C" CUresult cuStreamBeginCapture(CUstream hStream,
                                         CUstreamCaptureMode mode) {
  if (mode != CU_STREAM_CAPTURE_MODE_GLOBAL) return CUDA_ERROR_NOT_SUPPORTED;
  if (!hStream) return CUDA_ERROR_INVALID_VALUE;
  auto& table = async_task::umd::shim::g_captures;
  std::lock_guard<std::mutex> lock(table.mu);
  if (table.state[hStream] == 1) {
    table.state[hStream] = 2;
    return CUDA_ERROR_ILLEGAL_STATE;
  }
  table.state[hStream] = 1;
  return CUDA_SUCCESS;
}
```

### 3.1 cuStreamCapture Shim (3 funcs)

**文件**: `src/umd/libcuda_shim/cu_stream_capture.cpp` (~120 lines)

**全局 state**:
```cpp
struct CaptureTable {
  std::atomic<std::uint64_t> next_graph_id{1};
  std::unordered_map<CUstream, uint32_t> state;  // 0=NONE, 1=ACTIVE, 2=INVALID
  std::mutex mu;
};
CaptureTable g_captures;
```

**3 个函数**（cuStreamBeginCapture/EndCapture 从 cu_stream.cpp:95-106 迁移过来）：

```cpp
extern "C" CUresult cuStreamBeginCapture(CUstream hStream,
                                         CUstreamCaptureMode mode) {
  if (mode != CU_STREAM_CAPTURE_MODE_GLOBAL) return CUDA_ERROR_NOT_SUPPORTED;
  if (!hStream) return CUDA_ERROR_INVALID_VALUE;
  auto& table = async_task::umd::shim::g_captures;
  std::lock_guard<std::mutex> lock(table.mu);
  if (table.state[hStream] == 1) {
    table.state[hStream] = 2;
    return CUDA_ERROR_ILLEGAL_STATE;
  }
  table.state[hStream] = 1;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuStreamEndCapture(CUstream hStream, CUgraph* phGraph) {
  if (!phGraph) return CUDA_ERROR_INVALID_VALUE;
  if (!hStream) return CUDA_ERROR_INVALID_VALUE;
  auto& table = async_task::umd::shim::g_captures;
  std::lock_guard<std::mutex> lock(table.mu);
  auto it = table.state.find(hStream);
  if (it == table.state.end() || it->second != 1) {
    return CUDA_ERROR_ILLEGAL_STATE;
  }
  table.state[hStream] = 0;
  *phGraph = reinterpret_cast<CUgraph>(
      static_cast<uintptr_t>(table.next_graph_id.fetch_add(1)));
  return CUDA_SUCCESS;
}

extern "C" CUresult cuStreamIsCapturing(CUstream hStream,
                                        CUstreamCaptureStatus* captureStatus) {
  if (!captureStatus) return CUDA_ERROR_INVALID_VALUE;
  if (!hStream) {
    *captureStatus = CU_STREAM_CAPTURE_STATUS_NONE;
    return CUDA_SUCCESS;
  }
  auto& table = async_task::umd::shim::g_captures;
  std::lock_guard<std::mutex> lock(table.mu);
  auto it = table.state.find(hStream);
  uint32_t state = (it == table.state.end()) ? 0 : it->second;
  switch (state) {
    case 0: *captureStatus = CU_STREAM_CAPTURE_STATUS_NONE; break;
    case 1: *captureStatus = CU_STREAM_CAPTURE_STATUS_ACTIVE; break;
    case 2: *captureStatus = CU_STREAM_CAPTURE_STATUS_INVALID; break;
  }
  return CUDA_SUCCESS;
}
```

**注**：原 cu_stream.cpp:95-106 的 `cuStreamBeginCapture`/`cuStreamEndCapture` stub **删除**（移到本文件）。原 cu_stream.cpp:140-148 `cuStreamGetCaptureInfo` 改为调 `cuStreamIsCapturing`。

### 3.2 cuGraph Shim (11 funcs)

**文件**: `src/umd/libcuda_shim/cu_graph.cpp` (~280 lines)
**辅助文件**: `cu_graph_node.cpp` (~50 lines) + `cu_graph_exec.cpp` (~60 lines)

**全局 state** (cu_graph.cpp 内):
```cpp
struct GraphTable {
  std::atomic<uint64_t> next_graph_id{1};
  std::atomic<uint64_t> next_exec_id{1};
  std::atomic<uint64_t> next_node_id{1};
  std::unordered_map<CUgraph, uint64_t> graph_meta;
  std::unordered_map<CUgraphExec, uint64_t> exec_meta;
  std::unordered_map<uint64_t, std::vector<uint64_t>> graph_nodes;
  std::mutex mu;
};
GraphTable g_graphs;
```

**7 个核心函数**（cuGraphCreate 从 cu_mem.cpp:258-261 迁移过来）：

```cpp
extern "C" CUresult cuGraphCreate(CUgraph* phGraph, unsigned int flags) {
  if (!phGraph) return CUDA_ERROR_INVALID_VALUE;
  (void)flags;
  auto& t = async_task::umd::shim::g_graphs;
  std::lock_guard<std::mutex> lock(t.mu);
  uint64_t id = t.next_graph_id.fetch_add(1);
  *phGraph = reinterpret_cast<CUgraph>(static_cast<uintptr_t>(id));
  t.graph_meta[*phGraph] = id;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuGraphDestroy(CUgraph hGraph) {
  if (!hGraph) return CUDA_ERROR_INVALID_VALUE;
  auto& t = async_task::umd::shim::g_graphs;
  std::lock_guard<std::mutex> lock(t.mu);
  auto it = t.graph_meta.find(hGraph);
  if (it == t.graph_meta.end()) return CUDA_ERROR_INVALID_HANDLE;
  t.graph_meta.erase(it);
  t.graph_nodes.erase(reinterpret_cast<uintptr_t>(hGraph));
  return CUDA_SUCCESS;
}

extern "C" CUresult cuGraphAddKernelNode(CUgraphNode* phGraphNode, CUgraph hGraph,
                                        const CUDA_KERNEL_NODE_PARAMS* nodeParams) {
  if (!phGraphNode || !hGraph || !nodeParams) return CUDA_ERROR_INVALID_VALUE;
  // F-3: kernargs_bo 透传 (0 = 无 BO，sim 层跳过校验)
  uint64_t kernargs_bo = (nodeParams->kernelParams != nullptr)
                            ? reinterpret_cast<uintptr_t>(nodeParams->kernelParams[0])
                            : 0;
  (void)kernargs_bo;
  auto& t = async_task::umd::shim::g_graphs;
  std::lock_guard<std::mutex> lock(t.mu);
  if (t.graph_meta.find(hGraph) == t.graph_meta.end()) {
    return CUDA_ERROR_INVALID_HANDLE;
  }
  uint64_t node_id = t.next_node_id.fetch_add(1);
  *phGraphNode = reinterpret_cast<CUgraphNode>(static_cast<uintptr_t>(node_id));
  t.graph_nodes[reinterpret_cast<uintptr_t>(hGraph)].push_back(node_id);
  return CUDA_SUCCESS;
}

extern "C" CUresult cuGraphAddMemcpyNode(CUgraphNode* phGraphNode, CUgraph hGraph,
                                        const CUDA_MEMCPY3D* copyParams, CUcontext ctx) {
  if (!phGraphNode || !hGraph || !copyParams) return CUDA_ERROR_INVALID_VALUE;
  (void)copyParams; (void)ctx;
  auto& t = async_task::umd::shim::g_graphs;
  std::lock_guard<std::mutex> lock(t.mu);
  if (t.graph_meta.find(hGraph) == t.graph_meta.end()) {
    return CUDA_ERROR_INVALID_HANDLE;
  }
  uint64_t node_id = t.next_node_id.fetch_add(1);
  *phGraphNode = reinterpret_cast<CUgraphNode>(static_cast<uintptr_t>(node_id));
  t.graph_nodes[reinterpret_cast<uintptr_t>(hGraph)].push_back(node_id);
  return CUDA_SUCCESS;
}

extern "C" CUresult cuGraphInstantiate(CUgraphExec* phGraphExec, CUgraph hGraph,
                                       CUgraphNode* pErrorNode, char* pLogBuffer,
                                       size_t bufferSize) {
  if (!phGraphExec || !hGraph) return CUDA_ERROR_INVALID_VALUE;
  (void)pErrorNode; (void)pLogBuffer; (void)bufferSize;
  auto& t = async_task::umd::shim::g_graphs;
  std::lock_guard<std::mutex> lock(t.mu);
  if (t.graph_meta.find(hGraph) == t.graph_meta.end()) {
    return CUDA_ERROR_INVALID_HANDLE;
  }
  uint64_t id = t.next_exec_id.fetch_add(1);
  *phGraphExec = reinterpret_cast<CUgraphExec>(static_cast<uintptr_t>(id));
  t.exec_meta[*phGraphExec] = id;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuGraphLaunch(CUgraphExec hGraphExec, CUstream hStream) {
  if (!hGraphExec) return CUDA_ERROR_INVALID_VALUE;
  (void)hStream;
  auto& t = async_task::umd::shim::g_graphs;
  std::lock_guard<std::mutex> lock(t.mu);
  if (t.exec_meta.find(hGraphExec) == t.exec_meta.end()) {
    return CUDA_ERROR_INVALID_HANDLE;
  }
  return CUDA_SUCCESS;  // PoC no-op (Phase 4+ 调 GpuDriverClient::submit_graph)
}

extern "C" CUresult cuGraphExecDestroy(CUgraphExec hGraphExec) {
  if (!hGraphExec) return CUDA_ERROR_INVALID_VALUE;
  auto& t = async_task::umd::shim::g_graphs;
  std::lock_guard<std::mutex> lock(t.mu);
  auto it = t.exec_meta.find(hGraphExec);
  if (it == t.exec_meta.end()) return CUDA_ERROR_INVALID_HANDLE;
  t.exec_meta.erase(it);
  return CUDA_SUCCESS;
}
```

**2 个 Node 函数** (cu_graph_node.cpp):
```cpp
extern "C" CUresult cuGraphNodeGetType(CUgraphNode hNode, CUgraphNodeType* pType) {
  if (!pType) return CUDA_ERROR_INVALID_VALUE;
  *pType = CU_GRAPH_NODE_TYPE_KERNEL;  // Phase 3.1 only kernel
  (void)hNode;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuGraphNodeSetAttribute(CUgraphNode hNode, CUgraphNodeAttr attr,
                                           const CUgraphNodeAttrValue* value) {
  (void)hNode; (void)attr; (void)value;
  return CUDA_ERROR_NOT_SUPPORTED;
}
```

**2 个 Exec 函数** (cu_graph_exec.cpp):
```cpp
extern "C" CUresult cuGraphExecKernelNodeSetParams(CUgraphExec hGraphExec,
                                                    CUgraphNode hNode,
                                                    const CUDA_KERNEL_NODE_PARAMS* nodeParams) {
  if (!hGraphExec || !hNode || !nodeParams) return CUDA_ERROR_INVALID_VALUE;
  (void)hGraphExec; (void)hNode; (void)nodeParams;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuGraphExecMemcpyNodeSetParams(CUgraphExec hGraphExec,
                                                    CUgraphNode hNode,
                                                    const CUDA_MEMCPY3D* nodeParams) {
  if (!hGraphExec || !hNode || !nodeParams) return CUDA_ERROR_INVALID_VALUE;
  (void)hGraphExec; (void)hNode; (void)nodeParams;
  return CUDA_SUCCESS;
}
```

### 3.3 cuMemPool Shim (8 funcs)

**文件**: `src/umd/libcuda_shim/cu_mem_pool.cpp` (~200 lines)

**全局 state**:
```cpp
struct MemPoolTable {
  std::atomic<uint64_t> next_pool_id{1};
  std::unordered_map<CUmemPool, uint64_t> pool_meta;  // handle → maxSize
  std::mutex mu;
};
MemPoolTable g_pools;
```

**8 个函数**:
```cpp
extern "C" CUresult cuMemPoolCreate(CUmemPool* pool, const CUmemPoolProps* poolProps) {
  if (!pool || !poolProps) return CUDA_ERROR_INVALID_VALUE;
  if (poolProps->vaSpaceHandle == 0) return CUDA_ERROR_INVALID_VALUE;
  auto& t = async_task::umd::shim::g_pools;
  std::lock_guard<std::mutex> lock(t.mu);
  uint64_t id = t.next_pool_id.fetch_add(1);
  *pool = reinterpret_cast<CUmemPool>(static_cast<uintptr_t>(id));
  t.pool_meta[*pool] = poolProps->maxSize;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuMemPoolDestroy(CUmemPool pool) {
  if (!pool) return CUDA_ERROR_INVALID_VALUE;
  auto& t = async_task::umd::shim::g_pools;
  std::lock_guard<std::mutex> lock(t.mu);
  auto it = t.pool_meta.find(pool);
  if (it == t.pool_meta.end()) return CUDA_ERROR_INVALID_HANDLE;
  t.pool_meta.erase(it);
  return CUDA_SUCCESS;
}

// WARNING (Phase 3 PoC): cuMemPoolAlloc returns a SYNTHETIC pointer
//   (pool_handle + size). The pointer MUST NOT be dereferenced or passed
//   to any memory operation. Real allocation is deferred to Phase 4.
//   Shims/tests may store this value but cannot use it as memory.
extern "C" CUresult cuMemPoolAlloc(CUdeviceptr* dptr, size_t size, CUmemPool pool,
                                    CUmemAllocationParams* allocParams) {
  if (!dptr || !pool || size == 0) return CUDA_ERROR_INVALID_VALUE;
  (void)allocParams;
  auto& t = async_task::umd::shim::g_pools;
  std::lock_guard<std::mutex> lock(t.mu);
  auto it = t.pool_meta.find(pool);
  if (it == t.pool_meta.end()) return CUDA_ERROR_INVALID_HANDLE;
  *dptr = reinterpret_cast<CUdeviceptr>(static_cast<uintptr_t>(it->first) + size);
  return CUDA_SUCCESS;
}

extern "C" CUresult cuMemPoolFree(CUdeviceptr dptr) {
  if (!dptr) return CUDA_ERROR_INVALID_VALUE;
  return CUDA_SUCCESS;  // PoC no-op
}

extern "C" CUresult cuMemPoolSetAttribute(CUmemPool pool, CUmemPoolAttr attr, void* value) {
  if (!pool || !value) return CUDA_ERROR_INVALID_VALUE;
  if (attr != CU_MEMPOOL_ATTR_RELEASE_THRESHOLD &&
      attr != CU_MEMPOOL_ATTR_REUSE_FOLLOW_EVENT_DEPENDENCIES) {
    return CUDA_ERROR_NOT_SUPPORTED;
  }
  (void)pool; (void)attr; (void)value;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuMemPoolGetAttribute(CUmemPool pool, CUmemPoolAttr attr, void* value) {
  if (!pool || !value) return CUDA_ERROR_INVALID_VALUE;
  if (attr == CU_MEMPOOL_ATTR_RELEASE_THRESHOLD) {
    *static_cast<uint64_t*>(value) = 0;
  } else if (attr == CU_MEMPOOL_ATTR_REUSE_FOLLOW_EVENT_DEPENDENCIES) {
    *static_cast<uint32_t*>(value) = 0;
  } else {
    return CUDA_ERROR_NOT_SUPPORTED;
  }
  return CUDA_SUCCESS;
}

extern "C" CUresult cuMemPoolTrim(CUmemPool pool, size_t minBytesToKeep) {
  if (!pool) return CUDA_ERROR_INVALID_VALUE;
  (void)pool; (void)minBytesToKeep;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuMemPoolAllocAsync(CUdeviceptr* dptr, size_t size, CUmemPool pool,
                                        CUstream hStream) {
  if (!dptr || !pool || size == 0) return CUDA_ERROR_INVALID_VALUE;
  (void)hStream;
  return cuMemPoolAlloc(dptr, size, pool, nullptr);
}

extern "C" CUresult cuMemPoolFreeAsync(CUdeviceptr dptr, CUstream hStream) {
  (void)hStream;
  return cuMemPoolFree(dptr);
}
```

**F-2 attr value blob 简化**：PoC 不实际存 attr 值（仅校验 attr 枚举合法）。Phase 4 完整实现可扩展为 `value_blob[4]` 数组存储。

### 3.4 现有 stub 迁移

| 现有 stub | 当前位置 | 处理 |
|-----------|----------|------|
| `cuStreamBeginCapture` | cu_stream.cpp:95-100 | **删除**，移到 cu_stream_capture.cpp |
| `cuStreamEndCapture` | cu_stream.cpp:102-106 | **删除**，移到 cu_stream_capture.cpp |
| `cuStreamGetCaptureInfo` | cu_stream.cpp:140-148 | **修改**调 `cuStreamIsCapturing` |
| `cuGraphCreate` | cu_mem.cpp:258-261 | **删除**，移到 cu_graph.cpp |

## 4. Test Design (Workstream 4)

### 4.1 Test Pattern

```cpp
// tests/umd/test_cu_stream_capture.cpp
TEST_CASE("cu_stream_capture: GLOBAL begin → end → graph") {
  CUstream stream = create_test_stream();  // helper

  // GIVEN: capture state is NONE
  CUstreamCaptureStatus status;
  REQUIRE(cuStreamIsCapturing(stream, &status) == CUDA_SUCCESS);
  REQUIRE(status == CU_STREAM_CAPTURE_STATUS_NONE);

  // WHEN: begin capture with GLOBAL
  REQUIRE(cuStreamBeginCapture(stream, CU_STREAM_CAPTURE_MODE_GLOBAL) == CUDA_SUCCESS);

  // THEN: state ACTIVE
  REQUIRE(cuStreamIsCapturing(stream, &status) == CUDA_SUCCESS);
  REQUIRE(status == CU_STREAM_CAPTURE_STATUS_ACTIVE);

  // WHEN: end capture
  CUgraph graph = nullptr;
  REQUIRE(cuStreamEndCapture(stream, &graph) == CUDA_SUCCESS);
  REQUIRE(graph != nullptr);

  // THEN: state back to NONE
  REQUIRE(cuStreamIsCapturing(stream, &status) == CUDA_SUCCESS);
  REQUIRE(status == CU_STREAM_CAPTURE_STATUS_NONE);

  // CLEANUP
  cuGraphDestroy(graph);
  destroy_test_stream(stream);
}
```

### 4.2 Test Categorization

| Test file | cases | 覆盖 |
|-----------|-------|------|
| `test_cu_stream_capture.cpp` | ≥30 | State machine, GLOBAL mode, replay replay, 错误路径 |
| `test_cu_graph.cpp` | ≥20 | create/destroy/add_*/instantiate/launch/exec_destroy + 错误路径 |
| `test_cu_mem_pool.cpp` | ≥25 | create/destroy/alloc/free/attr/trim + Option B boundary |
| **总计** | **≥75** | **151+ total** (含 76+ 回归) |

## 5. File-Level TODO (C1-C7 commits)

| # | Commit | Files | 行数 |
|---|--------|-------|------|
| C1 | docs(sync-plan)-prep | `plans/sync-plan.md` | +5 |
| C2 | feat(gpu-driver-client) | `include/test_fixture/gpu_driver_client.h` | +250 |
| C3 | feat(shim)-3.1 | `src/umd/libcuda_shim/cu_stream_capture.cpp` (NEW) + 修改 cu_stream.cpp (删除 11 行 stub) | +130 |
| C4 | feat(shim)-3.2 | `src/umd/libcuda_shim/cu_graph.cpp` + `cu_graph_node.cpp` + `cu_graph_exec.cpp` (3 NEW) + 修改 cu_mem.cpp (删除 4 行 cuGraphCreate stub) | +400 |
| C5 | feat(shim)-3.3 | `src/umd/libcuda_shim/cu_mem_pool.cpp` (NEW) | +200 |
| C6 | test(shim) | `tests/umd/test_cu_stream_capture.cpp` + `test_cu_graph.cpp` + `test_cu_mem_pool.cpp` | +600 |
| C7 | docs(sync) + openspec archive | `plans/sync-plan.md` (Step 3 done entry) + `openspec/changes/archive/2026-07-05-phase3-1-igpu-driver-extension/` | +200 |

**总行数**: 5 + 250 + 130 + 400 + 200 + 600 + 200 = **~1785 行**

## 6. Architectural Decisions (formalized)

### D-SC-5: Capture mode GLOBAL only (Step 3)
- cuStreamBeginCapture: mode != CU_STREAM_CAPTURE_MODE_GLOBAL → `CUDA_ERROR_NOT_SUPPORTED`
- 不静默 fallback（避免行为不一致）

### D-SC-9: GpuQueueEmu API integration path (Step 2 端)
- `submit_graph` 的 GpuDriverClient override 调 `GPU_IOCTL_GRAPH_LAUNCH`（IOCTL 已存在）
- 内部 handler 调 `GpuQueueEmu::submit(gpfifo_addr, entry_count)`（单 API）

### D-SC-11: fence_id range partition (Step 2 端)
- HAL fence: `[1, 1<<32-1]`（Step 2 之前已使用）
- Sim fence: `[1<<32, INT64_MAX]`（`sim_fence_id_alloc()` 分配）
- `gpu_ioctl_wait_fence` 在 Step 2 端按范围分发

### D-SC-12: kernargs_bo_handle == 0 means no BO (Step 3)
- shim 层透传 0（不修改）
- sim 层 0 跳过 BO 校验

### D-MP-1: Pool VA range Option B (Step 2 端)
- shim 层调 `mem_pool_create(va_space_handle, ...)`（via GpuDriverClient override）
- sim 层在 VA Space 内预留子范围
- **不**修改 `libgpu_core/gpu_buddy`

### D-S3-1: Self-contained shim pattern (Step 3, NEW)
- 每个 shim .cpp 在匿名 namespace 内维护自己的 atomic + map + mutex state table
- shim 不调 GpuDriverClient（GpuDriverClient 是独立路径）
- 与 cu_stream.cpp / cu_event.cpp / cu_mem.cpp 既有模式一致

### D-S3-2: GpuDriverClient inline ioctl pattern (Step 3, NEW)
- 15 个新 override 方法各自 inline `ioctl(fd_, GPU_IOCTL_*, &args)`
- **不**抽取 `submit_ioctl()` helper（与既有 31 个方法风格一致）

### D-S3-3: Existing stub migration (Step 3, NEW)
- cu_stream.cpp:95-106 (BeginCapture/EndCapture) → 删除，移到 cu_stream_capture.cpp
- cu_stream.cpp:140-148 (GetCaptureInfo) → 修改调 cuStreamIsCapturing
- cu_mem.cpp:258-261 (cuGraphCreate) → 删除，移到 cu_graph.cpp

## 7. References

- [UsrLinuxEmu PR #20](https://github.com/chisuhua/UsrLinuxEmu/pull/20) — Step 2 (MERGED)
- [TaskRunner Step 1 commit 21f71c9](https://github.com/chisuhua/TaskRunner/commit/21f71c9) — IGpuDriver 31→46
- [Coordination PR](../2026-07-05-phase3-1-stream-mempool-coordination.md) — 4-step coordination SSOT
- [TADR-301 IGpuDriver 46-method contract](../../../shared/adr/tadr-301-igpu-driver-contract.md)
- [ADR-015 GPU IOCTL unification](https://github.com/chisuhua/UsrLinuxEmu) — 0x50-0x67
- [ADR-032 IGpuDriver abstraction](https://github.com/chisuhua/TaskRunner)
- [ADR-035 cross-repo governance](https://github.com/chisuhua/TaskRunner)