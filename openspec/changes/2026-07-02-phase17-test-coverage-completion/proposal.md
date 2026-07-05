# Change: phase17-test-coverage-completion

> **状态**: ⚠️ PROPOSED（2026-07-03，待启动）
> **创建**: 2026-07-03
> **基础**: `2026-07-02-phase17-wait-period-work/` 已应用（REAL_IMPL 76→91, STUB 68→53, test 49→69, **但 15 个新 API 完全无测试**）
> **Scope**: 单 workstream E（仅测试扩展，不改源代码）

## Why

### 现状（Phase 1.7 已应用，2026-07-03 审查发现）

Phase 1.7 wait-period work 完成了 15 个 backend-independent cu\* STUB → REAL_IMPL 的代码实现：
- A.1: cuFuncGetAttribute / cuFuncSetAttribute / cuFuncSetCacheConfig / cuFuncGetModule（4 个）
- A.2: cuOccupancyMaxActiveBlocksPerMultiprocessor / WithFlags / cuOccupancyMaxPotentialBlockSize（3 个）
- A.3: cuPointerGetAttribute（1 个）
- A.4: cuStreamCreateWithFlags / cuStreamGetCaptureInfo / cuEventCreateWithFlags / cuMemsetD16 / cuProfiler{Start,Stop,Initialize}（7 个）

代码已落库但**覆盖率审查（2026-07-03）揭示严重 gap**：
- 测试文件 `test_cuda_shim.cpp` 共 69 个 TEST_CASE
- 50.5% 的 REAL_IMPL API 被测试覆盖（46/91）
- **Phase 1.7 新增的 15 个 API 中 0 个被测试**（0% 覆盖）
- 多个 cuCtx API（9 个）、PrimaryCtx API（3 个）、Launch API（3 个）缺失测试
- 53 个 STUB API 没有 sanity 测试

### Gap（覆盖率审查产物）

| 类别 | 缺失 API 数 | 影响 |
|------|------------|------|
| **A.1 cuFunc\*** | 4 | 4 个新 API 完全没有测试守护，重构 cu_module.cpp 时行为变化不会被发现 |
| **A.2 cuOccupancy\*** | 3 | 3 个新 API 完全没有测试，启发式可能产生无效 blocks 数 |
| **A.3 cuPointer\*** | 1 | 1 个新 API 完全没有测试，BO 指针属性可能返回错值 |
| **A.4 轻量 stub** | 7 | 7 个新 API 完全没有测试（部分仅 trivial stub 但仍需 sanity test）|
| **cuCtx 完整集** | 8 | Push/Pop/Sync/GetDevice/Flags/SharedMemConfig/SetLimit 等 |
| **PrimaryCtx** | 3 | Reset/GetState/SetFlags |
| **Launch** | 3 | cuLaunchKernel/Ex/HostFunc |
| **Memory advanced** | 5 | cuMemAllocHost/FreeHost/Managed/Pitch/Async/Set* |
| **STUB sanity** | 53 | 53 个 NOT_IMPLEMENTED API 没有任何 sanity test（应验证确实返回 NOT_IMPLEMENTED）|

### Why Now

- Phase 1.7 代码已合并，重构窗口已打开
- Phase 3 kickoff（UsrLinuxEmu Stage 1.4 触发后）将基于现有 91 个 REAL_IMPL API，新 API 的回归保护越早补全越好
- UsrLinuxEmu Stage 1.0-1.3 仍在进行（2-4 周），是补全测试的窗口期
- Workstream B（CI infrastructure）已建立测试基础设施（doctest + sanitizer），新增测试立即可用

## What Changes

| 类别 | 文件 | 新增 | 风险 |
|------|------|------|------|
| **新测试** | `tests/umd/test_cuda_shim.cpp` | +25-30 TEST_CASE | None（仅追加，不修改现有）|
| **辅助修复** | `src/umd/libcuda_shim/cu_module.cpp` | 可能修复 INVALID_HANDLE 检测（cuFuncGetAttribute 对已销毁的 function） | Low |
| **辅助修复** | `src/umd/libcuda_shim/cu_mem.cpp` | 可能增强 cuMemsetD8 stub（仅写第一个值） | Low |
| **辅助修复** | `src/umd/libcuda_shim/cu_stream.cpp` | 可能修复 cuStreamBeginCapture/EndCapture | Low |

## Capabilities

### New Capabilities

- **`phase17-test-coverage-completion`**: 补全 Phase 1.7 新增 15 API + cuCtx 集 + PrimaryCtx + Launch + STUB sanity 测试，REAL_IMPL 覆盖 50.5%→≥85%

## Impact

| 影响项 | 数量 | 风险 |
|--------|------|------|
| 测试 | +25-30 cases（69→95+） | None（purely additive）|
| 代码 | 可能 2-3 个小修复（INVALID_HANDLE 检查、trivial stub 增强）| Low |
| ABI | 0 changes | None |
| docs-audit | 0 changes | None |
| UsrLinuxEmu | 0 lines | None |

**风险缓解**：
- 全部为测试追加，不修改现有测试断言
- 任何代码修复都是 pre-existing 问题暴露（如 cuFuncGetAttribute 对已销毁 function 的检查缺失）
- 修复遵循最小变更原则，bugfix 规则：仅修复测试暴露的问题，不重构

## 关联 Changes

- **前置**: `2026-07-02-phase17-wait-period-work/`（已应用，本 change 在其上补全测试）
- **后续**: 无（覆盖率补全为最终状态）
- **关联文档**:
  - `openspec/changes/2026-07-02-phase17-wait-period-work/{proposal,design,tasks}.md`
  - `docs/superpowers/specs/2026-07-02-phase3-*-design.md`（Phase 3 设计稿）
