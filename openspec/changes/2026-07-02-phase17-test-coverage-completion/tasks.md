# Tasks: phase17-test-coverage-completion

> **状态**: ⚠️ PROPOSED（2026-07-03）
> **依赖**: `2026-07-02-phase17-wait-period-work/` ✅ 已应用
> **基础基线**: 144 声明 / 91 REAL_IMPL / 53 STUB / 49+20=69 测试 / 53 docs-audit

---

## 前置条件

- [ ] **0.1** 确认 `2026-07-02-phase17-wait-period-work/` 已应用:
  ```bash
  grep -c "^// REAL_IMPL" src/umd/libcuda_shim/cu_stub_table.inc   # 91
  grep -c "^// STUB" src/umd/libcuda_shim/cu_stub_table.inc       # 53
  grep -c "TEST_CASE" tests/umd/test_cuda_shim.cpp                # 69
  ```
- [ ] **0.2** 确认基线测试全 PASS:
  ```bash
  cd build && make -j4
  ./test_cuda_shim 2>&1 | tail -2   # 69/69 PASS
  ```

---

## Workstream E: Test Coverage Completion ⏱ 0.5-1 天

> **Commit**: `test(shim): Phase 1.7 — test coverage completion (69→95+ for Phase 1.7 new APIs + cuCtx/Launch/STUB sanity)`

### E.1: cuFunc* 属性 API 测试（10 cases, ~1.5 h）

- [ ] 添加 10 个 TEST_CASE 到 `tests/umd/test_cuda_shim.cpp` 末尾（namespace 内）:
  - `cuFuncGetAttribute returns 1024 for MAX_THREADS_PER_BLOCK`
  - `cuFuncGetAttribute returns 48KB for SHARED_SIZE_BYTES`
  - `cuFuncGetAttribute returns 64KB for CONST_SIZE_BYTES`
  - `cuFuncGetAttribute returns 32 for NUM_REGS`
  - `cuFuncGetAttribute returns INVALID_VALUE for unknown attribute`
  - `cuFuncGetAttribute returns INVALID_VALUE for null function`
  - `cuFuncSetAttribute then Get returns same value` (round-trip)
  - `cuFuncSetCacheConfig accepts valid config`
  - `cuFuncGetModule returns owning module handle`
  - `cuFuncGetModule returns INVALID_VALUE for null function`
- [ ] 测试 helper：使用现有 `cuModuleLoad + cuModuleGetFunction` 获取 CUfunction handle
- [ ] 验证: `./build/test_cuda_shim 2>&1 | tail -2` — 79+ PASS
- [ ] **如果 T-FUNC-6/T-FUNC-10 失败**（cuFuncGetAttribute/cuFuncGetModule 对 null function 返回 INVALID_HANDLE 而非 INVALID_VALUE）:
  - 决策树：检查源代码 `cuFuncGetAttribute` 的 null 检查
  - 如缺失则修复（最小变更：添加 `if (!f) return CUDA_ERROR_INVALID_VALUE;`）
  - 但 `cuFuncGetModule` 当前对 null 返回 INVALID_VALUE 已正确

### E.2: cuOccupancy* 启发式 API 测试（3 cases, ~30 min）

- [ ] 添加 3 个 TEST_CASE:
  - `cuOccupancyMaxActiveBlocksPerMultiprocessor returns >= 1 for block_size 256`
  - `cuOccupancyMaxActiveBlocksPerMultiprocessorWithFlags delegates to non-flags variant`
  - `cuOccupancyMaxPotentialBlockSize returns blockSize=256 minGridSize=80`
- [ ] 验证: 82+ PASS

### E.3: cuPointerGetAttribute + A.4 轻量 stub 测试（6 cases, ~1 h）

- [ ] 添加 6 个 TEST_CASE:
  - `cuPointerGetAttribute CONTEXT after cuMemAlloc` (返回 current TLS context，可能 null)
  - `cuPointerGetAttribute MEMORY_TYPE returns DEVICE`
  - `cuPointerGetAttribute DEVICE_POINTER returns identity`
  - `cuPointerGetAttribute RANGE_SIZE returns alloc size`
  - `cuMemsetD16 writes first 16-bit value and returns SUCCESS`
  - `cuProfilerStart/Stop/Initialize return NOT_SUPPORTED` (一个 TEST_CASE 串 3 个)
- [ ] 验证: 88+ PASS

### E.4: cuCtx 完整集（8 cases, ~1 h）

- [ ] 添加 8 个 TEST_CASE:
  - `cuCtxGetDevice returns 0 for current context`
  - `cuCtxGetFlags returns non-zero after ctx create`
  - `cuCtxPushCurrent then PopCurrent round-trip` (创建 ctx → push → pop)
  - `cuCtxSynchronize returns SUCCESS (no-op)`
  - `cuCtxGetSharedMemConfig returns valid config`
  - `cuCtxSetSharedMemConfig then Get round-trip`
  - `cuCtxSetLimit STACK_SIZE accepts value`
  - `cuCtxGetApiVersion returns CUDA version`
- [ ] **如果 T-CTX-5/T-CTX-6 失败**（cuCtxGetSharedMemConfig 同样问题：未实现，参数类型问题）:
  - 检查源代码 `cuCtxGetSharedMemConfig` 和 `cuCtxSetSharedMemConfig`
  - 可能需要: 添加 `g_shared_mem_config` 静态变量，类比 `g_cache_config` 的修复
- [ ] 验证: 96+ PASS

### E.5: PrimaryCtx 完整集（3 cases, ~30 min）

- [ ] 添加 3 个 TEST_CASE:
  - `cuDevicePrimaryCtxReset returns SUCCESS for dev 0`
  - `cuDevicePrimaryCtxGetState returns active=1`
  - `cuDevicePrimaryCtxSetFlags accepts flags for dev 0`
- [ ] 验证: 99+ PASS

### E.6: Launch API（3 cases, ~1 h）

- [ ] 添加 3 个 TEST_CASE:
  - `cuLaunchKernel with valid registered function returns SUCCESS` (需要先 cuModuleLoad + cuModuleGetFunction)
  - `cuLaunchKernelEx with valid registered function returns SUCCESS`
  - `cuLaunchHostFunc with default stream returns SUCCESS`
- [ ] **如果 T-LAUNCH-1/T-LAUNCH-2 失败**（cuLaunchKernel/Ex 对已注册 function 返回 NOT_FOUND 或 INVALID_HANDLE）:
  - 检查 cu_launch.cpp 中 to_cuda_error 映射
  - 确认 CudaRuntimeApi::launch_kernel 行为
- [ ] **如果 T-LAUNCH-3 失败**（cuLaunchHostFunc 未实现）:
  - 检查 cu_launch.cpp 是否有 cuLaunchHostFunc 实现
  - 如缺失则 NOT_IMPLEMENTED 是合法 stub 行为，调整测试期望
- [ ] 验证: 102+ PASS

### E.7: STUB sanity 批量测试（1 case 串 10 个，~15 min）

- [ ] 添加 1 个 TEST_CASE `STUB APIs return NOT_IMPLEMENTED`:
  - 内含 10 个 API 调用（cuModuleLoadData/Ex/FatBinary、cuTexRefCreate、cuArrayCreate、cuGraphCreate、cuMemcpyAsync、cuMemsetD32、cuProfilerStart、cuMemHostRegister）
  - 全部断言 `== CUDA_ERROR_NOT_IMPLEMENTED`
- [ ] 验证: 103+ PASS

### E.8: Final E verification

- [ ] `grep -c "TEST_CASE" tests/umd/test_cuda_shim.cpp` ≥ 95
- [ ] `./build/test_cuda_shim 2>&1 | tail -2` — 95+ PASS / 0 FAIL
- [ ] `grep -c "^// STUB" cu_stub_table.inc` = 53 (不变)
- [ ] `grep -c "^// REAL_IMPL" cu_stub_table.inc` = 91 (不变)
- [ ] `cmake .. -DSANITIZER_ADDRESS=ON -DSANITIZER_UNDEFINED=ON && make -j4 && ./build/test_cuda_shim` — 0 sanitizer error
- [ ] `./tools/docs-audit.sh` — 53 PASS (不变)
- [ ] 全 5 binaries 测试: 134+ PASS (108 原 + 26 新)

---

## 最终验证

- [ ] **测试计数**: ≥95 cases PASS (原 69, +26+)
- [ ] **REAL_IMPL 覆盖**: ≥85% (原 50.5%)
- [ ] **Phase 1.7 新增 15 API**: 全部有专属测试
- [ ] **ASan+UBSan**: 0 errors
- [ ] **docs-audit**: 53 PASS (不变)
- [ ] **STUB 计数**: 53 (不变)
- [ ] **REAL_IMPL 计数**: 91 (不变)
- [ ] **nm 导出**: 79 (不变)
- [ ] **全测试套件**: 134+ PASS (5 binaries)
- [ ] **Cross-repo**: 0 lines 改动 UsrLinuxEmu

## 关联变更

任何源代码修复（如 cuFuncGetAttribute null 检查、cuCtxGetSharedMemConfig 真实实现）应在 `feat(shim): Phase 1.7.X` commit 中包含，作为本 change 的一部分。**不**单独提交 bugfix commit，因为这些修复是测试覆盖驱动的，由本 change 暴露。
