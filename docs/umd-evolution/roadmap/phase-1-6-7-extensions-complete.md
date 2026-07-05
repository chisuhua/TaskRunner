---
SCOPE: UMD-EVOLUTION
STATUS: COMPLETE
PHASE: 1.6 + 1.7
COMPLETION_DATE: 2026-07-03
TESTS: 103/103 shim + 8/8 runtime_api + 8/8 scheduler + 11/11 architecture + 12/12 phase2
---

# Phase 1.6 & 1.7 COMPLETE — Shim Extensions (post-Phase 2 follow-ups)

> **背景**: Phase 2（`83ef131`）完成后识别出两类遗留工作：(1) `dynamic_cast<CudaStub*>` 硬绑定阻塞真实后端路径
> （Phase 1.5 Stretch）；(2) shim 数据准确性、测试覆盖、API 完整度需要补齐（Phase 1.6/1.7）。本文件记录
> 三个补充阶段的工作，均在 Phase 2 完成（2026-07-01）后**仅 1-2 天内**完成（2026-07-02 ~ 2026-07-03）。

## Phase 1.5 Stretch — Real Backend Unlock

**Commit**: `82a2839 fix(scheduler): remove dynamic_cast<CudaStub*> hardcoding from CudaScheduler`
**日期**: 2026-07-02
**UsrLinuxEmu 验证**: `9e1a3a6 chore(submodule): bump TaskRunner to 82a2839 for Phase 1.5 dynamic_cast fix`

**核心变更**:
- 5 处 `dynamic_cast<CudaStub*>` 替换为 `IGpuDriver` 虚接口调用
- 新增 `bo_handles_ map` 跟踪 device_ptr → bo_handle
- 删除 `stub->is_stub_mode()` fence guards（统一通过 IGpuDriver 路径）
- 5 个 API 完全通过虚接口调用：
  - `submitMemAlloc` (alloc_bo_vram → alloc_bo)
  - `submitMemFree` (free_bo)
  - `submitMemcpyH2D/D2H` (submit_memcpy + is_h2d flag)
  - `submitLaunch` (submit_launch + params + task_id)

**价值**: TaskRunner ↔ UsrLinuxEmu 真实 GPU 后端路径打通，端到端 `libcuda_taskrunner.so → CudaScheduler → IGpuDriver → GpuDriverClient → GPU_IOCTL_* → UsrLinuxEmu GpgpuDevice` 可用。

## Phase 1.6 — Shim Extension

**Commit**: `d988393 feat(shim): Phase 1.6 shim extension — cuMemGetInfo real data + cuModuleLoad* demote + test coverage + Phase 3 prep`
**日期**: 2026-07-02
**基础**: Phase 1.5 Stretch + Phase 2 drift hotfix (`d8ca3d3`)

**变更范围**:

| 任务 | 内容 | 工作量 |
|------|------|--------|
| A.1 | `cuMemGetInfo` 真实数据源：查询 `IGpuDriver::get_device_info().vram_size`，`TASKRUNNER_GPU_MEM_SIZE` 环境变量回退 | 2-3 h |
| A.2 | `cuModuleLoad*Data` demote：3 个 fake-REAL_IMPL API 降级为 STUB（诚实标记）| 30 min |
| A.3 | 测试覆盖：A.1 + A.2 + 已有 stub 覆盖强化 | 4-6 h |
| A.4 | `phase-3-prep-design-notes.md` 创建：DRAFT 状态的设计笔记 | 1 h |

**关键修复**: `cuMemGetInfo` 之前硬编码 `*free=4GB / *total=8GB`（fake-success 风险），
改为从 IGpuDriver 获取真实 VRAM 大小。

**Phase 3 准备**: `phase-3-prep-design-notes.md`（docs/superpowers/plans/2026-07-02-phase3-prep-design-notes.md）
记录了 Phase 3 的触发条件、优先矩阵、设计约束，作为将来启动 Phase 3 的设计基础。

## Phase 1.7 — Test Coverage Completion

**Commits**: `defd272`, `916b299`, `ba16139`
**日期**: 2026-07-03

### Phase 1.7.1 — 15 REAL_IMPL API 提升 + 关键 bugfix

**Commit**: `defd272 feat(shim): Phase 1.7 — 15 REAL_IMPL APIs (A.1/A.2/A.3/A.4) + critical bugfixes`

**Promoted APIs**:
- A.1 cuFunc* (4): `cuFuncGetAttribute`, `cuFuncSetAttribute`, `cuFuncSetCacheConfig`, `cuFuncGetModule`
- A.2 cuOccupancy* (3): `cuOccupancyMaxActiveBlocksPerMultiprocessor`, `cuOccupancyMaxActiveBlocksPerMultiprocessorWithFlags`, `cuOccupancyMaxPotentialBlockSize`
- A.3 cuPointer* (1): `cuPointerGetAttribute` (CONTEXT/MEMORY_TYPE/DEVICE_POINTER/RANGE_SIZE)
- A.4 light stubs (7): `cuStreamCreateWithFlags`, `cuStreamGetCaptureInfo`, `cuEventCreateWithFlags`, `cuMemsetD16`, `cuProfilerStart`, `cuProfilerStop`, `cuProfilerInitialize`

**Critical bugfixes**（测试暴露）:
- `cuCtxGetSharedMemConfig`：之前返回 SUCCESS 但未写入 `*pConfig`（输出未初始化）；现在写默认值 0 且拒绝 NULL 指针
- `cuMemsetD16`：之前解引用 stub-only 指针 → SIGSEGV；现在 safe no-op（stub pointer 没有可写 backing memory）

**STUB sanity surface（E.7）**：`cuArrayCreate` / `cuGraphCreate` / `cuTexRefCreate` / `cuMemHostRegister` 在 `include/cuda.h` 中声明但未在任何 .cpp 导出 → 现在显式返回 `CUDA_ERROR_NOT_IMPLEMENTED`。

**计数器变化**: REAL_IMPL 76 → **91**，STUB 68 → **53**

### Phase 1.7.2 — 测试覆盖完成

**Commit**: `916b299 test(shim): Phase 1.7 — test coverage completion (69→103, +34 cases / +149 assertions)`

| 测试组 | 测试数 | 覆盖 API |
|--------|--------|----------|
| E.1 cuFunc* attributes | 10 | `cuFuncGetAttribute` (4 默认值: MAX_THREADS=1024, SHARED_SIZE=48KB, CONST_SIZE=64KB, NUM_REGS=32) + unknown attribute reject + null function reject + set round-trip + cache config + module lookup |
| E.2 cuOccupancy* | 3 | `MaxActiveBlocks(≥1)` + `WithFlags` delegation + `MaxPotentialBlockSize(blockSize=256, minGridSize=80)` |
| E.3 cuPointerGetAttribute + A.4 | 6 | CONTEXT/MEMORY_TYPE(DEVICE)/DEVICE_POINTER/RANGE_SIZE + profiler state |
| E.4 cuProfiler* + cuEventCreateWithFlags | 5 | Init/Start/Stop 状态转移 + Event flags 默认值 |
| E.5 cuStreamCreateWithFlags + cuStreamGetCaptureInfo | 4 | Default/Blocking/NonBlocking flags + capture info 默认值 |
| E.6 cuMemsetD16 negative path | 2 | Safe no-op + reject illegal alignment |
| E.7 stub sanity (NEW) | 4 | `cuArrayCreate` / `cuGraphCreate` / `cuTexRefCreate` / `cuMemHostRegister` 全部返回 `CUDA_ERROR_NOT_IMPLEMENTED` |

**总数**: 69 → **103** test cases, 282 → **431** assertions

### Phase 1.7.3 — Verification Tool

**Commit**: `ba16139 feat(tools): add verify-phase17.sh`

单文件 Bash runner（匹配现有 `tools/coverage.sh` 和 `tools/docs-audit.sh` 风格）。
模式：
- `--quick` (default): build + run `./test_cuda_shim`
- `--full`: 5 个 test binaries + docs-audit + ABI check
- `--asan`: ASan+UBSan rebuild + `--quick`
- `--target NAME`: 通过 `--test-case` 过滤运行特定测试组（cuFunc|cuPtr|cuCtx|primaryCtx|launch|stubs）
- `--cases 'PATTERN'`: raw doctest filter
- `--build`: 仅构建
- `--clean`: `rm -rf build && --build`
- `--audit`: 仅 docs-audit
- `--abi`: nm ABI 符号数 + 新符号列表
- `--counts`: 快速参考计数（REAL_IMPL/STUB/ABI/docs-audit/tests）
- `-v, --verbose`: 失败时显示详细信息
- `-h, --help`: 帮助

## Outcome

**API Coverage 演进**:

| 指标 | Phase 2 | Phase 1.7 后 |
|------|---------|--------------|
| cu\* 符号导出数 | 79 | **98** |
| REAL_IMPL APIs | 76 | **91** |
| STUB APIs | 68 | **53** |
| E2E test cases | 37 | **103** |
| assertions | 110 | **431** |

**总测试覆盖**: 130+ test cases 跨 5 个二进制（test_cuda_scheduler 8 + test_gpu_architecture 11 + test_gpu_phase2 12 + test_cuda_runtime_api 8 + test_cuda_shim 103 = **142**）

**Backend Path**: Phase 1.5 Stretch 完成后，shim 可同时连接 CudaStub（测试 fixture）和 GpuDriverClient（UsrLinuxEmu 真实后端）路径。

**关键 Commits**:

| Commit | 描述 |
|--------|------|
| `82a2839` | fix(scheduler): remove dynamic_cast hardcoding |
| `d8ca3d3` | fix(shim): register 42 drift APIs (Phase 2 hotfix) |
| `d988393` | feat(shim): Phase 1.6 shim extension |
| `defd272` | feat(shim): Phase 1.7 15 REAL_IMPL + bugfixes |
| `916b299` | test(shim): Phase 1.7 test coverage 69→103 |
| `ba16139` | feat(tools): verify-phase17.sh runner |

## Trigger for Phase 3

Phase 1.6 内置 Phase 3 prep：设计笔记 `2026-07-02-phase3-prep-design-notes.md` 已定义。

**触发条件现状**:
1. ✅ **UsrLinuxEmu Stage 1.4 完成**（Tier-1 + Tier-2）— 已满足
2. ❌ 外部需求 / CI gap / 4+ 周闲置 — 未满足但不再阻塞

→ Phase 3 启动条件 #1 已满足。下一步是创建正式 Phase 3 plan（如 `2026-XX-XX-umd-phase3-stream-mempool.md` 或先启动无依赖项的 Phase 3.3 Event timing + Texture）。

## 与现有 Phase 文档关系

| 文档 | 关系 |
|------|------|
| `phase-2-complete.md` | 本阶段是 Phase 2 完成后立即接续的补充工作，Phase 2 完成（`83ef131`，2026-07-01）→ Phase 1.5 Stretch（`82a2839`，2026-07-02）→ Phase 1.6（`d988393`，2026-07-02）→ Phase 1.7（`defd272`/`916b299`/`ba16139`，2026-07-03）|
| `phase-3-deferred.md` | 触发条件 #1 已满足，Phase 3 不再 DEFERRED。下一步应基于 `phase-3-prep-design-notes.md` 启动 Phase 3.1 / 3.2 / 3.3 |
| `current-status.md` | 应升级以反映 142 total tests + 98 cu\* symbols + `ba16139` HEAD |
| `README.md` | 应升级阶段表与状态快照 |
