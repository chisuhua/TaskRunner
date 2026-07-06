# Design: phase17-test-coverage-completion

> **Change 类型**: 单 workstream E（测试补全 + 必要的最小 bugfix）
> **依赖**: `2026-07-02-phase17-wait-period-work/` ✅ 已应用
> **Scope**: umd-evolution（test files + 必要的 source fix）

## 缺失测试分类（按文件分组）

### E.1: cu_module.cpp 新增 API 测试（10 个）

A.1 + A.2 共 7 个新 API 完全无测试：

| 测试 ID | API | 关键断言 |
|---------|-----|---------|
| T-FUNC-1 | `cuFuncGetAttribute(MAX_THREADS_PER_BLOCK)` | val=1024 |
| T-FUNC-2 | `cuFuncGetAttribute(SHARED_SIZE_BYTES)` | val=48*1024 |
| T-FUNC-3 | `cuFuncGetAttribute(CONST_SIZE_BYTES)` | val=64*1024 |
| T-FUNC-4 | `cuFuncGetAttribute(NUM_REGS)` | val=32 |
| T-FUNC-5 | `cuFuncGetAttribute` 对无效 attribute | INVALID_VALUE |
| T-FUNC-6 | `cuFuncGetAttribute` 对 null function | INVALID_VALUE |
| T-FUNC-7 | `cuFuncSetAttribute` → `cuFuncGetAttribute` round-trip | 读回写入值 |
| T-FUNC-8 | `cuFuncSetCacheConfig` 接受任意值 | SUCCESS |
| T-FUNC-9 | `cuFuncGetModule` 返回 module | mod==原始 module handle |
| T-FUNC-10 | `cuFuncGetModule` 对 null function | INVALID_VALUE |

A.2 occupancy 测试（3 个）：

| 测试 ID | API | 关键断言 |
|---------|-----|---------|
| T-OCC-1 | `cuOccupancyMaxActiveBlocksPerMultiprocessor(256, 0)` | blocks ≥1 且 ≤32 |
| T-OCC-2 | `cuOccupancyMaxActiveBlocksPerMultiprocessorWithFlags` | blocks 同上 |
| T-OCC-3 | `cuOccupancyMaxPotentialBlockSize` | blockSize=256, minGridSize=80 |

### E.2: cu_mem.cpp 新增 API 测试（4 个）

A.3 + A.4：

| 测试 ID | API | 关键断言 |
|---------|-----|---------|
| T-PTR-1 | `cuPointerGetAttribute(CONTEXT)` after `cuMemAlloc` | 返回有效 context（可能 null）|
| T-PTR-2 | `cuPointerGetAttribute(MEMORY_TYPE)` | CU_MEMORYTYPE_DEVICE |
| T-PTR-3 | `cuPointerGetAttribute(DEVICE_POINTER)` | identity（ptr == ptr）|
| T-PTR-4 | `cuPointerGetAttribute(RANGE_SIZE)` | size 等于 alloc 的 size |
| T-MEMSETD16-1 | `cuMemsetD16` 写入第一个 16-bit 值 | SUCCESS |
| T-PROF-1 | `cuProfilerStart/Stop/Initialize` | NOT_SUPPORTED |

### E.3: cu_stream.cpp + cu_event.cpp 新增 API 测试（2 个）

| 测试 ID | API | 关键断言 |
|---------|-----|---------|
| T-STREAM-1 | `cuStreamCreateWithFlags(0)` | 等同 cuStreamCreate |
| T-STREAM-2 | `cuStreamGetCaptureInfo` | status=CAPTURE_STATUS_NONE |
| T-EVENT-1 | `cuEventCreateWithFlags(0)` | 等同 cuEventCreate |

### E.4: cuCtx 完整集（8 个）

| 测试 ID | API | 关键断言 |
|---------|-----|---------|
| T-CTX-1 | `cuCtxGetDevice` | device=0 |
| T-CTX-2 | `cuCtxGetFlags` | flags 有效 |
| T-CTX-3 | `cuCtxPushCurrent` then `cuCtxPopCurrent` | round-trip |
| T-CTX-4 | `cuCtxSynchronize` | SUCCESS（no-op）|
| T-CTX-5 | `cuCtxGetSharedMemConfig` | config 有效 |
| T-CTX-6 | `cuCtxSetSharedMemConfig` then Get | round-trip |
| T-CTX-7 | `cuCtxSetLimit(STACK_SIZE, 2048)` | SUCCESS |
| T-CTX-8 | `cuCtxGetApiVersion` | version ≥11000 |

### E.5: PrimaryCtx 完整集（3 个）

| 测试 ID | API | 关键断言 |
|---------|-----|---------|
| T-PRI-1 | `cuDevicePrimaryCtxReset` | SUCCESS |
| T-PRI-2 | `cuDevicePrimaryCtxGetState` | flags=0, active=1 |
| T-PRI-3 | `cuDevicePrimaryCtxSetFlags` | SUCCESS |

### E.6: Launch API（3 个）

| 测试 ID | API | 关键断言 |
|---------|-----|---------|
| T-LAUNCH-1 | `cuLaunchKernel` 对已注册 kernel | SUCCESS（不是 INVALID_HANDLE）|
| T-LAUNCH-2 | `cuLaunchKernelEx` 对已注册 kernel | SUCCESS |
| T-LAUNCH-3 | `cuLaunchHostFunc` 在 default stream | SUCCESS |

### E.7: STUB sanity（1 batch 测试）

抽样验证 STUB 行为：

| 测试 ID | 验证 | 期望 |
|---------|------|------|
| T-STUB-1 | 抽样 10 个 NOT_IMPLEMENTED API（cuModuleLoadData/Ex/FatBinary、cuTexRefCreate、cuArrayCreate、cuGraphCreate、cuMemcpyAsync、cuMemsetD32、cuProfilerStart、cuMemHostRegister）| 全部返回 CUDA_ERROR_NOT_IMPLEMENTED |

**总测试数**: 10 + 3 + 6 + 2 + 8 + 3 + 3 + 1 = **36 个新测试**（可能因合并而少 1-2 个）

## 关键决策

### D1: 范围严格限定

**选项 A**: 仅添加测试，不改源代码
**选项 B**: 添加测试 + 必要的最小 bugfix（如 T-FUNC-6 暴露的 cuFuncGetAttribute 对 null function 的处理）

**决策**: 选 B。如果某个测试因源代码 pre-existing bug 而失败，**只**修复测试暴露的最小问题，不引入新功能或重构。

### D2: STUB sanity 测试策略

**选项 A**: 1 个测试函数遍历 53 个 STUB 并断言（紧凑但失败信息不明确）
**选项 B**: 1 个测试抽样 10 个 STUB 验证 NOT_IMPLEMENTED 行为

**决策**: 选 B。53 个 STUB 全覆盖意义不大（行为都是 return NOT_IMPLEMENTED），抽样 10 个足够确保生成脚本正常工作。

### D3: 测试粒度

- 每个新 API 一个独立 TEST_CASE（即使行为简单）
- 不使用 `SUBCASE`（doctest feature 但会增加测试报告复杂度）
- 共享 setup（cuModuleLoad + cuModuleGetFunction）放测试内部，不提取 fixture

### D4: 不使用新文件

**选项 A**: 新建 `test_cuda_shim_phase17.cpp` 文件
**选项 B**: 追加到现有 `test_cuda_shim.cpp`

**决策**: 选 B。doctest 简单追加更易维护。`test_cuda_shim` 已是 shim 测试主文件，新测试属于同一 scope。

## 验证矩阵

| Workstream | 验证 | 预期 |
|-----------|------|------|
| E | `grep -c "TEST_CASE" tests/umd/test_cuda_shim.cpp` | ≥95 (原 69, +26+) |
| E | `./build/test_cuda_shim 2>&1 | tail -2` | 95+ PASS / 0 FAIL |
| E | `grep -c "^// STUB" cu_stub_table.inc` | 53 (不变) |
| E | `grep -c "^// REAL_IMPL" cu_stub_table.inc` | 91 (不变) |
| E | `cmake .. -DSANITIZER_ADDRESS=ON -DSANITIZER_UNDEFINED=ON && make -j4 && ./build/test_cuda_shim` | 0 sanitizer error |
| E | `./tools/docs-audit.sh` | 53 PASS（不变）|
| E | 全测试套件: 5 binaries | 134+ PASS（108 原 + 26 新）|

## REAL_IMPL 覆盖目标

| 阶段 | 覆盖 | 增加 |
|------|------|------|
| Phase 1.7 之前 (commit d988393) | 32/76 = 42% | — |
| Phase 1.7 已应用 | 46/91 = 50.5% | +8.5% |
| **Phase 1.7 + 本 change (E)** | **≥77/91 ≥ 85%** | **+34%** |

覆盖公式：当前未覆盖 45 个 → E.1-7 新增 26+ 测试可覆盖 31+ → 总覆盖 77+ / 91 = 85%
