## Context

### 背景

TaskRunner umd-evolution roadmap Phase 3（[`docs/umd-evolution/roadmap/phase-3-deferred.md`](../../../docs/umd-evolution/roadmap/phase-3-deferred.md)）已满足触发条件 1：UsrLinuxEmu Stage 1.4 Tier-1 + Tier-2 完成 2026-07-04。Phase 3.1（Stream Capture + CUDA Graph）+ Phase 3.2（Memory Pool）需要 TaskRunner 与 UsrLinuxEmu 协同实施。

跨仓协调已完成：
1. TaskRunner 起草 [`cross-repo-prs/2026-07-05-phase3-1-stream-mempool-coordination.md`](../../../superpowers/cross-repo-prs/2026-07-05-phase3-1-stream-mempool-coordination.md)（507 行，§12 记录 11 项决议）
2. UsrLinuxEmu Architecture Team 反馈 [`cross-repo-prs/2026-07-05-usrlx-architecture-review.md`](../../../superpowers/cross-repo-prs/2026-07-05-usrlx-architecture-review.md)（CONDITIONAL ACCEPT，3 BLOCKER + 4 MUST-FIX + 4 NICE）
3. TaskRunner owner 已接受所有 11 项决策并修订 Phase 3.1/3.2 spec
4. UsrLinuxEmu 仓 OpenSpec `2026-07-05-sim-stream-primitive-support/` 已 ✅ **ACCEPTED** + Fix-1~Fix-14 已应用 + docs-audit 43/43 ✅

### 现状盘点

| 模块 | 现状 | 待变更 |
|------|------|--------|
| `include/shared/igpu_driver.hpp` | 31 方法（含 H-2.5 + H-3 + H-3.5）| **+15 方法（Phase 3.1 10 + Phase 3.2 5）** → 46 方法 |
| `src/test_fixture/gpu_driver_client.cpp` | 31 forwarding 方法 | 不变（Step 3 才 override） |
| `src/test_fixture/cuda_stub.cpp` | override 31 方法 | 不变（默认 no-op 兼容） |
| `tests/mock/mock_gpu_driver.cpp` | override 31 方法 | 不变（默认 no-op 兼容） |
| `tools/verify-phase17.sh` | 76+ 测试基线 | 验证脚本不变 |

### 4 步协调顺序（Step 1 范围）

```
Step 0 — 准备（双方）
  ├─ UsrLinuxEmu: OpenSpec ACCEPTED ✅
  └─ TaskRunner: phase3-prep-design-notes.md ACTIVE ✅

🔜 Step 1 — TaskRunner only（**本 change 范围**）
  - IGpuDriver 31 → 46 方法扩展
  - 全部虚函数 + 默认 no-op（非纯虚）
  - 不引用任何 GPU_IOCTL_* #define
  - Commit + push 到 TaskRunner main

⏸ Step 2 — UsrLinuxEmu only（依赖 Step 0 已完成）
  - sim + IOCTL #define + handler + ADR-015 更新
  - target: 2026-07-15

⏸ Step 3 — TaskRunner（依赖 Step 2 IOCTL #define 通过 symlink）
  - GpuDriverClient 15 override + shim 层
  - target: 2026-07-22

⏸ Step 4 — UsrLinuxEmu submodule bump
  - bump external/TaskRunner 到 Step 3 commit
  - target: 2026-07-25
```

## Goals / Non-Goals

### Goals

1. **G1**: IGpuDriver 方法数 31 → 46（10 graph/capture + 5 mempool + 之前 31）
2. **G2**: 15 个新方法**全部**为虚函数 + 默认 no-op（**非纯虚**）— 保证 CudaStub / MockGpuDriver 向后兼容
3. **G3**: 3 个 fence_id-返回方法（`submit_graph` / `mem_pool_alloc_async` / `mem_pool_free_async`）用 `int64_t` 返回类型
4. **G4**: 现有 76+ 测试全过（验证向后兼容）
5. **G5**: `cmake --build build` 无 warning
6. **G6**: 无任何 `GPU_IOCTL_*` #define 引用（保持 Step 1 独立性）
7. **G7**: Commit + push 成功，通知 UsrLinuxEmu owner Step 1 完成

### Non-Goals

1. **NG1**: GpuDriverClient override 实现（属于 Step 3）
2. **NG2**: cuStreamCapture / CUgraph / cuMemPool* shim 层（属于 Step 3）
3. **NG3**: CudaStub / MockGpuDriver 任何修改（默认 no-op 已兼容）
4. **NG4**: TADR-301 文档更新（Step 3 完成后）
5. **NG5**: UsrLinuxEmu IOCTL #define（Step 2 才存在）
6. **NG6**: Phase 3.5 多设备 / Phase 3.2 扩展（set_attr / get_attr / trim）— 后续 Phase
7. **NG7**: 任何外部库依赖新增（保持头文件自包含）

## Decisions

### Decision 1: 所有 15 方法为虚函数 + 默认 no-op（**非纯虚**）

**背景**:Oracle 第一次评审指出原始 "纯虚占位" 描述错误（实际应为非纯虚），否则会破坏 CudaStub / MockGpuDriver 编译（强制 override）。

**方案**:
```cpp
// 正确（非纯虚 + 默认 no-op）
virtual int stream_capture_begin(uint32_t stream_id, uint32_t mode) { return -1; }

// 错误（纯虚 — 破坏向后兼容）
virtual int stream_capture_begin(uint32_t stream_id, uint32_t mode) = 0;
```

**理由**:
- CudaStub（`src/test_fixture/cuda_stub.cpp`）当前 override 31 方法，不强制要求 override 15 新方法
- MockGpuDriver 同理
- Step 3 实施前保持默认 no-op 即可（Phase 3.1/3.2 shim 尚未启用）

### Decision 2: 3 个 fence_id-返回方法用 `int64_t`（非 `int`）

**背景**:UsrLinuxEmu Architecture Team B-3 + F-4 决策：
- HAL fence_id 范围 `[1, (1<<32)-1]`
- sim fence_id 范围 `[(1<<32), INT64_MAX]`
- 错误约定：`<0` = 负 errno

**方案**:`submit_graph` / `mem_pool_alloc_async` / `mem_pool_free_async` 返回 `int64_t`，其他 12 个方法返回 `int`。

**理由**:
- `int`（32-bit signed）无法容纳 `INT64_MAX`，必须 `int64_t`
- shim 层 GpuDriverClient override 不 cast 到 `int`（保持负 errno 原样）
- 现有 `submit_batch` / `submit_memcpy` / `submit_launch` 已用 `int64_t`（H-2.5 一致）

### Decision 3: 不引用任何 UsrLinuxEmu IOCTL #define（Step 1 独立性）

**背景**:Step 1 是 TaskRunner 单独实施，不依赖 UsrLinuxEmu 任何代码。如果引入 `GPU_IOCTL_*` #define 引用，Step 1 必须等待 Step 2 的 IOCTL #define merge 才能编译。

**方案**:本 change 仅扩展 IGpuDriver 接口（纯虚函数 + 默认 no-op），不涉及 IOCTL 转发。

**理由**:
- Step 1 → Step 2 顺序的关键：Step 1 不依赖 Step 2，Step 2 不依赖 Step 1（并行）
- Step 3 才依赖 Step 2 的 IOCTL #define（通过 symlink 访问）

### Decision 4: commit message 命名规范

**方案**:
```
feat(igpu_driver): 15-method no-op extension for Phase 3.1/3.2 (per 4-step coordination Step 1)
```

**理由**:
- `feat(igpu_driver)` — 符合 UsrLinuxEmu / TaskRunner AGENTS.md 命名约定（"功能 + scope"）
- "15-method no-op extension" — 明确方法数 + 默认实现类型
- "(per 4-step coordination Step 1)" — 关联协调 PR（便于 grep 检索）

### Decision 5: 不更新 TADR-301 在本 change

**背景**:TADR-301（IGpuDriver 31 方法契约）应扩展为 46 方法文档，但这是 **Step 3 后的清理工作**，不阻塞 Step 1。

**方案**:本 change 不修改 TADR-301；Step 3 完成后单独 TADR 更新。

**理由**:
- Step 1 焦点：IGpuDriver 接口扩展（实现）
- Step 3 完成后焦点：文档同步（TADR-301 + cross-repo PR 引用）
- 避免 Step 1 范围蔓延

## 详细技术设计

### IGpuDriver 31 → 46 方法完整映射

**保留 31 方法**（H-2.5 + H-3 + H-3.5）：不在本 change 范围内修改。

**新增 15 方法**：

#### Phase 3.1 Stream Capture + Graph（10 方法）

| # | 方法签名 | 默认实现 | 决策来源 |
|---|----------|---------|---------|
| 1 | `stream_capture_status(uint32_t stream_id, uint32_t* status_out)` | `return -1;` | Oracle C1 + F-4 |
| 2 | `stream_capture_begin(uint32_t stream_id, uint32_t mode)` | `return -1;` | F-1 (GLOBAL only) |
| 3 | `stream_capture_end(uint32_t stream_id, uint64_t* graph_handle_out)` | `return -1;` | F-4 |
| 4 | `graph_create(uint64_t* graph_handle_out)` | `return -1;` | Oracle C1 |
| 5 | `graph_destroy(uint64_t graph_handle)` | `return -1;` | Oracle C1 |
| 6 | `graph_add_kernel_node(uint64_t graph_handle, uint32_t kernel_index, grid, block, uint64_t kernargs_bo_handle)` | `return -1;` | F-3 (kernargs=0) |
| 7 | `graph_add_memcpy_node(uint64_t graph_handle, uint64_t src_va, uint64_t dst_va, uint64_t size, uint32_t is_h2d)` | `return -1;` | Oracle C1 |
| 8 | `graph_instantiate(uint64_t graph_handle, uint64_t* exec_handle_out)` | `return -1;` | Oracle C1 |
| 9 | **`submit_graph(uint64_t graph_exec_handle, uint32_t stream_id)` → `int64_t`** | `return -1;` | **B-3** (fence_id 范围) + **F-4** (int64_t 约定) |
| 10 | `destroy_graph_exec(uint64_t graph_exec_handle)` | `return -1;` | Oracle C1 |

#### Phase 3.2 Memory Pool（5 方法）

| # | 方法签名 | 默认实现 | 决策来源 |
|---|----------|---------|---------|
| 11 | `mem_pool_create(uint64_t va_space_handle, uint64_t size, uint32_t flags, uint64_t* pool_handle_out)` | `return -1;` | **B-2** (Option B VA 子范围) |
| 12 | `mem_pool_destroy(uint64_t pool_handle)` | `return -1;` | — |
| 13 | `mem_pool_alloc(uint64_t pool_handle, uint64_t size, uint64_t* va_out)` | `return -1;` | — |
| 14 | **`mem_pool_alloc_async(uint64_t pool_handle, uint64_t size, uint32_t stream_id, uint64_t* va_out)` → `int64_t`** | `return -1;` | **B-3** + **F-4** |
| 15 | **`mem_pool_free_async(uint64_t va, uint32_t stream_id)` → `int64_t`** | `return -1;` | **B-3** + **F-4** |

### 与现有方法的兼容性

**虚函数表扩展语义**:
- C++ 虚函数表在每个类对象中按声明顺序排列
- 新增虚函数**追加**到表末尾（vtable reordering 由编译器管理）
- 现有 override 索引**不变**（CudaStub / MockGpuDriver 已 override 的方法索引保持）
- 旧编译产物（无 15 新方法）与新编译产物（无 override）的二进制兼容性：

| 场景 | CudaStub | IGpuDriver* driver = new CudaStub() | 行为 |
|------|----------|-----------------------------------|------|
| 旧 CudaStub（31 override）+ 新 IGpuDriver | CudaStub override 31 方法 | vtable[31..45] → IGpuDriver 默认 `return -1;` | 兼容 |
| 新 CudaStub（46 override）+ 旧 IGpuDriver | 不可能（Step 1 必须在 Step 3 之前） | — | — |

**结论**: Step 1 不破坏现有 CudaStub / MockGpuDriver 编译和运行。

### 错误约定（来自 UsrLinuxEmu F-4）

**`int64_t` 返回方法**（3 个）：
- `< 0` → 负 errno（如 `-EINVAL`、`-ENOSPC`）
- `>= (1 << 32)` → 有效 fence_id
- `0` → success 但无 fence（罕见）
- `-1` → not implemented（默认 no-op）

**`int` 返回方法**（12 个）：
- `0` → 成功
- `-1` → 失败 / not implemented
- 其他正数 → handle / status

## 风险与缓解

| 风险 | 概率 | 影响 | 缓解 |
|------|------|------|------|
| 编译破坏（CudaStub override 不匹配） | 极低 | 中 | 虚函数追加到 vtable 末尾，不影响现有 override 索引 |
| 测试回归 | 极低 | 中 | 默认 no-op 实现，不改变现有行为路径 |
| Step 1 与 Step 2 顺序冲突 | 零 | 零 | 不引用 `GPU_IOCTL_*` #define（Decision 3）|
| GpuDriverClient override 失败 | 零 | 零 | Step 1 不修改 GpuDriverClient（Step 3 才 override） |
| 头文件 ABI 不兼容 | 极低 | 中 | 仅追加虚函数（vtable 扩展）；现有 CudaStub 二进制仍可调用 |
| 命名冲突（与其他 workstream） | 低 | 低 | 命名空间 `async_task::gpu` 内，无外部冲突 |

## Out of Scope（重申）

参见 [`proposal.md`](proposal.md) §"Out of Scope" 段。

## References

- [`../proposal.md`](../proposal.md) — 本 change 提案
- [`../tasks.md`](../tasks.md) — 详细任务清单
- [`../specs/igpu-driver-extension/spec.md`](../specs/igpu-driver-extension/spec.md) — Capability 规格
- [`../../../superpowers/specs/2026-07-02-phase3-stream-capture-design.md`](../../../superpowers/specs/2026-07-02-phase3-stream-capture-design.md) — Phase 3.1 设计
- [`../../../superpowers/specs/2026-07-02-phase3-mempool-design.md`](../../../superpowers/specs/2026-07-02-phase3-mempool-design.md) — Phase 3.2 设计
- [`../../../superpowers/cross-repo-prs/2026-07-05-phase3-1-stream-mempool-coordination.md`](../../../superpowers/cross-repo-prs/2026-07-05-phase3-1-stream-mempool-coordination.md) — 协调 PR（507 行）
- [`../../../superpowers/cross-repo-prs/2026-07-05-usrlx-architecture-review.md`](../../../superpowers/cross-repo-prs/2026-07-05-usrlx-architecture-review.md) — UsrLinuxEmu Architecture Team 反馈