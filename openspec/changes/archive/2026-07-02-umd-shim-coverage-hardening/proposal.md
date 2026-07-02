# Change: umd-shim-coverage-hardening

> **状态**: ⚠️ PROPOSED（2026-07-02，等待启动）
> **创建**: 2026-07-02
> **来源**: TaskRunner umd-evolution roadmap Phase 2 complete + Phase 3 deferred
> **关联 TaskRunner Spec**: `external/TaskRunner/docs/superpowers/specs/2026-07-02-umd-shim-coverage-hardening.md`
> **关联 TaskRunner Plan**: `external/TaskRunner/docs/superpowers/plans/2026-07-02-umd-shim-coverage-hardening.md`
> **Scope**: `umd-evolution` + minor `test-fixture` (H-3 fixes) + minor `shared` (docs-audit checks)

## Why

### 现状

Phase 2 LD_PRELOAD shim (`libcuda_taskrunner.so`) 已于 2026-07-01 完成（commit `83ef131`），导出 **79 个 cu\* 符号**。其中 **41 个是真实实现**，**38 个返回 `CUDA_ERROR_NOT_IMPLEMENTED`**（functional placeholders）。

Phase 1.5 Stretch (commit `82a2839`) 已完成 dynamic_cast 修复，打通了 `libcuda_taskrunner.so → GpuDriverClient → UsrLinuxEmu` 真实后端路径。

Phase 3 (Stream/MemPool/Event) **⏸️ DEFERRED**，等待 UsrLinuxEmu Stage 1.4 KFD Consumer 集成验证触发。

**与此同时，UsrLinuxEmu 已启动 Stage 1.0（PCIe 设备模拟）**，预计 2-3 周完成 Stage 1.0-1.3。这段窗口期 TaskRunner 侧需要可独立推进的工作。

### Gap

| Gap | 影响 |
|-----|------|
| 38 个 cu\* stub 中约 10 个仅需 TaskRunner 内部 bookkeeping（不依赖 UsrLinuxEmu 后端能力），可立即实现 | shim API 覆盖率不足，CUDA 应用程序可能在非测试路径上获得 `NOT_IMPLEMENTED` |
| 测试覆盖仅 37 个 E2E cases，缺少 threading、error path、attribute coverage | 回归风险高，Phase 3 时新增功能难以发现 breakage |
| 4 项 H-3 follow-up fix（F1-F4）尚未处理 | H-3 激活 commit 留下文档内部不一致 |
| Phase 3 启动时需要重新设计架构（无骨架） | Stage 1.4 触发 Phase 3 时延迟 1-2 周 |
| docs-audit.sh 仅检查 ≥30 symbols + 41 critical APIs | 不能跟踪 Phase 1.6 stub reduction 进度 |

### Why Now

1. **Phase 1.5 Stretch 已完成**（2026-07-02, openspec `taskrunner-umd-backend-enable` 已归档）
2. **UsrLinuxEmu Stage 1.0 已启动**（约 2-3 周后才到 Stage 1.4）
3. **所有 Phase 1.6 工作均独立于 UsrLinuxEmu Stage 1.0-1.3**（无跨仓耦合）
4. **估算 5-8 工作日**，风险低，价值高（提升 Phase 2 shim 实用度，为 Phase 3 铺路）
5. **H-3 follow-up 4 项已逾 1 周**（自 2026-06-22 起），欠清理

## What Changes

### TaskRunner 侧（本 change）

**Sub-plan A — H-3 Follow-up Fixes（4 项 ~20 min）**:
- F1 [MEDIUM]: `openspec/changes/h3-phase2-management/README.md` ACTIVE/DRAFT 不一致
- F2 [MEDIUM]: `openspec/changes/h3-phase2-management/tasks.md` 测试计数 10/10 → 12/12
- F3 [LOW]: `openspec/changes/h3-phase2-management/design.md` vs `spec.md` 日志冲突
- F4 [MINOR]: `openspec/changes/h3-phase2-management/design.md:277` 缺日期前缀

**Sub-plan B — cuDeviceGetAttribute Expansion（~1 天）**:
- 扩展 `src/umd/libcuda_shim/cu_query.cpp` 覆盖 ~30 个 CUdeviceAttribute
- 提升 `cuDeviceComputeCapability` 为独立 REAL_IMPL
- 重新生成 `cu_stub_table.inc`（STUB → REAL_IMPL）
- 新增 4 个 cuDeviceGetAttribute 测试

**Sub-plan C — Context Config & Error Helpers（~1.5 天）**:
- 实现 `cuCtxGet/SetCacheConfig`（cu_ctx.cpp）
- 实现 `cuCtxGet/SetSharedMemConfig`（cu_ctx.cpp）
- 实现 `cuCtxGet/SetLimit`（cu_ctx.cpp）
- 实现 `cuGetErrorName` / `cuGetErrorString`（cu_init.cpp，含 80+ 错误码查找表）
- 新增 5 个测试

**Sub-plan D — Memory Info & Pointer/Func Attributes（~1.5 天）**:
- 实现 `cuMemGetInfo`（cu_mem.cpp，含虚拟 GPU 内存大小）
- 实现 `cuFuncGetAttribute`（cu_module.cpp，4 种常用 attribute）
- 实现 `cuPointerGetAttribute`（cu_mem.cpp，5 种常用 attribute）
- 新增 4 个测试

**Sub-plan E — Test Coverage Expansion + docs-audit Enhancement（~1 天）**:
- 新增 threading 测试（2 个）
- 新增 error path 测试（4 个）
- 新增 docs-audit 检查（G5: critical API count ≥41, backend-independent stub count ≤28）
- 创建 Phase 3 骨架文档 `docs/superpowers/plans/2026-07-02-umd-phase3-skeleton.md`
- 在 `docs/umd-evolution/roadmap/README.md` 添加 `phase-3-skeleton` 条目

### UsrLinuxEmu 侧

**无代码改动**。本 change 完成后按 ADR-035 §Rule 5.1 4 步协议同步 submodule 指针。

## Capabilities

### New Capabilities

- **`umd-shim-coverage-hardening`**: 提升 `libcuda_taskrunner.so` shim 的 cu\* API 覆盖率（10 stubs → real impls），强化测试覆盖（37 → ≥50 cases），跟踪 Phase 1.6 stub reduction 进度

### Modified Capabilities

- NONE（IGpuDriver 31-method 契约 tadr-301 保持 frozen）

## Impact

| 影响项 | 范围 | 风险 |
|--------|------|------|
| 代码 | `src/umd/libcuda_shim/` 4 个 .cpp + `tests/umd/test_cuda_shim.cpp` | 低（additive only） |
| 工具 | `tools/generate_cu_stubs.py` 参数化 + `tools/docs-audit.sh` +2 检查 | 无（additive） |
| 文档 | 3 个 H-3 follow-up 文件 + 1 个 Phase 3 骨架 | 极低 |
| ABI | `libcuda_taskrunner.so` 79 符号不变 | 无 |
| 测试 | 76 → ≥89 cases | 低（新增测试，不修改现有） |
| UsrLinuxEmu | 0 行代码改动 | 无 |
| 跨仓 | Submodule pointer bump（按 ADR-035 §Rule 5.1） | 低 |

**风险缓解**:
- 所有改动都是 additive（不修改现有 API 签名）
- 79 cu\* 符号契约保持（docs-audit.sh Check 9 强制）
- 76 现有测试保持（任何回归都会被现有测试捕获）
- 单 commit 可回滚（`git revert`）
- 无 IGpuDriver 契约变更（tadr-301 frozen）

## 关联 Changes

- **前置依赖**: `openspec/changes/archive/2026-07-02-taskrunner-umd-backend-enable/` ✅ DONE（Phase 1.5 Stretch）
- **后续依赖**: Phase 3 kickoff (when UsrLinuxEmu Stage 1.4 starts)
- **并行工作**: UsrLinuxEmu Stage 1.0-1.3 (无依赖)
- **关联文档**:
  - `external/TaskRunner/docs/umd-evolution/roadmap/phase-2-complete.md`
  - `external/TaskRunner/docs/umd-evolution/roadmap/phase-3-deferred.md`
  - `external/TaskRunner/docs/superpowers/specs/2026-07-02-phase1.5-cudastub-dynamic-cast-fix.md`
  - `../../../UsrLinuxEmu/docs/07-integration/h3-activation-followup.md`
- **关联 ADR/TADR**:
  - [UsrLinuxEmu ADR-032](../../00_adr/adr-032-h2-5-igpu-driver-abstraction.md) (IGpuDriver)
  - [UsrLinuxEmu ADR-035](../../00_adr/adr-035-governance-policy.md) (governance)
  - [UsrLinuxEmu ADR-036](../../00_adr/adr-036-three-way-separation.md) (3-way)
  - TaskRunner tadr-301 (IGpuDriver contract frozen)
  - TaskRunner tadr-205 (UMD PoC roadmap, deferred Phase D)