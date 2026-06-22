# Change: h2-phase2-implementation

> **状态**: ⚠️ DRAFT SKELETON — 决策点未填，待 review 后激活
> **创建**: 2026-06-19
> **前置依赖**:
> - ✅ `fix-gpu-pushbuffer-va-space-validation` (UsrLinuxEmu, 已完成)
> - ✅ `h1-pushbuffer-validation-closeout` (跨仓 sync, PR #6 已合并)
> - ✅ UsrLinuxEmu ADR-024 Phase 2 Accepted v1
> **关联 SSOT**: `docs/02_architecture/post-refactor-architecture.md` §1.3 (v0.1.5 待加)
> **激活流程**: 本 change 在 UsrLinuxEmu `openspec/changes/` 下创建正式条目

## Why

### 现状

**H-1 closeout (PR #6) 完成后**：

| 侧 | 能力 |
|---|---|
| UsrLinuxEmu (kernel + GPU plugin) | Phase 2 ioctls 全部实现 (`gpu_ioctl.h` line 166-218)<br>+ H-1 PUSHBUFFER 校验链路 (`bf8192f`)<br>+ 4 个测试 (`09ae1b0`)<br>+ ADR-024 Accepted v1 |
| TaskRunner (`GpuDriverClient`) | ✅ H-1 `setCurrentVASpace()` opt-in 透传<br>❌ 无 `createVASpace()` / `destroyVASpace()` / `registerGPU()` / `createQueue()` / `destroyQueue()`<br>❌ 无 VA Space 生命周期管理 |

**调用链断点**：
```
[TaskRunner caller] → setCurrentVASpace(handle)
                          ↓
                     GpuDriverClient 持有 handle
                          ↓
                     submit_batch() 在 ioctl 前填入 args.va_space_handle ✓
                          ↓
                     [UsrLinuxEmu handler] 校验 VA Space 存在 + Queue 归属 ✓
```

**问题**：
- `setCurrentVASpace()` 的 `handle` 从哪来？当前 TaskRunner **无 API 创建 VA Space**
- 调用方要么自己 `ioctl()` 绕过 `GpuDriverClient`（违反封装），要么**等本 change**
- 同步点 S5 (`sync-plan.md` line 265) 标记"⏳ 待发起"——TaskRunner 侧消费链未补齐导致 S5 无法关闭

### Why Now

1. **H-1 已就绪**：校验链路可被触发，但创建 API 缺失形成"读路径走通 / 写路径未铺"的不对称
2. **PRD 2 季度目标**：Phase 2 是 UsrLinuxEmu 路线图的 v0.2 里程碑，TaskRunner 必须配套才能联调
3. **S5 同步点关闭**：本 change 是 S5 在 TaskRunner 侧的产出物
4. **避免再次 ABI 漂移**：越晚开始，TaskRunner 与 UsrLinuxEmu 的 ioctl struct 字段漂移风险越大

## What Changes

### 1. GpuDriverClient 新增 5 个 Phase 2 wrapper 方法

`include/gpu_driver_client.h` 新增（命名待 D3 决定）：

```cpp
// VA Space 管理
uint64_t createVASpace(uint32_t flags);          // 返回 handle
int      destroyVASpace(uint64_t va_space_handle);

// GPU 注册（VA Space → 物理 GPU 绑定）
int      registerGPU(uint64_t va_space_handle, uint32_t gpu_id);

// Queue 管理（绑定到 VA Space）
uint32_t createQueue(uint64_t va_space_handle, uint32_t flags);  // 返回 queue_id
int      destroyQueue(uint32_t queue_id);
```

**每个方法**：零参数检查 + 填充对应 struct + `ioctl()` + 返回 handle 或 -1 (errno via `std::cerr`)。

### 2. 测试覆盖（doctest stub 模式）

`tests/test_cuda_scheduler.cpp` 新增 5 个 test cases：
- `createVASpace_returns_nonzero_handle`
- `destroyVASpace_succeeds_with_valid_handle`
- `registerGPU_succeeds_with_valid_va_space`
- `createQueue_returns_nonzero_id`
- `destroyQueue_succeeds_with_valid_id`

### 3. 同步点 S5 关闭

- `plans/sync-plan.md` line 247-249 改为"✅ 已完成"
- `AGENTS.md` "Phase 1.5 进度" section 增加"S5 ✅ Phase 2 管理 API (2026-06-XX)"

### 4. 跨仓同步（仿 H-1 closeout 模式）

- TaskRunner 仓独立 commit + PR
- UsrLinuxEmu 仓 submodule 指针更新
- UsrLinuxEmu 仓 openspec archive（含 spec.md / .openspec.yaml 等 git tracking）

## Capabilities

### New Capabilities

- `gpu-phase2-management`: 跟踪 TaskRunner GpuDriverClient 的 Phase 2 ioctl wrapper 集。一旦所有 Requirements 满足，本 capability 可归档。

### Modified Capabilities

- **不修改** `gpu-pushbuffer-validation` capability (H-1 主能力) — Phase 2 wrapper 是新维度
- **不修改** `gpu-pushbuffer-validation-deployment` capability (H-1 closeout) — 那是部署层

## Impact

| 影响项 | 范围 | 风险 |
|--------|------|------|
| `GpuDriverClient` 类 | `include/gpu_driver_client.h` 新增 5 方法 | **低**：纯新增，不改既有方法签名 |
| ABI 兼容性 | 既有调用方零影响 | **零**：不删不改既有 API |
| 编译产物 | `libtaskrunner.a` / `test_cuda_scheduler` | **低**：增量编译时间影响小 |
| `gpu_ioctl.h` | 不修改 | **零**：TaskRunner 仅消费，不改 ABI |
| UsrLinuxEmu submodule | 指针更新 + openspec archive | **低**：仿 H-1 closeout 已验证流程 |
| TaskRunner `sync-plan.md` | line 247-249 状态更新 | **零**：纯文档 |

## 开放问题（待 design.md 解决）

1. **D1 - VA Space 生命周期归属**：CudaScheduler / GpuDriverClient / Caller？
2. **D2 - Queue 生命周期**：与 stream 1:1 / 显式 create-destroy？
3. **D3 - 方法命名**：CamelCase / snake_case？
4. **D4 - Handle 存储**：GpuDriverClient 内部 map / 仅返回？
5. **D5 - 默认 VA Space**：构造时自动创建 / 保持 opt-in？

每项决定后，对应 design.md / tasks.md / spec.md 的"TBD"段需同步更新。