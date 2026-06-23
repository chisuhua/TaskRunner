# TaskRunner Architecture Decision Records (TADR)

> **范围**: TaskRunner 用户态 CUDA/Vulkan API 兼容层 + Runtime Stub
> **最后更新**: 2026-06-23（H-4.5 docs governance cleanup）
> **关联治理**: UsrLinuxEmu [ADR-035 governance-policy](../../../docs/00_adr/adr-035-governance-policy.md)

TaskRunner 持有**独立 ADR 体系**，编号前缀 `TADR-`，与 UsrLinuxEmu 内核侧决策的 `ADR-` 区分。两者主题不同：

| 维度 | TaskRunner TADR | UsrLinuxEmu ADR |
|------|----------------|----------------|
| 运行空间 | 用户态驱动 + Runtime | 内核驱动模拟环境 |
| 决策范围 | CUDA Stub / CudaScheduler / GpuDriverClient / IGpuDriver 28 方法 | 内核设备驱动 / VA Space / Ring Buffer / HAL |
| 跨引用 | `external/TaskRunner/docs/adr/` | `docs/00_adr/` |

## 命名规范

文件命名：`tadr-NNN-short-title.md`（小写，与 UsrLinuxEmu `adr-NNN-*.md` 风格一致）

- `tadr-000-template.md` — MADR 模板
- `tadr-001` ~ `tadr-008` — 实际 ADR

## 状态语义

复用 UsrLinuxEmu [ADR-035 §Rule 2](../../../docs/00_adr/adr-035-governance-policy.md) 四状态：

| 状态 | 含义 |
|------|------|
| ✅ Accepted | 已批准并实施 |
| 🔄 Proposed | 待评审 |
| ⏸️ Deferred | 已知问题但推迟 |
| 🚫 Rejected | 已否决 |

## 索引

| TADR | 状态 | 主题 | 关联 UsrLinuxEmu ADR |
|------|------|------|---------------------|
| TADR-000 | 📝 Template | MADR 模板 | — |
| TADR-001 | ✅ Accepted | CUDA/Vulkan Runtime 统一调度器 B 方案 | — |
| TADR-002 | ✅ Accepted | CUDA/Vulkan Runtime 分层设计 | — |
| TADR-003 | ✅ Accepted | CUDA/Vulkan Runtime 同步统一内部表示 | — |
| TADR-004 | ✅ Accepted | CUDA/Vulkan Runtime Stub 独立追踪 | — |
| TADR-005 | ✅ Accepted | IGpuDriver consumer-lens (H-2.5) | [ADR-032](../../../docs/00_adr/adr-032-h2-5-igpu-driver-abstraction.md) |
| TADR-006 | ✅ Accepted | Phase 2 5 方法 consumer-lens (H-3) | [ADR-033](../../../docs/00_adr/adr-033-h3-phase2-lifecycle.md) |
| TADR-007 | ✅ Accepted | R2 mapping contract (LOW32 truncation 显式化) | [ADR-033 §R2](../../../docs/00_adr/adr-033-h3-phase2-lifecycle.md) |
| TADR-008 | ⏸️ Deferred | H-7 上游 issue TaskRunner 侧注册点 | [ADR-034](../../../docs/00_adr/adr-034-h7-deferred-registry.md) |

## 跨仓同步协议

TADR 改动必须遵循 UsrLinuxEmu [ADR-035 §Rule 5.1](../../../docs/00_adr/adr-035-governance-policy.md) 4 步流程（TaskRunner 是 UsrLinuxEmu 的 submodule）：

1. **TaskRunner 仓**：`git add docs/adr/ && git commit && git push`
2. **UsrLinuxEmu 仓**：`git add external/TaskRunner`（submodule 指针）`&& git commit`
3. **UsrLinuxEmu 仓**（仅新增 cross-ref 段时）：更新 `docs/00_adr/README.md` 末尾的 "TaskRunner TADR mirror" 子表 `&& git commit`
4. **UsrLinuxEmu 仓**：`git push`

## 模板

见 [`tadr-000-template.md`](./tadr-000-template.md)

## 维护指南

### 添加新 TADR

1. 文件命名：`tadr-NNN-short-title.md`（NNN = 当前最大编号 + 1，零填充 3 位）
2. 使用 TADR-000 模板
3. 更新本 README 的索引表
4. 在相关 TADR 中添加跨引用
5. 关联上游 UsrLinuxEmu ADR（若为 consumer-lens mirror）
6. 关联 openspec change 路径（若有）

### 更新现有 TADR

1. 修改对应文件
2. 更新"最后更新"日期
3. 如状态变更，在索引表中同步更新

---

**维护者**: TaskRunner owner
**最后更新**: 2026-06-23（H-4.5 docs governance cleanup 建立 TADR 体系）
