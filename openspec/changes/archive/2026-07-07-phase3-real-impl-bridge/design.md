# Design: phase3-real-impl-bridge

> **Companion to**: proposal.md
> **Target capability**: `phase3-real-impl-bridge`（specs/phase3-real-impl-bridge/spec.md）
> **Audience**: Phase 4 实施者 + UsrLinuxEmu 端 reviewer

## Context

### Background

Phase 3 Step 3（PR #7 merge @ 02363b8）建立了 cuGraph/cuMemPool/cuStreamCapture shim 的 **REAL_IMPL 骨架**，但因 PoC 设计选择（D-S3-1：shim 不调 GpuDriverClient；D-MP-1：合成 VA 用于测试），其中 5 个 API 行为与真实 GPU 语义有差异：

- **cuGraphLaunch**: PoC no-op (`return CUDA_SUCCESS`) — 不提交任何工作到 sim，调用方拿不到 fence
- **cuMemPoolAlloc**: PoC monotonic counter-as-pointer — 不可解引用，仅用于 handle 唯一性
- **cuMemPoolAllocAsync/FreeAsync**: PoC 复用 sync impl — return `CUDA_SUCCESS` 而非 sim fence_id
- **cuMemPoolExportToShareableHandle**: PoC no-op — 不导出 FD

UsrLinuxEmu Step 2（PR #20 merge @ 138f15a）已提供对应 8 个 IOCTL handlers（`gpu_ioctl.h` 0x60-0x67 范围：CREATE/DESTROY/ALLOC/ALLOC_ASYNC/FREE_ASYNC/SET_ATTR/GET_ATTR/TRIM）。

**Metis MISSED-1 关键发现（2026-07-07）**：`cuMemPoolExportToShareableHandle` 引用的 `GPU_IOCTL_MEM_POOL_EXPORT` 在 UsrLinuxEmu 端**不存在**：
- `UsrLinuxEmu/plugins/gpu_driver/shared/gpu_ioctl.h:580-623` 中 0x60-0x67 完整映射 8 个 IOCTL，**无 export**
- proposal 把 export 写成 IOCTL 0x64，但 0x64 实际是 `GPU_IOCTL_MEM_POOL_FREE_ASYNC`
- `IGpuDriver` 46 个方法中无 `mem_pool_export_shareable`

**方案 B（扩展 scope）**：先在 UsrLinuxEmu 新增 IOCTL 0x68 = `GPU_IOCTL_MEM_POOL_EXPORT`，再在 TaskRunner 端桥接。TaskRunner 端 GpuDriverClient 已在 Step 3 C2 commit 添加了 14 个 forwarding override（不含 export）。

### Current State

| API | 当前实现位置 | 当前行为 |
|---|---|---|
| `cuGraphLaunch` | `cu_graph.cpp:122` | `if (!hGraphExec) return INVALID_VALUE; return SUCCESS;` (PoC) |
| `cuMemPoolAlloc` | `cu_mem_pool.cpp:59` | `uint64_t aid = atomic counter; *ptr = aid; return SUCCESS;` (PoC) |
| `cuMemPoolAllocAsync` | (复用 sync impl) | 当前**不存在独立函数** (待新增) |
| `cuMemPoolFreeAsync` | (复用 sync impl) | 当前**不存在独立函数** (待新增) |
| `cuMemPoolExportToShareableHandle` | `cu_mem_pool.cpp:105` | `if (!shareableHandle \|\| !pool) return INVALID_VALUE; return SUCCESS;` (PoC) |

### Constraints

1. **Shim-only 模式必须延续**：其他 cuStream*/cuGraph*/cuMemPool* API 保持自包含 atomic+map+mutex，不调 GpuDriverClient（D-S3-1 决策）。仅这 5 个 PoC-limited API 转为真实桥接。
2. **测试隔离原则**：测试不应依赖真实 UsrLinuxEmu sim 进程（这会让测试变成 E2E 系统测试而非单元测试）。MockGpuDriver 注入路径必须保持。
3. **零额外依赖**：不引入新 library；仅使用现有 `g_gpu_client` 全局（IGpuDriver 实例）+ `MockGpuDriver`（测试）。
4. **跨仓 ABI 同步（REVISED）**：
   - **UsrLinuxEmu 端**：新增 IOCTL 0x68 + handler；扩展 ABI 范围 0x60-0x67 → 0x60-0x68
   - **TaskRunner 端**：IGpuDriver 扩展 1 个方法 46 → 47；新增 `mem_pool_export_shareable`
   - **协调顺序**（ADR-035 §Rule 5.1 完整 4 步）：UsrLinuxEmu PR → TaskRunner PR → UsrLinuxEmu submodule bump
5. **B-2 sentinel 约束**：所有 `mem_pool_*` 调用必须先校验 `va_space_handle != 0`（H-1 sentinel）。`g_gpu_client->mem_pool_*` 已自带此校验（Step 3 C2 commit），但 shim 层需要确保 handle 是非零再传入。
6. **(REVISED Metis M1)** **`g_gpu_client` 类型**：从 `GpuDriverClient*` 改为 `IGpuDriver*`，以支持 `MockGpuDriver` 注入。blast radius 已确认不影响 `cmd_cuda.cpp`（其调用方法均为 IGpuDriver 虚函数）。

## Goals / Non-Goals

### Goals
- **G1**: 把 5 个 PoC API 桥接到真实 `g_gpu_client->*` 调用，行为符合 CUDA Runtime API 规范
- **G2**: 保持 MockGpuDriver 注入路径可用，新增测试用例验证 IOCTL 被正确调用
- **G3**: `g_gpu_client == nullptr` 时优雅降级（返回 `CUDA_ERROR_NOT_INITIALIZED` 而非 segfault）
- **G4**: 测试基线 225 → 240+ cases，零 regression
- **G5**: 不破坏既有 shim API 的自包含模式（其他 cuGraph*/cuMemPool* 不变）

### Non-Goals
- **NG1**: 不修改 IGpuDriver 接口（46 方法契约稳定）
- **NG2**: 不修改 UsrLinuxEmu sim 端（IOCTL handlers 已就绪）
- **NG3**: 不实现 `cuGraphLaunch` 的依赖关系解析（PoC 也不解析，Phase 5+ 再说）
- **NG4**: 不实现 `cuMemPoolExportToShareableHandle` 的 Win32 HANDLE / fabric handle 导出（仅 POSIX FD）
- **NG5**: 不修改 `cuStreamCapture` shim（D-S3-1 决策保留）

## Decisions

### Decision 1: Shim-only → Hybrid 模式（per-API）

**决策**：仅 5 个 PoC API 转为 hybrid 模式（调用 g_gpu_client），其他 14 个 shim API 保持自包含模式。

**理由**：
- 4 个 cuStream* capture APIs 状态机本质是 client-side，不需要 sim 参与（capturing 期间所有 kernel launch 都被截获，不真正提交）
- 5 个 cuGraph* lifecycle APIs（Create/Destroy/AddKernelNode/AddMemcpyNode/Instantiate/ExecDestroy）本质是 in-memory graph 构建，不涉及 sim 提交
- 2 个 cuGraphNode* accessors 是纯查询
- 2 个 cuGraphExec* setters 是参数更新

**仅** Launch 涉及真实提交，**仅** cuMemPool* 涉及真实资源分配/导出。

**替代方案考虑**：所有 shim 都走 g_gpu_client。**拒绝**：增加耦合，与现有 D-S3-1 决策不符，测试复杂度激增。

### Decision 2: g_gpu_client 全局注入而非 DI

**决策**：使用现有 `g_gpu_client` 全局指针（在 `gpu_driver_client.cpp` 定义，CLI 模式下 `init_gpu_client()` 初始化，stub 模式下 nullptr）。

**理由**：
- 全局已是项目惯例（CLI 主入口使用）
- Shim 是 libcuda_taskrunner.so 的导出符号，应用进程在 load-time 注入全局
- 测试可通过 setUp/tearDown 修改全局（参考 `test_gpu_architecture.cpp` 已用 `MockGpuDriver` 模式）

**替代方案考虑**：把 `IGpuDriver*` 注入每个 shim 函数。**拒绝**：shim 函数签名是 CUDA Runtime API 规范（不能改），无法插入参数。

### Decision 3: nullptr Fallback 策略

**决策**：当 `g_gpu_client == nullptr` 时返回 `CUDA_ERROR_NOT_INITIALIZED`（700），并 log to stderr。

**理由**：
- 比 segfault 安全
- 比返回 SUCCESS 诚实（应用层能检测到）
- `CUDA_ERROR_NOT_INITIALIZED = 700` 是 NVIDIA CUDA 12.x 标准错误码

**替代方案考虑**：保持 PoC 行为（monotonic counter）。**拒绝**：行为不一致会让库用户困惑。

### Decision 4: cuMemPoolAllocAsync/FreeAsync 新增独立函数

**决策**：在 `cu_mem_pool.cpp` 新增独立 `cuMemPoolAllocAsync` 和 `cuMemPoolFreeAsync` 函数（之前 PoC 没有独立实现，因为 sync impl 已经够用）。

**理由**：
- CUDA Runtime API 12.x 有这两个函数（`cuMemPoolAllocAsync` signature 包含 `CUstream` 参数，sync 版本没有）
- PoC 阶段直接复用 sync impl 简化测试，但 production 必须独立
- 独立函数让 async fence_id 能正确返回给调用方

**替代方案考虑**：把 PoC 的 sync impl 标记为 deprecated，调用方迁移到 Async 版本。**拒绝**：spec 要求两者并存。

### Decision 5: LaunchTrace 状态表用于 fence_id 记录

**决策**：`cuGraphLaunch` 真实化后返回 sim fence_id (int64_t ≥ 1<<32)。需要记录 `exec_handle → fence_id` 映射供后续 `cuStreamSynchronize/wait` 使用。

**理由**：
- shim 当前没有 `cuStreamSynchronize` 的 fence query（CUDA Runtime API 需要 cuStreamWaitEvent 等来 sync）
- 短期内 fence_id 记录只是 metadata，不影响功能
- 长期 Phase 5+ 可以扩展 LaunchTrace 为 `exec_handle → {fence_id, completion_flag}` 支持 wait

**替代方案考虑**：直接丢弃 fence_id，不记录。**拒绝**：违反 F-4 约定（"int64_t 返回 ≥ 1<<32 = valid sim fence"），未来扩展困难。

### Decision 6: 测试策略 - Mock 重定向 vs 真实调用

**决策**：测试通过 `g_gpu_client = &mock_instance` 注入 MockGpuDriver，验证 shim 调用 IGpuDriver 方法的参数和次数。

**理由**：
- 已存在的 `test_gpu_architecture.cpp` 和 `test_gpu_phase2.cpp` 都用 MockGpuDriver 模式
- 不需要真实 UsrLinuxEmu sim 进程（CI 友好）
- 验证的是 shim ↔ GpuDriverClient 调用契约，不是 sim 内部逻辑

**替代方案考虑**：直接 e2e 测真实 sim。**拒绝**：测试慢、CI 复杂、跨仓依赖。

## Risks / Trade-offs

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| **R1**: Shim 调用 `g_gpu_client->mem_pool_alloc` 后，sim 返回的 VA 与现有测试预期（counter）冲突 | medium | medium | (a) 保留 PoC 行为作为 `g_gpu_client == nullptr` fallback；(b) 测试更新 mock 返回 `va = 0x10000` 等固定值；(c) 检查 83 case 中所有 mempool 测试 |
| **R2**: `cuMemPoolAllocAsync` 新增函数后，符号导出可能与 cu_stub_table.inc 旧条目冲突 | low | low | cu_stub_table.inc 已包含该 stub；新增 REAL_IMPL 时检查 stub 是否会被覆盖（linker 行为） |
| **R3**: LaunchTrace 表未与 cuStreamSynchronize 联动，fence_id 沦为 dead metadata | medium | low | Phase 4 PoC 阶段可接受；Phase 5+ 添加 cuStreamSynchronize + fence poll 逻辑 |
| **R4**: struct padding 不匹配导致 IOCTL 失败（g_gpu_client→sim 链路） | medium | high | ASan/UBSan 严格测试；按 struct 字段顺序（不是字节）严格定义；参考 PR #20 的 IOCTL handler 测试 |
| **R5**: g_gpu_client 全局被多线程并发修改导致 race | low | high | MockGpuDriver 测试时用 mutex 保护 setUp/tearDown；生产模式 g_gpu_client 仅 main thread 初始化一次 |
| **R6**: cuMemPoolAlloc 真实化后，测试假设"counter 唯一"需要改为"VA 唯一" | low | low | MockGpuDriver 返回 `va = pool_handle * 0x100000 + alloc_seq` 保证唯一 |
| **R7**: 跨仓 ABI 漂移（UsrLinuxEmu sim 修改了 IOCTL struct） | low | high | pin submodule pointer；ci 检查 `gpu_ioctl.h` hash 一致性 |

## Migration Plan

### Phase 1: Foundation（CU8.1 + C10.1）
1. 新增 `LaunchTrace` 状态表（atomic + map + mutex）到 `cu_graph.cpp`
2. 验证 `g_gpu_client` 全局在 libcuda_taskrunner.so 链接时可达（链接 libtaskrunner_test_fixture.a）

### Phase 2: cu_graph.cpp 真实化（C8.2 + C8.3）
1. 修改 `cuGraphLaunch`：检查 `g_gpu_client`，调用 `submit_graph`，记录 fence_id
2. 新增 5 个 Launch 测试

### Phase 3: cu_mem_pool.cpp 真实化（C9.1-C9.5 + 新增 async）
1. 修改 `cuMemPoolAlloc` 调用 `mem_pool_alloc`
2. 新增 `cuMemPoolAllocAsync` 函数（独立）
3. 新增 `cuMemPoolFreeAsync` 函数（独立）
4. 修改 `cuMemPoolExportToShareableHandle` 调用 `mem_pool_export_shareable`
5. 新增 8 个测试

### Phase 4: docs sync（C12）
1. 更新 TADR-301 添加 "Phase 3 真实化" 行
2. 更新 UsrLinuxEmu README.md mirror
3. 更新 sync-plan.md v2.4

### Rollback
- 所有改动可分 PR revert（每个 commit 是 atomic unit）
- 紧急情况：`git revert <commit>` 回退到 PoC 行为

## Open Questions

1. **OQ1**: `cuMemPoolAlloc` 在 `g_gpu_client == nullptr` 时是 return `CUDA_ERROR_NOT_INITIALIZED` 还是保留 PoC counter 行为？
   - 倾向 **D3** 决策：`NOT_INITIALIZED`
   - **需要 PR reviewer 确认**

2. **OQ2**: `cuGraphLaunch` 返回的 fence_id 是要 (a) 注入到 LaunchTrace + return SUCCESS 还是 (b) 直接返回 fence_id 让调用方 wait？
   - CUDA Runtime API 12.x 规定 cuGraphLaunch return CUresult，**不是** CUstreamWaitValue
   - **倾向 (a)**：与 CUDA 规范一致；fence_id 仅 metadata
   - **需要 PR reviewer 确认**

3. **OQ3**: `cuMemPoolAllocAsync` 是否需要等 fence 完成才返回 VA，还是立即返回 placeholder VA + fence_id？
   - NVIDIA CUDA 12.x 行为：异步 alloc 立即返回 ptr，但 ptr 在 fence 完成前不可用
   - **倾向后者**：mem_pool_alloc_async 返回 `{va, fence_id}`，va 在 fence 完成前不可解引用
   - **需要 PR reviewer 确认是否需在 shim 层加 fence_id → va "ready" 标志**

4. **OQ4**: cuMemPoolAllocAsync 是否要更新 `cuda.h` 的函数原型声明（sync vs async 签名区别）？
   - 当前 `cuda.h` 是 vendored copy（无 async 声明）
   - **倾向更新 vendored cuda.h** 添加 Async 声明
   - **需要确认 cuda.h 维护策略**

5. **OQ5**: g_gpu_client 多线程安全？CLI 模式下 g_gpu_client 在 main thread init，但 test fixture 可能多线程访问？
   - 当前 test fixture 是单线程（test_gpu_phase2 显示）
   - **倾向加锁保护**：g_gpu_client 是全局可变状态
   - **可在 Phase 5+ 加，目前暂不**