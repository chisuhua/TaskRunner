# Change: phase3-1-igpu-driver-extension

> **状态**: 🔄 PROPOSED（2026-07-05，TaskRunner owner 起草，等待启动）
> **创建**: 2026-07-05
> **基础**: UsrLinuxEmu `openspec/changes/2026-07-05-sim-stream-primitive-support/` 已 ✅ **ACCEPTED**（Fix-1~Fix-14 已应用）+ Phase 3 prep design notes 已 ACTIVE
> **Scope**: 单 workstream（Step 1 — 仅 IGpuDriver 接口扩展）
> **依赖**: 无前置变更（纯 IGpuDriver 内部扩展，不引用任何 `GPU_IOCTL_*` #define）

## Why

### 现状

TaskRunner umd-evolution roadmap Phase 3（[`docs/umd-evolution/roadmap/phase-3-deferred.md`](../../../../docs/umd-evolution/roadmap/phase-3-deferred.md)）触发条件 1 已满足（Stage 1.4 完成 2026-07-04）。Phase 3.1/3.2 跨仓协调已完成 UsrLinuxEmu Architecture Team 审查（3 BLOCKER + 4 MUST-FIX + 4 NICE 全部接受），进入 4 步协调实施阶段。

**本 change 是 Step 1**（4 步协调的第一步）：

```
Step 1 (TaskRunner only, 不依赖 IOCTL #define)
Step 2 (UsrLinuxEmu only, 不依赖 TaskRunner)  ← 2026-07-15
Step 3 (TaskRunner, 依赖 Step 2)
Step 4 (UsrLinuxEmu submodule bump)            ← 2026-07-25
```

### Gap

当前 `include/shared/igpu_driver.hpp` 含 **31 方法**（不含析构；3 lifecycle + 1 fd + 8 device info + 4 BO + 3 submit + 2 fence + 2 VA Space + 5 H-3 Phase 2 + 3 H-3.5）。

Phase 3.1（Stream Capture + Graph）需要 **10 新方法**：
- 3 stream_capture (status / begin / end)
- 5 graph (create / destroy / add_kernel_node / add_memcpy_node / instantiate)
- 1 graph launch (submit_graph, 返回 `int64_t` fence_id)
- 1 graph exec destroy

Phase 3.2（Memory Pool）需要 **5 新方法**：
- 2 pool lifecycle (create / destroy)
- 1 sync alloc (mem_pool_alloc)
- 2 async ops (mem_pool_alloc_async / mem_pool_free_async, 返回 `int64_t` fence_id)

**总计 15 新方法**（IGpuDriver 31 → 46）。

### Why Now

1. **UsrLinuxEmu OpenSpec ACCEPTED**（Fix-1~Fix-14 已应用）— Step 2 的 IOCTL #define 已就绪在 UsrLinuxEmu 仓（待 merge）。
2. **本 change 不依赖任何 UsrLinuxEmu IOCTL #define**（关键！避免 Step 2/Step 1 顺序冲突）。
3. **4 步协调要求 Step 1 必须先 merge**（Step 3 才引用 Step 2 的 IOCTL #define）。
4. **估时极低**（30 min 内编辑 + 测试 + push），高 ROI 启动跨仓实施链路。
5. **风险最低**（所有 15 个方法均为虚函数 + 默认 no-op，**非纯虚** — 向后兼容保证）。

### Why This Is The Only Workstream

本 change 严格限定于 **IGpuDriver 接口扩展**（不实施）：
- ❌ 不写 GpuDriverClient override（属于 Step 3）
- ❌ 不写 shim 层 cuStreamCapture/CUgraph/cuMemPool*（属于 Step 3）
- ❌ 不引用 `GPU_IOCTL_*` #define（Step 2 才存在）
- ❌ 不修改 CudaStub / MockGpuDriver（默认 no-op 已兼容）

## What Changes

### 1. `include/shared/igpu_driver.hpp` — IGpuDriver 接口扩展（核心变更）

**31 → 46 方法**（+15 新方法，全部虚函数 + 默认 no-op + 非纯虚）。

**10 Phase 3.1 方法**（Stream Capture + Graph）：

```cpp
// ============================================================
// Phase 3.1 Stream Capture / Graph (10, 虚函数 默认 no-op, 非纯虚)
// ============================================================
//
// 决策来源（UsrLinuxEmu Architecture Team 2026-07-05 反馈）：
// - F-1 capture mode 仅接受 GLOBAL
// - B-3 fence_id 范围划分（HAL [1, 1<<32-1] + sim [1<<32, INT64_MAX]）
// - F-3 kernargs_bo_handle=0 表示无 kernargs BO
// - F-4 int64_t 返回约定（<0 = errno, >= (1<<32) = valid fence_id）

/**
 * @brief Query stream capture status (Phase 3.1)
 */
virtual int stream_capture_status(uint32_t stream_id, uint32_t* status_out) { return -1; }

/**
 * @brief Begin stream capture (Phase 3.1)
 * @note Only SIM_CAPTURE_MODE_GLOBAL (0) accepted by sim; other modes return -EINVAL
 */
virtual int stream_capture_begin(uint32_t stream_id, uint32_t mode) { return -1; }

/**
 * @brief End stream capture, return graph handle (Phase 3.1)
 */
virtual int stream_capture_end(uint32_t stream_id, uint64_t* graph_handle_out) { return -1; }

/**
 * @brief Create empty CUDA graph (Phase 3.1)
 */
virtual int graph_create(uint64_t* graph_handle_out) { return -1; }

/**
 * @brief Destroy CUDA graph (Phase 3.1)
 */
virtual int graph_destroy(uint64_t graph_handle) { return -1; }

/**
 * @brief Add kernel-launch node to graph (Phase 3.1)
 * @note kernargs_bo_handle == 0 means no kernargs BO (no validation)
 */
virtual int graph_add_kernel_node(uint64_t graph_handle, uint32_t kernel_index,
                                   uint32_t grid_x, uint32_t grid_y, uint32_t grid_z,
                                   uint32_t block_x, uint32_t block_y, uint32_t block_z,
                                   uint64_t kernargs_bo_handle) { return -1; }

/**
 * @brief Add memcpy node to graph (Phase 3.1)
 */
virtual int graph_add_memcpy_node(uint64_t graph_handle,
                                   uint64_t src_va, uint64_t dst_va,
                                   uint64_t size, uint32_t is_h2d) { return -1; }

/**
 * @brief Instantiate graph into executable (Phase 3.1)
 */
virtual int graph_instantiate(uint64_t graph_handle, uint64_t* exec_handle_out) { return -1; }

/**
 * @brief Launch instantiated graph (Phase 3.1)
 * @return fence_id (>= 1<<32 = valid), <0 = negative errno, -1 = not implemented
 */
virtual int64_t submit_graph(uint64_t graph_exec_handle, uint32_t stream_id) { return -1; }

/**
 * @brief Destroy graph executable (Phase 3.1)
 */
virtual int destroy_graph_exec(uint64_t graph_exec_handle) { return -1; }
```

**5 Phase 3.2 方法**（Memory Pool）：

```cpp
// ============================================================
// Phase 3.2 Memory Pool (5, 虚函数 默认 no-op, 非纯虚)
// ============================================================
//
// 决策来源（UsrLinuxEmu Architecture Team 2026-07-05 反馈）：
// - B-2 Pool VA 范围采用 Option B（VA 子范围预留）
// - F-2 attr value blob 布局（不在本 change 范围，Step 3 处理）

/**
 * @brief Create memory pool (Phase 3.2)
 * @note UsrLinuxEmu B-2: pool reserves maxSize VA sub-range via sim_mem_pool_props_t
 */
virtual int mem_pool_create(uint64_t va_space_handle, uint64_t size,
                             uint32_t flags, uint64_t* pool_handle_out) { return -1; }

/**
 * @brief Destroy memory pool (Phase 3.2)
 */
virtual int mem_pool_destroy(uint64_t pool_handle) { return -1; }

/**
 * @brief Synchronous allocation from pool (Phase 3.2)
 */
virtual int mem_pool_alloc(uint64_t pool_handle, uint64_t size,
                            uint64_t* va_out) { return -1; }

/**
 * @brief Asynchronous allocation from pool (Phase 3.2)
 * @return fence_id (>= 1<<32 = valid), <0 = negative errno, -1 = not implemented
 */
virtual int64_t mem_pool_alloc_async(uint64_t pool_handle, uint64_t size,
                                      uint32_t stream_id, uint64_t* va_out) { return -1; }

/**
 * @brief Asynchronous free (Phase 3.2)
 * @return fence_id (>= 1<<32 = valid), <0 = negative errno, -1 = not implemented
 */
virtual int64_t mem_pool_free_async(uint64_t va, uint32_t stream_id) { return -1; }
```

### 2. 不变更的文件（显式排除）

| 文件 | 原因 |
|------|------|
| `src/test_fixture/gpu_driver_client.cpp` | GpuDriverClient override 属于 **Step 3**（依赖 Step 2 IOCTL #define）|
| `src/umd/libcuda_shim/cu_graph.cpp` / `cu_graph_node.cpp` / `cu_graph_exec.cpp` | shim 层属于 **Step 3** |
| `src/umd/libcuda_shim/cu_stream.cpp` | shim 层属于 **Step 3** |
| `src/umd/libcuda_shim/cu_mem_pool.cpp` | shim 层属于 **Step 3** |
| `src/test_fixture/cuda_stub.cpp` | CudaStub 默认 no-op 兼容，**不**修改 |
| `tests/mock/mock_gpu_driver.cpp` | MockGpuDriver 默认 no-op 兼容，**不**修改 |
| `include/shared/tadr-301-igpu-driver-contract.md` | TADRs 在 Step 3 完成后更新（不阻塞 Step 1）|

## Capabilities

### New Capabilities

- `igpu-driver-phase3-extension`（**TaskRunner internal**）:
  - IGpuDriver 新增 15 方法（10 graph/capture + 5 mempool）
  - 所有方法为虚函数 + 默认 no-op（向后兼容 CudaStub / MockGpuDriver）
  - 3 个 fence_id-返回方法用 `int64_t`（遵循 UsrLinuxEmu B-3 + F-4）

### Modified Capabilities

- `igpu-driver-abstraction`（[ADR-032 + TADR-301](https://github.com/chisuhua/TaskRunner)）:
  - 方法数 31 → 46（Phase 3.1 10 + Phase 3.2 5 + 之前 31）
  - TADR-301 文档**不**在 Step 1 修改（Step 3 完成后更新）

## Impact

| 影响项 | 范围 | 风险 |
|--------|------|------|
| 代码 | `igpu_driver.hpp` +15 方法（~80 行含注释） | 极低 |
| 头文件 ABI | 新增虚函数（默认实现） | **零**（虚函数表扩展，旧代码不重新编译仍可工作） |
| 现有 76+ 测试 | 全部应通过（向后兼容验证） | 极低 |
| 编译 | 无新依赖，无新警告 | 极低 |
| 跨仓 | 无（不引用 UsrLinuxEmu 任何符号） | 零 |

**风险缓解**:
- 所有 15 方法**非纯虚**+ 默认 `return -1`（vs 纯虚 `= 0`）— 即使子类不 override 也能编译运行
- Step 1 不引用任何 UsrLinuxEmu IOCTL #define（避免顺序冲突）
- 测试基线：`tests/test_cuda_scheduler` + `tests/test_gpu_architecture` + `tests/test_gpu_phase2` 76+ cases 全过
- 实施前可单独编译 `make gpu_sim` 验证 CudaStub 兼容

## Tasks

> 详细任务清单见 [`tasks.md`](tasks.md)

高层分解：
1. **T1** 编辑 `igpu_driver.hpp` 加 15 方法 + 注释（含 F-1/B-3/F-3/F-4 决策引用）
2. **T2** 编译验证 `cmake --build build` 无 warning
3. **T3** 跑现有 76+ 测试，全 PASS
4. **T4** git commit + push 到 TaskRunner `origin/main`
5. **T5** 通知 UsrLinuxEmu owner Step 1 完成 → 启动 Step 2

## Acceptance Criteria

本 change 在以下条件满足时视为 COMPLETE：

- [ ] IGpuDriver 方法数 31 → 46（验证：`grep -c "^    virtual " include/shared/igpu_driver.hpp` = 47 含析构）
- [ ] 15 个新方法**全部非纯虚**+ 默认 `return -1`（验证：grep 无 `= 0;` 在新方法中）
- [ ] 3 个 fence_id-返回方法用 `int64_t`（验证：`submit_graph` / `mem_pool_alloc_async` / `mem_pool_free_async`）
- [ ] 无任何对 `GPU_IOCTL_*` #define 的引用（验证：`grep "GPU_IOCTL" include/shared/igpu_driver.hpp` 应为空或仅含注释）
- [ ] 现有 76+ 测试全 PASS（`./build/test_*`）
- [ ] `cmake --build build` 无 warning
- [ ] Commit + push 成功（`git log -1 -- include/shared/igpu_driver.hpp`）
- [ ] UsrLinuxEmu owner 收到 Step 1 完成通知

## Out of Scope（显式排除 — Step 3）

- GpuDriverClient override 实现（15 forwarding 方法）
- cuStreamCapture/CUgraph/cuMemPool* shim 层
- CudaStub / MockGpuDriver override（保持默认 no-op）
- TADR-301 文档更新（Step 3 后）
- UsrLinuxEmu IOCTL #define（Step 2 才存在）

## Launch Conditions

LC1 — UsrLinuxEmu OpenSpec ACCEPTED ✅ 已满足（2026-07-05）
LC2 — Phase 3 prep design notes ACTIVE ✅ 已满足（2026-07-05）
LC3 — 无前置 commit 阻塞 ✅（直接基于 TaskRunner `ba16139` HEAD）

无其他 LC。

## Sync Protocol Acknowledgment

按 `ADR-035 §Rule 5.1` 4 步同步流程：

1. **TaskRunner**: git add + commit + push（Step 1 commit）
2. **UsrLinuxEmu**: bump `external/TaskRunner` submodule 指针（**不需要** — Submodule bump 仅在 Step 4）
3. **UsrLinuxEmu**: Step 2 merge（sim + IOCTL）— 等待 Step 1 完成
4. **TaskRunner**: Step 3 PR（依赖 Step 2）

## Related Changes

- **TaskRunner docs**:
  - [`docs/superpowers/specs/2026-07-02-phase3-stream-capture-design.md`](../../superpowers/specs/2026-07-02-phase3-stream-capture-design.md) — Phase 3.1 设计（405 行，含 B-1/B-3/F-1/F-3/F-4 决策）
  - [`docs/superpowers/specs/2026-07-02-phase3-mempool-design.md`](../../superpowers/specs/2026-07-02-phase3-mempool-design.md) — Phase 3.2 设计（269 行，含 B-2 决策）
  - [`docs/superpowers/plans/2026-07-02-phase3-prep-design-notes.md`](../../superpowers/plans/2026-07-02-phase3-prep-design-notes.md) — ACTIVE（2026-07-05 promoted）
  - [`docs/superpowers/cross-repo-prs/2026-07-05-phase3-1-stream-mempool-coordination.md`](../../superpowers/cross-repo-prs/2026-07-05-phase3-1-stream-mempool-coordination.md) — 协调 PR（507 行，§12 记录 11 项决议）
  - [`docs/superpowers/cross-repo-prs/2026-07-05-usrlx-architecture-review.md`](../../superpowers/cross-repo-prs/2026-07-05-usrlx-architecture-review.md) — UsrLinuxEmu Architecture Team 反馈（CONDITIONAL ACCEPT → 已 ack 全部 11 项）

- **UsrLinuxEmu docs**（外部）:
  - `openspec/changes/2026-07-05-sim-stream-primitive-support/` — ✅ ACCEPTED + Fix-1~Fix-14
  - `openspec/changes/archive/2026-07-02-taskrunner-umd-backend-enable/` — Phase 1.5 Stretch 协调模板

## 关联 ADR

- [ADR-032 H-2.5 IGpuDriver 抽象层](https://github.com/chisuhua/TaskRunner)（UsrLinuxEmu 侧）— 保持单一抽象边界
- [ADR-035 治理规则](https://github.com/chisuhua/TaskRunner) — 跨仓同步协议
- [TADR-301 IGpuDriver 31 方法契约](../../shared/adr/tadr-301-igpu-driver-contract.md) — 当前基线（Step 3 后扩展为 46 方法）
- [TADR-109 H-3.5 生命周期扩展](../../test-fixture/adr/tadr-109-igpu-driver-uniform-scheduling.md) — 同样模式（非纯虚 + 默认 no-op）

## 决策编号（TaskRunner spec 系统）

本 change 实现以下决策编号：

- **D-SC-5** Capture mode 限制（GLOBAL only）
- **D-SC-9** GpuQueueEmu API 集成路径（`submit(uint64_t, uint32_t)`）
- **D-SC-11** fence_id 范围划分（HAL `[1, 1<<32-1]` + sim `[1<<32, INT64_MAX]`）
- **D-SC-12** kernargs_bo_handle=0 语义
- **D-MP-1** Pool VA 范围采用 Option B（VA 子范围预留）