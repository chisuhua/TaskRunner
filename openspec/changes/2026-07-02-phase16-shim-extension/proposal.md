# Change: phase16-shim-extension

> **状态**: ⚠️ PROPOSED（2026-07-02，等待启动）
> **创建**: 2026-07-02
> **来源**: Phase 1.6 wait-period work (Phase 1.5 Stretch 完成 → Phase 3 等待 UsrLinuxEmu Stage 1.4)
> **前置**: commit d8ca3d3（Phase 2 drift hotfix）✅ DONE
> **supersedes**: `openspec/changes/archive/2026-07-02-umd-shim-coverage-hardening/` (SUPERSEDED)

## Why

### 现状（hotfix d8ca3d3 之后）

- `libcuda_taskrunner.so` 导出 79 cu\* 符号，全部在 .cpp 中有真实实现
- `tools/generate_cu_stubs.py` CRITICAL_APIS_IMPL_REQUIRED 字典 = 79 项（消除 drift）
- `cu_stub_table.inc` 总声明 144（79 REAL_IMPL + 65 STUB）
- docs-audit.sh Check 9 改为动态 AST 读取（单一数据源）
- 76/76 测试通过（baseline 保持）

### Gap（hotfix 揭示的语义层问题）

| Gap | 来源 | 影响 |
|-----|------|------|
| `cuMemGetInfo` 硬编码 4GB free / 8GB total | cu_mem.cpp 当前实现 | fake-success — 应用程序基于错误信息分配资源 |
| `cuModuleLoadData/Ex/FatBinary` 标记为 REAL_IMPL 但仍返回 NOT_IMPLEMENTED | cu_module.cpp 当前实现 | 假 REAL_IMPL — 误导 docs-audit 和开发人员 |
| 8 类新注册 API（cuEvent*/cuStream*/cuCtx*Config 等）测试覆盖低 | test_cuda_shim.cpp 仅 37 cases | 回归风险高，Phase 3 kickoff 时难发现 breakage |
| H-3 follow-up F1-F4 仍 OPEN | UsrLinuxEmu/openspec/changes/archive/2026-06-22-h3-phase2-management/ | 文档内部不一致累计 10 天 |
| Phase 3 skeleton 文档缺失 | docs/superpowers/plans/2026-XX-XX-umd-phase3-* | Stage 1.4 触发后延迟 1-2 周 |

### Why Now

1. **Phase 1.5 Stretch + hotfix d8ca3d3 完成** — 基础设施层已就位
2. **UsrLinuxEmu Stage 1.0-1.3 进行中**（2-4 周窗口）— TaskRunner 有独立可推进的工作
3. **所有 Phase 1.6 工作不依赖 UsrLinuxEmu Stage 1.0-1.3**（A.1 除外需要 GpuDriverClient 数据源）
4. **3-4 天可完成**，风险低，价值高（修复 fake-success + 测试覆盖 + 跨仓 PR 准备）

## What Changes

### A.1: cuMemGetInfo 真实数据源（2-3 h）

**当前**：cu_mem.cpp 硬编码 `4GB free / 8GB total`
**修正**：通过 GpuDriverClient 接口查询 `gpu_device_info.total_memory`
**若 GpuDriverClient 不可用**：返回 `CUDA_ERROR_NOT_SUPPORTED`（不假成功）

### A.2: cuModuleLoad* 假 REAL_IMPL 修正（30 min）

**当前**：`cuModuleLoadData`、`cuModuleLoadDataEx`、`cuModuleLoadFatBinary` 在 CRITICAL_APIS_IMPL_REQUIRED 字典中，但实现体仅返回 NOT_IMPLEMENTED
**修正**：从 CRITICAL_APIS_IMPL_REQUIRED 移除这 3 个，回归到 `// STUB` 标记
**重新生成 cu_stub_table.inc**：STUB 从 65 升到 68，REAL_IMPL 从 79 降到 76
**长期方案**：Phase D-3 ELF/CUBIN parser 实现（deferred indefinitely per gap-analysis.md）

### A.3: 新注册 API 测试覆盖（1-2 d）

为 hotfix 注册的 8 类 API 各添加 1-2 个 E2E 测试：
- cuEventCreate/Record/Synchronize/ElapsedTime（4 tests）
- cuStreamCreate/Synchronize/Query/WaitEvent（3 tests）
- cuCtxGet/SetCacheConfig、cuCtxGet/SetSharedMemConfig、cuCtxGet/SetLimit（3 tests）
- cuGetErrorName/String 错误码映射（已存在测试可能需要扩展）
- cuLaunchCooperativeKernel 返回 NOT_SUPPORTED 而非 delegate（1 test）

目标：37 → ≥47 cases

### H-3: 跨仓 PR 描述（30 min）

为 UsrLinuxEmu 仓的 H-3 follow-up F1-F4 准备 PR 描述：
- F1 [MEDIUM]: README.md ACTIVE/DRAFT 不一致
- F2 [MEDIUM]: tasks.md 测试计数 10/10 → 12/12
- F3 [LOW]: design.md vs spec.md 日志冲突
- F4 [MINOR]: design.md:277 缺日期前缀

**不在 TaskRunner 仓执行**（文件在 UsrLinuxEmu 仓），仅生成 PR 描述文档供 owner 采纳。

### Phase 3 skeleton: `phase3-prep-design-notes.md`（1 h）

为 Phase 3 kickoff 准备设计备注文档（避免与 `phase-3-deferred.md` 命名冲突）：
- Phase 3 priority matrix（从 phase-3-deferred.md 复制）
- 4 个 Trigger Conditions（Stage 1.4 / 外部需求 / CI 缺口 / 时间驱动）
- 5 个 Open Decisions（Q1-Q5）
- 实施时序预估（3.1 = 1-2 w, 3.2 = 2-3 w, 3.3 = 6-9 w）
- 标记 DRAFT — 无实施，仅设计保留

## Capabilities

### New Capabilities

- **`phase16-shim-extension`**: 解决 hotfix 揭示的语义层问题（fake-success + 假 REAL_IMPL），扩展测试覆盖，准备 H-3 跨仓 cleanup 和 Phase 3 skeleton

### Modified Capabilities

- NONE（IGpuDriver 31-method 契约 frozen）

## Impact

| 影响项 | 范围 | 风险 |
|--------|------|------|
| 代码 | `src/umd/libcuda_shim/cu_mem.cpp` + `cu_module.cpp`（实现修改） | 低 |
| 工具 | `tools/generate_cu_stubs.py`（删除 3 项）+ 重新生成 cu_stub_table.inc | 低 |
| 测试 | `tests/umd/test_cuda_shim.cpp`（+10 cases） | 低 |
| 文档 | 1 个 Phase 3 prep 文档新建 + 1 个 PR 描述 | 极低 |
| ABI | 79 符号不变 | 无 |
| 测试 | 37 → ≥47 cases | 低（新增测试） |
| UsrLinuxEmu | 0 行 TaskRunner 侧代码改动 | 无 |
| 跨仓 | 1 个 PR 描述（不在 TaskRunner 仓执行） | 极低 |

**风险缓解**：
- A.1 修改 cuMemGetInfo 时保留 8GB 硬编码 fallback（若 GpuDriverClient 不可用）
- A.2 仅修改工具配置，不修改 .cpp 行为
- A.3 新增测试不修改现有 37 个测试
- 79 符号契约保持
- 所有改动都是 additive（除 A.2 是配置层面的 demote）

## 关联 Changes

- **前置**: commit d8ca3d3（hotfix，独立完成）
- **supersedes**: `archive/2026-07-02-umd-shim-coverage-hardening/`（错误基线）
- **后续**: Phase 3 kickoff（Stage 1.4 触发后）
- **关联文档**:
  - `docs/umd-evolution/roadmap/phase-2-complete.md`
  - `docs/umd-evolution/roadmap/phase-3-deferred.md`
  - `docs/superpowers/specs/2026-07-02-phase1.5-cudastub-dynamic-cast-fix.md`
  - `../../../UsrLinuxEmu/docs/07-integration/h3-activation-followup.md`
- **关联 ADR/TADR**:
  - UsrLinuxEmu ADR-032 (IGpuDriver)
  - UsrLinuxEmu ADR-035 (governance)
  - UsrLinuxEmu ADR-036 (3-way separation)
  - TaskRunner tadr-301 (IGpuDriver contract frozen)
  - TaskRunner tadr-205 (UMD PoC roadmap, deferred Phase D)