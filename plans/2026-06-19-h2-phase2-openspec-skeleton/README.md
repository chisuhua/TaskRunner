# h2-phase2-implementation (⚠️ DEPRECATED — SUPERSEDED)

> **⚠️ 状态**: **DEPRECATED** — 本骨架已被拆分到两个新的 openspec，**不再激活**
> **创建**: 2026-06-19
> **创建者**: TaskRunner 侧 H-2 协调发起
> **Deprecation 日期**: 2026-06-19
> **Deprecation 原因**: Oracle review + explore 调查揭示 GpuDriverClient 是 dead code；按 Path D（重构优先）拆分

## 🔀 请改用以下两个 openspec（替代本 DEPRECATED 骨架）

| 替代 openspec | 范围 | 目录 |
|---|---|---|
| **H-2.5** `h2-5-architecture-foundation` | `IGpuDriver` 抽象接口 + GpuDriverClient/CudaStub 实现 + CudaScheduler DI + MockGpuDriver + CLI 死调用修复 | `plans/2026-06-19-h2-5-architecture-foundation/`（待建） |
| **H-3** `h3-phase2-management` | 在 H-2.5 之上实现 5 个 Phase 2 ioctl wrapper（create_va_space / destroy_va_space / register_gpu / create_queue / destroy_queue） | `plans/2026-06-19-h3-phase2-openspec-skeleton/` |

## 本目录保留原因

历史追溯：保留 proposal / design / tasks / spec 草稿可作为 Path D 路线推导的决策证据。

## 本目录原状态（仅供参考）

- 前置依赖 ✅：H-1 / H-1 closeout / ADR-024
- 原目标：在 GpuDriverClient 加 5 个 Phase 2 wrapper
- 原 D1-D5 决策：经 Oracle review + 数据验证后**已重新框架**，最终决策落在 H-3 骨架中（C/B/B/B/B）
- 关联 SSOT：`docs/02_architecture/post-refactor-architecture.md` §1.3（v0.1.5 改在 H-3 中更新）

## ⚠️ 本目录内容已不准确

下列文件保留为历史快照，**不要参考**：
- `proposal.md` — 假设 GpuDriverClient 是 active code（实际是 dead code）
- `design.md` — D1-D5 决策已重写
- `tasks.md` — 测试方案基于错误的 test_cuda_scheduler.cpp 假设
- `spec.md` — 5 个方法签名基于 u32 类型假设（实际应为 u64）

**激活本目录的指导**：
1. **不要**激活到 UsrLinuxEmu `openspec/changes/`
2. 如需查找原始草稿，本目录仍可读取
3. 真正的实现工作走 H-2.5 + H-3

## 原 D1-D5（已被新决策取代）

| ID | 原问题 | 原状态 | **最终决策**（在 H-3） |
|---|---|---|---|
| **D1** | VA Space 生命周期归属 | ❓ 待决 | **C. Caller owns** |
| **D2** | Queue 生命周期 | ❓ 待决 | **B. Explicit create-destroy** |
| **D3** | 方法命名风格 | ❓ 待决 | **B. snake_case**（H-1 的 CamelCase 是异常） |
| **D4** | Handle 存储 | ❓ 待决 | **B. Return only** |
| **D5** | 默认 VA Space | ❓ 待决 | **B. opt-in** |

## 原文件清单（保留为历史快照）

```
plans/2026-06-19-h2-phase2-openspec-skeleton/
├── README.md                            # 本文件（DEPRECATED 标记）
├── DECISIONS.md                         # 原决策矩阵（已过时）
├── .openspec.yaml                       # openspec 元数据（status: DEPRECATED）
├── proposal.md                          # 历史快照，不参考
├── design.md                            # 历史快照，不参考
├── tasks.md                             # 历史快照，不参考
└── specs/gpu-phase2-management/
    └── spec.md                          # 历史快照，不参考
```

## What This Is

H-1 closeout 后，**TaskRunner 侧 GpuDriverClient 仍缺 Phase 2 管理 API**：
- `submit_batch()` 可以 opt-in 传 `va_space_handle`（H-1）
- 但 **没有** `createVASpace()` / `createQueue()` / `registerGPU()` 等生命周期管理

UsrLinuxEmu 侧 ioctl 已就绪 (`gpu_ioctl.h` line 166-218)，TaskRunner 侧消费链断在 wrapper 层。

本 change 是 TaskRunner **消费** Phase 2 ioctl 的最小骨架，定义：
- 5 个 GpuDriverClient wrapper 方法
- VA Space / Queue 生命周期归属的**决策点**（D1-D5 中标注）
- 测试覆盖要求
- 同步点 S5 的产出物

## What's NOT In Scope

- 不实现 CudaScheduler 层的 VA Space 自动管理（属于更高层抽象，超出 GpuDriverClient wrapper 范围）
- 不修改 Phase 2 ioctl 的 ABI（UsrLinuxEmu 侧已稳定，TaskRunner 只消费）
- 不实现 GPU 内存映射 / GART / 页面表等 Phase 2 内部细节

## 关键开放问题（在 design.md D1-D5 详细描述）

| ID | 问题 | 决定后填入 |
|----|------|-----------|
| **D1** | VA Space 生命周期归属：CudaScheduler / GpuDriverClient / Caller | ❓ 待决 |
| **D2** | Queue 生命周期：与 stream 1:1 绑定 / 显式 create-destroy | ❓ 待决 |
| **D3** | 方法命名风格：CamelCase (`createVASpace`) / snake_case | ❓ 待决 |
| **D4** | Handle 存储：GpuDriverClient 内部维护 map / 仅返回值给 caller | ❓ 待决 |
| **D5** | GpuDriverClient 构造时是否自动创建默认 VA Space | ❓ 待决 |

## 文件清单（草稿状态）

```
plans/2026-06-19-h2-phase2-openspec-skeleton/
├── README.md                            # 本文件
├── .openspec.yaml                       # openspec 元数据
├── proposal.md                          # Why + What + Capabilities + Impact
├── design.md                            # How + D1-D5 决策 + 风险 + Migration
├── tasks.md                             # 实施步骤 checklist
└── specs/gpu-phase2-management/
    └── spec.md                          # ADDED Requirements + Scenarios
```

激活后：
1. `mv plans/2026-06-19-h2-phase2-openspec-skeleton /workspace/project/UsrLinuxEmu/openspec/changes/h2-phase2-implementation`
2. 移除本 README 的 "DRAFT" 标记
3. 在 UsrLinuxEmu 仓 commit 并发起 review