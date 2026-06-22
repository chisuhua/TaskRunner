# h2-5-architecture-foundation (⚠️ DRAFT SKELETON)

> **⚠️ 状态**: **DRAFT** — 骨架已就位，待 review 后激活
> **创建**: 2026-06-19
> **创建者**: TaskRunner 侧 H-2.5 协调发起
> **前置依赖**: **无**（本 change 是基础，后续 H-3 依赖本 change 完成）
> **历史**: 取代 `plans/2026-06-19-h2-phase2-openspec-skeleton/`（DEPRECATED 2026-06-19，拆分自原 H-2；拆分依据：Path D 重构优先 —— "slow is OK, architecture debt is not acceptable"）

## What This Is

TaskRunner 当前存在两层**解耦但不连通**的 GPU 抽象：
- `GpuDriverClient`（System C ioctl 封装）在 `include/gpu_driver_client.h` —— 运行时**实际是 dead code**（CLI 入口 `init_gpu_client()` 从未被调用，`g_gpu_client` 全程为 `nullptr`）
- `CudaStub`（CUDA Driver API 封装）在 `include/cuda_stub.hpp` `namespace taskrunner` —— 零 VA Space / Queue 概念

本 change 构建**统一抽象层** `IGpuDriver`（已起草于 `include/igpu_driver.hpp`，311 行 28 个纯虚方法），让 `CudaScheduler` 等下游消费者透明地使用任一后端：
- `GpuDriverClient` 实现 `IGpuDriver`（真实 ioctl 路径）
- `CudaStub` 迁入 `async_task::gpu` 命名空间 + 实现 `IGpuDriver`（mock 路径）
- `CudaScheduler` 接受 `IGpuDriver*` 注入（DI）
- `MockGpuDriver` 测试夹具（单测注入）
- CLI `init_gpu_client()` 死调用修复

## What's NOT In Scope

- **不**实现 5 个 Phase 2 ioctl wrapper 方法（`create_va_space` / `destroy_va_space` / `register_gpu` / `create_queue` / `destroy_queue`）—— 属于 H-3 范围
- **不**改 UsrLinuxEmu 侧 ioctl ABI（TaskRunner 仅消费 `GPU_IOCTL_*`）
- **不**实现多 GPU / peer-to-peer 场景（属于 Phase 3+）
- **不**实现 mmap 快速路径（`MAP_QUEUE_RING`）
- **不**改 `gpu_ioctl.h` / `gpu_types.h`（上游 SSOT，TaskRunner 通过符号链接访问）
- **不**重命名 H-1 既有 `setCurrentVASpace()` / `getCurrentVASpace()`（保持向后兼容，仅加 snake_case alias）

## 关键决策（D6-D11 已 FINALIZED）

| ID | 问题 | **最终决策** |
|---|---|---|
| **D6** | `alloc_bo` 签名 | `(size, flags) → u64`（2 参数）—— `domain` 折入 `flags`（domain bit + flag bit 复用）；`gpu_va` 通过 `map_bo()` 单独获取 |
| **D7** | `free_bo` handle 类型 | `uint64_t bo_handle` —— u32→u64 拓宽，与 H-3 Phase 2 handle 一致 |
| **D8** | `map_bo` 签名 | `(handle, size) → void*`（2 参数，返回 CPU 指针）—— 重塑：返回值替代原输出参数 `*gpu_va`；`flags` 字段被移除 |
| **D9** | `CudaStub` 命名空间 | `async_task::gpu` —— **迁移** 到统一命名空间（**不**用 adapter 模式） |
| **D10** | `CudaScheduler` DI | `CudaScheduler(IGpuDriver* driver = nullptr)` —— 接受 `IGpuDriver*` 注入，`nullptr` 时自动 `new CudaStub()`（向后兼容） |
| **D11** | CLI 死调用修复 | `cli_main.cpp` 调用 `init_gpu_client()` 启动 + `shutdown_gpu_client()` 退出 —— `g_gpu_client` 不再恒为 `nullptr` |

> ⚠️ D6/D7/D8 **改变** `GpuDriverClient` 现有签名（`include/gpu_driver_client.h` line 240/264/278/304）—— 既有调用方（`src/cuda_scheduler.cpp` / `src/cmd_cuda.cpp`）需同步适配。

## 既有约束

- **H-1 closeout pattern**: 跨仓同步遵循 `openspec/changes/archive/2026-06-17-h1-pushbuffer-validation-closeout/` 模式（D1: 直接 commit 默认 / 留指针备选；D3: archive `git add` 不重新生成；D5: 关联 task 合并 1 commit）
- **snake_case 命名**: H-1 既有 `setCurrentVASpace()` / `getCurrentVASpace()`（CamelCase）保留作 deprecated alias；新方法统一 snake_case
- **D9 命名空间迁移**: `taskrunner::CudaStub` → `async_task::gpu::CudaStub` —— 旧命名空间声明 `namespace taskrunner { namespace gpu = async_task::gpu; using CudaStub = gpu::CudaStub; }` 作为 1 个 release 的过渡

## 文件清单

```
plans/2026-06-19-h2-5-architecture-foundation/
├── README.md                            # 本文件（DRAFT 入口）
├── .openspec.yaml                       # openspec 元数据（status: DRAFT）
├── proposal.md                          # Why + What + Capabilities + Impact
├── design.md                            # How + D6-D11 决策 + 风险 + Migration
├── tasks.md                             # 实施步骤 checklist
└── specs/gpu-driver-architecture/
    └── spec.md                          # 8 ADDED Requirements + Scenarios
```

## 激活流程

1. 召集 review：TaskRunner owner + UsrLinuxEmu owner
2. `mv plans/2026-06-19-h2-5-architecture-foundation /workspace/project/UsrLinuxEmu/openspec/changes/h2-5-architecture-foundation`
3. 移除本 README 的 ⚠️ DRAFT 标记 + 更新 `.openspec.yaml` 的 `status: DRAFT` → `status: ACTIVE`
4. 在 UsrLinuxEmu 仓 commit + 发起 PR
5. 实施完成后，**H-3** `plans/2026-06-19-h3-phase2-openspec-skeleton/` 才可激活

## 历史与交叉引用

- **DEPRECATED H-2**: `plans/2026-06-19-h2-phase2-openspec-skeleton/`（2026-06-19 弃用，拆分依据：Path D 重构优先）
- **H-3（下游消费者）**: `plans/2026-06-19-h3-phase2-openspec-skeleton/` —— **本 change 完成前 H-3 不可激活**
- **H-1 closeout**（参考格式）: UsrLinuxEmu `openspec/changes/archive/2026-06-17-h1-pushbuffer-validation-closeout/`
- **IGpuDriver 已起草**: `include/igpu_driver.hpp`（311 行，2026-06-22）
- **SSOT**: `docs/02_architecture/post-refactor-architecture.md` §1.3 (v0.1.5 待加)