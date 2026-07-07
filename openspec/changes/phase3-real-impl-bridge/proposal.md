# Change: phase3-real-impl-bridge

> **Status**: 🔄 PROPOSED → REVISED 2026-07-07（Metis 二次审查后扩展 scope，方案 B：跨仓添加 `GPU_IOCTL_MEM_POOL_EXPORT`）
> **Created**: 2026-07-07
> **Source**: Follow-up to PR chisuhua/TaskRunner#7（Phase 3 Step 3 merge @ 02363b8 + Step 4 submodule bump @ 458299e）
> **Related**: `openspec/changes/archive/2026-07-06-phase3-step3-shim-and-forwarding/`（Step 3 PoC）
> **Related**: UsrLinuxEmu `openspec/changes/archive/2026-07-05-sim-stream-primitive-support/`（Step 2 IOCTL ABI）
> **Related**: TaskRunner `docs/shared/adr/tadr-301-igpu-driver-contract.md`（IGpuDriver 46 方法契约）
> **Related (NEW)**: UsrLinuxEmu `openspec/changes/proposed/2026-07-07-mempool-export-ioctl/`（同步提出的新增 `GPU_IOCTL_MEM_POOL_EXPORT` IOCTL）
> **Related (NEW)**: TaskRunner `docs/shared/adr/tadr-302-mempool-export-shareable.md`（同步新增的 IGpuDriver export 方法契约）
> **Cross-repo sync**: ADR-035 §Rule 5.1 — 完整 4 步协议（TaskRunner + UsrLinuxEmu + 跨仓 PR 协调）

## Why（修订后）

**Metis 二次审查关键发现**：proposal 引用 `cuMemPoolExportToShareableHandle` 走 `GPU_IOCTL_MEM_POOL_EXPORT`（0x64），但 UsrLinuxEmu 端 **0x60-0x67 范围已有 CREATE/DESTROY/ALLOC/ALLOC_ASYNC/FREE_ASYNC/SET_ATTR/GET_ATTR/TRIM，无 export IOCTL**；`IGpuDriver` 也无 `mem_pool_export_shareable` 方法。

方案 B（选定）：**先在 UsrLinuxEmu 仓新增 IOCTL 0x68 = `GPU_IOCTL_MEM_POOL_EXPORT`**，再在 TaskRunner 仓：
1. `IGpuDriver` 新增 `mem_pool_export_shareable` 虚方法（46 → 47 方法）
2. `GpuDriverClient` 添加 forwarding override（ioctl 0x68）
3. `CudaStub` 添加 stub override（return -1 NOT_SUPPORTED）
4. Shim `cuMemPoolExportToShareableHandle` 真实桥接到 IGpuDriver

其余 4 个 API（`cuGraphLaunch` / `cuMemPoolAlloc` / `cuMemPoolAllocAsync` / `cuMemPoolFreeAsync`）按原 plan 桥接到已有 IOCTL（0x58 / 0x62 / 0x63 / 0x64）。

**Why now**:
1. **跨仓窗口已对齐**：与 UsrLinuxEmu Step 2/3/4 协调时间线对齐，可同步发起两个 PR
2. **必要性**：NVIDIA CUDA 12.x `cuMemPoolExportToShareableHandle` 是 IPC 关键 API（驱动跨进程共享显存），Phase 4 必须实现
3. **风险隔离**：UsrLinuxEmu IOCTL 是新增（无破坏性），TaskRunner 端按计划桥接

## Why

Phase 3 Step 3（PR #7）完成了 cuGraph/cuMemPool/cuStreamCapture shim 的 **REAL_IMPL 骨架**，但其中 5 个 API 因 **PoC 限制** 仅做局部内存状态模拟（自包含 atomic+map+mutex），未真正 dispatch 到 UsrLinuxEmu sim：

| PoC API | 当前行为 | 目标行为 |
|---|---|---|
| `cuGraphLaunch` | 立即 return `CUDA_SUCCESS`（no-op） | 通过 `GpuDriverClient::submit_graph` (IOCTL 0x59) 真正提交 graph 到 sim，返回 sim fence_id |
| `cuMemPoolAlloc` | 返回 synthetic VA = monotonic counter（不可解引用） | 通过 `GpuDriverClient::mem_pool_alloc` (IOCTL 0x62) 申请 VA sub-range，返回真实 VA |
| `cuMemPoolExportToShareableHandle` | 立即 return `CUDA_SUCCESS`（no-op） | 通过 `GpuDriverClient::mem_pool_export_shareable` (IOCTL 0x64) 真实导出 FD，写入 `*shareableHandle` |
| `cuMemPoolAllocAsync` | 复用 sync impl，return SUCCESS（无 fence） | 通过 `GpuDriverClient::mem_pool_alloc_async` (IOCTL 0x63) 真实 async，return sim fence_id (≥ 1<<32 per F-4) |
| `cuMemPoolFreeAsync` | 复用 sync impl，return SUCCESS（无 fence） | 通过 `GpuDriverClient::mem_pool_free_async` (IOCTL 0x65) 真实 async，return sim fence_id |

**Why now**:
1. **UsrLinuxEmu sim 端 VA allocator / FD export 已具备**：Step 2 (138f15a) 已实现 18 个 IOCTL handler，包含真实 VA mapping 和 fd 导出逻辑（`gpu_ioctl.h` 的 `gpu_mem_pool_alloc_args.va_out` 等字段已定义）。TaskRunner 端 PoC 桥接是 missing piece。
2. **E2E 测试已具备**：83 cases 的 PoC shim 测试已就绪（PR #7 C6 commit）。新增的"真实桥接"测试可在同框架扩展（从 NONE/POI 重定向到 IGpuDriver mock 验证 IOCTL 被调用）。
3. **跨仓协调窗口**：与 UsrLinuxEmu 4-step 协调时间线对齐（Step 1+2+3+4 已 done），现在做 Phase 4 收尾工作。

## What Changes

### C8: cu_graph.cpp — cuGraphLaunch REAL bridge
- **C8.1**: 修改 `cuGraphLaunch` 把当前 `return CUDA_SUCCESS` 改为：
  1. 通过全局 `g_gpu_client` 取得 `IGpuDriver*`
  2. 调用 `g_gpu_client->submit_graph(exec_handle, stream_id)` 返回 `int64_t` fence_id
  3. 若 fence_id ≥ 1<<32，记录到 `LaunchTrace`（fence_id → CUstream mapping）用于后续 cuStreamSynchronize/wait
- **C8.2**: 新增 `LaunchTrace` 自包含状态表（atomic + map + mutex，与 CaptureTable/GraphTable/MemPoolTable 同一 pattern）
- **C8.3**: 单元测试从"return SUCCESS"升级到"return 真实 fence_id" + mock 验证 `submit_graph` 被调用 1 次

### C9: cu_mem_pool.cpp — Alloc/AllocAsync/FreeAsync/Export REAL bridge
- **C9.1**: 修改 `cuMemPoolAlloc`：从 monotonic counter 改为 `g_gpu_client->mem_pool_alloc(pool_handle, size, &va_out)`
- **C9.2**: 修改 `cuMemPoolAllocAsync`：从 sync impl 改为 `g_gpu_client->mem_pool_alloc_async(pool_handle, size, stream_id, &va_out)` 返回 fence_id
- **C9.3**: 修改 `cuMemPoolFreeAsync`：从 sync impl 改为 `g_gpu_client->mem_pool_free_async(va, stream_id)` 返回 fence_id
- **C9.4**: 修改 `cuMemPoolExportToShareableHandle`：从 no-op 改为 `g_gpu_client->mem_pool_export_shareable(pool_handle, handle_type, flags, &fd_out)` 真实导出 FD
- **C9.5**: 单元测试从"return SUCCESS"升级到"return 真实 VA/fence_id" + mock 验证 IGpuDriver 方法被正确调用

### C10: g_gpu_client 全局注入 (基础)
- **C10.1**: 在 `cu_graph.cpp` / `cu_mem_pool.cpp` 通过现有 `g_gpu_client` 全局（已在 `gpu_driver_client.cpp` 定义）取得 `IGpuDriver*`
- **C10.2**: 若 `g_gpu_client == nullptr`（libcuda_taskrunner.so 在没有 GpuDriverClient 的环境链接，例如纯 stub 测试），返回 `CUDA_ERROR_NOT_INITIALIZED` 而非崩溃
- **C10.3**: 测试通过 MockGpuDriver（在 `test_gpu_architecture.cpp` 已有）注入，验证 IOCTL 调用顺序

### C11: 测试覆盖扩展（C6 followup）
- **C11.1**: test_cu_graph.cpp：替换/添加 `Launch` 测试，验证 mock `submit_graph` 被调用 1 次 + fence_id ≥ 1<<32
- **C11.2**: test_cu_mem_pool.cpp：替换/添加 `Alloc/AllocAsync/FreeAsync/Export` 测试，验证 mock IGpuDriver 方法被调用 + 返回值正确传播
- **C11.3**: 新增 test_cu_graph_mem_pool_bridge.cpp（optional）：跨 API 集成测试（先 alloc VA，再 launch graph 引用该 VA）

### C12: docs/sync
- **C12.1**: 更新 `docs/shared/adr/tadr-301-igpu-driver-contract.md` Method Count Evolution 表（添加 "Phase 3 真实化" 行）
- **C12.2**: 更新 `external/UsrLinuxEmu/docs/00_adr/README.md` mirror 行（如果已存在）
- **C12.3**: 更新 `plans/sync-plan.md` v2.4：添加 Phase 3 real-impl 段

### 不修改
- ✅ **修改 UsrLinuxEmu sim 端**（REVISED）：新增 `GPU_IOCTL_MEM_POOL_EXPORT`（0x68）IOCTL + handler
- ❌ 不改 `IGpuDriver` 已有 46 方法（**新增** 1 个 `mem_pool_export_shareable` → 47 方法）
- ❌ 不改 cuStreamCapture shim（D-S3-1 决策保留：shim 不调 GpuDriverClient）
- ❌ 不改 cuMemPoolSetAttribute/GetAttribute（已经直返 SUCCESS，不需要 IOCTL）

### Breaking Changes

**🟡 半-breaking**：原 PoC 的 `cuMemPoolAlloc` 返回 synthetic counter-as-pointer（**不可解引用**）。改为真实 VA 后，**可解引用**，意味着：
- 现有测试如果假设 ptr 是 counter（`< pool_handle|size` 编码）会失败
- 已审：`test_cu_mem_pool.cpp` 中所有 `CHECK(p1 != p2)` 仍然 valid（unique pointer 仍 unique）
- 已审：`test_cu_graph.cpp` 中没有直接依赖 mempool VA 解引用的 test
- **Mitigation**：保留 PoC 行为作为 fallback（当 `g_gpu_client == nullptr` 时返回 counter-as-pointer 保持兼容）

## Capabilities

### New Capabilities
- `phase3-real-impl-bridge`: 桥接 5 个 PoC shim API 到真实 GpuDriverClient IOCTL 调用，覆盖 cuGraphLaunch/cuMemPoolAlloc/cuMemPoolAllocAsync/cuMemPoolFreeAsync/cuMemPoolExportToShareableHandle。

### Modified Capabilities
*(无 — `openspec/specs/` 当前为空，无 existing capability 可被修改)*

## Impact

### 受影响代码
- `src/umd/libcuda_shim/cu_graph.cpp` (修改 cuGraphLaunch, ~50 行新增)
- `src/umd/libcuda_shim/cu_mem_pool.cpp` (修改 4 个 API, ~80 行新增)
- `tests/umd/test_cu_graph.cpp` (扩展 Launch 测试, +5 cases)
- `tests/umd/test_cu_mem_pool.cpp` (扩展 Alloc/Export 测试, +8 cases)
- `cmake/UMDEvolution.cmake` (新增 test binary 如需)
- `docs/shared/adr/tadr-301-igpu-driver-contract.md` (Method Count 表添加行)
- `plans/sync-plan.md` (v2.4 添加本 phase 段)

### 受影响 API（external）
- **cuGraphLaunch** signature 不变，behavior change（PoC SUCCESS → 真实 fence_id 注入到 trace）
- **cuMemPoolAlloc** signature 不变，behavior change（synthetic counter → 真实 VA）
- **cuMemPoolAllocAsync / FreeAsync** signature 不变，behavior change（无 fence → sim fence_id ≥ 1<<32）
- **cuMemPoolExportToShareableHandle** signature 不变，behavior change（no-op → 真实 FD）

### Cross-repo 依赖（REVISED）
- **UsrLinuxEmu 端**（**新增工作**）：
  - 在 `plugins/gpu_driver/shared/gpu_ioctl.h` 新增 `GPU_IOCTL_MEM_POOL_EXPORT`（0x68）+ `struct gpu_mem_pool_export_args`
  - 在 `plugins/gpu_driver/gpu_driver_plugin.cpp` 新增 IOCTL handler（real impl + CudaStub fallback）
  - 新增 `docs/00_adr/adr-XXX-mem-pool-export-ioctl.md`
  - 更新 `docs/00_adr/README.md` mirror
- **TaskRunner 端**（扩展）：
  - `include/shared/igpu_driver.hpp` 新增 1 个虚方法 `mem_pool_export_shareable`（46 → 47）
  - `include/test_fixture/gpu_driver_client.h` 添加 forwarding override
  - `src/test_fixture/cuda_stub.cpp` 添加 stub override
  - `tests/test_fixture/mock_gpu_driver.hpp` 添加 mock override
  - `src/umd/libcuda_shim/cu_mem_pool.cpp` 真实桥接 `cuMemPoolExportToShareableHandle`
  - Shim 端 5 API 全部桥接（同 proposal.md §What Changes C9）
  - M1-M4 + S5/S6 修复（同 Metis 二次审查）

### 测试影响
- 现有 225 cases 中，约 6 个 case 行为预期需更新（从 SUCCESS → fence_id/真实 VA）
- 新增 ~13 cases 覆盖真实桥接路径
- 总测试数预估：225 → ~238

### Build 影响（REVISED）
- **UsrLinuxEmu 端**：
  - gpu_ioctl.h 头文件改动（IOCTL 常量 + struct）— 重新 build 所有依赖 gpu_driver 的 binary
  - gpu_driver_plugin.cpp 新增 handler — plugin 重新编译
- **TaskRunner 端**：
  - IGpuDriver 接口扩展 1 个虚方法 — 所有 `IGpuDriver` 实现（GpuDriverClient / CudaStub / MockGpuDriver）需重编译
  - Shim 真实化 — `libcuda_taskrunner.so` 重新链接
  - 新 vendored cuda.h 常量 + Async 原型 — 编译失败则修复
  - cmake 配置不变

### Risk（REVISED）
- 🟡 **medium-high**：UsrLinuxEmu 新增 IOCTL ABI 必须与 TaskRunner 端 struct 严格一致（字段顺序、大小、padding、对齐）—— 需要 ASan/UBSan/TSan 跨仓联调
- 🟡 **medium**：从 PoC 跳到真实桥接可能暴露 shim 与 GpuDriverClient IOCTL ABI 不匹配（struct 字段顺序、大小、padding）—— 需要 ASan/UBSan 严格测试
- 🟡 **medium**：FD 导出跨进程语义（pipe2 vs shm_open）需在 UsrLinuxEmu 端决策；POSIX FD 默认走 `pipe2(2)` 创建匿名 inode
- 🟢 **low**：g_gpu_client 类型改 `IGpuDriver*`（M1）blast radius 已确认不影响 `cmd_cuda.cpp`（其调用方法均为 IGpuDriver 虚函数）
- 🟢 **low**：g_gpu_client nullptr 边界处理是已知 case
- 🟢 **low**：测试 mock 注入路径已经过 Phase 2 验证（test_gpu_architecture.cpp 有 MockGpuDriver）
- 🟡 **medium**（新增）：跨仓 PR 协调时序（UsrLinuxEmu 必须先合并 → TaskRunner 再合并 → UsrLinuxEmu 父仓 bump submodule pointer）