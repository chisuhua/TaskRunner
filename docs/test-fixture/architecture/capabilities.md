# TaskRunner Capabilities 分组

> **最后更新**: 2026-06-23（H-4.5 docs governance cleanup）
> **关联**: UsrLinuxEmu [ADR-035 governance-policy §按 Capability 分组](../../../../docs/00_adr/adr-035-governance-policy.md)

TaskRunner capabilities 镜像 UsrLinuxEmu 端分组（canonical 在 UsrLinuxEmu `docs/00_adr/README.md`）。

## Capabilities 索引

| Capability | TADR | 主题 |
|------------|------|------|
| **gpu-driver-architecture** | [TADR-001](../adr/tadr-001-cuda-vulkan-runtime-unified-scheduler.md), [TADR-002](../adr/tadr-002-cuda-vulkan-runtime-layered-design.md), [TADR-003](../adr/tadr-003-cuda-vulkan-runtime-sync-unified-internal.md), [TADR-004](../adr/tadr-004-cuda-vulkan-runtime-stub-tracker.md), [TADR-005](../adr/tadr-005-h2-5-igpu-driver-consumer-lens.md) | IGpuDriver 抽象 + 3 实现 + DI (+ TADR-001~004 historical runtime decisions, see [roadmap/retrospective.md](../roadmap/retrospective.md)) |
| **gpu-phase2-management** | [TADR-006](../adr/tadr-006-h3-phase2-consumer-lens.md), [TADR-007](../adr/tadr-007-r2-mapping-contract.md), [TADR-008](../adr/tadr-008-h7-deferred-mirror.md) | Phase 2 5 方法 + R2 mapping + H-7 deferred |
| **architecture-governance** | [TADR-000](../adr/tadr-000-template.md) + [docs/adr/](../adr/) | TaskRunner 独立 ADR 体系 |

## 与 UsrLinuxEmu capabilities 的关系

UsrLinuxEmu 端的 capability 分组（canonical，源自 [docs/00_adr/README.md §H-4 governance 增量](../../../../docs/00_adr/README.md)）：

- **gpu-driver-architecture**: UsrLinuxEmu [ADR-032](../../../../docs/00_adr/adr-032-h2-5-igpu-driver-abstraction.md), [ADR-033](../../../../docs/00_adr/adr-033-h3-phase2-lifecycle.md)
- **gpu-phase2-management**: UsrLinuxEmu ADR-033, [ADR-034](../../../../docs/00_adr/adr-034-h7-deferred-registry.md)
- **architecture-governance**: UsrLinuxEmu [ADR-035](../../../../docs/00_adr/adr-035-governance-policy.md)

TaskRunner 这边的 TADR 是对应上游 ADR 的 **consumer-lens mirror**，**不重复决策内容**，仅记录 TaskRunner 侧落地细节。

## Capability 边界

| Capability | TaskRunner 责任 | UsrLinuxEmu 责任 |
|------------|----------------|------------------|
| gpu-driver-architecture | IGpuDriver 实现 + DI 注入 + CLI 集成 | IGpuDriver 接口契约 + 命名空间策略 + DI 模式定义 |
| gpu-phase2-management | 5 Phase 2 方法的 C++ 包装 + 测试 + CLI | 5 ioctl (`GPU_IOCTL_*`) 实现 + R2 mapping 校验 |
| user-space-runtime-decisions | CUDA Stub / Vulkan Stub 实施细节 | 不涉及（纯用户态决策）|
| architecture-governance | TADR 体系（独立编号 `TADR-NNN`） | ADR-035 governance 规则 |

---

**END OF CAPABILITIES**
