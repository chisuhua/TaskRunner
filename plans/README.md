# plans/ 目录说明

> **最后更新**: 2026-06-23（H-4 governance cleanup）
> **归档政策**: 见 ADR-035 governance-policy

## 当前文件

| 文件 | 状态 | 用途 |
|------|------|------|
| `sync-plan.md` | ✅ 活跃 | TaskRunner-UsrLinuxEmu 跨仓同步计划（H-3 后 v2.0 精简版）|

## 归档（`archive/` 子目录）

历史文件归档区，仅供追溯，不维护。

| 文件 | 原始日期 | 归档原因 |
|------|----------|----------|
| `archive/2026-06-19-rebase-h1-onto-main.md` | 2026-06-19 | H-1 rebase 执行记录，H-1 已 shippable |
| `archive/findings.md` | 2026-05-06 | H-2.5 之前接口分析，与 IGpuDriver 架构矛盾 |
| `archive/progress.md` | 2026-05-06 | H-2.5 之前进度，已 outdated |
| `archive/interface-unification-plan.md` | 2026-05-06 | H-2.5 之前计划，已 outdated |
| `archive/gpu_queue_architecture_research.md` | 2026-04-29 | AMD vs NVIDIA GPU queue research，content 仍有参考价值但决策已被 ADR-024 取代 |

## 归档政策（双层结构）

### Layer 1: TaskRunner 本地归档（`plans/archive/`）

**用途**: TaskRunner submodule 内部历史文件

**规则**:
- 仅 archive H-2.5 之前的历史文件（与 IGpuDriver 架构无关）
- 文件原状保留（不修改内容）
- 文件头可加 DEPRECATED 标记（指向 ADR 取代关系）

### Layer 2: UsrLinuxEmu 跨仓归档（`openspec/changes/archive/`）

**用途**: 跨仓 openspec change（含已 shippable 的 H-N）

**规则**:
- 所有 openspec change（含 DRAFT/DEPRECATED skeleton）必须移至此目录
- 命名规范：`openspec/changes/archive/YYYY-MM-DD-<change-name>/`
- 仅在 change 完成（archived）后才移动

## 跨仓归档参考

- UsrLinuxEmu `openspec/changes/archive/` (canonical openspec archive): H-1, H-2.5, H-3, H-4 等
- TaskRunner submodule HEAD: 与 UsrLinuxEmu `external/TaskRunner` 一致

## 跨引用

- ADR-035 governance-policy（UsrLinuxEmu `docs/00_adr/`）：归档规则的 ADR 形式
- UsrLinuxEmu `openspec/changes/`：openspec change 活跃目录