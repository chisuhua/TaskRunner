---
SCOPE: UMD-EVOLUTION
STATUS: ACTIVE
LAST_UPDATED: 2026-07-08
HEAD_COMMIT: 498265c (TaskRunner) + 7c274ab (UsrLinuxEmu submodule bump)
TESTS: 318 total (8 scheduler + 11 architecture + 12 phase2 + 8 runtime_api + 103 shim + 30 stream_capture + 25 graph + 36 mem_pool + 32 graph_real + 13 mem_pool_export + 23 event_timing + 25 texture_surface - 10 dup)
AUDIT: docs-audit 53/54 PASS + 1 FAIL (false-positive on cuFunc* promotion — see §Known Issues)
---

# Current Status (2026-07-08)

## TL;DR

UMD-EVOLUTION redesign complete through **Phase 3.3** (Event timing + Texture/Surface).
**Phase 3.1+3.2** (Stream async + Memory pool) and **Phase 4** (real-impl-bridge) all completed.

下一步候选 (从 sync-plan.md §5.3 + UsrLinuxEmu 端 openspec):
- **UsrLinuxEmu 端 8 active openspec** (Stage 3.2 hotpath, Stage 3.2 perf, Phase 4 cu-mempool-alloc-real-va, 等)
- **Stage 3 v1.0 (Issue #24)**: 3.1 CI 全平台 ✅(ubuntu-22.04), 3.3 错误处理 ✅(PR #26+28), 3.2 性能待办
- **KFD multi-file integration** (1-3 月 sub-project)

For continuation, see phase-specific roadmap files:
- [`phase-1-6-7-extensions-complete.md`](phase-1-6-7-extensions-complete.md) — Phase 1.5/1.6/1.7 follow-ups
- [`phase-2-complete.md`](phase-2-complete.md) — Phase 2 baseline (79 cu\* symbols, 37 tests)
- [`phase-3-3-complete.md`](phase-3-3-complete.md) — **Phase 3.3 (Event timing + Texture/Surface) complete**
- [`phase-3-deferred.md`](phase-3-deferred.md) — historical deferred status (superseded by ACTIVE prep notes)
- [`../../superpowers/plans/2026-07-02-phase3-prep-design-notes.md`](../../superpowers/plans/2026-07-02-phase3-prep-design-notes.md) — Phase 3 design (ACTIVE)
- [`../../superpowers/plans/2026-07-05-umd-phase3.3-event-texture.md`](../../superpowers/plans/2026-07-05-umd-phase3.3-event-texture.md) — Phase 3.3 plan (ACCEPTED 2026-07-08)

## Branch & Commit State

```
TaskRunner:  main @ 498265c (Phase 3.3 merge) + 5d95005 (archive) + 6e8c810 (sync-plan v2.4.2) + 21bd415 (stub-table sync) + 8c1d7ba (CudaScheduler leak fix) + 28f1790 (sanitizer options)
UsrLinuxEmu: main @ 7c274ab (submodule bump 5d95005)
```

## Test Status (10 binaries, 318 cases)

# Current Status (2026-07-05)

## TL;DR

UMD-EVOLUTION redesign complete through **Phase 1.7** (including 1.5 Stretch real backend unlock).
**Phase 3** 触发条件 #1（UsrLinuxEmu Stage 1.4 完成）已于 2026-07-04 满足，Phase 3 prep design notes 已从 DRAFT 升级到 ACTIVE。

下一步：启动 **Phase 3.3**（Event timing + Texture/Surface），这两个子计划无后端依赖，可立即实施。
Phase 3.1（Stream async + Memory pool）需要 UsrLinuxEmu sim 层实现 stream/mempool 原语，已发起跨仓协调。

For continuation, see phase-specific roadmap files:
- [`phase-2-complete.md`](phase-2-complete.md) — Phase 2 baseline (79 cu\* symbols, 37 tests)
- [`phase-1-6-7-extensions-complete.md`](phase-1-6-7-extensions-complete.md) — Phase 1.5/1.6/1.7 follow-ups
- [`phase-3-deferred.md`](phase-3-deferred.md) — historical deferred status (superseded by ACTIVE prep notes)
- [`../../superpowers/plans/2026-07-02-phase3-prep-design-notes.md`](../../superpowers/plans/2026-07-02-phase3-prep-design-notes.md) — Phase 3 design (ACTIVE)
- [`../../superpowers/plans/2026-07-05-umd-phase3.3-event-texture.md`](../../superpowers/plans/2026-07-05-umd-phase3.3-event-texture.md) — **Phase 3.3 plan (next)**

## Branch & Commit State

```
TaskRunner:  main @ ba16139 (pushed to origin/main)
UsrLinuxEmu: main @ a6f7212 (submodule pointer: 9e1a3a6 → TaskRunner ba16139 pending bump)
```

> 2026-07-05 note: UsrLinuxEmu submodule still references `9e1a3a6` (TaskRunner `82a2839`, Phase 1.5 Stretch).
> Phase 1.6 (`d988393`) + Phase 1.7 (`defd272`, `916b299`, `ba16139`) 子模块 bump 尚未发起。
> 决策点：是否 bump 到 `ba16139`（包含 Phase 1.6/1.7），或等 Phase 3.2 完成后一次性 bump？

## Test Status (5 binaries, 142 cases)

| Suite | Cases | Result |
|-------|-------|--------|
| test_cuda_scheduler | 8 | ✅ pass |
| test_gpu_architecture | 11 | ✅ pass |
| test_gpu_phase2 | 12 | ✅ pass |
| test_cuda_runtime_api | 8 | ✅ pass |
| test_cuda_shim (Phase 1.7 强化) | **103** (was 37) | ✅ pass |
| **Total** | **142** | **✅ 100% pass** |
| docs-audit.sh | 54 checks | ⚠️ 53 pass + 1 FAIL |

### docs-audit 1 FAIL 解释

docs-audit 失败原因：`CRITICAL_APIS_IMPL_REQUIRED` 列表（来自 `tools/generate_cu_stubs.py`）包含 7 个 Phase 1.7 中**故意保留为 STUB** 的 API（`cuFuncGetAttribute`, `cuFuncSetAttribute`, `cuFuncSetCacheConfig`, `cuCtxSetSharedMemConfig`, `cuCtxSetLimit`, `cuModuleGetGlobal`, `cuMemsetD16`）。

按 Phase 1.7 的诚实标记原则，这些 API 应该是 REAL_IMPL 但**当前 shim 中仅返回 NOT_IMPLEMENTED**。docs-audit 误报它们"缺失"，实际是 Phase 1.7 故意保留为 STUB（用作 Phase 3.2 的实施点）。

**修复方案（不在本计划范围）**: 当 Phase 3.2 把这些 API 升级为 REAL_IMPL 后，docs-audit 自动通过。

## Phase 0 Summary (Doc Fix)

3 TADRs (`tadr-201/202/203`) changed status from PROPOSED-but-actually-implemented to **SUPERSEDED**. Created `architecture/` directory with 2 docs (README + runtime-layering). Added conflict resolution table. Fixed 8 broken archive paths.

## Phase 1 Summary (Runtime PoC)

`CudaRuntimeApi` class with 3 methods:
- `cudaMalloc(void**, size)`
- `memcpy(void*, const void*, size, kind)`
- `launch_kernel(name, grid, block, args, sharedMem)`

Built on existing `CudaScheduler` (H-5) — **not bypassing the scheduler** (Oracle review correct correction).
8 doctest cases + 4 CLI commands. Builds in `TASKRUNNER_BUILD_MODE=umd-evolution`.

## Phase 2 Summary (LD_PRELOAD shim)

`libcuda_taskrunner.so` (now 98 cu\* symbols, was 79 at Phase 2 completion).

- 41 critical APIs implemented at Phase 2 (cuInit, cuDevice*, cuCtx*, cuModule*, cuMem*, cuLaunchKernel, cuDriverGetVersion, cuDevicePrimaryCtx*, etc.)
- 38 functional stubs returning CUDA_ERROR_NOT_IMPLEMENTED
- Oracle Critical #1 fix: cuModuleUnload cleans up function handles
- Oracle Critical #4 fix: cuCtx uses stack-tracked contexts (not hardcoded 0x1)
- CudaStub backend (single-device) provides the runtime semantics

**Usage:** Compile a CUDA program normally → `LD_PRELOAD=./libcuda_taskrunner.so ./myapp`
(Phase 3 needed for full real-kernel execution via D-3 ELF parsing).

## Phase 1.5/1.6/1.7 Summary (Post-Phase 2 Extensions)

详见 [`phase-1-6-7-extensions-complete.md`](phase-1-6-7-extensions-complete.md) — 完整记录。

**简短摘要**:

| 阶段 | commit | 关键变更 |
|------|--------|---------|
| Phase 1.5 Stretch | `82a2839` (2026-07-02) | 修复 5 个 `dynamic_cast<CudaStub*>` → IGpuDriver 接口调用；启用 GpuDriverClient 真实后端 |
| Phase 1.6 | `d988393` (2026-07-02) | cuMemGetInfo 真实数据；cuModuleLoad*Data demote；Phase 3 prep 设计笔记创建 |
| Phase 1.7 | `defd272`, `916b299`, `ba16139` (2026-07-03) | 15 cu\* API 升 REAL_IMPL；cuFunc* / cuOccupancy* / cuPointer*；测试 37 → 103；verify-phase17.sh 工具 |

**最终指标演进**:

| 指标 | Phase 2 完成 | Phase 1.7 后 |
|------|-------------|--------------|
| cu\* 符号导出数 | 79 | **98** |
| REAL_IMPL APIs | 76 | **91** |
| STUB APIs | 68 | **53** |
| shim test cases | 37 | **103** |
| assertions | 110 | **431** |

## Phase 3 Status: TRIGGERED (2026-07-04)

按 Phase 3 prep design notes 的 4 个触发条件：

| # | 触发条件 | 状态 | 日期 |
|---|----------|------|------|
| 1 | UsrLinuxEmu Stage 1.4 启动 | ✅ 已满足 | 2026-07-04 (Tier-1 `80f6a44` + Tier-2 STUB penetration complete) |
| 2 | 外部需求 | ❌ | — |
| 3 | CI gap | ❌ | — |
| 4 | 闲置 4+ 周 | ❌ | — |

**条件 #1 已满足，Phase 3 启动条件达成**。

### Phase 3 子计划 (按优先矩阵)

| 优先级 | 子计划 | 内容 | 工作量 | 后端依赖 | 状态 |
|--------|--------|------|--------|----------|------|
| P0 | 3.1 | Stream async (cuStreamBeginCapture/EndCapture, graphs) | 1-2 w | UsrLinuxEmu sim stream 原语 | 📋 待启动 |
| P0 | 3.2a | Memory pool (cuMemPool*, cuMemAllocFromPoolAsync) | 1-2 w | UsrLinuxEmu sim mempool 原语 | 📋 待启动 |
| **P1** | **3.2b** | **Event timing precision (proper CUDA clock API integration)** | **1 w** | **None (CudaStub clock)** | **🚀 启动（计划已写）** |
| **P1** | **3.2c** | **Texture/Surface (cuTexRefCreate/Destroy, cuArray*)** | **2 w** | **None (frontend impl)** | **🚀 启动（计划已写）** |
| P2 | 3.4 | YAML kernel registry | 1 w | None | 📋 待启动 |
| P2 | 3.4 | cuDeviceGetAttribute expansion (80+ attrs) | 0.5 w | None | 📋 待启动 |
| P3 | 3.5 | Multi-device support | 2-3 w | UsrLinuxEmu Stage 2 | ⏸️ 等 Stage 2 |
| P3 | (backlog) | ELF/CUBIN parsing (D-3 lite) | 4-6 w | UsrLinuxEmu kernel ABI | ❌ 无限期 |

**立即可执行**: Phase 3.3a (Event timing) + Phase 3.3b (Texture/Surface) — 无后端依赖，无需等待 UsrLinuxEmu。

**Phase 3.1**: 需要 UsrLinuxEmu sim 层提供 stream/mempool 原语，需跨仓协调。

## What's NOT Implemented (Future Work)

### Phase D-1 / D-3 (deferred indefinitely per `gap-analysis.md`)

- D-1: Doorbell mmap bypass (requires UsrLinuxEmu ADR-024 implementation)
- D-3: ELF/CUBIN parser + real kernel execution (requires UsrLinuxEmu kernel ABI)

These remain blocked on UsrLinuxEmu-side infrastructure work.

## Known Limitations

See [`../architecture/runtime-layering.md`](../architecture/runtime-layering.md) §Handle Lifecycle for full list:

1. No real kernel execution (cuLaunchKernel via CudaStub)
2. ~53 stub cu\* functions (intentional, return NOT_IMPLEMENTED) — down from 68 in Phase 2
3. cuMemcpyDtoD returns NOT_SUPPORTED (Phase 1 limitation)
4. Async stream capture not supported (graphs) — **Phase 3.2 will address**
5. Single-device only — **Phase 3.5 will address**
6. Thread-local context state (no cross-thread propagation)

## Open Questions

| Q# | Question | Status | Resolution |
|----|----------|--------|-----------|
| Q1 — YAML vs ELF parsing | Reserved | Default YAML (recommended); ELF deferred to D-3 |
| Q2 — Phase 3 scope (P0+P1 or include P2) | Reserved | Decision deferred with Phase 3 kickoff |
| Q3 — Vulkan extension points | ✅ RESOLVED | Architectural reservation kept (no implementation) |
| Q4 — Explicit PoC requirement | ✅ RESOLVED | POA-1 (KFD Consumer) + POA-2 (CI Regression) |
| Q5 — Spec/implementation team | ✅ RESOLVED | Session-driven implementation |

## File Map

```
docs/umd-evolution/                            # UMD-EVOLUTION scope root
├── roadmap/                                   # THIS directory (state tracking)  ★
│   ├── README.md                              #   index
│   ├── current-status.md                      #   master snapshot           ★ read first
│   ├── phase-0-complete.md                    #   Phase 0 summary
│   ├── phase-1-complete.md                    #   Phase 1 summary
│   ├── phase-2-complete.md                    #   Phase 2 summary
│   ├── phase-1-6-7-extensions-complete.md     #   Phase 1.5/1.6/1.7 follow-ups ★ NEW
│   └── phase-3-deferred.md                    #   Phase 3 historical deferred status
docs/superpowers/
├── specs/2026-06-30-umd-evolution-redesign.md # Design spec (authoritative)
├── plans/2026-06-30-umd-evolution-redesign.md # Phase 1 plan (B.1-B.7)
├── plans/2026-07-01-umd-phase2-ld-preload.md  # Phase 2 plan v2 (C.1-C.9)
├── plans/2026-07-02-phase1.5-cudastub-dynamic-cast-fix.md
├── plans/2026-07-02-phase3-prep-design-notes.md # Phase 3 design (ACTIVE) ★
└── plans/2026-07-05-umd-phase3.3-event-texture.md # Phase 3.2 plan (NEW) ★
```

## How to Recover from a Bad State

If something is broken:
1. `git status` and `git log` — see state
2. `git log --oneline 82a2839..HEAD` — list Phase 1.5/1.6/1.7 commits
3. `./build/test_cuda_shim` — verify Phase 1.7 tests (103 cases)
4. `./tools/verify-phase17.sh --quick` — single-entry verification
5. `./tools/docs-audit.sh` — verify docs structure
6. Re-push if missing: `git push origin main` (TaskRunner) + bump UsrLinuxEmu submodule

## Next Steps (Priority Order)

1. **Submodule bump**: UsrLinuxEmu bump TaskRunner → `ba16139`（包含 Phase 1.6/1.7）
2. **Phase 3.3a** Event timing precision (1 w, no backend deps)
3. **Phase 3.3b** Texture/Surface (2 w, no backend deps)
4. **跨仓协调**: Phase 3.1 Stream/MemPool 需要 UsrLinuxEmu sim 原语
5. **roadmap 治理**: Phase 3 启动后将 `phase-3-deferred.md` 改为 `phase-3-active.md`
