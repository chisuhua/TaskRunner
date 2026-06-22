# Change: h3-phase2-management

> **状态**: ⚠️ DRAFT — 骨架已就位，待 review 后激活
> **创建**: 2026-06-19
> **前置依赖**:
> - ⚠️ **H-2.5** `h2-5-architecture-foundation`（**必须先完成** —— 提供 `IGpuDriver` 抽象接口 + MockGpuDriver + CudaScheduler DI）
> - ✅ `fix-gpu-pushbuffer-va-space-validation` (UsrLinuxEmu，已完成)
> - ✅ `h1-pushbuffer-validation-closeout` (跨仓 sync，PR #6 已合并)
> - ✅ UsrLinuxEmu ADR-024 Phase 2 Accepted v1
>
> **关联 SSOT**: `docs/02_architecture/post-refactor-architecture.md` §1.3 (v0.1.5 待加)
> **历史**: 取代 `plans/2026-06-19-h2-phase2-openspec-skeleton/`（DEPRECATED 2026-06-19）
> **激活流程**: 本 change 在 UsrLinuxEmu `openspec/changes/` 下创建正式条目

## Why

### 现状（H-1 closeout 之后）

| 侧 | 能力 |
|---|---|
| UsrLinuxEmu (kernel + GPU plugin) | Phase 2 ioctls 全部实现（`gpu_ioctl.h` line 166-218）<br>+ H-1 PUSHBUFFER 校验链路（`bf8192f`）<br>+ 4 个测试（`09ae1b0`）<br>+ ADR-024 Accepted v1 |
| TaskRunner `GpuDriverClient` | ✅ H-1 `setCurrentVASpace()` opt-in 透传<br>❌ 无 `create_va_space` / `create_queue` / `register_gpu` 等生命周期管理方法 |
| TaskRunner `CudaScheduler` | ✅ 与 `CudaStub` 联调（8 个 doctest 用例 pass）<br>❌ **零** VA Space / Queue 概念（仅依赖 `CudaStub` mock 路径） |
| `GpuDriverClient` ↔ `CudaScheduler` | ❌ **解耦但不连通** — H-2.5 引入 `IGpuDriver` 抽象层后才补齐 |

### 调用链断点

```
[TaskRunner caller] → setCurrentVASpace(handle)  // H-1：handle 从哪来？
                           ↓
                      GpuDriverClient 持有 handle（H-1 内部成员）
                           ↓
                      submit_batch() 填入 args.va_space_handle ✓
                           ↓
                      [UsrLinuxEmu handler] 校验 VA Space + Queue 归属 ✓
```

**Gap 1（写路径缺失）**：`setCurrentVASpace()` 的 `handle` 从哪来？TaskRunner 当前**无 API 创建 VA Space**。调用方要么自己 `ioctl()` 绕过 `GpuDriverClient`（违反封装），要么等本 change。

**Gap 2（架构断点）**：H-2 时期 GpuDriverClient 与 CudaScheduler 是两套互不连通的层。H-2.5 引入 `IGpuDriver` 抽象 + DI 后，两者才在同一接口上对接。

**Gap 3（测试架构）**：现有 `tests/test_cuda_scheduler.cpp` 测试的是 `CudaScheduler + CudaStub`，**不**测试 `GpuDriverClient`。需要新文件 `tests/test_gpu_phase2.cpp`，通过 `IGpuDriver*` 注入 `MockGpuDriver`，**不**触发真实 `/dev/gpgpu0` ioctl。

### Why Now

1. **H-1 已就绪**：校验链路可被触发，但创建 API 缺失形成"读路径走通 / 写路径未铺"的不对称
2. **H-2.5 即将完成**：DI 抽象层就位后，H-3 是其首个真实业务消费者
3. **PRD 2 季度目标**：Phase 2 是 UsrLinuxEmu 路线图的 v0.2 里程碑，TaskRunner 必须配套才能联调
4. **S5 同步点关闭**：本 change 是 S5 在 TaskRunner 侧的产出物
5. **避免再次 ABI 漂移**：越晚开始，TaskRunner 与 UsrLinuxEmu 的 ioctl struct 字段漂移风险越大

## What Changes

### 1. `IGpuDriver` 新增 5 个 Phase 2 方法（H-2.5 抽象 + H-3 实现）

```cpp
// 完整签名见 design.md §5 Method Signatures
class IGpuDriver {
  // ... H-2.5 已定义方法 ...
  virtual uint64_t create_va_space(uint32_t flags) = 0;
  virtual int      destroy_va_space(uint64_t va_space_handle) = 0;
  virtual int      register_gpu(uint64_t va_space_handle, uint32_t gpu_id, uint32_t flags) = 0;
  virtual uint64_t create_queue(uint64_t va_space_handle, uint32_t queue_type,
                                uint32_t priority, uint64_t ring_buffer_size) = 0;
  virtual int      destroy_queue(uint64_t queue_handle) = 0;
};
```

**两个实现**：
- **`GpuDriverClient`**（`src/gpu_driver_client.cpp`）：填 struct + `ioctl()` + 透传 handle / 返回 0/-1
- **`CudaStub`**（`src/cuda_stub.cpp`）：mock 返回递增 handle（u64, monotonic from 1）+ 无 ioctl 调用

### 2. CLI 集成（`src/cmd_cuda.cpp`）

新增 2 个 subcommand：
- `taskrunner cuda_va_space create <flags>` → `g_gpu_client->create_va_space(flags)` → 打印 handle
- `taskrunner cuda_va_space destroy <handle>` → `g_gpu_client->destroy_va_space(handle)`
- `taskrunner cuda_queue create <va_space> <type> <priority> <ring_size>` → `create_queue(...)` → 打印 queue_handle
- `taskrunner cuda_queue destroy <handle>` → `destroy_queue(...)`

修复 H-2.5 之前的"CLI 死调用"问题：`cli_main.cpp` 的 `init_gpu_client()` 始终调用，**不**再有 dead code。

### 3. 测试覆盖（新建 `tests/test_gpu_phase2.cpp`，10 cases）

通过 `IGpuDriver*` 注入 `MockGpuDriver`：

| # | Test Case | 验证 |
|---|-----------|------|
| 1 | `create_va_space_returns_nonzero_handle` | 成功路径：非零 handle |
| 2 | `destroy_va_space_succeeds_with_valid_handle` | 成功路径：0 返回 |
| 3 | `register_gpu_succeeds_with_valid_va_space` | 成功路径：0 返回 |
| 4 | `create_queue_returns_u64_handle` | 成功路径：u64 handle，>= 1 |
| 5 | `destroy_queue_succeeds_with_valid_handle` | 成功路径：0 返回 |
| 6 | `create_va_space_guard_when_closed` | 失败路径：is_open guard |
| 7 | `destroy_va_space_guard_when_handle_zero` | 失败路径：handle=0 守卫 |
| 8 | `register_gpu_guard_when_va_space_zero` | 失败路径：H-1 sentinel 守卫 |
| 9 | `create_queue_guard_when_va_space_zero` | 失败路径：H-1 sentinel 守卫 |
| 10 | `r2_mapping_stream_id_equals_low32_of_queue_handle` | R2 mapping 契约：stream_id == (uint32_t)handle |

**关键不变量**：测试**不**触发真实 `/dev/gpgpu0` ioctl（`MockGpuDriver` 仅记录调用 + 返回 canned values）。

### 4. 同步点 S5 关闭

- `plans/sync-plan.md` line 247-249 改为 "✅ 已完成"
- `AGENTS.md` "Phase 1.5 进度" section 增加 "S5 ✅ Phase 2 管理 API (2026-06-XX)"

### 5. 跨仓同步（仿 H-1 closeout 模式）

- TaskRunner 仓独立 commit + PR
- UsrLinuxEmu 仓 submodule 指针更新
- UsrLinuxEmu 仓 openspec archive（含 spec.md / .openspec.yaml 等 git tracking）

## Capabilities

### New Capabilities

- `gpu-phase2-management`：跟踪 TaskRunner `IGpuDriver` 的 5 个 Phase 2 ioctl wrapper 方法（`create_va_space` / `destroy_va_space` / `register_gpu` / `create_queue` / `destroy_queue`）。一旦所有 9 个 ADDED Requirements 满足，本 capability 可归档。

### Modified Capabilities

- **不修改** `gpu-pushbuffer-validation` capability（H-1 主能力，行为层未变）
- **不修改** `gpu-pushbuffer-validation-deployment` capability（H-1 closeout，部署层）
- **不修改** H-2.5 的 `gpu-driver-architecture` capability（接口形状不变，仅填充方法体）

## Impact

| 影响项 | 范围 | 风险 |
|--------|------|------|
| `IGpuDriver` 抽象（H-2.5 定义） | H-3 在 `GpuDriverClient` / `CudaStub` 上加 5 方法实现 | **低**：纯新增方法，不改既有签名 |
| `GpuDriverClient` 内部 | `current_va_space_handle_` 保留（H-1 兼容）；新增 5 方法（`create_va_space` 等）| **低**：增量字段 + 增量方法 |
| `CudaStub` | 新增 5 mock 方法（递增 handle 返回）| **低**：纯 mock，零 ioctl |
| ABI 兼容性 | 既有调用方（`submit_batch` / `setCurrentVASpace` / `gpu_alloc` 等）零影响 | **零**：不删不改既有 API |
| 编译产物 | `libtaskrunner.a` / `test_cuda_scheduler`（既有 8 case 不变）/ `test_gpu_phase2`（新增 10 case）| **低**：增量编译时间影响小 |
| `gpu_ioctl.h` | 不修改 | **零**：TaskRunner 仅消费，不改 ABI |
| UsrLinuxEmu submodule | 指针更新 + openspec archive | **低**：仿 H-1 closeout 已验证流程 |
| `sync-plan.md` | line 247-249 状态更新 | **零**：纯文档 |
| CLI 入口 | `cli_main.cpp` 的 `init_gpu_client()` 始终被调（不再 dead code）| **低**：H-2.5 修复的副作用自然生效 |

## 交叉引用

- **H-2.5**（前置）: `plans/2026-06-19-h2-5-architecture-foundation/` —— `IGpuDriver` 抽象 + MockGpuDriver + DI
- **H-1**（已就绪）: UsrLinuxEmu `fix-gpu-pushbuffer-va-space-validation` —— VA Space 校验链路
- **H-7 ADR**（deferred）: UsrLinuxEmu owner-flagged 3 issues：
  1. `stream_id` (u32) vs `queue_handle` (u64) 类型不匹配 → 上游需在 `gpu_pushbuffer_args` 增字段或扩 `stream_id` 到 u64
  2. ioctl 路径绕过 `GpuQueueEmu`（不通过 mmap `MAP_QUEUE_RING`）—— 上游架构选择
  3. `attached_queues` 弱校验：跨 VA Space `stream_id` 未被阻断 —— 上游需加强 lookup 路径
  TaskRunner **不**解决上述 3 issue，仅遵守 R2 mapping 契约（`stream_id = LOW32(queue_handle)`）使本侧代码工作。
- **R2 mapping 契约**: 见 `design.md §R2 Mapping Contract` 与 `specs/gpu-phase2-management/spec.md` R6
- **DEPRECATED H-2**（历史）: `plans/2026-06-19-h2-phase2-openspec-skeleton/` —— 本 change 的拆分源
