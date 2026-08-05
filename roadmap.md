---
SCOPE: SHARED
STATUS: ACCEPTED
DATE: 2026-08-04
RELATED:
  - docs/test-fixture/roadmap/README.md
  - docs/umd-evolution/roadmap/README.md
  - docs/umd-evolution/roadmap/current-status.md
---

# TaskRunner 路线图 (Roadmap — 3-scope 索引)

> **范围**: TaskRunner 仓 3-scope (test-fixture / umd-evolution / shared) 路线图总览
> **最后更新**: 2026-08-04 (arch-done 索引创建)
> **关联**: [docs/{test-fixture,umd-evolution,shared}/architecture/](./docs/test-fixture/architecture/) (架构), [docs/{test-fixture,umd-evolution,shared}/adr/](./docs/shared/adr/) (TADR 索引), [UsrLinuxEmu docs/roadmap/](../../docs/roadmap/) (上游)

本文件是 TaskRunner 路线图的 **canonical 总览索引**。详细 phase 文档按 3-scope 分别维护:

| Scope | 路径 | 状态 |
|-------|------|------|
| **test-fixture** (默认主线) | [docs/test-fixture/roadmap/](./docs/test-fixture/roadmap/) | ACTIVE |
| **umd-evolution** (实验性愿景 → ACCEPTED 推进中) | [docs/umd-evolution/roadmap/](./docs/umd-evolution/roadmap/) | ACTIVE |
| **shared** (跨切面契约) | (无 phase 文档,稳定) | STABLE |

## 元信息

- **创建时间**: 2026-08-04
- **最后更新**: 2026-08-04
- **当前阶段**:
  - test-fixture: **Phase 2 完成** (Phase 3 ⏸️ Deferred 待 H-7)
  - umd-evolution: **Phase 3.3 完成** (UMD-EVOLUTION → ACCEPTED 推进中,TADR-401)
  - shared: 稳定 (无 phase 演进)
- **关联 ADR (UsrLinuxEmu)**: ADR-035 (governance), ADR-036 (3-way separation)

## Phase 索引 (test-fixture scope)

| Phase | 主题 | 状态 | 日期 | 文档 |
|-------|------|------|------|------|
| **Phase 1** | CUDA Runtime 兼容 MVP | ✅ 完成 | 2026-04-29 | [phase-1.md](./docs/test-fixture/roadmap/phase-1.md) |
| **Phase 1.5** | S3.5 fence_id + S3.1 va_space_handle | ✅ 完成 | 2026-06-17 | [phase-1.5.md](./docs/test-fixture/roadmap/phase-1.5.md) |
| **Phase 2** | IGpuDriver 抽象 + Phase 2 lifecycle | ✅ 完成 | 2026-06-23 | [phase-2.md](./docs/test-fixture/roadmap/phase-2.md) |
| **Phase 3** | Multi-GPU / P2P | ⏸️ Deferred (待 H-7) | TBD | [phase-3.md](./docs/test-fixture/roadmap/phase-3.md) |
| **Retrospective** | 实际 vs v0.1 提案 deviation | ✅ 整理 | 2026-06-23 | [retrospective.md](./docs/test-fixture/roadmap/retrospective.md) |

> **test-fixture 当前状态**: Phase 1+1.5+2 ✅ 完成;Phase 3 ⏸️ 等 H-7 触发;Phase 3.1/3.2 (stream/graph/mempool) 实施中 (TADR-110)。

## Phase 索引 (umd-evolution scope)

| Phase | 主题 | 状态 | 日期 | 文档 |
|-------|------|------|------|------|
| Phase 0 | Doc fix + architecture/ + Q4 motivation | ✅ 完成 | 2026-06-30 | [phase-0-complete.md](./docs/umd-evolution/roadmap/phase-0-complete.md) |
| Phase 1 | CudaRuntimeApi + 8 tests + 4 CLI commands | ✅ 完成 | 2026-06-30 | [phase-1-complete.md](./docs/umd-evolution/roadmap/phase-1-complete.md) |
| Phase 2 | libcuda_taskrunner.so LD_PRELOAD shim | ✅ 完成 | 2026-07-01 | [phase-2-complete.md](./docs/umd-evolution/roadmap/phase-2-complete.md) |
| Phase 1.5/1.6/1.7 | dynamic_cast fix + 15 cu\* REAL_IMPL + 103 tests | ✅ 完成 | 2026-07-03 | [phase-1-6-7-extensions-complete.md](./docs/umd-evolution/roadmap/phase-1-6-7-extensions-complete.md) |
| Phase 3.1+3.2 | IGpuDriver 31→46 + GpuDriverClient forwarding + shim REAL_IMPL | ✅ 完成 | 2026-07-06 (PR #7) | sync-plan.md §5.3 |
| Phase 3.3 | Event timing + Texture/Surface (frontend) | ✅ 完成 | 2026-07-08 | [phase-3-3-complete.md](./docs/umd-evolution/roadmap/phase-3-3-complete.md) |
| Phase 4 | real-impl-bridge (5 shim APIs → GpuDriverClient IOCTLs) | ✅ 完成 | 2026-07-08 (PR #8) | `archive/2026-07-08-mempool-export-shareable-real-bridge/` |
| **UMD → ACCEPTED** | **TADR-401 promotion (5 entry checklist)** | 🔄 进行中 | ETA 2026-08-21 | [current-status.md](./docs/umd-evolution/roadmap/current-status.md) §Forward Roadmap |

> **umd-evolution 当前状态**: Phase 0~4 全完成,UMD build 默认开启 (87c9879, 07-10),TADR-401 promote 推进中 (5 项 entry checklist)。

## 关键同步点 (跨 scope)

| 同步点 / Change | 状态 | 日期 | 关联 |
|--------|------|------|---------|
| S0-S4 (接口冻结 + ioctl + sim + scheduler + CLI) | ✅ | 2026-04-28~29 | Phase 1 |
| S3.5 (fence_id 扩展) | ✅ | 2026-05-13 | UsrLinuxEmu `a7f4463` |
| S3.1 (va_space_handle 透传) | ✅ | 2026-06-17 | PR #6 (`c40a149`) |
| S5 (Architecture foundation) | ✅ | 2026-06-19 | UsrLinuxEmu `c64301c` |
| H-2.5 (IGpuDriver 抽象层) | ✅ | 2026-06-22 | TaskRunner `1684fa1` |
| H-3 (Phase 2 lifecycle) | ✅ | 2026-06-23 | TaskRunner `241f3ed..8625b82` |
| Phase 1.5/1.6/1.7 (UMD shim 强化) | ✅ | 2026-07-03 | `82a2839` `d988393` `defd272` |
| Phase 3.1+3.2 (IGpuDriver 31→46 + shim REAL_IMPL) | ✅ | 2026-07-06 | PR #7 (4-step coord) |
| Phase 3.3 (Event+Texture) | ✅ | 2026-07-08 | `498265c` |
| UMD build default-on (BREAKING) | ✅ | 2026-07-10 | `87c9879` |
| **TADR-110/111/113/114 (test-fixture) + TADR-206/207 (umd-evolution) + TADR-306 (shared, PROPOSED 待 dual-ack) (Phase 3.1/3.2 consumer-lens + tracking + drift)** | 🔄 **PROPOSED** | **2026-08-04** | **本次 arch-done 新增 + TADR-306 由 112 迁移** |
| UMD-EVOLUTION → ACCEPTED (5-entry checklist) | 🔄 进行中 | ETA 2026-08-21 | TADR-401 + cross-repo follow-up |
| Phase 3 Multi-GPU/P2P (test-fixture) | ⏸️ Deferred | 待 H-7 | TADR-105 |

## 测试基线 (截至 2026-08-04)

| Scope | 测试 | 状态 | 来源 |
|-------|------|------|------|
| test-fixture | `test_cuda_scheduler` | ✅ 8/8 | H-1 baseline preserved |
| test-fixture | `test_gpu_architecture` | ⚠️ 10/11 | H-2.5 Bonus 预存在 baseline |
| test-fixture | `test_gpu_phase2` | ✅ 12/12 | H-3 新增 |
| umd-evolution | 10 binaries (shim/scheduler/architecture/...) | ✅ 328/328 | 318 (07-08) + 10 (07-12~07-20) |
| umd-evolution | Sanitizers (ASan/UBSan/TSan) | ✅ 全绿 | `28f1790` |
| umd-evolution | `docs-audit.sh` | ⚠️ 53/54 PASS + 1 FAIL (false-positive) | current-status.md |

## 当前推进项 (Phase 4-5 重点)

### test-fixture
- **P0**: 补齐 CudaStub 16 方法 (TADR-110 G1)
- **P0**: umd E2E 重跑 (TADR-110 G2 / TADR-111)
- **P1**: `query_queue` 新增 (TADR-114)
- **P1**: Stage 4 ioctl 7 类消费 (TADR-113)
- **P2**: stream_id u64 漂移监控 (TADR-306 PROPOSED,待 dual-ack;shared scope)

### umd-evolution
- **P0**: TADR-401 promote 5 项 entry checklist
- **P1**: CI 默认 UMD 模式 + sanitizer 全覆盖
- **P1**: Phase 3.1/3.2 UMD shim 真实路径联调
- **P2**: API parity 检查 + 跨仓 mirror 同步

### shared
- 无 phase 演进 (契约稳定)

## 跨仓引用

- **UsrLinuxEmu 上游 roadmap**: `../../docs/roadmap/` (canonical SSOT)
- **跨仓治理协议**: `../../docs/00_adr/adr-035-governance-policy.md`
- **3-way separation**: `../../docs/00_adr/adr-036-three-way-separation.md`
- **本仓 TADR canonical 索引**: [docs/shared/adr/README.md](./docs/shared/adr/README.md)
- **AGENTS.md §Scope Classification**: `./AGENTS.md`

## 维护政策

1. 本文件是 roadmap **canonical 总览**;详细 phase 由 3-scope 子目录维护。
2. 3-scope phase 文档更新时,需同步更新本索引的状态表 + 最后更新日期。
3. Phase 状态变化时,需评估是否触发 ADR (含跨仓协议)。

---

**最后更新**: 2026-08-04 (arch-done 索引创建,含 tadr-110/111/113/114 + 206/207 + 306 同步点)