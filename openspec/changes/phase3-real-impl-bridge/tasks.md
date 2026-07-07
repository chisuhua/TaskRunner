# Tasks: phase3-real-impl-bridge

> **Status**: 🔄 PROPOSED → REVISED 2026-07-07（Metis 二次审查后扩展 scope，方案 B：跨仓添加 `GPU_IOCTL_MEM_POOL_EXPORT`）
> **Source**: PR chisuhua/TaskRunner#7 merge @ 02363b8 + submodule bump @ 458299e
> **Scope (REVISED)**: 6 workstreams
>   - (0) **UsrLinuxEmu 端新增 `GPU_IOCTL_MEM_POOL_EXPORT` IOCTL**（critical path）
>   - (1) TaskRunner 端 Foundation (g_gpu_client 类型 + cuda.h 前置)
>   - (2) cu_graph real bridge
>   - (3) cu_mem_pool real bridge（含 export 桥接）
>   - (4) Tests（含 mock_gpu_driver.hpp 扩展 + 现有 test 适配）
>   - (5) docs sync（含 tadr-302 + 跨仓 mirror）
> **Cross-repo**: **双向改动**（Metis MISSED-1 修复）—— UsrLinuxEmu 先 PR → TaskRunner 再 PR → UsrLinuxEmu 父仓 bump submodule pointer
> **前置**: Phase 3 Step 1 (e6a34eb) + Step 2 (138f15a) + Step 3 (02363b8) + Step 4 (458299e) 全部 ✅ DONE

## 0. 前置条件（验证基线）

- [ ] **0.1** 确认 PR #7 已 merge 到 TaskRunner main (`git merge-base --is-ancestor 02363b8 HEAD`)
- [ ] **0.2** 确认 GpuDriverClient 已添加 15 个 Phase 3 forwarding override（`grep -c "GPU_IOCTL_GRAPH_LAUNCH\|GPU_IOCTL_MEM_POOL_ALLOC\|GPU_IOCTL_MEM_POOL_ALLOC_ASYNC\|GPU_IOCTL_MEM_POOL_FREE_ASYNC" include/test_fixture/gpu_driver_client.h` = 4）— **REVISED**：去掉 MEM_POOL_EXPORT（尚未实现）
- [ ] **0.3** 确认 g_gpu_client 全局指针已声明（`grep -r "extern.*g_gpu_client" include/`）— **REVISED**：声明类型需改为 `IGpuDriver*`（M1）
- [ ] **0.4** 确认测试基线 257 cases pass（实测：test_cuda_scheduler 12 + test_gpu_architecture 15 + test_gpu_phase2 16 + test_cuda_runtime_api 12 + test_cuda_shim 107 + test_cu_stream_capture 34 + test_cu_graph 29 + test_cu_mem_pool 32）— **REVISED**：proposal 数字与实测不符
- [ ] **0.5** 确认 UsrLinuxEmu 符号链接 + IOCTL ABI 可达（`cat UsrLinuxEmu/plugins/gpu_driver/shared/gpu_ioctl.h | grep -E "GPU_IOCTL_GRAPH_LAUNCH|GPU_IOCTL_MEM_POOL_ALLOC|GPU_IOCTL_MEM_POOL_EXPORT"`）— **REVISED**：grep 应仅命中前两个，EXPORT 不存在（MISSED-1）
- [ ] **0.6** 创建 TaskRunner worktree（基于 main @ d66fe94）：
  ```bash
  cd /workspace/project/UsrLinuxEmu/external/TaskRunner
  git worktree add .rddf/wt/phase3-real-impl-bridge-extended -b phase3-real-impl-bridge-extended
  cd .rddf/wt/phase3-real-impl-bridge-extended
  ```
  **路径约定**：TaskRunner 仓用 `.rddf/wt/`（AGENTS.md 2026-07-06 project-local 约定）
- [ ] **0.7** 确认 MockGpuDriver 注入模式已在 test_gpu_architecture/test_gpu_phase2 中使用（参考 `MockGpuDriver::submit_graph` mock 实现）
- [ ] **0.8 (REVISED)** 验证 vendored `include/cuda.h` 前置常量：
  - `CUDA_ERROR_NOT_INITIALIZED` 必须**不存在**（待 S5 新增）
  - `cuMemPoolAllocAsync` / `cuMemPoolFreeAsync` 原型必须**不存在**（待 S6 新增）
  - `CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR` 应已存在（grep 验证）
- [ ] **0.9 (REVISED)** 创建 UsrLinuxEmu worktree（关键路径）：
  ```bash
  cd /workspace/project/UsrLinuxEmu
  git worktree add .worktrees/phase4-mempool-export-ioctl -b phase4-mempool-export-ioctl main
  cd .worktrees/phase4-mempool-export-ioctl
  ```
  **路径约定差异**：UsrLinuxEmu 仓用 `.worktrees/`（其 `.gitignore` 已 ignore；TaskRunner 用 `.rddf/wt/`）
  — **此 worktree 必须先完成 Phase 2.x（UsrLinuxEmu IOCTL）才能进入 Phase 3.x**

---

## 0.B UsrLinuxEmu 端：新增 `GPU_IOCTL_MEM_POOL_EXPORT` IOCTL（critical path）

> **重要**：本节所有任务必须在 Phase 3（TaskRunner 端）开工前完成。否则 TaskRunner 仓会因 `IGpuDriver::mem_pool_export_shareable` 与 `gpu_ioctl.h` IOCTL 不存在而无法编译。
> **Worktree**: `/workspace/project/UsrLinuxEmu/.worktrees/phase4-mempool-export-ioctl/`（UsrLinuxEmu 仓的 `.gitignore` 只 ignore `.worktrees/`，不用 `.rddf/`）

### 0.B.1 ABI 头文件 + struct 定义

- [ ] **0.B.1.1** 在 `plugins/gpu_driver/shared/gpu_ioctl.h` 添加 `GPU_IOCTL_MEM_POOL_EXPORT`（0x68）常量
- [ ] **0.B.1.2** 在 `gpu_ioctl.h` 添加 `struct gpu_mem_pool_export_args`：
  ```c
  struct gpu_mem_pool_export_args {
      __u64 pool_handle;       /* input: from cuMemPoolCreate */
      __u32 handle_type;       /* input: CU_MEM_HANDLE_TYPE_* */
      __u32 flags;             /* input: reserved */
      __s32 fd_out;            /* output: POSIX FD (>= 0) or -1 */
      __u32 _pad;              /* alignment */
  };
  ```
- [ ] **0.B.1.3** 在 `gpu_ioctl.h` 添加 doc comment 引用 NVIDIA `cuMemPoolExportToShareableHandle` 行为契约

### 0.B.2 Real implementation handler

- [ ] **0.B.2.1** 在 `plugins/gpu_driver/gpu_driver_plugin.cpp` 添加 `case GPU_IOCTL_MEM_POOL_EXPORT:` 分支
- [ ] **0.B.2.2** 实现 FD 导出逻辑（`pipe2(2)` + `O_CLOEXEC`）：
  - 创建匿名 pipe，返回 read end 作为 shareable FD
  - 若 pool_handle 无效或 handle_type != POSIX_FD，返回 `-ENOSYS`
  - **注意**：当前 UsrLinuxEmu sim 是进程内状态，导出 FD 仅作为 IPC token；后续 Phase 5+ 实现跨进程 import
- [ ] **0.B.2.3** 注册到 `ioctl_dispatch_table[]`（在 plugin 初始化代码附近）

### 0.B.3 CudaStub fallback

- [ ] **0.B.3.1** 在 `plugins/gpu_driver/cuda_stub.cpp`（或 equivalent）添加 `mem_pool_export_shareable` stub override 返回 `-1`（NOT_SUPPORTED）
- [ ] **0.B.3.2** 在 stub 模式 CLI 中验证：调用 `cuMemPoolExportToShareableHandle` 返回 `CUDA_ERROR_NOT_SUPPORTED`

### 0.B.4 Tests

- [ ] **0.B.4.1** 新增 `plugins/gpu_driver/tests/test_gpu_mempool_export.cpp`（doctest）：
  - `gpu_ioctl: MEM_POOL_EXPORT with valid pool returns positive FD`
  - `gpu_ioctl: MEM_POOL_EXPORT with invalid pool_handle returns -ENOSYS`
  - `gpu_ioctl: MEM_POOL_EXPORT with non-POSIX handle_type returns -ENOSYS`
  - `gpu_ioctl: MEM_POOL_EXPORT FD is O_CLOEXEC and closeable`
- [ ] **0.B.4.2** 在 `plugins/gpu_driver/CMakeLists.txt` 注册 `test_gpu_mempool_export` 目标
- [ ] **0.B.4.3** 验证：`cd UsrLinuxEmu/build && make test_gpu_mempool_export && ./bin/test_gpu_mempool_export` 全过

### 0.B.5 ADR

- [ ] **0.B.5.1** 创建 `docs/00_adr/adr-XXX-mem-pool-export-ioctl.md`：
  - **Context**：Phase 4 需要 `cuMemPoolExportToShareableHandle` 真实化；当前 IOCTL 范围 0x60-0x67 无 export
  - **Decision**：新增 IOCTL 0x68 = `GPU_IOCTL_MEM_POOL_EXPORT`，采用 `pipe2(2)` 匿名 inode 方案
  - **Consequences**：扩展 ABI 范围 0x60-0x68；Phase 5+ 需要补充 `GPU_IOCTL_MEM_POOL_IMPORT` 反向 IOCTL
- [ ] **0.B.5.2** 在 `docs/00_adr/README.md` 添加新 ADR 索引行

### 0.B.6 UsrLinuxEmu 端提交

- [ ] **0.B.6.1** 在 UsrLinuxEmu worktree commit + push：
  ```bash
  cd /workspace/project/UsrLinuxEmu/.rddf/wt/phase4-mempool-export-ioctl
  git add plugins/gpu_driver/shared/gpu_ioctl.h \
          plugins/gpu_driver/gpu_driver_plugin.cpp \
          plugins/gpu_driver/cuda_stub.cpp \
          plugins/gpu_driver/tests/test_gpu_mempool_export.cpp \
          plugins/gpu_driver/CMakeLists.txt \
          docs/00_adr/adr-XXX-mem-pool-export-ioctl.md \
          docs/00_adr/README.md
  git commit -m "feat(gpu-ioctl): add MEM_POOL_EXPORT (0x68) for cuMemPoolExportToShareableHandle

  New IOCTL handler that exports a memory pool as a POSIX file descriptor
  via pipe2(O_CLOEXEC). Used by Phase 4 shim cuMemPoolExportToShareableHandle
  real bridge.

  Refs: TaskRunner openspec/changes/phase3-real-impl-bridge/
  Refs: ADR-XXX"
  git push origin phase4-mempool-export-ioctl
  ```
- [ ] **0.B.6.2** 在 UsrLinuxEmu 仓开 PR（**必须先合并**才能进入 Phase 3.x）

---

## 1. Foundation: g_gpu_client + LaunchTrace（REVISED 含 Metis MUST FIX）

> **Commit**: `feat(shim-foundation): launch_trace + g_gpu_client helper for hybrid mode`
> **影响文件**:
> - `include/test_fixture/gpu_driver_client.h` (M1: `g_gpu_client` 类型改 `IGpuDriver*`)
> - `src/test_fixture/gpu_driver_client.cpp` (M1: 定义同步)
> - `src/umd/libcuda_shim/cu_graph.cpp`（LaunchTrace struct + helpers + M2 include）
> - `src/umd/libcuda_shim/cu_mem_pool.cpp`（M2 include + helper）
> - `include/cuda.h`（S5 + S6 常量/原型）

### 1.A M1: g_gpu_client 类型修复（Metis MUST FIX）

- [ ] **1.A.1** 在 `include/test_fixture/gpu_driver_client.h:819` 改声明：
  ```cpp
  // 原: extern GpuDriverClient* g_gpu_client;
  extern IGpuDriver* g_gpu_client;  // (M1: 改为 IGpuDriver* 以支持 mock 注入)
  ```
- [ ] **1.A.2** 在 `src/test_fixture/gpu_driver_client.cpp:11` 改定义：
  ```cpp
  // 原: GpuDriverClient* g_gpu_client = nullptr;
  IGpuDriver* g_gpu_client = nullptr;  // (M1 同步)
  ```
- [ ] **1.A.3** 验证 `init_gpu_client()` 中 `new GpuDriverClient()` 隐式转换为 `IGpuDriver*` 仍编译（IS-A 关系）
- [ ] **1.A.4** 验证 `src/test_fixture/cmd_cuda.cpp` 不需修改（其调用方法 `is_open/alloc_bo_vram/submit_memcpy/wait_fence/create_va_space/...` 均为 IGpuDriver 虚函数 — Metis MISSED-3 已确认）

### 1.B S5 + S6: vendored cuda.h 前置常量（Metis PROMOTE-TO-MUST）

- [ ] **1.B.1** 在 `include/cuda.h` 添加 `CUDA_ERROR_NOT_INITIALIZED = 700`（在 `CUDA_ERROR_NOT_SUPPORTED` 附近）
- [ ] **1.B.2** 在 `include/cuda.h` 添加原型：
  ```c
  CUresult cuMemPoolAllocAsync(CUmemPoolPtr* ptr, size_t size, CUmemPool pool,
                                CUstream hStream, CUmemPoolProps* props);
  CUresult cuMemPoolFreeAsync(CUmemPoolPtr ptr, CUstream hStream, CUmemPool pool);
  ```

### 1.C M2: shim 文件 include 修复（Metis MUST FIX）

- [ ] **1.C.1** 在 `src/umd/libcuda_shim/cu_graph.cpp:21` 后加：
  ```cpp
  #include "test_fixture/gpu_driver_client.h"  // (M2: 暴露 g_gpu_client + IGpuDriver)
  ```
- [ ] **1.C.2** 在 `src/umd/libcuda_shim/cu_mem_pool.cpp:17` 后加相同 include

### 1.D LaunchTrace 基础设施

- [ ] **1.D.1** 在 `cu_graph.cpp` 顶部新增 `LaunchTrace` 状态表（atomic + map + mutex）：
  ```cpp
  struct LaunchTrace {
    std::unordered_map<CUgraphExec, std::int64_t> fence_ids;
    std::mutex mu;
  };
  LaunchTrace g_launches;
  ```
- [ ] **1.D.2** 在 `cu_graph.cpp` 新增 `get_driver_or_log()` helper：
  ```cpp
  static IGpuDriver* get_driver_or_log(const char* api_name) {
    if (!g_gpu_client) {
      std::cerr << "[cu_graph] " << api_name << ": g_gpu_client not initialized\n";
      return nullptr;
    }
    return g_gpu_client;
  }
  ```
- [ ] **1.D.3** 在 `cu_mem_pool.cpp` 添加相同 helper（建议提取到共享头 `shim_bridge.h` 避免 DRY 违反）
- [ ] **1.D.4** 验证 0 warning build：`cmake --build build --target cuda_taskrunner 2>&1 | grep -iE "warning|error"` 应为空

### 1.E S4: LaunchTrace cleanup in cuGraphExecDestroy（Metis PROMOTE-TO-MUST）

- [ ] **1.E.1** 修改 `cu_graph.cpp:128` 的 `cuGraphExecDestroy`：
  ```cpp
  extern "C" CUresult cuGraphExecDestroy(CUgraphExec hGraphExec) {
    if (!hGraphExec) return CUDA_ERROR_INVALID_VALUE;
    {
      std::lock_guard<std::mutex> lock(g_launches.mu);
      g_launches.fence_ids.erase(hGraphExec);
    }
    return CUDA_SUCCESS;
  }
  ```

## 2. cu_graph.cpp: cuGraphLaunch REAL Bridge

> **Commit**: `feat(shim): cuGraphLaunch bridges to GpuDriverClient::submit_graph (Phase 4)`
> **影响文件**: `src/umd/libcuda_shim/cu_graph.cpp`
> **Pattern**: 复用 `get_driver_or_log()`，单次 `submit_graph` 调用 + fence_id 记录到 LaunchTrace

- [ ] **2.1** 修改 `cuGraphLaunch` 函数（原 line 122，PoC no-op）：
  ```cpp
  extern "C" CUresult cuGraphLaunch(CUgraphExec hGraphExec, CUstream hStream) {
    if (!hGraphExec) return CUDA_ERROR_INVALID_VALUE;
    auto* driver = get_driver_or_log("cuGraphLaunch");
    if (!driver) return CUDA_ERROR_NOT_INITIALIZED;
    std::int64_t fence = driver->submit_graph(hGraphExec, reinterpret_cast<uintptr_t>(hStream));
    if (fence < 0) return CUDA_ERROR_UNKNOWN;
    std::lock_guard<std::mutex> lock(g_launches.mu);
    g_launches.fence_ids[hGraphExec] = fence;
    return CUDA_SUCCESS;
  }
  ```
- [ ] **2.2** 添加 `CUDA_ERROR_NOT_INITIALIZED = 700` 到 vendored `include/cuda.h`（如未定义）
- [ ] **2.3** 验证：`nm build/libcuda_taskrunner.so | grep "T cuGraphLaunch"` 应仍导出（strong symbol）
- [ ] **2.4** 验证 0 warning build

## 3. cu_mem_pool.cpp: Alloc/AllocAsync/FreeAsync/Export REAL Bridges

> **Commit**: `feat(shim): cuMemPool Alloc/Async/Export bridges to GpuDriverClient (Phase 4)`
> **影响文件**: `src/umd/libcuda_shim/cu_mem_pool.cpp`, `src/umd/libcuda_shim/cu_stub_table.inc`（注册新 async 函数）
> **Pattern**: 与 cu_graph 同步 5 个函数 + 新增 2 个独立 async 函数

### 3.1 修改现有 3 个 API

- [ ] **3.1.1** 修改 `cuMemPoolAlloc`（原 line 59，synthetic counter）：
  ```cpp
  extern "C" CUresult cuMemPoolAlloc(CUmemPoolPtr* ptr, size_t size,
                                      CUmemPool pool, CUmemPoolProps* props) {
    if (!ptr || !pool) return CUDA_ERROR_INVALID_VALUE;
    if (size == 0) return CUDA_ERROR_INVALID_VALUE;
    (void)props;
    auto* driver = get_driver_or_log("cuMemPoolAlloc");  // 共用 helper
    if (!driver) return CUDA_ERROR_NOT_INITIALIZED;
    uint64_t va = 0;
    if (driver->mem_pool_alloc(pool, size, &va) < 0) return CUDA_ERROR_UNKNOWN;
    *ptr = reinterpret_cast<CUmemPoolPtr>(static_cast<uintptr_t>(va));
    return CUDA_SUCCESS;
  }
  ```
- [ ] **3.1.2** 修改 `cuMemPoolExportToShareableHandle`（原 line 105，no-op）：
  ```cpp
  extern "C" CUresult cuMemPoolExportToShareableHandle(void* shareableHandle,
                                                        CUmemPool pool,
                                                        CUmemPoolHandleType handleType,
                                                        unsigned int flags) {
    if (!shareableHandle || !pool) return CUDA_ERROR_INVALID_VALUE;
    if (handleType != CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR) {
      return CUDA_ERROR_NOT_SUPPORTED;
    }
    auto* driver = get_driver_or_log("cuMemPoolExportToShareableHandle");
    if (!driver) return CUDA_ERROR_NOT_INITIALIZED;
    uint64_t fd = 0;
    if (driver->mem_pool_export_shareable(pool, handleType, flags, &fd) < 0) {
      return CUDA_ERROR_UNKNOWN;
    }
    *reinterpret_cast<int*>(shareableHandle) = static_cast<int>(fd);
    return CUDA_SUCCESS;
  }
  ```

### 3.2 新增 cuMemPoolAllocAsync / cuMemPoolFreeAsync

- [ ] **3.2.1** 在 `cu_mem_pool.cpp` 新增 `cuMemPoolAllocAsync`：
  ```cpp
  extern "C" CUresult cuMemPoolAllocAsync(CUmemPoolPtr* ptr, size_t size,
                                           CUmemPool pool, CUstream hStream,
                                           CUmemPoolProps* props) {
    if (!ptr || !pool) return CUDA_ERROR_INVALID_VALUE;
    if (size == 0) return CUDA_ERROR_INVALID_VALUE;
    (void)hStream; (void)props;
    auto* driver = get_driver_or_log("cuMemPoolAllocAsync");
    if (!driver) return CUDA_ERROR_NOT_INITIALIZED;
    uint64_t va = 0;
    uint32_t stream_id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hStream));
    int64_t fence = driver->mem_pool_alloc_async(pool, size, stream_id, &va);
    if (fence < 0) return CUDA_ERROR_UNKNOWN;
    *ptr = reinterpret_cast<CUmemPoolPtr>(static_cast<uintptr_t>(va));
    return CUDA_SUCCESS;
  }
  ```
- [ ] **3.2.2** 在 `cu_mem_pool.cpp` 新增 `cuMemPoolFreeAsync`：
  ```cpp
  extern "C" CUresult cuMemPoolFreeAsync(CUmemPoolPtr ptr, CUstream hStream, CUmemPool pool) {
    if (!ptr || !pool) return CUDA_ERROR_INVALID_VALUE;
    auto* driver = get_driver_or_log("cuMemPoolFreeAsync");
    if (!driver) return CUDA_ERROR_NOT_INITIALIZED;
    uint64_t va = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ptr));
    uint32_t stream_id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hStream));
    int64_t fence = driver->mem_pool_free_async(va, stream_id);
    if (fence < 0) return CUDA_ERROR_UNKNOWN;
    return CUDA_SUCCESS;
  }
  ```

### 3.3 共享 helper（cu_mem_pool 端）

- [ ] **3.3.1** 在 `cu_mem_pool.cpp` 添加与 cu_graph 相同的 `get_driver_or_log()` helper（避免循环依赖问题，考虑直接 include `<test_fixture/gpu_driver_client.h>` 或新建 `shim_bridge.h`）
- [ ] **3.3.2** 添加 `CUDA_ERROR_NOT_INITIALIZED` 使用（如 2.2 已添加则跳过）

### 3.4 vendored cuda.h 函数声明

- [ ] **3.4.1** 在 `include/cuda.h` 添加新函数原型：
  ```c
  CUresult cuMemPoolAllocAsync(CUmemPoolPtr* ptr, size_t size, CUmemPool pool,
                                CUstream hStream, CUmemPoolProps* props);
  CUresult cuMemPoolFreeAsync(CUmemPoolPtr ptr, CUstream hStream, CUmemPool pool);
  ```
- [ ] **3.4.2** 在 `src/umd/libcuda_shim/cu_stub_table.inc` 注册新 stub：
  ```cpp
  // REAL_IMPL in cu_mem_pool.cpp
  __attribute__((weak, visibility("default")))
  CUresult cuMemPoolAllocAsync(void);

  // REAL_IMPL in cu_mem_pool.cpp
  __attribute__((weak, visibility("default")))
  CUresult cuMemPoolFreeAsync(void);
  ```
- [ ] **3.4.3** 在 `tools/generate_cu_stubs.py` CRITICAL_APIS_IMPL_REQUIRED 字典添加：
  ```python
  "cuMemPoolAllocAsync": "cu_mem_pool.cpp",
  "cuMemPoolFreeAsync": "cu_mem_pool.cpp",
  ```

### 3.5 验证

- [ ] **3.5.1** 编译：`cmake --build build --target cuda_taskrunner` 无警告
- [ ] **3.5.2** 符号导出：`nm build/libcuda_taskrunner.so | grep -E "T cuMemPool(Alloc|AllocAsync|FreeAsync|ExportToShareableHandle)"` 应输出 4 个符号
- [ ] **3.5.3** 回归测试：原 225 cases 仍全过（更新 PoC 行为预期）

## 4. 测试扩展（C11 followup + Metis M3/S2 修复）

> **Commit**: `test(shim): Phase 4 real-bridge test coverage (mock-injection path)`
> **影响文件**:
> - `tests/test_fixture/mock_gpu_driver.hpp`（MISSED-2：扩展 mock 覆盖 Phase 3+4 方法）
> - `tests/umd/test_cu_graph.cpp`（+5 cases）
> - `tests/umd/test_cu_mem_pool.cpp`（M3/S2 修复 + +8 cases）

### 4.0 M3/S2: 现有测试修复（Metis MUST FIX / PROMOTE）

- [ ] **4.0.1** 在 `tests/test_fixture/mock_gpu_driver.hpp` 添加 4 个新 override：
  ```cpp
  // 现有 mock 仅 H-2.5 范围；Phase 3+4 需扩展
  int64_t submit_graph(uint64_t exec, uint32_t stream) override {
    submit_graph_calls++;
    submit_graph_arg_exec = exec;
    submit_graph_arg_stream = stream;
    return submit_graph_return;
  }
  int mem_pool_alloc(uint64_t pool, uint64_t size, uint64_t* va_out) override {
    mem_pool_alloc_calls++;
    // 关键：S2 唯一性策略 — 用 pool+seq 保证 unique
    *va_out = pool * 0x100000ULL + mem_pool_alloc_calls * 0x1000ULL;
    return 0;
  }
  // ... mem_pool_alloc_async / mem_pool_free_async / mem_pool_export_shareable 同理
  ```
- [ ] **4.0.2** 在 `tests/umd/test_cu_mem_pool.cpp:108-124` 用例 setUp 中注入 mock（g_gpu_client = &mock）
- [ ] **4.0.3** 在 `tests/umd/test_cu_mem_pool.cpp:177-189` 用例 setUp 中注入 mock
- [ ] **4.0.4** 验证 `CHECK(p1 != p2)` 仍通过（mock 现在返回 `pool*0x100000 + seq` 保证唯一）
- [ ] **4.0.5** 决策点：MISSED-4 提议 (a) 所有现有测试注入 mock 或 (b) 保留 PoC counter fallback — **选 (a)**（更干净，避免 fallback 路径模糊）

### 4.1 test_cu_graph.cpp 扩展（5 new cases）

- [ ] **4.1.1** 在 test 文件顶部 include `mock_gpu_driver.hpp` 并复用全局 mock 实例
- [ ] **4.1.2** 添加新测试 `cu_graph: Launch with mock driver calls submit_graph once`
- [ ] **4.1.3** 添加 `cu_graph: Launch with g_gpu_client nullptr returns NOT_INITIALIZED`
- [ ] **4.1.4** 添加 `cu_graph: Launch with mock returning -1 propagates error`
- [ ] **4.1.5** 添加 `cu_graph: Launch records fence_id to LaunchTrace (PoC check)`
- [ ] **4.1.6** 添加 `cu_graph: Launch with NULL exec returns INVALID_VALUE before driver call`
- [ ] **4.1.7 (S4)** 添加 `cu_graph: ExecDestroy erases LaunchTrace entry`

### 4.2 test_cu_mem_pool.cpp 扩展（8 new cases）

- [ ] **4.2.1** 复用 `mock_gpu_driver.hpp`（已扩展）
- [ ] **4.2.2** 添加 `cu_mem_pool: Alloc calls mem_pool_alloc and writes mock VA`
- [ ] **4.2.3** 添加 `cu_mem_pool: Alloc with mock returning error propagates`
- [ ] **4.2.4** 添加 `cu_mem_pool: AllocAsync calls mem_pool_alloc_async and writes VA`
- [ ] **4.2.5** 添加 `cu_mem_pool: FreeAsync calls mem_pool_free_async with correct VA`
- [ ] **4.2.6** 添加 `cu_mem_pool: FreeAsync with NULL ptr returns INVALID_VALUE`
- [ ] **4.2.7 (REVISED)** 添加 `cu_mem_pool: ExportToShareableHandle with POSIX_FD calls mem_pool_export_shareable and writes FD`（现在可测试，因 IGpuDriver 已新增方法）
- [ ] **4.2.8** 添加 `cu_mem_pool: ExportToShareableHandle with Win32 returns NOT_SUPPORTED`
- [ ] **4.2.9** 添加 `cu_mem_pool: All 5 REAL-bridged APIs return NOT_INITIALIZED when g_gpu_client null`

### 4.3 验证

- [ ] **4.3.1** 全测试：`ctest --output-on-failure -j4` 应 270+ cases 全过（实测基线 257 + ~13 new）
- [ ] **4.3.2** 回归确认：原 257 cases 中只有 mock-injection tests 行为变化，pure shim tests 不变
- [ ] **4.3.3** ASan/UBSan：`cmake -B build_asan -DSANITIZER_ADDRESS=ON && cmake --build build_asan && ctest -j4` 全过
- [ ] **4.3.4** `tools/docs-audit.sh --strict` exit 0
- [ ] **4.3.5** TSan：`cmake -B build_tsan -DSANITIZER_THREAD=ON && ctest -j4` 全过（g_gpu_client 全局并发安全验证）

## 5. Documentation + Cross-Repo Sync（REVISED M4 修复）

> **Commit 1 (TaskRunner 仓)**：`docs(sync): Phase 4 real-impl + tadr-301 + tadr-302 + sync-plan v2.4`
> **Commit 2 (UsrLinuxEmu 仓)**：`docs(sync): update TaskRunner TADR mirror for tadr-302 + bump submodule`
> **影响文件**:
> - TaskRunner: `docs/shared/adr/tadr-301-igpu-driver-contract.md`（Method Count Evolution 46→47）, `docs/shared/adr/tadr-302-mempool-export-shareable.md`（**新增**）, `plans/sync-plan.md` v2.4
> - UsrLinuxEmu: `docs/00_adr/README.md`（mirror 行更新）
> **Sync 协议**: **触发完整 ADR-035 §Rule 5.1 4 步**（Metis M4 修复）

### 5.1 更新 tadr-301-igpu-driver-contract.md

- [ ] **5.1.1** 在 Method Count Evolution 表添加新行：
  ```markdown
  | Phase 4 真实化 (current) | **47 (+1)** | Phase 4 shim bridge + mem_pool_export_shareable 新增 |
  ```
- [ ] **5.1.2** 在 §Stability rules 段补充：
  ```markdown
  5. Shim hybrid mode (Phase 4+): 5 APIs (`cuGraphLaunch` / `cuMemPoolAlloc` / 
     `cuMemPoolAllocAsync` / `cuMemPoolFreeAsync` / `cuMemPoolExportToShareableHandle`)
     MUST bridge to GpuDriverClient IOCTLs (range 0x58, 0x62-0x64, 0x68) when `g_gpu_client != nullptr`.
  ```
- [ ] **5.1.3** 更新 §Decision Date：`2026-06-24` → `2026-07-07`（grep 定位 `Decision Date` 字段后再改 — Metis S1）

### 5.2 新增 tadr-302-mempool-export-shareable.md

- [ ] **5.2.1** 创建 `docs/shared/adr/tadr-302-mempool-export-shareable.md`：
  ```markdown
  ---
  SCOPE: shared
  STATUS: ACCEPTED
  DATE: 2026-07-07
  RELATED: tadr-301-igpu-driver-contract.md
  RELATED: UsrLinuxEmu adr-XXX-mem-pool-export-ioctl.md
  ---
  
  # TADR-302: IGpuDriver::mem_pool_export_shareable Contract
  
  ## Context
  Phase 4 shim 真实化要求 cuMemPoolExportToShareableHandle 桥接到 sim；
  当前 IGpuDriver 46 个方法无 export，导致 shim 无可调用 API。
  
  ## Decision
  新增第 47 个虚方法：
  ```cpp
  virtual int mem_pool_export_shareable(uint64_t pool_handle,
                                        uint32_t handle_type,
                                        uint32_t flags,
                                        int64_t* fd_out) { return -1; }
  ```
  返回值：0 成功，-1 失败。
  handle_type: 1 = POSIX FD（Phase 4 支持），其他返回 -1。
  
  ## Consequences
  - 所有 IGpuDriver 实现（GpuDriverClient / CudaStub / MockGpuDriver）必须 override
  - UsrLinuxEmu sim 必须新增 GPU_IOCTL_MEM_POOL_EXPORT (0x68) IOCTL
  - ABI 范围扩展：原 0x60-0x67 → 0x60-0x68
  ```

### 5.3 更新 sync-plan.md v2.4

- [ ] **5.3.1** 版本号：`v2.3` → `v.4`
- [ ] **5.3.2** 添加新 §1.5 Phase 4 段：
  ```markdown
  ### 1.5 Phase 4 真实化（2026-07-07）
  - **跨仓工作**：
    - UsrLinuxEmu: 新增 GPU_IOCTL_MEM_POOL_EXPORT (0x68) IOCTL + ADR-XXX
    - TaskRunner: IGpuDriver 46→47 方法 + tadr-302 新增
  - C8: cuGraphLaunch REAL bridge (Phase 3 PoC → real submit_graph IOCTL 0x58)
  - C9: 5 cuMemPool* APIs REAL bridge (Alloc/AllocAsync/FreeAsync/ExportToShareableHandle)
  - C10: g_gpu_client 类型 IGpuDriver* + nullptr fallback (CUDA_ERROR_NOT_INITIALIZED)
  - C11: 13 new E2E test cases (mock-injection path)
  - C12: tadr-301/302 + sync-plan update
  ```
- [ ] **5.3.3** 更新 §5.1 sync status table，添加 Phase 4 行（含 UsrLinuxEmu PR + TaskRunner PR + submodule bump）
- [ ] **5.3.4** 更新 §5.2 测试基线：`257` → `270+`（实测基线，非估算）

### 5.4 UsrLinuxEmu docs mirror（M4 修复）

- [ ] **5.4.1** 在 `external/UsrLinuxEmu/docs/00_adr/README.md` 添加 tadr-302 行：
  ```markdown
  | [tadr-302](../external/TaskRunner/docs/shared/adr/tadr-302-mempool-export-shareable.md) | IGpuDriver mem_pool_export_shareable 契约（Phase 4 新增）| [ADR-XXX](../external/TaskRunner/.../adr-XXX-mem-pool-export-ioctl.md) |
  ```
- [ ] **5.4.2** 验证：UsrLinuxEmu docs-audit 通过

### 5.5 提交（双仓，Metis M4 修复完整 4 步）

**Step 1: TaskRunner 仓 commit + push**：
```bash
cd /workspace/project/UsrLinuxEmu/external/TaskRunner/.rddf/wt/phase3-real-impl-bridge-extended

git add docs/shared/adr/tadr-301-igpu-driver-contract.md \
        docs/shared/adr/tadr-302-mempool-export-shareable.md \
        plans/sync-plan.md

git commit -m "docs(sync): Phase 4 real-impl + tadr-301/302 + sync-plan v2.4

- tadr-301 Method Count Evolution: 46→47 (mem_pool_export_shareable added)
- tadr-302 NEW: mem_pool_export_shareable contract
- sync-plan.md v2.3 → v2.4: Phase 4 status, +13 new test cases

Refs: openspec/changes/phase3-real-impl-bridge/
Refs: UsrLinuxEmu adr-XXX-mem-pool-export-ioctl.md"
git push origin phase3-real-impl-bridge-extended
```

**Step 2: UsrLinuxEmu 仓 commit + push（mirror）**：
```bash
cd /workspace/project/UsrLinuxEmu/.rddf/wt/phase4-mempool-export-ioctl

# 此 worktree 已 commit 0.B.6.1 的 IOCTL 工作
# 同步更新 mirror 行（指向 tadr-302）
git add docs/00_adr/README.md
git commit -m "docs(sync): add tadr-302 mirror + bump TaskRunner submodule pointer"
```

**Step 3: UsrLinuxEmu 父仓 bump submodule pointer**（**关键 M4 步骤**）：
```bash
cd /workspace/project/UsrLinuxEmu

# 拉取 TaskRunner 最新提交
git fetch external-task-runner  # 或 TaskRunner remote 配置

# 更新 submodule
git add external/TaskRunner
git commit -m "chore(submodule): bump TaskRunner to <TaskRunner commit SHA>

Phase 4 sync: tadr-302 added, g_gpu_client type IGpuDriver*,
5 APIs real-bridged.

Refs: TaskRunner openspec/changes/phase3-real-impl-bridge/
Refs: TaskRunner PR #<N>"
git push origin main
```

## 验收准则 (Definition of Done) — REVISED

### UsrLinuxEmu 仓（critical path）

- [ ] **C0.1**：1 个 commit `feat(gpu-ioctl): add MEM_POOL_EXPORT (0x68)`（IOCTL + handler + stub + tests + ADR）
- [ ] **C0.2**：UsrLinuxEmu `ctest` 全过（plugin tests + mempool_export tests）
- [ ] **C0.3**：UsrLinuxEmu PR 合并到 main

### TaskRunner 仓（依赖 C0.x）

- [ ] **C1 验收**：1 个 commit `feat(shim-foundation)`（M1+M2+S5+S6+S4 + LaunchTrace + helper + cleanup），0 warning
- [ ] **C2 验收**：1 个 commit `feat(shim): cuGraphLaunch bridges to GpuDriverClient::submit_graph`，测试覆盖 6 cases（含 4.1.7 cleanup）
- [ ] **C3 验收**：1 个 commit `feat(shim): cuMemPool Alloc/Async/Export bridges to GpuDriverClient`（**5 API** 含 export），测试覆盖 8 cases
- [ ] **C4 验收**：1 个 commit `test(shim): mock-injection pattern + 13 new cases`，270+ total tests pass
- [ ] **C5 验收**：1 个 commit `docs(sync): Phase 4 + tadr-301/302 + sync-plan v2.4 + mirror`
- [ ] **C6 验收（REVISED M4 修复）**：1 个 commit 在 UsrLinuxEmu 仓 bump `external/TaskRunner` submodule pointer
- [ ] **共 5+1 atomic commits**（TaskRunner 5 + UsrLinuxEmu submodule bump 1）

### 全局

- [ ] **零 regression**：原 257 cases 中 pure shim tests 不变化；mock-injection tests 行为更新
- [ ] **270+ total tests passing**（实测基线 257 + ~13 new + M3 修复的 6 个 case）
- [ ] **ASan/UBSan/TSan 全过**（参考 PR #7 C6 commit 验证方法）
- [ ] **`tools/docs-audit.sh --strict` exit 0**
- [ ] **`nm libcuda_taskrunner.so | grep "T cu.*"` 含 5 新增/修改符号**（cuGraphLaunch, cuMemPoolAlloc, cuMemPoolAllocAsync, cuMemPoolFreeAsync, cuMemPoolExportToShareableHandle）
- [ ] **PR 提交**：
  - UsrLinuxEmu: PR `phase4-mempool-export-ioctl` → main（**必须先合并**）
  - TaskRunner: PR `phase3-real-impl-bridge-extended` → main（依赖 UsrLinuxEmu PR 已合并）
  - UsrLinuxEmu parent: PR `chore(submodule): bump TaskRunner to <SHA>` → main（依赖 TaskRunner PR 已合并）

## 时间线总览

```
Day 1 (0.5h):  0.* 前置验证 + 创建 worktree
Day 1 (4h):    1.* Foundation (LaunchTrace + g_gpu_client helper)
Day 1 (4h):    2.* cu_graph.cpp real bridge (cuGraphLaunch)
Day 2 (6h):    3.* cu_mem_pool.cpp real bridge (4 API + 2 new async)
Day 3 (4h):    4.* 测试扩展 (13 cases + mock framework)
Day 3 (2h):    5.* docs sync
Day 4 (1h):    PR 提交 + review
```

**总工作量**: 4 working days ≈ 0.5-1 week (per user estimation)
**依赖对齐**: 与 Phase 4 sync 时序对齐（无需等待 UsrLinuxEmu sim 端改动）

## 风险 checklist

- [ ] **R1 验证**：原 mempool 测试中假设 "counter 唯一" 仍然 valid（mock 返回 VA unique 即可）
- [ ] **R4 验证**：ASan/UBSan 严格测试通过（struct padding 一致）
- [ ] **R7 验证**：pin submodule pointer (不要在 Phase 4 工作中 bump submodule，避免 ABI 漂移)
- [ ] **OQ1 决策**：cuMemPoolAlloc 在 `g_gpu_client == nullptr` 时返回 `CUDA_ERROR_NOT_INITIALIZED` ✓ (已在 Design D3 决策)
- [ ] **OQ2 决策**：cuGraphLaunch 返回 SUCCESS + fence_id 注入 LaunchTrace ✓ (已在 Design D5 决策)
- [ ] **OQ3 决策**：cuMemPoolAllocAsync 立即返回 ptr + fence_id，va 在 fence 完成前不可解引用 ✓ (已在 Spec 明确)
- [ ] **OQ4 决策**：更新 vendored cuda.h 添加 Async 函数声明 ✓ (已在 Tasks 3.4.1)