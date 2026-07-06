# Change: phase3-step3-shim-and-forwarding

> **状态**: 🔄 PROPOSED（2026-07-06，Step 1 + Step 2 已 merge，启动 Step 3 实施）
> **创建**: 2026-07-06
> **修订**: 2026-07-06 self-review — 修正 struct 字段名 + 移除虚构的 `submit_ioctl()` helper + 改为自包含 shim 模式
> **前置**: TaskRunner `12bed8d` (Step 1 IGpuDriver 31→46) + UsrLinuxEmu `138f15a` (Step 2 sim + IOCTL PR #20 merged)
> **下一步**: review → 实施 C1-C7 atomic commits → 通知 UsrLinuxEmu owner 触发 Step 4

## Why

### 现状

Phase 3.1/3.2 跨仓协调的 Step 1（TaskRunner IGpuDriver 31→46）和 Step 2（UsrLinuxEmu sim + IOCTL + handlers）均已 merge 到各自仓的 main：
- TaskRunner @ `12bed8d`（commit `e6a34eb` merge phase3-1-igpu-driver-extension）
- UsrLinuxEmu @ `138f15a`（PR #20 merged 2026-07-06 06:22 UTC, 36 处新 IOCTL 引用）

但 **桥接层**（GpuDriverClient 15 override + shim）完全缺失：
- `src/test_fixture/gpu_driver_client.cpp` 仍只 override 31 个 IGpuDriver 方法（Step 1 加的 15 个仍是 no-op default）
- `src/umd/libcuda_shim/` 无 `cu_stream_capture.cpp` / `cu_graph.cpp` / `cu_mem_pool.cpp`
- 无 Phase 3 E2E 测试覆盖

### Gap

| 类别 | Step 1 后状态 | Step 3 后状态 |
|------|--------------|--------------|
| GpuDriverClient override | 31 methods | 46 methods (15 forwarding to GPU_IOCTL_*) |
| cuStream API 覆盖 | cuStreamBeginCapture STUB (returns NOT_IMPLEMENTED) | cuStreamBeginCapture/EndCapture/IsCapturing REAL_IMPL → sim_stream_capture_* |
| cuGraph API 覆盖 | 11 STUB (NOT_IMPLEMENTED) | 11 REAL_IMPL → sim_graph_* |
| cuMemPool API 覆盖 | 8 STUB (NOT_IMPLEMENTED) | 8 REAL_IMPL → sim_mem_pool_* |
| E2E 测试 | 76+ cases | 151+ cases (≥75 new) |
| docs-audit false-positives | 8 (cu* STUB sanity) | 0 (cu* → REAL_IMPL) |

### Why Now

1. **Step 1 + Step 2 已就位** — IOCTL #defines 可通过 symlink `UsrLinuxEmu` 访问（验证：36 处引用）
2. **fence_id 范围划分已确定** — B-3 决策：HAL `[1, 1<<32-1]` + sim `[1<<32, INT64_MAX]`
3. **sim_mem_pool_alloc 已调 alloc_bo**（Fix-2 Option B）— 验证：`mem_pool.cpp` first-fit 4K-aligned scan
4. **PR #20 49 测试 cases 全过** — backend primitives 验证充分，可直接接入
5. **估时可控**：5-7 天（5 个 workstream），按 N-3 拆 3 commit 风险分散

### Why This Is The Right Step

- **去耦设计**：Step 3 完全在 TaskRunner 仓内实施，不需要 UsrLinuxEmu 仓进一步动作
- **N-3 拆分**：按协调 PR §12.3 决议，Step 3 拆 3 commit（GpuDriverClient override / shim / E2E）以降低回滚成本
- **可测试性强**：GpuDriverClient override 单独可编译测试，shim 单独可链接测试，E2E 端到端验证

## What Changes

### 1. GpuDriverClient 15 forwarding overrides (Workstream 1, 1 day)

**文件**: `include/test_fixture/gpu_driver_client.h`（声明 + 15 个 override 全部为 inline 实现，**不**创建或修改任何 .cpp；与既有 31 个 override 同模式）

**关键事实**：
- GpuDriverClient 类已存在（577 行），Step 1 加的 15 个 IGpuDriver 虚函数目前**未实现**（仍是 no-op `return -1`）
- **没有** `submit_ioctl()` helper — 每个方法 inline `ioctl(fd_, GPU_IOCTL_*, &args)`（参考既有 `free_bo` / `wait_fence` 模式）
- 全局变量名是 `g_gpu_client`（在 `async_task::gpu::` 命名空间），**不是** `g_gpu_driver_client`

**15 个新 override 方法**：

| IGpuDriver 方法 | IOCTL 编号 | IOCTL struct |
|----------------|------------|--------------|
| `stream_capture_status` | 0x52 | `gpu_stream_capture_status_args` |
| `stream_capture_begin` | 0x50 | `gpu_stream_capture_args` |
| `stream_capture_end` | 0x51 | `gpu_stream_capture_args` |
| `graph_create` | 0x53 | `gpu_graph_create_args` |
| `graph_destroy` | 0x54 | `gpu_graph_destroy_args` |
| `graph_add_kernel_node` | 0x55 | `gpu_graph_add_kernel_node_args` |
| `graph_add_memcpy_node` | 0x56 | `gpu_graph_add_memcpy_node_args` |
| `graph_instantiate` | 0x57 | `gpu_graph_instantiate_args` |
| `submit_graph` | 0x58 | `gpu_graph_launch_args` |
| `destroy_graph_exec` | 0x59 | `gpu_graph_destroy_exec_args` |
| `mem_pool_create` | 0x60 | `gpu_mem_pool_create_args` |
| `mem_pool_destroy` | 0x61 | `gpu_mem_pool_destroy_args` |
| `mem_pool_alloc` | 0x62 | `gpu_mem_pool_alloc_args` |
| `mem_pool_alloc_async` | 0x63 | `gpu_mem_pool_alloc_async_args` |
| `mem_pool_free_async` | 0x64 | `gpu_mem_pool_free_async_args` |

**实施模式**（以 `graph_create` 为例，header inline 实现）：
```cpp
int graph_create(uint64_t* graph_handle_out) override {
  if (!is_open()) return -1;
  if (!graph_handle_out) return -EINVAL;
  gpu_graph_create_args args = {};  // zero-init OUT fields
  if (ioctl(fd_, GPU_IOCTL_GRAPH_CREATE, &args) < 0) {
    std::cerr << "GpuDriverClient: GPU_IOCTL_GRAPH_CREATE failed"
              << " (errno=" << errno << ")\n";
    return -1;
  }
  *graph_handle_out = args.graph_handle_out;
  return 0;
}
```

### 2. cuStreamCapture + cuGraph shim (Workstream 2, 1.5 days)

**关键事实（self-review 修正）**：shim 是**自包含**的，每个 .cpp 在匿名 namespace 内维护自己的 atomic + map + mutex state table（与既有 cu_stream.cpp / cu_event.cpp / cu_mem.cpp 一致）。Shim **不**调 GpuDriverClient。**Phase 4+** 可桥接 shim → GpuDriverClient。

**新增文件**：
- `src/umd/libcuda_shim/cu_stream_capture.cpp`（3 funcs: cuStreamBeginCapture / cuStreamEndCapture / cuStreamIsCapturing）
- `src/umd/libcuda_shim/cu_graph.cpp`（7 funcs: cuGraphCreate / cuGraphDestroy / cuGraphAddKernelNode / cuGraphAddMemcpyNode / cuGraphInstantiate / cuGraphLaunch / cuGraphExecDestroy）
- `src/umd/libcuda_shim/cu_graph_node.cpp`（2 funcs: cuGraphNodeGetType / cuGraphNodeSetAttribute）
- `src/umd/libcuda_shim/cu_graph_exec.cpp`（2 funcs: cuGraphExecKernelNodeSetParams / cuGraphExecMemcpyNodeSetParams）

**修改文件**：
- `src/umd/libcuda_shim/cu_stream.cpp`（删除 11 行 stub）
- `src/umd/libcuda_shim/cu_mem.cpp`（删除 4 行 cuGraphCreate stub）

**全局 state**（cu_stream_capture.cpp 内，匿名 namespace）：
```cpp
struct CaptureTable {
  std::atomic<std::uint64_t> next_graph_id{1};
  std::unordered_map<CUstream, uint32_t> state;  // 0=NONE, 1=ACTIVE, 2=INVALID
  std::mutex mu;
};
CaptureTable g_captures;
```

**实施模式**（以 cuStreamBeginCapture 为例，与 design.md §3.1 一致）：
```cpp
extern "C" CUresult cuStreamBeginCapture(CUstream hStream,
                                         CUstreamCaptureMode mode) {
  if (mode != CU_STREAM_CAPTURE_MODE_GLOBAL) return CUDA_ERROR_NOT_SUPPORTED;  // F-1
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

**Handle 约定**：
- `CUstream` = `void*`（直接用作 CaptureTable state key，不经 stream_id 转换）
- `CUgraph` / `CUgraphExec` = `void*`（atomic counter 分配）
- `CUresult` 映射：`< 0` → `CUDA_ERROR_*`，`== 0` → `CUDA_SUCCESS`

### 3. cuMemPool shim (Workstream 3, 2 days)

**新增文件**：
- `src/umd/libcuda_shim/cu_mem_pool.cpp`（8 funcs: cuMemPoolCreate / cuMemPoolDestroy / cuMemPoolAlloc / cuMemPoolFree / cuMemPoolSetAttribute / cuMemPoolGetAttribute / cuMemPoolTrim / cuMemPoolExportToShareableHandle）

**全局 state**（cu_mem_pool.cpp 内，匿名 namespace）：
```cpp
struct MemPoolTable {
  std::atomic<uint64_t> next_pool_id{1};
  std::unordered_map<CUmemPool, uint64_t> pool_meta;  // handle → maxSize
  std::mutex mu;
};
MemPoolTable g_pools;
```

**实施模式**（以 cuMemPoolCreate 为例，与 design.md §3.3 + B-2 Option B 一致）：
```cpp
extern "C" CUresult cuMemPoolCreate(CUmemPool* pool, const CUmemPoolProps* poolProps) {
  if (!pool || !poolProps) return CUDA_ERROR_INVALID_VALUE;
  if (poolProps->vaSpaceHandle == 0) return CUDA_ERROR_INVALID_VALUE;  // B-2: H-1 sentinel guard
  auto& t = async_task::umd::shim::g_pools;
  std::lock_guard<std::mutex> lock(t.mu);
  uint64_t id = t.next_pool_id.fetch_add(1);
  *pool = reinterpret_cast<CUmemPool>(static_cast<uintptr_t>(id));
  t.pool_meta[*pool] = poolProps->maxSize;
  return CUDA_SUCCESS;
}
```

> **注**：PoC 中 `cuMemPoolAlloc` 返回合成指针 `pool_handle + size`（design.md §3.3 WARNING），不可解引用。Phase 4+ 接入 `g_gpu_client->mem_pool_alloc()` IOCTL 路径。
> 全局 GpuDriverClient 实例名是 `g_gpu_client`（`async_task::gpu::` 命名空间），**不是** `g_gpu_driver_client`（参见上文"关键事实"）。

### 4. E2E test coverage (Workstream 4, 2 days)

**新增文件**：
- `tests/umd/test_cu_stream_capture.cpp`（≥30 cases）
- `tests/umd/test_cu_graph.cpp`（≥20 cases）
- `tests/umd/test_cu_mem_pool.cpp`（≥25 cases）

**测试模式**（以 cuStreamBeginCapture 为例，shim-only 验证，不经 GpuDriverClient）：
```cpp
TEST_CASE("cu_stream_capture: cuStreamBeginCapture with GLOBAL mode") {
  // GIVEN: a fresh stream with empty capture state
  CUstream stream = create_test_stream();  // helper: returns unique handle

  CUstreamCaptureStatus status;
  REQUIRE(cuStreamIsCapturing(stream, &status) == CUDA_SUCCESS);
  REQUIRE(status == CU_STREAM_CAPTURE_STATUS_NONE);

  // WHEN: begin capture with GLOBAL mode
  CUresult ret = cuStreamBeginCapture(stream, CU_STREAM_CAPTURE_MODE_GLOBAL);

  // THEN: capture state is ACTIVE
  CHECK(ret == CUDA_SUCCESS);
  REQUIRE(cuStreamIsCapturing(stream, &status) == CUDA_SUCCESS);
  CHECK(status == CU_STREAM_CAPTURE_STATUS_ACTIVE);

  // CLEANUP
  CUgraph graph = nullptr;
  REQUIRE(cuStreamEndCapture(stream, &graph) == CUDA_SUCCESS);
  REQUIRE(graph != nullptr);
  REQUIRE(cuGraphDestroy(graph) == CUDA_SUCCESS);
  destroy_test_stream(stream);
}
```

> **注**：测试通过 shim 自身的 `cuStreamIsCapturing` 验证状态变迁（CUstreamCaptureStatus 枚举），**不**直接调 GpuDriverClient 的 `stream_capture_status`（保持 E2E 测试纯走 shim 路径，符合 D-S3-1 自包含 shim 设计）。

### 5. Documentation + cross-repo sync (Workstream 5, 0.5 day)

**修改文件**：
- `plans/sync-plan.md`：状态更新 Step 3 done + Step 4 触发
- `../../../UsrLinuxEmu/docs/07-integration/phase3-step3.md`（NEW）：Step 3 实施记录

**OpenSpec archive**：
- `openspec/changes/2026-07-05-phase3-1-igpu-driver-extension/` → archive/（本 change 完成后）

## Capabilities

### New Capabilities

- `phase3-step3-shim`: cuStreamCapture/cuGraph/cuMemPool 完整 shim 实现 + E2E 测试
- `phase3-step3-forwarding`: GpuDriverClient 15 IOCTL forwarding 完整实现
- `fence-id-range-dispatch`: HAL/sim fence_id 范围分发（Step 2 端实施，Step 3 端使用）

### Modified Capabilities

- `igpu-driver-abstraction` (TADR-301): 31 → 46 方法，15 个新增有完整 GpuDriverClient override
- `shim-layer` (post-refactor-architecture §1.10): 102 → 117+ REAL_IMPL APIs
- `docs-audit-baseline`: 8 STUB sanity false-positives 消失（cu* → REAL_IMPL）

## Impact

| 影响项 | 范围 | 风险 |
|--------|------|------|
| 代码 | 15 override (~200 lines) + 24 shim funcs (~530 lines) + 75 E2E tests (~600 lines) = **~1330 lines** | 中 |
| 头文件 ABI | 0 changes（IGpuDriver 已扩展） | 零 |
| GpuDriverClient | 31 → 46 methods override | 低 |
| Shim API | 24 新 REAL_IMPL | 低 |
| 测试 | 76 → 151+ cases（+75） | 低 |
| 文档 | sync-plan.md + docs/07-integration/phase3-step3.md | 零 |
| 跨仓 | 无 UsrLinuxEmu 改动（Step 3 完成后才触发 Step 4） | 零 |

**风险缓解**：
- 严格 3 commit 拆分（N-3）：override / shim / E2E 各自可独立回滚
- 每个 commit 后跑 76+ 回归 + 新 commit 自身测试
- IOCTL struct 完整引用 UsrLinuxEmu `gpu_ioctl.h`（通过 symlink），编译期验证
- fence_id 范围分发在 Step 2 端已实施，Step 3 端仅消费（不修改）

## Tasks

> 完整 tasks 列表见 [`tasks.md`](tasks.md)。高层分解（5 个 workstream）：

### Phase 1: 准备（Day 1, 0.5h）
- [ ] 1.1 创建 openspec change 目录与 4 个文件（本文档 + design.md + tasks.md + spec.md）
- [ ] 1.2 创建 worktree `phase3-step3-shim-and-forwarding`（基于 main @ 12bed8d）
- [ ] 1.3 验证前置：Step 1 已 merge（grep IGpuDriver 46 方法）+ Step 2 IOCTL 可达（cat symlink gpu_ioctl.h）
- [ ] 1.4 验证 build：`cmake --build build` 无警告，76+ 回归测试全过

### Phase 2: GpuDriverClient override (Day 1, 1 day)
- [ ] 2.1 实施 10 graph/capture override 方法（C2 commit #1）
- [ ] 2.2 实施 5 mempool override 方法（C2 commit #2） — 或合并
- [ ] 2.3 验证：cmake --build 通过 + 76+ 回归测试全过
- [ ] 2.4 提交 commit `feat(gpu-driver-client): 15 forwarding overrides for Phase 3.1/3.2`

### Phase 3: cuStream + cuGraph shim (Day 2-3, 2 days)
- [ ] 3.1 实施 cu_stream_capture.cpp（3 funcs）
- [ ] 3.2 实施 cu_graph.cpp（7 funcs）+ cu_graph_node.cpp（2 funcs）+ cu_graph_exec.cpp（2 funcs）
- [ ] 3.3 验证：build 通过 + shim symbols 导出（nm libcuda_taskrunner.so | grep -E "cuStreamBegin|cuGraphCreate"）
- [ ] 3.4 提交 commit `feat(shim): Phase 3.1 cuStreamCapture + cuGraph shim (11 REAL_IMPL)`

### Phase 4: cuMemPool shim (Day 4-5, 2 days)
- [ ] 4.1 实施 cu_mem_pool.cpp（8 funcs）
- [ ] 4.2 验证：build 通过 + cuMemPool* symbols 导出
- [ ] 4.3 提交 commit `feat(shim): Phase 3.2 cuMemPool shim (8 REAL_IMPL)`

### Phase 5: E2E tests (Day 5-6, 1.5 days)
- [ ] 5.1 实施 test_cu_stream_capture.cpp（≥30 cases）
- [ ] 5.2 实施 test_cu_graph.cpp（≥20 cases）
- [ ] 5.3 实施 test_cu_mem_pool.cpp（≥25 cases）
- [ ] 5.4 验证：75+ 新测试全过，76+ 回归全过 = 151+ total
- [ ] 5.5 提交 commit `test(shim): Phase 3.1+3.2 E2E coverage (≥75 cases)`

### Phase 6: 文档 + 跨仓 sync (Day 7, 0.5 day)
- [ ] 6.1 更新 plans/sync-plan.md（Step 3 done → Step 4 触发）
- [ ] 6.2 新建 docs/07-integration/phase3-step3.md
- [ ] 6.3 openspec archive 2026-07-05-phase3-1-igpu-driver-extension
- [ ] 6.4 提交 commit `docs(sync): Phase 3 Step 3 done + Step 4 trigger`
- [ ] 6.5 push 到 origin + 通知 UsrLinuxEmu owner

## Launch Conditions

本 change 满足以下条件后启动实施：

- ✅ **LC1**: Step 1 IGpuDriver 31→46 已 merge（commit `21f71c9` + `e6a34eb`）
- ✅ **LC2**: Step 2 UsrLinuxEmu sim + IOCTL PR #20 已 merge（commit `138f15a`）
- ✅ **LC3**: Symlink `UsrLinuxEmu` 验证可达（36 处 IOCTL 引用可读）
- ✅ **LC4**: TaskRunner 76+ 回归基线已通过
- ⏳ **LC5**: worktree `phase3-step3-shim-and-forwarding` 待创建（实施 Day 1）

## Out of Scope（显式排除）

| 排除项 | 原因 | 延后到 |
|--------|------|--------|
| UsrLinuxEmu 仓任何改动 | Step 3 纯 TaskRunner 侧 | Step 4 |
| `libgpu_core/gpu_buddy` 改动 | sim_mem_pool 已通过 Option B 集成 | D-3 lite |
| 真实 kernel 执行 | 需要 ELF/CUBIN parser | D-3 lite |
| 多设备 cuDeviceGetCount > 1 | Stage 2 之后 | Phase 3.5 |
| Vulkan Runtime API | Phase 0 决策（Q3）| 不实现 |
| Pool 跨进程共享 | Phase 1.5 之后 | PR-025 |

## 关联 Changes

### TaskRunner 侧
- **前置**（merged）: `openspec/changes/2026-07-05-phase3-1-igpu-driver-extension/` — Step 1 IGpuDriver 31→46
- **本 change 完成后 archive**: `2026-07-05-phase3-1-igpu-driver-extension/`
- **独立 parallel**: `2026-07-02-phase17-test-coverage-completion/` (frontend-only)
- **计划**: `docs/superpowers/plans/2026-07-05-umd-phase3.3-event-texture.md` (frontend-only, DRAFT)

### UsrLinuxEmu 侧（外部）
- **前置**（merged）: `openspec/changes/2026-07-05-sim-stream-primitive-support/` — PR #20 @ commit `138f15a`
- **后续**: 触发 Step 4 submodule bump（本 change 完成后）

### 跨仓协调 PR
- `docs/superpowers/cross-repo-prs/2026-07-05-phase3-1-stream-mempool-coordination.md` (507 行)
- `docs/superpowers/cross-repo-prs/2026-07-05-usrlx-architecture-review.md` (CONDITIONAL ACCEPT → 11 acks)

### 关联 ADR
- [ADR-032 H-2.5 IGpuDriver 抽象层](https://github.com/chisuhua/TaskRunner)
- [ADR-035 治理规则](https://github.com/chisuhua/TaskRunner) — 跨仓同步协议
- [TADR-301 IGpuDriver 46 方法契约](../../../shared/adr/tadr-301-igpu-driver-contract.md) — 当前基线
- [ADR-015 GPU IOCTL 统一](https://github.com/chisuhua/UsrLinuxEmu) — IOCTL 0x50-0x67
- [ADR-033 H-3 Phase 2 Lifecycle](https://github.com/chisuhua/UsrLinuxEmu) — VA Space 集成
