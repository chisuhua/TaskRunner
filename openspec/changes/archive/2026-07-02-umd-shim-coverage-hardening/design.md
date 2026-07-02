# Design: umd-shim-coverage-hardening

> **Change 类型**: 实施型（implementation + tests + docs hygiene）— TaskRunner 侧独立
> **依赖**: Phase 1.5 Stretch (`openspec/changes/archive/2026-07-02-taskrunner-umd-backend-enable/`)
> **Scope**: umd-evolution + minor test-fixture (H-3 fixes) + minor shared (docs-audit)

## 数据流

```
CUDA Application (uses cu* via libcudart.so)
    ↓ LD_PRELOAD hook
libcuda_taskrunner.so
    ├── 41 critical impls (Phase 2 unchanged)
    ├── 38 → 28 stubs
    │     ├── Phase 1.6 promotions (10):
    │     │     cuDeviceComputeCapability → cu_query.cpp
    │     │     cuDeviceGetAttribute (extended) → cu_query.cpp
    │     │     cuCtxGet/SetCacheConfig → cu_ctx.cpp
    │     │     cuCtxGet/SetSharedMemConfig → cu_ctx.cpp
    │     │     cuCtxGet/SetLimit → cu_ctx.cpp
    │     │     cuGetErrorName/String → cu_init.cpp
    │     │     cuMemGetInfo → cu_mem.cpp
    │     │     cuFuncGetAttribute → cu_module.cpp
    │     │     cuPointerGetAttribute → cu_mem.cpp
    │     └── Phase 3 deferred (28):
    │           cuStream* (9), cuEvent* (6), cuMemPool* (5), cuMemAddress* (4)
    │           cuMemHostRegister/Unregister (2), cuModuleLoadData/Ex/FatBinary (3)
    │           cuTexRef*/cuSurfRef*/cuArray* (12), cuGraph* (13), cuLink* (5)
    │           cuProfiler* (3), cuOccupancy* (3), cuLaunchHostFunc (1)
    ↓ routes to (unchanged)
CudaRuntimeApi (Phase 1, unchanged)
    ↓
CudaScheduler (Phase 1.5 dynamic_cast fixed)
    ↓
IGpuDriver (H-2.5, 31 methods, tadr-301 frozen)
```

## 关键决策

### D1: 仅实现 backend-independent 10 个 stub，不触碰 28 个 backend-dependent stub

**理由**: 28 个 stub 涉及 Stream/MemPool/Event/Graphs/Texture/UVM 等，需要 UsrLinuxEmu Stage 1.1+ 后端支持。提前实现会引入"假成功"（fake success）风险，违反 §Verify "Type-safety in stub" 原则。

**边界**:
- ✅ cuCtx*CacheConfig / SharedMemConfig / Limit — 纯 thread-local state，无后端
- ✅ cuGetErrorName/String — 纯查找表，无后端
- ✅ cuDeviceComputeCapability / cuDeviceGetAttribute — 静态枚举值
- ✅ cuMemGetInfo — 内存统计，可从 CudaScheduler 现有 BO 池计算
- ✅ cuFuncGetAttribute — 静态查询（无实际 kernel ABI 需求）
- ✅ cuPointerGetAttribute — 内部 BO map 查询
- ❌ cuStream* / cuEvent* / cuMemPool* / cuMemAddress* / cuMemHostRegister — 需 Stage 1.3 UVM
- ❌ cuTexRef* / cuArray* / cuSurfRef* — 需 Phase 3.3
- ❌ cuGraph* — 需 Phase 3.1
- ❌ cuLaunchHostFunc / cuLaunchCooperativeKernel — 需后端 kernel dispatch 增强

**验证**: Sub-plan E Task E.3 新增 docs-audit 检查跟踪 stub 数量（≤28）。

### D2: 测试覆盖策略 — 横向扩展（threading/error path） + 纵向加深（attribute coverage）

**理由**: Phase 2 的 37 个测试主要是 happy path。Phase 1.6 重点是:
- **横向**: threading（CUDA 应用程序常 multi-thread）+ error path（NULL/invalid handle/OOM）
- **纵向**: cuDeviceGetAttribute 全覆盖（80+ 值）；cuMemGetInfo 准确性（追踪 BO 池）

**新增 13 个测试分布**:
- 4 cuDeviceGetAttribute cases (B.4)
- 5 cuCtx* / cuGetError* cases (C.5)
- 4 cuMemGetInfo / cuFuncGetAttribute / cuPointerGetAttribute cases (D.4)
- 2 threading + 4 error path cases (E.1-E.2) = 6 个，但本 plan 中部分合并为单 TEST_CASE（multi-step）

实际新增：约 13 个 TEST_CASE blocks。

### D3: H-3 follow-up 4 项打包为单 commit `docs(h3): cleanup post-activation regressions`

**理由**: h3-activation-followup.md §TL;DR 明确建议独立 commit 处理。4 项均 < 5 行修改，合并为单 commit 减少噪音。

**Commit 边界**: Sub-plan A 单独 commit（docs-only），Sub-plan B-E 合并为单 commit（feat+test+tool+docs）。

### D4: Phase 3 骨架作为 DRAFT 文档而非 PROPOSED

**理由**: Phase 3 实际 kickoff 取决于 UsrLinuxEmu Stage 1.4 触发。提前将设计写入 PROPOSED 会误导后续 session 误以为可立即实施。

**标记**: `docs/superpowers/plans/2026-07-02-umd-phase3-skeleton.md` 状态为 DRAFT，引用 `phase-3-deferred.md` 的 trigger conditions。

### D5: docs-audit 新增 2 检查项（G5）

**理由**: 当前 docs-audit.sh Check 9 只验证 ≥30 symbols + 41 critical APIs。Phase 1.6 stub reduction 进度需跟踪，否则未来 regression 难以察觉。

**新增 2 检查**:
1. **Minimum critical API count** ≥41（已有，移到 Phase 1.6 命名空间下）
2. **Backend-independent stub count** ≤28（新增 — 当前 38，目标是 28）

## 验证矩阵

| 步骤 | 测试/命令 | 预期结果 | 负责方 |
|------|----------|---------|--------|
| 前置 Gate | TaskRunner 76/76 tests PASS | 全部绿 | Phase 1.5 baseline |
| Sub-plan A | `grep -n "DRAFT\|待建\|激活流程" openspec/changes/h3-phase2-management/README.md` | 仅 line 1 "✅ ACTIVE" | 本 change |
| Sub-plan B | `nm -D --defined-only build/libcuda_taskrunner.so \| grep -c " cu[A-Z]"` | 79 (unchanged) | 本 change |
| Sub-plan C | `./build/test_cuda_shim` | ≥42 cases PASS (37+5) | 本 change |
| Sub-plan D | `./build/test_cuda_shim` | ≥46 cases PASS (42+4) | 本 change |
| Sub-plan E | `for t in test_*; do ./build/$t; done` | ≥89 cases PASS (76+13) | 本 change |
| Sub-plan E | `grep -c "// STUB" cu_stub_table.inc` | ≤28 (was ~38) | 本 change |
| Sub-plan E | `./tools/docs-audit.sh` | ≥58 checks PASS | 本 change |
| Sub-plan E | `ls docs/superpowers/plans/2026-07-02-umd-phase3-skeleton.md` | exists | 本 change |
| 跨仓 | UsrLinuxEmu submodule bump | pointer updated | 本 change (ADR-035) |

## 风险缓解

| 风险 | 概率 | 影响 | 缓解 |
|------|------|------|------|
| cuMemGetInfo 计算错误（BO 池未对齐） | M | 测试失败 | Sub-plan D Task D.1 用 `TASKRUNNER_GPU_MEM_SIZE` env var 默认 8GB，对齐到 4KB page |
| cuGetErrorName 字符串不一致 | L | ABI 微变 | 字符串按 NVIDIA CUDA Toolkit 12.x 文档定义，单元测试覆盖每个 error code |
| 13 新测试导致 Phase 2 测试失败 | L | change 不可合并 | 每个 Sub-plan B/C/D 完成后单独跑全套测试，failure 立即 abort |
| H-3 fix 4 项相互冲突 | L | docs 不一致 | Sub-plan A 4 个 fix 按文件分组，git 顺序 review |
| Phase 3 skeleton 与未来实际设计偏差 | M | kickoff 时需重写 | DRAFT 状态，明确标注 "no implementation, design reservation only" |
| UsrLinuxEmu Stage 1.0-1.3 进度延迟 | L | Phase 1.6 仍可完成 | Phase 1.6 与 Stage 1 完全独立 |
| 跨仓 submodule bump 冲突 | L | sync 失败 | 唯一 UsrLinuxEmu 改动是 submodule pointer，按 ADR-035 §Rule 5.1 标准流程 |

## Test Plan (Add to Existing Test Suite)

新增测试用例模板（统一格式）:

```cpp
TEST_CASE("umd-shim-coverage-<api-name>") {
  // Setup: typically init shim + create ctx
  CUresult ret = <api-call>;
  REQUIRE(ret == CUDA_SUCCESS);
  // Verify side effects (return values, state changes)
}
```

具体 13 个用例:

| # | Test | Sub-plan | Estimated Time |
|---|------|----------|----------------|
| 1 | cuDeviceGetAttribute(MAX_BLOCK_DIM_X) | B.4 | 5 min |
| 2 | cuDeviceGetAttribute(MAX_GRID_DIM_X) | B.4 | 5 min |
| 3 | cuDeviceGetAttribute(COMPUTE_CAPABILITY_MAJOR) | B.4 | 5 min |
| 4 | cuDeviceComputeCapability major/minor | B.4 | 5 min |
| 5 | cuCtxGetCacheConfig default | C.5 | 5 min |
| 6 | cuCtxSetCacheConfig then Get | C.5 | 5 min |
| 7 | cuCtxGetLimit(STACK_SIZE) | C.5 | 5 min |
| 8 | cuGetErrorName(SUCCESS) | C.5 | 5 min |
| 9 | cuGetErrorName(OUT_OF_MEMORY) | C.5 | 5 min |
| 10 | cuMemGetInfo total ≥ free | D.4 | 10 min |
| 11 | cuMemGetInfo after alloc decreases free | D.4 | 10 min |
| 12 | cuFuncGetAttribute(MAX_THREADS_PER_BLOCK) | D.4 | 10 min |
| 13 | cuPointerGetAttribute(CTX, ptr) | D.4 | 10 min |
| 14 | threading: 2 threads ctx isolation | E.1 | 20 min |
| 15 | threading: 4 threads alloc safety | E.1 | 20 min |
| 16 | cuMemAlloc NULL dptr rejection | E.2 | 5 min |
| 17 | cuMemFree invalid ptr rejection | E.2 | 5 min |
| 18 | cuModuleLoad nonexistent file | E.2 | 5 min |
| 19 | cuCtxGetCurrent before create | E.2 | 5 min |

Total: 19 cases (vs initial estimate 13 — adds threading + error path value)

---

**设计成熟度**: 与 Phase 1.5 Stretch 一致（已通过 Oracle 审查）
**架构风险**: 低（无 IGpuDriver 契约变更）
**集成风险**: 无（不触碰 Phase 2 ABI）