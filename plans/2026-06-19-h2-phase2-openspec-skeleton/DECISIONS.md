# H-2 决策矩阵（Decision Matrix）— ⚠️ DEPRECATED

> **⚠️ 状态**: **DEPRECATED** — 2026-06-19 起弃用
> **弃用原因**: Oracle review + explore 调查揭示 GpuDriverClient 是 dead code；按 Path D（重构优先）拆分
> **历史用途**: review 时只需勾选/填写本文件，然后据此批量更新 design.md / proposal.md / spec.md / tasks.md
> **位置**: `plans/2026-06-19-h2-phase2-openspec-skeleton/DECISIONS.md`

## 🔀 最终决策已迁移到 H-3 openspec

| ID | 原问题 | **最终决策**（H-3 中） |
|---|---|---|
| D1 | VA Space 生命周期归属 | **C. Caller owns** |
| D2 | Queue 生命周期 | **B. Explicit create-destroy** |
| D3 | 方法命名风格 | **B. snake_case** |
| D4 | Handle 存储 | **B. Return only** |
| D5 | 默认 VA Space | **B. opt-in** |

**请在 H-3 openspec（`plans/2026-06-19-h3-phase2-openspec-skeleton/`）中进行 review 与决策。本文件保留为历史快照，不再激活。**

---

## 原决策矩阵内容（保留为历史快照）

> **用途**: review 时只需勾选/填写本文件，然后我据此批量更新 design.md / proposal.md / spec.md / tasks.md
> **位置**: `plans/2026-06-19-h2-phase2-openspec-skeleton/DECISIONS.md`
> **状态**: ⚠️ 已弃用

---

## D1 - VA Space 生命周期归属

**问题**: VA Space handle 由谁持有？

| 选项 | 描述 | 一致性 |
|------|------|--------|
| **A** | CudaScheduler 构造时创建 / 析构时销毁 | 透明 |
| **B** ⭐ | GpuDriverClient 内部 `current_va_space_handle_`（H-1 已有） | 与 H-1 一致 |
| **C** | Caller 持有 + 显式传 `setCurrentVASpace()` | 最灵活 |

**你的选择**: `[ A / B / C ]`

**理由补充**（可选）: `_______________________________________________`

---

## D2 - Queue 生命周期

**问题**: Queue 与 stream / 显式 create-destroy？

| 选项 | 描述 | 一致性 |
|------|------|--------|
| **A** | 与 stream_id 1:1 绑定（隐式） | 简单 |
| **B** ⭐ | 显式 create-destroy，返回 `queue_id` | 与 H-1 opt-in 模式一致 |

**你的选择**: `[ A / B ]`

**理由补充**（可选）: `_______________________________________________`

---

## D3 - 方法命名风格

**问题**: CamelCase vs snake_case？

| 选项 | 例子 | 风格一致性 |
|------|------|-----------|
| **A** ⭐ | `createVASpace()` | 与 `setCurrentVASpace()` (H-1) 一致 |
| **B** | `create_va_space()` | 与 `submit_memcpy()` / `gpu_alloc()` 一致 |

**你的选择**: `[ A / B ]`

**理由补充**（可选）: `_______________________________________________`

---

## D4 - Handle 存储

**问题**: GpuDriverClient 内部维护 handle map？

| 选项 | 描述 | 复杂度 |
|------|------|--------|
| **A** | 内部 `unordered_map<handle, metadata>` | 高 |
| **B** ⭐ | 仅返回值给 caller，caller 自管 | 低 |

**你的选择**: `[ A / B ]`

**理由补充**（可选）: `_______________________________________________`

---

## D5 - 默认 VA Space

**问题**: `GpuDriverClient::open()` 时自动创建默认 VA Space？

| 选项 | 描述 | 一致性 |
|------|------|--------|
| **A** | 自动创建并 `setCurrentVASpace()` | 隐式 |
| **B** ⭐ | 保持 opt-in，caller 显式 create + set | 与 H-1 backward-compat 一致 |

**你的选择**: `[ A / B ]`

**理由补充**（可选）: `_______________________________________________`

---

## 提交方式

填写后回复我，例如：
> D1=B D2=B D3=A D4=B D5=B

或者：
> 全部默认

我会据此：
1. 批量更新 `design.md` D1-D5 段（移除 ❓ TBD，填入选定方案与理由）
2. 更新 `proposal.md §开放问题` 移除已决项
3. 更新 `spec.md` 中 D3 / D1 的 TBD 标注
4. 给出"决策定型版"骨架供最终 review