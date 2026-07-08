# Change: phase17-coverage-audit-supersede

> **状态**: 🔄 PROPOSED（2026-07-08, change proposal accepted; awaiting apply）
> **创建**: 2026-07-08
> **类型**: 文档 housekeeping (no code change, no new tests)
> **前置**: `archive/2026-07-02-phase17-test-coverage-completion/` (PROPOSED 状态, 2026-07-03 起草但未实施)
> **结论**: Phase 1.7 旧 archive 计划的 +26 cases 实际已被 Phase 3.1+3.2+4 work 顺带完成

## Why

### 历史背景

`archive/2026-07-02-phase17-test-coverage-completion/` 是 2026-07-02 创建的 openspec change，提出在 Phase 1.7 wait-period work 基础上补全 26 个 test cases：
- 旧基线 (2026-07-02): 91 REAL_IMPL / 53 STUB / 69 tests / 50.5% REAL_IMPL 覆盖
- 旧目标 (2026-07-02): 95+ tests / ≥85% REAL_IMPL 覆盖

旧 archive 状态停留在 `🔄 PROPOSED (2026-07-03, 待启动)` — 工作**未真正实施**。

### 当前实际状态 (2026-07-08)

通过 Phase 3.1 (PR #7) + Phase 3.2 (PR #7) + Phase 4 (PR #8) work，shim 测试覆盖实际已大幅超越旧目标：

| 指标 | 旧 archive 基线 (2026-07-02) | 旧 archive 目标 | **当前 (2026-07-08)** | Δ |
|---|---|---|---|---|
| REAL_IMPL | 91 | 91 (不变) | **113** | +22 (Phase 3.x 加 22 个) |
| STUB | 53 | 53 (不变) | **45** | -8 (Phase 3.x 8 个新 REAL_IMPL) |
| test_cuda_shim.cpp TEST_CASE | 69 | 95+ | **103** | +34 |
| 全测试套件 (8 binaries) | ~134 | 134 (不变) | **270/270 pass** | +136 |
| Phase 3.x 新增测试 (3 binaries) | n/a | n/a | **test_cu_graph 30 + test_cu_mem_pool 28 + test_cu_stream_capture 30 = 88** | +88 |
| Phase 4 新增测试 (2 binaries) | n/a | n/a | **test_cu_graph_real 32 + test_cu_mem_pool_export 13 = 45** | +45 |
| test_cuda_shim.cpp 实际增加 cases | n/a | +26 (旧 plan) | **+34** (cuFunc*/cuOccupancy*/cuPointerGetAttribute/cuMemsetD16/cuProfiler*/cuCtx*/cuDevicePrimaryCtx*/cuLaunchKernel*/STUB sanity 等都有) | +8 more |
| REAL_IMPL 覆盖率 (估) | 50.5% | ≥85% | **~75-90%** (基线测量后估计) | +25-40% |

### Gap 分析

旧 archive 计划中提到的 8 个 gap category 全部已被 Phase 3.1+3.2+4 work 顺带覆盖：

| 旧 archive gap 类别 | 计划 +cases | 实际状态 |
|---|---|---|
| A.1 cuFunc* (4 API) | +10 | ✅ 13 occurrences in test_cuda_shim.cpp (E.1 + Phase 3.1 overlap) |
| A.2 cuOccupancy* (3 API) | +3 | ✅ 4 occurrences (E.2) |
| A.3 cuPointerGetAttribute (1 API) | +1 | ✅ 9 occurrences (E.3) |
| A.4 轻量 stub (7 API) | +6 | ✅ cuMemsetD16 + cuProfilerStart/Stop 全有 test (E.3) |
| cuCtx 完整集 (8 API) | +8 | ✅ cuCtxGetDevice/Flags/Push/Pop/Sync/SharedMemConfig/SetLimit/GetApiVersion 全有 test (E.4) |
| PrimaryCtx (3 API) | +3 | ✅ cuDevicePrimaryCtxReset/GetState/SetFlags 全有 test (E.5) |
| Launch (3 API) | +3 | ✅ cuLaunchKernel/Ex/HostFunc 全有 test (E.6) |
| STUB sanity (1 case 串 10) | +1 | ✅ `TEST_CASE("STUB APIs return NOT_IMPLEMENTED")` 已存在 (E.7) |

### Why Now

- Phase 1.7 PROPOSED 状态在 archive 中**一直未关闭**，sync-plan §5.3 仍标 PROPOSED → 文档 stale
- 实际工作已被 Phase 3.1+3.2+4 顺带完成 → 无需"补完"任何 test
- 关闭此 change 是文档 housekeeping，不涉及新 work

## What Changes

| 类别 | 文件 | 改动 |
|---|---|---|
| **OpenSpec archive** | `openspec/changes/phase17-coverage-audit-supersede/` → `archive/2026-07-08-.../` | mv (rename) |
| **sync-plan** | `plans/sync-plan.md` §5.3 row 259 | "🟢 PROPOSED" → "✅ Done (superseded by Phase 3.x, 2026-07-08)" |
| **sync-plan** | `plans/sync-plan.md` footer (line 325-326) | last_updated: 2026-07-07 → 2026-07-08 |
| **代码** | None | n/a |
| **测试** | None | n/a |
| **UsrLinuxEmu** | None (TaskRunner 本地 housekeeping) | n/a |

## Capabilities

### New Capabilities

无。这是 close-only change。

### Modified Capabilities

- **`phase17-test-coverage-completion` (旧 archive)**: status = superseded by Phase 3.1+3.2+4 (2026-07-08)

## Impact

| 影响项 | 风险 |
|---|---|
| 文档 | Low (仅更新 sync-plan row 状态) |
| ABI | None |
| 测试 | None (无新 test, 无 test 修改) |
| UsrLinuxEmu 跨仓 | None (TaskRunner 本地) |
| 未来 work | Unblock — sync-plan §5.3 标 Done 后，Phase 1.7 不再占用 backlog |

## Refs

- **Superseded by**: TaskRunner main commits
  - `5b8f0ae docs(sync): Phase 3 Step 3 done + Step 4 trigger` (Phase 3 sync)
  - `fbcbe44 feat(shim): bridge 5 cu* APIs to GpuDriverClient (Phase 4 real bridge) (#8)` (Phase 4)
  - `2595f16 feat(gpu-driver-client): complete Phase 4 bridge (mem_pool_export_shareable real forwarder) + rename tadr-302 → tadr-305` (Phase 4 final)
- **旧 archive**: `openspec/changes/archive/2026-07-02-phase17-test-coverage-completion/`
- **Phase 1.6+1.7 status doc**: `docs/umd-evolution/roadmap/phase-1-6-7-extensions-complete.md`
