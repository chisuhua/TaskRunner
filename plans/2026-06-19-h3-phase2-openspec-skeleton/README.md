# h3-phase2-management (⚠️ DRAFT SKELETON)

> **⚠️ 状态**: **DRAFT** — 骨架已就位，待 review 后激活
> **创建**: 2026-06-19
> **创建者**: TaskRunner 侧 H-3 协调发起
> **前置依赖**: **H-2.5** `h2-5-architecture-foundation` 必须先完成（IGpuDriver 抽象接口）
> **历史**: 取代 `plans/2026-06-19-h2-phase2-openspec-skeleton/`（DEPRECATED 2026-06-19，拆分自原 H-2）

## What This Is

在 H-2.5 提供的 `IGpuDriver` 抽象接口之上，**真实实现** 5 个 Phase 2 ioctl wrapper：

- `create_va_space(flags) → u64`
- `destroy_va_space(handle) → int`
- `register_gpu(va_space_handle, gpu_id, flags) → int`
- `create_queue(va_space_handle, queue_type, priority, ring_buffer_size) → u64`
- `destroy_queue(queue_handle) → int`

这 5 个方法**消费** UsrLinuxEmu 侧已就绪的 Phase 2 ioctl（`gpu_ioctl.h` line 166-218），补齐 TaskRunner 侧 VA Space + Queue 生命周期管理 API。

## What's NOT In Scope

- **不**创建 `IGpuDriver` 抽象接口本身（属于 H-2.5 范围）
- **不**改既有 `submit_batch()` / `setCurrentVASpace()` 签名（H-1 已稳定）
- **不**改 UsrLinuxEmu 侧 ioctl ABI（TaskRunner 仅消费）
- **不**实现 mmap 快速路径（`MAP_QUEUE_RING`）—— TaskRunner 走 ioctl 路径即可
- **不**实现多 GPU / peer-to-peer 场景（属于 Phase 3+）
- **不**修改上游 3 个 owner-flagged issue（流式 ID 类型不匹配 / ioctl 绕过 GpuQueueEmu / attached_queues 弱校验）—— 全部 defer 给 **UsrLinuxEmu H-7 ADR**

## 关键决策（D1-D5 已 FINALIZED）

| ID | 问题 | **最终决策** |
|---|---|---|
| **D1** | VA Space handle 由谁持有 | **C. Caller owns** — `create_va_space()` 返回 u64 handle；`GpuDriverClient` **无** 内部 `current_va_space_handle_`；caller（CudaScheduler / CLI）持有 handle 并在 submit 前通过 `setCurrentVASpace()` 透传 |
| **D2** | Queue 生命周期 | **B. Explicit create-destroy** — `create_queue()` 返回 u64 `queue_handle`；caller 管理生命周期；不与 stream_id 隐式绑定 |
| **D3** | 方法命名风格 | **B. snake_case** — `create_va_space` / `destroy_va_space` / `register_gpu` / `create_queue` / `destroy_queue`（与 `submit_memcpy` / `gpu_alloc` 等 main 方法一致；H-1 的 CamelCase 是例外） |
| **D4** | Handle 存储 | **B. Return only** — driver 不在内部维护 map；只把 ioctl 返回值透传给 caller |
| **D5** | 默认 VA Space | **B. opt-in** — `GpuDriverClient` 构造时**不**自动创建 VA Space；`current_va_space_handle_` 默认 0（保留 H-1 sentinel 跳过校验路径） |

> ⚠️ D1 决策**改变**了 H-1 的设计：H-1 时期 `current_va_space_handle_` 是 `GpuDriverClient` 内部成员。H-2.5 将其迁移为 IGpuDriver 抽象的方法，H-3 之后所有 handle 流转由 caller 显式控制。

## R2 Mapping Contract（关键约束）

`create_queue()` 返回的 `uint64_t queue_handle` **必须**满足：

- 在 `submit_batch()` 时，`args.stream_id = (uint32_t)queue_handle`（取低 32 位）
- UsrLinuxEmu 端 `gpgpu_device.cpp:262` 逻辑：`static_cast<uint64_t>(args->stream_id)` 零扩展后必须在 `attached_queues` 列表中
- 因此 caller **必须**保存完整 u64 handle，**不能**自创计数器或截断

ioctl 路径**不需要** `MAP_QUEUE_RING` —— TaskRunner 通过 `submit_batch` 走 `GPFIFO_BASE + doorbell_ring`（参见 `gpgpu_device.cpp:284-300`）。

## 文件清单

```
plans/2026-06-19-h3-phase2-openspec-skeleton/
├── README.md                            # 本文件（DRAFT 入口）
├── .openspec.yaml                       # openspec 元数据（status: DRAFT）
├── proposal.md                          # Why + What + Capabilities + Impact
├── design.md                            # How + D1-D5 决策 + 风险 + Migration
├── tasks.md                             # 实施步骤 checklist
└── specs/gpu-phase2-management/
    └── spec.md                          # 9 ADDED Requirements + Scenarios
```

## 激活流程

1. 确认 H-2.5 已完成（`openspec/changes/archive/h2-5-architecture-foundation/` 存在）
2. 召集 review：TaskRunner owner + UsrLinuxEmu owner
3. `mv plans/2026-06-19-h3-phase2-openspec-skeleton /workspace/project/UsrLinuxEmu/openspec/changes/h3-phase2-management`
4. 移除本 README 的 ⚠️ DRAFT 标记 + 更新 `.openspec.yaml` 的 `status: DRAFT` → `status: ACTIVE`
5. 在 UsrLinuxEmu 仓 commit + 发起 PR

## 历史与交叉引用

- **DEPRECATED H-2**: `plans/2026-06-19-h2-phase2-openspec-skeleton/`（2026-06-19 弃用，拆分依据：Path D 重构优先）
- **H-1 closeout**（参考格式）: UsrLinuxEmu `openspec/changes/archive/2026-06-17-h1-pushbuffer-validation-closeout/`
- **H-2.5 前置**（待建）: `plans/2026-06-19-h2-5-architecture-foundation/`
- **Upstream ADR**: UsrLinuxEmu ADR-024 Phase 2 (Accepted v1)
- **SSOT**: `docs/02_architecture/post-refactor-architecture.md` §1.3 (v0.1.5 待加)
- **3 owner issues** (deferred to H-7 ADR): R2 mapping 类型不匹配 / ioctl 绕过 GpuQueueEmu / attached_queues 弱校验
