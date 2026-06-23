# TaskRunner 实施路径图 (Implementation Roadmap)

> **范围**: TaskRunner 用户态 CUDA/Vulkan API 兼容层 + Runtime Stub
> **最后更新**: 2026-06-23（H-4.5 docs governance cleanup）
> **关联**: [docs/architecture/](../architecture/) (当前架构), [docs/adr/](../adr/) (决策记录)

TaskRunner 实施路径分 4 个 phase。每个 phase 文档含：
- 目标 + 验收标准
- 关键 commit / 同步点
- 与上游 UsrLinuxEmu 的 sync 时机
- 实际实施 vs v0.1 提案的 deviation
- 测试基线

## Phase 索引

| Phase | 主题 | 状态 | 文档 |
|-------|------|------|------|
| **Phase 1** | CUDA Runtime 兼容 MVP | ✅ 完成 (2026-04-29) | [phase-1.md](./phase-1.md) |
| **Phase 1.5** | S3.5 fence_id + S3.1 va_space_handle | ✅ 完成 (2026-06-17) | [phase-1.5.md](./phase-1.5.md) |
| **Phase 2** | IGpuDriver 抽象 + Phase 2 lifecycle | ✅ 完成 (2026-06-23) | [phase-2.md](./phase-2.md) |
| **Phase 3** | Multi-GPU / P2P | ⏸️ Deferred (待 H-7) | [phase-3.md](./phase-3.md) |
| **Retrospective** | 实际 vs v0.1 提案 deviation | ✅ 整理 | [retrospective.md](./retrospective.md) |

## 关键同步点

| 同步点 / Change | 状态 | 日期 | 关联 PR / Commit |
|--------|------|------|---------|
| S0 (接口冻结) | ✅ | 2026-04-28 | Phase 1 |
| S1 (ioctl 路径打通) | ✅ | 2026-04-28 | Phase 1 |
| S2 (BasicGpuSimulator 集成) | ✅ | 2026-04-28 | Phase 1 |
| S3 (CudaScheduler 集成) | ✅ | 2026-04-28 | Phase 1 |
| S4 (CLI MVP) | ✅ | 2026-04-29 | Phase 1 |
| S3.5 (fence_id 扩展) | ✅ | 2026-05-13 | UsrLinuxEmu commit `a7f4463` |
| S3.1 (va_space_handle 透传) | ✅ | 2026-06-17 | PR #6 (`c40a149`) |
| S5 (Architecture foundation) | ✅ | 2026-06-19 | UsrLinuxEmu commit `c64301c` |
| H-2.5 (IGpuDriver 抽象层) | ✅ | 2026-06-22 | TaskRunner commit `1684fa1` |
| H-3 (Phase 2 lifecycle) | ✅ | 2026-06-23 | TaskRunner commits `241f3ed..8625b82` |
| H-3.5 (CudaStub guard verification) | 🔵 下一波 | TBD | [phase-3.md](./phase-3.md) |
| H-7 (3 owner-flagged upstream issues) | ⏸️ Deferred | 待 Phase 3 触发 | [TADR-008](../adr/tadr-008-h7-deferred-mirror.md) |
| Phase 3 (Multi-GPU / P2P) | ⏸️ Deferred | 待 H-7 | [phase-3.md](./phase-3.md) |

## 测试基线（截至 2026-06-23）

| 测试 | 状态 | 来源 |
|------|------|------|
| `test_cuda_scheduler` | ✅ 8/8 | H-1 baseline preserved |
| `test_gpu_architecture` | ⚠️ 10/11 | H-2.5 Bonus 预存在 baseline |
| `test_gpu_phase2` | ✅ 12/12 | H-3 新增 |

---

**END OF ROADMAP**
