# Tasks: phase3-step3-shim-and-forwarding

> **状态**: ✅ DONE（2026-07-07，PR #7 merge + submodule bump 完成，archiving）
> **前置**: Step 1 merged (e6a34eb) + Step 2 merged (138f15a, PR #20) + Step 3 merged (02363b8, PR #7) + Step 4 done (458299e)
> **Archive**: → openspec/changes/archive/2026-07-06-phase3-step3-shim-and-forwarding/ (post-merge)
> **修正**: 2026-07-06 self-review 后适配现有 shim 模式（自包含 atomic+map+mutex）+ GpuDriverClient inline ioctl

## 0. 前置条件（验证基线）

- [x] **0.1** 确认 Step 1 基线提交 `12bed8d` 是当前 HEAD 的祖先：
  ```bash
  cd /workspace/project/UsrLinuxEmu/external/TaskRunner
  git merge-base --is-ancestor 12bed8d HEAD && echo "Step 1 baseline present"
  # 预期: 输出 "Step 1 baseline present"（exit 0）
  # HEAD 可以是 ed8ecc0 或任何包含 12bed8d 的后继提交
  ```
- [x] **0.2** 确认 IGpuDriver 方法数 = 46（不含析构 = 47）：
  ```bash
  grep -c "^    virtual " include/shared/igpu_driver.hpp
  # 预期: 47 (46 + 1 析构)
  ```
- [x] **0.3** 确认 GpuDriverClient 当前 override 数 = 29（所有 inline 方法使用 `override` 关键字）：
  ```bash
  grep -c "override" include/test_fixture/gpu_driver_client.h
  # 预期: 29（当前 override 数量；Step 3 完成后 = 44）
  # 注: 使用 "override" 关键字计数而非 "GpuDriverClient::" 因为所有方法在 header inline 定义
  ```
- [x] **0.4** 确认 UsrLinuxEmu symlink + IOCTL 可达：
  ```bash
  cat UsrLinuxEmu/plugins/gpu_driver/shared/gpu_ioctl.h | grep -c "GPU_IOCTL_STREAM\|GPU_IOCTL_GRAPH\|GPU_IOCTL_MEM_POOL"
  # 预期: ≥ 18 (PR #20 merged 后)
  ```
- [x] **0.5** 确认基线构建 + 测试通过：
  ```bash
  cd build && cmake --build . -j4
  for t in test_cuda_scheduler test_gpu_architecture test_gpu_phase2 \
            test_cuda_runtime_api test_cuda_shim; do
    ./$t 2>&1 | tail -1
  done
  # 预期: 76/76+ PASS
  ```
- [x] **0.6** 创建 worktree（基于 main @ 12bed8d）：
  ```bash
  cd /workspace/project/UsrLinuxEmu/external/TaskRunner
  git worktree add .rddf/wt/phase3-step3-shim-and-forwarding -b phase3-step3-shim-and-forwarding
  cd .rddf/wt/phase3-step3-shim-and-forwarding
  # 后续所有 commit 在此 worktree 实施
  ```

## Workstream 1: GpuDriverClient 15 Forwarding Overrides ⏱ 1 day

> **Commit**: `feat(gpu-driver-client): 15 forwarding overrides for Phase 3.1/3.2 (commit C2)`
>
> **影响文件**: `include/test_fixture/gpu_driver_client.h`（inline 定义）
>
> **模式**：每个方法 inline `ioctl(fd_, GPU_IOCTL_*, &args)`（**不**抽 `submit_ioctl()` helper）

### 1.1 修改 header (gpu_driver_client.h)

- [x] **1.1.1** 在 `class GpuDriverClient : public IGpuDriver` 块内合适位置（如 "// H-3 Phase 2" 后）添加 15 个方法声明：

```cpp
// ============================================================
// Phase 3.1 Stream Capture / Graph (10) - IGpuDriver overrides
// ============================================================
// 实施依据：4-step coordination Step 3, UsrLinuxEmu PR #20 merged
// 每个方法 inline ioctl()（参考 wait_fence, free_bo 模式）

int stream_capture_status(uint32_t stream_id, uint32_t* status_out) override;
int stream_capture_begin(uint32_t stream_id, uint32_t mode) override;
int stream_capture_end(uint32_t stream_id, uint64_t* graph_handle_out) override;

int graph_create(uint64_t* graph_handle_out) override;
int graph_destroy(uint64_t graph_handle) override;
int graph_add_kernel_node(uint64_t graph_handle, uint32_t kernel_index,
                          uint32_t grid_x, uint32_t grid_y, uint32_t grid_z,
                          uint32_t block_x, uint32_t block_y, uint32_t block_z,
                          uint64_t kernargs_bo_handle) override;
int graph_add_memcpy_node(uint64_t graph_handle,
                          uint64_t src_va, uint64_t dst_va,
                          uint64_t size, uint32_t is_h2d) override;
int graph_instantiate(uint64_t graph_handle, uint64_t* exec_handle_out) override;
int64_t submit_graph(uint64_t graph_exec_handle, uint32_t stream_id) override;
int destroy_graph_exec(uint64_t graph_exec_handle) override;

// ============================================================
// Phase 3.2 Memory Pool (5) - IGpuDriver overrides
// ============================================================
// B-2: Pool VA 范围采用 Option B；D-MP-1 决策
// F-4: int64_t 返回 ≥ 1<<32 = sim fence

int mem_pool_create(uint64_t va_space_handle, uint64_t size,
                    uint32_t flags, uint64_t* pool_handle_out) override;
int mem_pool_destroy(uint64_t pool_handle) override;
int mem_pool_alloc(uint64_t pool_handle, uint64_t size, uint64_t* va_out) override;
int64_t mem_pool_alloc_async(uint64_t pool_handle, uint64_t size,
                             uint32_t stream_id, uint64_t* va_out) override;
int64_t mem_pool_free_async(uint64_t va, uint32_t stream_id) override;
```

### 1.2 修改 header (gpu_driver_client.h)

> Note: GpuDriverClient 现有的 31 个 override 全部 inline 定义在 `include/test_fixture/gpu_driver_client.h`（类定义体内）。新增 15 个方法沿用同一模式 `int method(...) override { ... }`，**不**创建或修改任何 .cpp。

- [x] **1.2.1** 在 gpu_driver_client.h 类体内（H-3 Phase 2 段后、private: 前）添加 stream_capture_status（Pattern B, header inline）：
  ```cpp
  int stream_capture_status(uint32_t stream_id, uint32_t* status_out) override {
    if (!is_open()) return -1;
    if (!status_out) return -EINVAL;
    gpu_stream_capture_status_args args = {};
    args.stream_id = stream_id;
    if (ioctl(fd_, GPU_IOCTL_STREAM_CAPTURE_STATUS, &args) < 0) {
      std::cerr << "GpuDriverClient: GPU_IOCTL_STREAM_CAPTURE_STATUS failed"
                << " (errno=" << errno << ")\n";
      return -1;
    }
    *status_out = args.status_out;
    return 0;
  }
  ```

- [x] **1.2.2** 在 header 中添加 `stream_capture_begin`（Pattern A, inline）
- [x] **1.2.3** 在 header 中添加 `stream_capture_end`（Pattern B, inline）
- [x] **1.2.4** 在 header 中添加 `graph_create`（Pattern B, inline）
- [x] **1.2.5** 在 header 中添加 `graph_destroy`（Pattern A, inline）
- [x] **1.2.6** 在 header 中添加 `graph_add_kernel_node`（Pattern A, inline）— F-3: kernargs_bo 透传
- [x] **1.2.7** 在 header 中添加 `graph_add_memcpy_node`（Pattern A, inline）— is_h2d 字段
- [x] **1.2.8** 在 header 中添加 `graph_instantiate`（Pattern B, inline）
- [x] **1.2.9** 在 header 中添加 `submit_graph`（Pattern C, inline）— **关键**：字段名是 `exec_handle`，返回 int64_t
  ```cpp
  int64_t submit_graph(uint64_t graph_exec_handle, uint32_t stream_id) override {
    if (!is_open()) return -1;
    if (graph_exec_handle == 0) return -1;
    gpu_graph_launch_args args = {};
    args.exec_handle = graph_exec_handle;  // 不是 graph_exec_handle
    args.stream_id = stream_id;
    if (ioctl(fd_, GPU_IOCTL_GRAPH_LAUNCH, &args) < 0) {
      std::cerr << "GpuDriverClient: GPU_IOCTL_GRAPH_LAUNCH failed"
                << " (errno=" << errno << ")\n";
      return -1;
    }
    return args.fence_id_out;  // F-4: ≥ 1<<32 = sim fence
  }
  ```

- [x] **1.2.10** 在 header 中添加 `destroy_graph_exec`（Pattern A, inline）
- [x] **1.2.11** 在 header 中添加 `mem_pool_create`（Pattern D, inline）— **关键**：嵌套 struct
  ```cpp
  int mem_pool_create(uint64_t va_space_handle, uint64_t size,
                      uint32_t flags, uint64_t* pool_handle_out) override {
    if (!is_open()) return -1;
    if (!pool_handle_out) return -EINVAL;
    if (va_space_handle == 0) {
      std::cerr << "[GpuDriverClient] mem_pool_create: rejected H-1 sentinel"
                << " (va_space_handle=0)\n";
      return -1;
    }
    gpu_mem_pool_create_args args = {};
    args.props.va_space_handle = va_space_handle;  // args.props.X 而非 args.X
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

- [x] **1.2.12** 在 header 中添加 `mem_pool_destroy`（Pattern A, inline）
- [x] **1.2.13** 在 header 中添加 `mem_pool_alloc`（Pattern B, inline）
- [x] **1.2.14** 在 header 中添加 `mem_pool_alloc_async`（Pattern E, inline）— 返回 int64_t
  ```cpp
  int64_t mem_pool_alloc_async(uint64_t pool_handle, uint64_t size,
                               uint32_t stream_id, uint64_t* va_out) override {
    if (!is_open()) return -1;
    if (!va_out) return -EINVAL;
    gpu_mem_pool_alloc_async_args args = {};
    args.pool_handle = pool_handle;
    args.size = size;
    args.stream_id = stream_id;
    if (ioctl(fd_, GPU_IOCTL_MEM_POOL_ALLOC_ASYNC, &args) < 0) {
      std::cerr << "GpuDriverClient: GPU_IOCTL_MEM_POOL_ALLOC_ASYNC failed"
                << " (errno=" << errno << ")\n";
      return -1;
    }
    *va_out = args.va_out;
    return args.fence_id_out;
  }
  ```

- [x] **1.2.15** 在 header 中添加 `mem_pool_free_async`（Pattern C, inline）— 返回 int64_t
  ```cpp
  int64_t mem_pool_free_async(uint64_t va, uint32_t stream_id) override {
    if (!is_open()) return -1;
    gpu_mem_pool_free_async_args args = {};
    args.va = va;
    args.stream_id = stream_id;
    if (ioctl(fd_, GPU_IOCTL_MEM_POOL_FREE_ASYNC, &args) < 0) {
      std::cerr << "GpuDriverClient: GPU_IOCTL_MEM_POOL_FREE_ASYNC failed"
                << " (errno=" << errno << ")\n";
      return -1;
    }
    return args.fence_id_out;
  }
  ```

### 1.3 验证

- [x] 编译验证：`cmake --build build --target taskrunner_test_fixture` 无警告
- [x] 全局构建：`cmake --build build -j4` 全部目标通过
- [x] 回归测试：跑 76+ 测试全过

### 1.4 提交

```bash
git add include/test_fixture/gpu_driver_client.h
git commit -m "feat(gpu-driver-client): 15 forwarding overrides for Phase 3.1/3.2

Implements the 15 new IGpuDriver methods added in Step 1 (commit 21f71c9) by
forwarding each to the corresponding GPU_IOCTL_* added in Step 2 (PR #20):
- 10 graph/capture (0x50-0x59): stream_capture_* + graph_*
- 5 mempool (0x60-0x64): mem_pool_*

Each method inlines ioctl(fd_, GPU_IOCTL_*, &args) (matches existing
free_bo/wait_fence pattern, NOT extracting submit_ioctl helper).

3 methods (submit_graph, mem_pool_alloc_async, mem_pool_free_async) return
int64_t per F-4 convention (≥ 1<<32 = valid sim fence).

B-2 enforced: mem_pool_create requires va_space_handle != 0 (H-1 sentinel).
gpu_mem_pool_create_args uses nested .props struct (B-2 Option B).
gpu_graph_launch_args uses exec_handle field (NOT graph_exec_handle).

Refs: openspec/changes/2026-07-06-phase3-step3-shim-and-forwarding/ (Step 3)
Refs: https://github.com/chisuhua/UsrLinuxEmu/pull/20 (Step 2, MERGED)
Refs: TaskRunner commit 21f71c9 (Step 1, MERGED)"
```

## Workstream 2: cuStreamCapture + cuGraph Shim ⏱ 1.5 days

> **Commit**: `feat(shim): Phase 3.1 cuStreamCapture + cuGraph shim (commit C3 + C4)`
>
> **影响文件**: 4 个新建 .cpp + 修改 cu_stream.cpp + 修改 cu_mem.cpp
>
> **模式**：自包含 atomic + map + mutex（**不**调 GpuDriverClient）

### 2.1 cu_stream_capture.cpp (3 funcs, NEW)

- [x] **2.1.1** 新建 `src/umd/libcuda_shim/cu_stream_capture.cpp`（按 design.md §3.1 代码骨架）
  - 包含 `g_captures` 全局 state table
  - 3 个函数：cuStreamBeginCapture / cuStreamEndCapture / cuStreamIsCapturing
  - F-1: mode != GLOBAL → CUDA_ERROR_NOT_SUPPORTED

### 2.2 修改 cu_stream.cpp

- [x] **2.2.1** 删除原 `cuStreamBeginCapture` stub（行 95-100）
- [x] **2.2.2** 删除原 `cuStreamEndCapture` stub（行 102-106）
- [x] **2.2.3** 修改 `cuStreamGetCaptureInfo`（行 140-148）调 `cuStreamIsCapturing`：
  ```cpp
  extern "C" CUresult cuStreamGetCaptureInfo(CUstream hStream,
                                              CUstreamCaptureStatus* captureStatus,
                                              cuuint64_t* id) {
    if (!captureStatus) return CUDA_ERROR_INVALID_VALUE;
    CUresult ret = cuStreamIsCapturing(hStream, captureStatus);
    if (ret == CUDA_SUCCESS && id != nullptr) {
      *id = 0;
    }
    return ret;
  }
  ```

### 2.3 cu_graph.cpp (7 funcs, NEW, ~280 lines)

- [x] **2.3.1** 新建 `src/umd/libcuda_shim/cu_graph.cpp`（按 design.md §3.2 代码骨架）
- [x] 重点函数：`cuGraphCreate` / `cuGraphDestroy` / `cuGraphAddKernelNode` (F-3) / `cuGraphAddMemcpyNode` / `cuGraphInstantiate` / `cuGraphLaunch` (PoC no-op) / `cuGraphExecDestroy`

### 2.4 cu_graph_node.cpp (2 funcs, NEW)

- [x] **2.4.1** 新建 `cu_graph_node.cpp`：`cuGraphNodeGetType` + `cuGraphNodeSetAttribute`

### 2.5 cu_graph_exec.cpp (2 funcs, NEW)

- [x] **2.5.1** 新建 `cu_graph_exec.cpp`：`cuGraphExecKernelNodeSetParams` + `cuGraphExecMemcpyNodeSetParams`

### 2.6 修改 cu_mem.cpp

- [x] **2.6.1** 删除原 `cuGraphCreate` stub（行 258-261，4 行）

### 2.7 注册到 CMake

- [x] 编辑 `CMakeLists.txt` 注册 4 个新 .cpp

### 2.8 验证

- [x] 构建：`cmake --build build --target libcuda_taskrunner` 无警告
- [x] 符号导出：`nm build/libcuda_taskrunner.so | grep -E "cuStreamBegin|cuGraphCreate|cuGraphLaunch"`
- [x] 回归测试：76+ 测试全过

### 2.9 提交

```bash
git add src/umd/libcuda_shim/cu_stream_capture.cpp \
        src/umd/libcuda_shim/cu_graph.cpp \
        src/umd/libcuda_shim/cu_graph_node.cpp \
        src/umd/libcuda_shim/cu_graph_exec.cpp \
        src/umd/libcuda_shim/cu_stream.cpp \
        src/umd/libcuda_shim/cu_mem.cpp \
        CMakeLists.txt
git commit -m "feat(shim): Phase 3.1 cuStreamCapture + cuGraph shim (11 REAL_IMPL)

Adds 4 new shim files (cu_stream_capture.cpp + cu_graph.cpp +
cu_graph_node.cpp + cu_graph_exec.cpp) with 11 cu* API REAL_IMPL.

Pattern: self-contained atomic + map + mutex state tables per file
(matches cu_stream.cpp / cu_event.cpp / cu_mem.cpp existing pattern).
Shim does NOT call GpuDriverClient (D-S3-1 decision).

Removed 3 stubs from existing files:
- cu_stream.cpp:95-100 (cuStreamBeginCapture)
- cu_stream.cpp:102-106 (cuStreamEndCapture)
- cu_mem.cpp:258-261 (cuGraphCreate)

F-1 enforced: cuStreamBeginCapture returns CUDA_ERROR_NOT_SUPPORTED for
non-GLOBAL capture mode.
F-3 enforced: cuGraphAddKernelNode passes kernargs_bo_handle=0 transparently.

Refs: openspec/changes/2026-07-06-phase3-step3-shim-and-forwarding/ (Step 3)"
```

## Workstream 3: cuMemPool Shim ⏱ 1 day

> **Commit**: `feat(shim): Phase 3.2 cuMemPool shim (commit C5)`
>
> **影响文件**: 1 个新建 .cpp
>
> **模式**：自包含 atomic + map + mutex

### 3.1 cu_mem_pool.cpp (8 funcs, NEW, ~200 lines)

- [x] **3.1.1** 新建 `src/umd/libcuda_shim/cu_mem_pool.cpp`（按 design.md §3.3 代码骨架）
- 重点：`cuMemPoolCreate` (B-2) / `cuMemPoolAlloc` / `cuMemPoolSetAttribute` (F-2) / `cuMemPoolAllocAsync` (复用 sync) / `cuMemPoolFreeAsync`

### 3.2 注册到 CMake + 验证

- [x] 注册 + 构建 + 符号导出 + 回归测试全过

### 3.3 提交

```bash
git add src/umd/libcuda_shim/cu_mem_pool.cpp CMakeLists.txt
git commit -m "feat(shim): Phase 3.2 cuMemPool shim (8 REAL_IMPL)

Adds cu_mem_pool.cpp with 8 cu* API REAL_IMPL using self-contained
MemPoolTable (atomic + map + mutex, D-S3-1 pattern).

B-2 enforced: cuMemPoolCreate rejects vaSpaceHandle=0 (H-1 sentinel).
F-2 enforced: cuMemPoolSetAttribute/GetAttribute accept only
RELEASE_THRESHOLD + REUSE_FOLLOW_EVENT_DEPENDENCIES enum values.

PoC limitations (deferred to Phase 4):
- No real VA sub-range reservation
- Async ops reuse sync impl (no fence return)

Refs: openspec/changes/2026-07-06-phase3-step3-shim-and-forwarding/ (Step 3)"
```

## Workstream 4: E2E Test Coverage ⏱ 1.5 days

> **Commit**: `test(shim): Phase 3.1+3.2 E2E coverage (commit C6)`
>
> **影响文件**: 3 个新建 test_cu_*.cpp 文件

### 4.1 test_cu_stream_capture.cpp (≥30 cases)

- [x] State machine 基础测试 (3 cases)
- [x] GLOBAL mode 测试 (5 cases)
- [x] 错误路径测试 (5 cases)
- [x] 集成测试 (10 cases)
- [x] Stage 1.4 回归 (2 cases)
- [x] Fence_id 测试 (5 cases)

### 4.2 test_cu_graph.cpp (≥20 cases)

- [x] 基础 lifecycle (5 cases)
- [x] AddKernelNode (5 cases)
- [x] AddMemcpyNode (3 cases)
- [x] Launch + 错误路径 (7 cases)

### 4.3 test_cu_mem_pool.cpp (≥25 cases)

- [x] 基础 lifecycle (4 cases)
- [x] 同步 alloc (3 cases)
- [x] 异步 alloc (5 cases)
- [x] Pool Option B 边界 (4 cases)
- [x] 属性 (3 cases)
- [x] Trim (3 cases)
- [x] Stage 1.4 回归 (3 cases)

### 4.4 注册到 CMake + 验证

- [x] 注册 3 个新 test binary
- [x] 75+ 新测试全过 + 76+ 回归全过 = 151+ total
- [x] `tools/docs-audit.sh --strict` exit 0

### 4.5 提交

```bash
git add tests/umd/test_cu_stream_capture.cpp \
        tests/umd/test_cu_graph.cpp \
        tests/umd/test_cu_mem_pool.cpp \
        tests/umd/CMakeLists.txt
git commit -m "test(shim): Phase 3.1+3.2 E2E coverage (≥75 cases)

Adds 3 test binaries (≥75 cases) covering:
- cuStreamCapture state machine + GLOBAL mode + fence_id lifecycle
- cuGraph create/destroy/add_*/instantiate/launch/exec_destroy
- cuMemPool Option B VA sub-range + sync/async alloc + attributes

Total test count: 76 → 151+ (75 new + 0 regression).

Refs: openspec/changes/2026-07-06-phase3-step3-shim-and-forwarding/ (Step 3)"
```

## Workstream 5: Documentation + Cross-Repo Sync ⏱ 0.5 day

> **Commit**: `docs(sync): Phase 3 Step 3 done + Step 4 trigger (commit C7)`
>
> **影响文件**: sync-plan.md + openspec archive

### 5.1 更新 sync-plan.md (v2.3)

- [x] 在 `plans/sync-plan.md` 添加 Step 3 完成条目 + 触发 Step 4

### 5.2 openspec archive

- [x] `mv openspec/changes/2026-07-05-phase3-1-igpu-driver-extension openspec/changes/archive/`
- [x] 编辑其 `.openspec.yaml`: status: PROPOSED → APPLIED, 加 archived: 2026-07-06

### 5.3 提交 + 推送 + 通知

```bash
git add plans/sync-plan.md openspec/changes/archive/2026-07-05-phase3-1-igpu-driver-extension/
git commit -m "docs(sync): Phase 3 Step 3 done + Step 4 trigger"
git push origin phase3-step3-shim-and-forwarding
```

## 验收准则（Definition of Done）

- [x] C2 commit: 15 GpuDriverClient override 方法 + inline ioctl + struct 字段正确
- [x] C3+C4 commit: 11 shim funcs + 删除 3 旧 stub
- [x] C5 commit: 8 shim funcs (cu_mem_pool)
- [x] C6 commit: 75+ E2E test cases
- [x] C7 commit: sync-plan.md v2.3 + openspec archive
- [x] 76+ 回归测试零 regression
- [x] 151+ total tests passing
- [x] `nm libcuda_taskrunner.so` 含 11+ cu* 新符号
- [x] `cmake --build build` 无 warning
- [x] `tools/docs-audit.sh --strict` exit 0
- [x] 7 atomic commits 顺序执行
- [x] PR review 通过 + merge 到 TaskRunner `main`
- [x] UsrLinuxEmu owner 收到 Step 3 完成通知
- [x] 触发 Step 4: UsrLinuxEmu submodule bump

## 时间线总览

```
Day 1 (0.5h):  0.* 前置验证 + 创建 worktree
Day 1 (8h):    C2 GpuDriverClient 15 override
Day 2-3:       C3+C4 cuStreamCapture + cuGraph shim (11 funcs)
Day 4 (1d):    C5 cuMemPool shim (8 funcs)
Day 5-6 (1.5d): C6 E2E tests (75+ cases)
Day 7 (0.5d):  C7 docs + archive + push + notify
```

**总工作量**: 7 working days ≈ 1.5-2 周（含 review）