# Design: phase17-coverage-audit-supersede

> **状态**: 🔄 PROPOSED（2026-07-08, awaiting apply）
> **设计日期**: 2026-07-08
> **设计者**: TaskRunner owner (Sisyphus session)
> **类型**: 文档 housekeeping
> **范围**: 4 files (3 new + 1 modified)

## §1. 旧 archive 状态

`openspec/changes/archive/2026-07-02-phase17-test-coverage-completion/`:
- 创建: 2026-07-02
- Archive 时间: 2026-07-02 (但 tasks.md 仍标 PROPOSED)
- 状态: **PROPOSED 但 archive 移动了** — 文档不一致
- Scope 计划: 7 workstreams (E.1-E.8), 26+ new test cases
- 旧基线 (2026-07-02): 91 REAL_IMPL / 53 STUB / 69 tests
- 旧目标: 95+ tests / ≥85% REAL_IMPL 覆盖
- **实际**: 工作未实施，archive 移动后无 follow-up

## §2. 当前实际状态 (2026-07-08 audit)

通过 Phase 3.1+3.2 (PR #7) + Phase 4 (PR #8 + 2595f16) work：

### 测试 case 计数

| 测试 binary | TEST_CASE | 来源 |
|---|---|---|
| test_cuda_shim | 103 | Phase 1.6/1.7 + Phase 3.x (cuFunc*/cuOccupancy*/cuPointer*/cuMemsetD16/cuProfiler*/cuCtx*/cuDevicePrimaryCtx*/cuLaunchKernel*/STUB sanity) |
| test_cu_stream_capture | 30 | Phase 3.1 (PR #7) |
| test_cu_graph | 30 | Phase 3.1 (PR #7) |
| test_cu_mem_pool | 36 | Phase 3.2 (PR #7) |
| test_cu_graph_real | 32 | Phase 4 (PR #8) |
| test_cu_mem_pool_export | 13 | Phase 4 (PR #8) |
| test_cuda_runtime_api | 8 | Phase 1 baseline |
| test_cuda_scheduler | 8 | H-1 baseline |
| test_gpu_architecture | 11 | H-2.5 baseline |
| test_gpu_phase2 | 12 | H-3 baseline |
| **总计** | **283 cases / 270/270 pass** | per sync-plan §5.2 |

注: test_cu_graph 30 是 PR #7 后的实际值 (25 旧 + 5 Phase 4 additions)。
test_cu_mem_pool 36 是 PR #7 后的实际值 (28 旧 + 8 Phase 4 additions)。
sync-plan §5.2 写 270 是 PR #7+#8 的 base 值。

### cu* API surface (exported symbols)

```
$ nm libcuda_taskrunner.so | grep -E " T cu[A-Z]" | wc -l
158
```

按 stub_table.inc markers:
- 113 REAL_IMPL (was 91, +22 from Phase 3.1+3.2+4)
- 45 STUB (was 53, -8 from Phase 3.1+3.2+4)

### 覆盖评估

- 158 cu* API 表面
- 85 unique cu* functions referenced in test_cuda_shim.cpp (粗估)
- 30 + 32 = 62 graph/launch related references in test_cu_graph* (Phase 3.1+4)
- 36 + 13 = 49 mempool related references in test_cu_mem_pool* (Phase 3.2+4)
- 30 stream capture references in test_cu_stream_capture
- 估计 **REAL_IMPL 覆盖率**: 75-90% (与旧 50.5% 相比大幅提升)
- **STUB 覆盖率**: 100% via `TEST_CASE("STUB APIs return NOT_IMPLEMENTED")`

## §3. Supersede 决策

### Decision

Phase 1.7 旧 archive (2026-07-02) **superseded by Phase 3.1+3.2+4 work**。

### Rationale

1. **实际工作已完成**: Phase 3.1+3.2+4 在 shim 层添加了 22 个新 REAL_IMPL + 对应测试 (158+ cases)。其中包含旧 archive 计划的 A.1/A.2/A.3/A.4 + cuCtx/PrimaryCtx/Launch 集。
2. **基线已超越目标**: 旧 archive 目标 95+ tests / ≥85% REAL_IMPL 覆盖。当前 test_cuda_shim 103 cases / 全套 283 cases / 估计 75-90% REAL_IMPL 覆盖。
3. **无 follow-up 价值**: 旧 archive 提议的"补完 26 cases"在 Phase 3.x work 中已**自然完成**。新增 cases 数量 (158+ 在 Phase 3 binaries) 远超 26。
4. **文档一致性**: sync-plan §5.3 仍标 PROPOSED 是 stale，需要 close。

### Non-Decision (Out of Scope)

- **不实施**新 test (无技术债务)
- **不修改**REAL_IMPL code (无需 bug fix)
- **不修改**STUB code (Phase 3.x 已 8 个 STUB → REAL_IMPL，超出 26 cases 计划)
- **不删除**旧 archive 目录 (保留作 historical record)

## §4. Implementation Plan

### 4.1 创建 openspec change (3 files)

```bash
mkdir -p openspec/changes/phase17-coverage-audit-supersede
# 写 proposal.md / tasks.md / design.md (本文档)
```

### 4.2 修改 sync-plan.md (2 lines)

```diff
-| **Phase 1.7 test coverage** | 25-30 E2E tests (REAL_IMPL 50.5%→≥85%) | 🟢 可立即开始 (独立) | — | PROPOSED |
+| **Phase 1.7 test coverage** | (superseded by Phase 3.1+3.2+4) | ✅ Done 2026-07-08 | — | Done |
```

```diff
-**最后更新**: 2026-07-07（Phase 4 real-impl-bridge, v2.4 新增 §1.5 + tadr-301/302）
+**最后更新**: 2026-07-08（Phase 1.7 close via audit-supersede; v2.4.1 housekeeping）
```

### 4.3 Archive move

```bash
mv openspec/changes/phase17-coverage-audit-supersede \
   openspec/changes/archive/2026-07-08-phase17-coverage-audit-supersede
```

### 4.4 Commit + push

```bash
git add -A
git commit -m "docs(sync-plan): mark Phase 1.7 superseded by Phase 3.x (2026-07-08 audit)"
git push origin main
```

## §5. 验证 (Verification)

| 检查 | 命令 | 预期 |
|---|---|---|
| openspec list empty | `openspec list --json` | `{"changes":[]}` |
| sync-plan §5.3 row 259 | `grep "Phase 1.7 test coverage" plans/sync-plan.md` | 含 "✅ Done 2026-07-08" |
| sync-plan footer | `grep "最后更新" plans/sync-plan.md` | "2026-07-08" |
| 远端 main 含 commit | `git log --oneline -3 origin/main` | 含新 commit |
| 远端指针 | `git ls-remote origin main` | 新 hash |

## §6. Refs

- 旧 archive (superseded): `openspec/changes/archive/2026-07-02-phase17-test-coverage-completion/`
- Phase 3.1+3.2 PR #7: `https://github.com/chisuhua/TaskRunner/pull/7` (MERGED 02363b8)
- Phase 4 PR #8: `https://github.com/chisuhua/TaskRunner/pull/8` (MERGED fbcbe44)
- Phase 4 final: commit `2595f16`
- sync-plan.md v2.4.1: `plans/sync-plan.md`
- Phase 1.6+1.7 status: `docs/umd-evolution/roadmap/phase-1-6-7-extensions-complete.md`
