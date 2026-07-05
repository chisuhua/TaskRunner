# Change: phase17-wait-period-work

> **状态**: ⚠️ PROPOSED（2026-07-02，待启动）
> **创建**: 2026-07-02
> **基础**: commit d988393（Phase 1.6 shim extension）✅ DONE
> **Scope**: 4 独立 workstream（A-D），可按需单独执行

## Why

### 现状

Phase 1.6（commit d988393）已完成。当前 shim 基线：
- 144 总声明 / 76 REAL_IMPL / 68 STUB / 79 导出符号
- 49 个测试，docs-audit 53/54 PASS
- cuMemGetInfo 真实数据源（不再 fake-success）
- cuModuleLoad*Data 已诚实标记 STUB
- Phase 3 prep 设计备注已就位

**UsrLinuxEmu Stage 1.0-1.3 进行中**（2-4 周窗口），Stage 1.4 触发前 TaskRunner 有 4 个可并行的独立 workstream。

### Workstream A: Backend-independent STUB 实现

68 个 STUB 中约 14 个不依赖 UsrLinuxEmu 后端能力，仅需 TaskRunner 侧 bookkeeping：
- `cuFuncGetAttribute`, `cuFuncSetAttribute`, `cuFuncSetCacheConfig`, `cuFuncGetModule` — 4 个 function 属性 API
- `cuOccupancyMax*` — 3 个 occupancy 启发式 API
- `cuPointerGetAttribute` — BO 内存指针属性查询（需 shim 层增加 BO 映射表）
- `cuProfilerStart/Stop/Initialize` — 3 个 profiler 状态 API
- `cuStreamCreateWithFlags`, `cuStreamGetCaptureInfo` — 2 个 stream 变体
- `cuEventCreateWithFlags`, `cuMemsetD16` — 2 个简单变体

**效果**：STUB 68 → ~54，REAL_IMPL 76 → ~90

### Workstream B: CI/Build Infrastructure

项目当前无任何 CI 配置。shim 测试完全靠手动执行。在等待窗口中补齐：
- **ASan/UBSan/TSan** CMake 选项（捕获内存错误、未定义行为、数据竞争）
- **Coverage**（gcov/lcov，量化测试缺口）
- **GitHub Actions 工作流**（自动构建 + 跑 49 测试 + docs-audit）
- **pre-commit hook**（自动 docs-audit + clang-format 检查）

### Workstream C: Test Coverage Depth

49 个测试主要覆盖 happy path。缺少：
- cuDeviceGetAttribute 全属性测试（当前 6/80+）
- Threading 测试（多线程 cuCtx/cuStream 并发）
- OOM 模拟测试（malloc 失败路径）
- 错误路径测试（NULL 指针、非法 handle）

### Workstream D: Documentation & Phase 3 Prep

为 Stage 1.4 触发后的 Phase 3 kickoff 加速：
- `cuMemPool*` 接口设计稿（Phase 3.2 核心）
- `cuStreamBeginCapture/EndCapture` 接口设计稿（Phase 3.1 核心）
- Shim 架构概览文档

## What Changes

| Workstream | Files Modified | Files Created | Risk |
|------------|---------------|---------------|------|
| **A** | `src/umd/libcuda_shim/cu_module.cpp`, `cu_mem.cpp`, `cu_stream.cpp`, `cu_event.cpp` | — | Low |
| **A** | `tools/generate_cu_stubs.py`（CRITICAL_APIS_IMPL_REQUIRED 加 14 项） | — | Low |
| **A** | `tests/umd/test_cuda_shim.cpp`（+11 测试） | — | Low |
| **B** | `CMakeLists.txt`（ASan/TSan 选项） | `.github/workflows/shim.yml`, `.githooks/pre-commit`, `tools/coverage.sh` | None |
| **C** | `tests/umd/test_cuda_shim.cpp`（+21 测试） | — | Low |
| **D** | — | `docs/superpowers/specs/2026-XX-XX-phase3-stream-design.md` | None |
| **D** | — | `docs/superpowers/specs/2026-XX-XX-phase3-mempool-design.md` | None |
| **D** | — | `docs/superpowers/architecture/shim-layer.md` | None |

## Capabilities

### New Capabilities

- **`phase17-stub-implementation`**: 14 个 backend-independent STUB → REAL_IMPL（STUB 68→54）
- **`phase17-ci-infrastructure`**: CI 工作流、sanitizer 构建、pre-commit、coverage
- **`phase17-test-depth`**: 测试从 49 → ~70（覆盖 threading、error path、属性全覆盖）
- **`phase17-phase3-prep`**: Phase 3.1/3.2 接口设计稿 + shim 架构文档

## Impact

| 影响项 | Workstream | 风险 |
|--------|-----------|------|
| 代码 | A（14 个新实现） | 低（additive，不影响现有 79 符号） |
| 构建 | B（CMake 选项） | 无（不改变默认构建行为） |
| 测试 | A（+11）+ C（+21）= +32 total | 低（新测试不修改现有） |
| 文档 | D（3 个新文档） | 极低 |
| ABI | A（79 符号不变） | 无 |
| CI | B | 无（新增 workflow 文件） |
| UsrLinuxEmu | 全部 | 0 行代码改动 |

**风险缓解**：
- Workstream A-D 完全独立，可按任意顺序或单独执行
- A 仅添加新实现 + 注册 CRITICAL_APIS，不修改 ABI
- B 仅添加新 CMake 选项（默认 off），不改变现有构建
- C 仅添加新测试，不修改现有测试
- D 纯文档

## 关联 Changes

- **前置**: commit d988393（Phase 1.6 shim extension）✅ DONE
- **前置**: commit d8ca3d3（Phase 2 drift hotfix）✅ DONE
- **后续**: Phase 3 kickoff（UsrLinuxEmu Stage 1.4 触发后）
- **关联文档**:
  - `docs/umd-evolution/roadmap/phase-2-complete.md`
  - `docs/umd-evolution/roadmap/phase-3-deferred.md`
  - `docs/superpowers/plans/2026-07-02-phase3-prep-design-notes.md`
  - `../../../../UsrLinuxEmu/docs/roadmap/stage-1-kernel-emu.md`