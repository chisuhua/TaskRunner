# Tasks: phase17-wait-period-work

> **状态**: ⚠️ PROPOSED（2026-07-02）
> **依赖**: commit d988393（Phase 1.6）✅
> **基础基线**: 144 声明 / 76 REAL_IMPL / 68 STUB / 79 导出 / 49 测试 / 53 docs-audit

---

## 前置条件

- [ ] **0.1** 确认 commit d988393 已合并：
  ```bash
  git log --oneline -1 | grep "d988393\|Phase 1.6"
  ```
- [ ] **0.2** 确认基线测试全 PASS：
  ```bash
  cd build && make -j4
  for t in test_cuda_scheduler test_gpu_architecture test_gpu_phase2 \
            test_cuda_runtime_api test_cuda_shim; do
    ./$t 2>&1 | tail -1
  done
  ```
- [ ] **0.3** 记录当前基线计数：
  ```bash
  grep -c "^// STUB$" src/umd/libcuda_shim/cu_stub_table.inc    # 68
  grep -c "^// REAL_IMPL" src/umd/libcuda_shim/cu_stub_table.inc # 76
  nm -D --defined-only build/libcuda_taskrunner.so | grep -c " cu[A-Z]"  # 79
  ```

---

## Workstream A: Backend-independent STUB impl ⏱ 1-2 天

> **Commit**: `feat(shim): Phase 1.7 — 14 backend-independent STUB implementations`

### A.1: cuFunc* 4 API（cu_module.cpp + 4 tests, ~2 h）

- [ ] Read `src/umd/libcuda_shim/cu_module.cpp` — find CUfunction tracking struct
- [ ] Add `uint8_t max_threads_per_block` field to function struct
- [ ] Implement `cuFuncGetAttribute(int* val, CUfunction_attribute attr, CUfunction f)`:
  - `MAX_THREADS_PER_BLOCK` → 1024, `SHARED_SIZE_BYTES` → 48KB, `CONST_SIZE_BYTES` → 64KB, `NUM_REGS` → 32
  - Unknown attr → `CUDA_ERROR_INVALID_VALUE`
- [ ] Implement `cuFuncSetAttribute(CUfunction f, CUfunction_attribute attr, int val)`:
  - Store val in function struct map. No enforcement.
- [ ] Implement `cuFuncSetCacheConfig(CUfunction f, CUfunc_cache config)`:
  - Store in function struct map
- [ ] Implement `cuFuncGetModule(CUmodule* mod, CUfunction f)`:
  - Reverse lookup from function→module (need module pointer per function)
- [ ] Add to `CRITICAL_APIS_IMPL_REQUIRED` in `generate_cu_stubs.py`
- [ ] Regenerate `cu_stub_table.inc`
- [ ] Add 4 test cases for cuFuncGetAttribute (MAX_THREADS, SHARED_SIZE, CONST_SIZE, NUM_REGS)
- [ ] Verify: `./build/test_cuda_shim 2>&1 | tail -5` — 53+ PASS

### A.2: cuOccupancyMax* 3 API（cu_module.cpp + 3 tests, ~1.5 h）

- [ ] Implement `cuOccupancyMaxActiveBlocksPerMultiprocessor(int* blocks, CUfunc f, int blockSize, size_t dynSMem)`:
  - 启发式: `*blocks = max(1, min(32, (48 * 1024) / max(blockSize, 1)))`（假设 48KB shared mem per SM）
- [ ] Implement `cuOccupancyMaxActiveBlocksPerMultiprocessorWithFlags` — 同 A.2，flags 忽略
- [ ] Implement `cuOccupancyMaxPotentialBlockSize(...)` — `*minGridSize = 80; *blockSize = 256;`
- [ ] Add to CRITICAL_APIS_IMPL_REQUIRED + regenerate
- [ ] Add 3 test cases
- [ ] Verify: 56+ PASS

### A.3: cuPointerGetAttribute（cu_mem.cpp + 1 test, ~2 h）

- [ ] Add `std::unordered_map<CUdeviceptr, gpu_device_info>` to shim cu_mem.cpp (static, mutex-protected)
- [ ] Track allocations in `cuMemAlloc`: after successful alloc, add ptr to map
- [ ] Track frees in `cuMemFree`: remove ptr from map
- [ ] Implement `cuPointerGetAttribute(void* data, CUpointer_attribute attr, CUdeviceptr ptr)`:
  - `CU_POINTER_ATTRIBUTE_CONTEXT` → current context from TLS
  - `CU_POINTER_ATTRIBUTE_MEMORY_TYPE` → `CU_MEMORYTYPE_DEVICE`
  - `CU_POINTER_ATTRIBUTE_DEVICE_POINTER` → identity
  - `CU_POINTER_ATTRIBUTE_RANGE_SIZE` → lookup map
- [ ] Add to CRITICAL_APIS_IMPL_REQUIRED + regenerate
- [ ] Add 1 test: alloc → cuPointerGetAttribute(CONTEXT) → check non-null
- [ ] Verify: 57+ PASS

### A.4: 轻量 STUB（cu_stream.cpp + cu_event.cpp + cu_mem.cpp, ~1 h）

- [ ] `cuStreamCreateWithFlags` → `cuStreamCreate(stream, 0) // flags ignored`
- [ ] `cuStreamGetCaptureInfo` → `*captureStatus = CU_STREAM_CAPTURE_STATUS_NONE; return SUCCESS`
- [ ] `cuEventCreateWithFlags` → `cuEventCreate(event, Flags)`
- [ ] `cuProfilerStart/Stop/Initialize` → `return CUDA_ERROR_NOT_SUPPORTED` (honest stub)
- [ ] `cuMemsetD16(CUdeviceptr dst, unsigned short us, size_t N)` → `*(volatile unsigned short*)dst = us; return SUCCESS`（写入第一个值，轻量 stub）
- [ ] `cuDevicePrimaryCtxSetFlags` → `return CUDA_ERROR_NOT_SUPPORTED`
- [ ] Add 7 entries to CRITICAL_APIS_IMPL_REQUIRED + regenerate
- [ ] Add 3-4 minimal test cases
- [ ] Verify: 60+ PASS

### A.5: Final A verification

- [ ] `grep -c "^// STUB" cu_stub_table.inc` ≤ 54
- [ ] `grep -c "^// REAL_IMPL" cu_stub_table.inc` ≥ 90
- [ ] `./build/test_cuda_shim 2>&1 | tail -5` — 60+ PASS
- [ ] `nm -D --defined-only build/libcuda_taskrunner.so | grep -c " cu[A-Z]"` — 79

---

## Workstream B: CI/Build Infrastructure ⏱ 1 天

> **Commit**: `ci(shim): Phase 1.7 — shim CI infrastructure (ASan/TSan/UBSan + workflow + pre-commit)`

### B.1: CMake sanitizer option

- [ ] Read `CMakeLists.txt` — find option definitions section
- [ ] Add `option(SANITIZER_ADDRESS "Enable AddressSanitizer" OFF)`
- [ ] Add `option(SANITIZER_UNDEFINED "Enable UndefinedBehaviorSanitizer" OFF)`
- [ ] Add `option(SANITIZER_THREAD "Enable ThreadSanitizer" OFF)`
- [ ] When enabled:
  - ASan: `-fsanitize=address -fno-omit-frame-pointer`（仅 C/CXX compiler，不用于 linker 会跳过 .so）
  - UBSan: `-fsanitize=undefined`
  - TSan: `-fsanitize=thread`（需同时设 CMAKE_{C,CXX,EXE}_LINKER_FLAGS）
- [ ] 注意: ASan 与 TSan **不能同时启用**（LLVM 限制）。CMake 中加互斥检查: `if(SANITIZER_ADDRESS AND SANITIZER_THREAD) message(FATAL_ERROR "...")`
- [ ] Verify: `cmake .. -DSANITIZER_ADDRESS=ON -DSANITIZER_UNDEFINED=ON && make -j4` — clean

### B.2: GitHub Actions workflow

- [ ] Create `.github/workflows/shim.yml`:
  - Trigger: push to main, PR to main
  - Job 1: `build-and-test` (ubuntu-latest, cmake, make, run 5 test bins)
  - Job 2: `docs-audit` (run `tools/docs-audit.sh`)
  - Job 3: `sanitizer` (cmake -DSANITIZER_ADDRESS=ON -DSANITIZER_UNDEFINED=ON, build, run)
- [ ] Verify: `yamllint .github/workflows/shim.yml` — no errors

### B.3: pre-commit hook

- [ ] Create `.githooks/pre-commit`:
  - Run `tools/docs-audit.sh` — fail if non-pass
  - Run `git diff --cached --name-only | grep '\.cpp$|\.hpp$|\.h$' | xargs clang-format --dry-run --Werror` — fail if format violations
- [ ] Make executable: `chmod +x .githooks/pre-commit`
- [ ] Install: `git config core.hooksPath .githooks`

### B.4: Coverage script

- [ ] Create `tools/coverage.sh`:
  - Build with `-DCMAKE_CXX_FLAGS=--coverage -DCMAKE_EXE_LINKER_FLAGS=--coverage`
  - Run all test bins
  - `lcov --capture --directory . --output-file coverage.info`
  - `genhtml coverage.info --output-directory coverage/`
  - Print summary: `lcov --summary coverage.info`

### B.5: Verify B

- [ ] `cmake .. -DSANITIZER_ADDRESS=ON && make -j4 && ./build/test_cuda_shim` — ASan clean
- [ ] `.github/workflows/shim.yml` 存在
- [ ] `.githooks/pre-commit` 存在且 executable

---

## Workstream C: Test Coverage Depth ⏱ 1-2 天

> **Commit**: `test(shim): Phase 1.7 — test depth expansion (49→70+, threading + attribute + error path)`

### C.1: Threading tests（6 cases, ~1 h）

- [ ] Test: `cuCtxCreate concurrent` — 4 threads each create+destroy ctx
- [ ] Test: `cuMemAlloc concurrent` — 8 threads each alloc 4KB, verify all distinct
- [ ] Test: `cuStreamCreate concurrent` — 4 threads create+destroy stream
- [ ] Test: `cuEventCreate concurrent` — 4 threads create+destroy event
- [ ] Test: `cuStreamSynchronize from multiple threads` — 2 threads sync same stream
- [ ] Test: `cuCtxGetCurrent thread-local isolation` — verify ctx not shared across threads

### C.2: cuDeviceGetAttribute full coverage（1 h）

- [ ] **契约性属性（必须覆盖，固定返回值）**: `WARP_SIZE`(32), `MAX_THREADS_PER_BLOCK`(1024), `MAX_SHARED_MEMORY_PER_BLOCK`(48KB), `TOTAL_CONSTANT_MEMORY`(64KB), `MAX_REGISTERS_PER_BLOCK`(32), `MULTIPROCESSOR_COUNT`(≥1)
- [ ] **非契约性属性（尽可能覆盖）**: `MEM_PITCH`, `INTEGRATED`, `CAN_MAP_HOST_MEMORY`, `KERNEL_EXEC_TIMEOUT`, `COMPUTE_CAPABILITY_MAJOR/MINOR`, `GLOBAL_MEMORY_BUS_WIDTH`, `L2_CACHE_SIZE`, `MAX_GRID_DIM_X/Y/Z`, `MAX_BLOCK_DIM_X/Y/Z`
- [ ] Target: cover 20+ attributes total（当前仅覆盖 ~6，需新增 ≥14）

### C.3: Error path tests（4 cases, ~30 min）

- [ ] Test: `cuMemAlloc with zero size` → `CUDA_ERROR_INVALID_VALUE`
- [ ] Test: `cuStreamCreate with NULL handle` → `CUDA_ERROR_INVALID_VALUE`
- [ ] Test: `cuEventSynchronize with NULL handle` → `CUDA_ERROR_INVALID_VALUE`
- [ ] Test: `cuModuleGetFunction with NULL function name` → `CUDA_ERROR_INVALID_VALUE`

### C.4: Resource lifecycle tests（4 cases, ~30 min）

- [ ] Test: `double cuMemFree` → first SUCCESS, second error
- [ ] Test: `cuStreamDestroy after double-create` → no crash
- [ ] Test: `cuEventDestroy after event sync` → no crash
- [ ] Test: `cuCtxDestroy then cuCtxGetCurrent` → `CUDA_ERROR_INVALID_CONTEXT`

### C.5: Verify C

- [ ] `grep -c "TEST_CASE" tests/umd/test_cuda_shim.cpp` ≥ 70 (was 49)
- [ ] `./build/test_cuda_shim 2>&1 | tail -5` — 70+ PASS

---

## Workstream D: Documentation & Phase 3 Prep ⏱ 0.5-1 天

> **Commit**: `docs(umd): Phase 1.7 — Phase 3 design sketches + shim architecture doc`

### D.1: cuMemPool* interface design

- [ ] Create `docs/superpowers/specs/2026-07-02-phase3-mempool-design.md`:
  - API surface: `cuMemPoolCreate`, `cuMemPoolDestroy`, `cuMemAllocFromPoolAsync`, `cuMemPoolExportToShareableHandle`
  - Key decisions: Pool size tracking, async allocation, VA space integration
  - Mark DRAFT

### D.2: cuStream* async capture design

- [ ] Create `docs/superpowers/specs/2026-07-02-phase3-stream-capture-design.md`:
  - API surface: `cuStreamBeginCapture`, `cuStreamEndCapture`, `cuStreamIsCapturing`
  - Key decisions: Capture tree representation, memory node tracking
  - Mark DRAFT

### D.3: Shim architecture overview

- [ ] Create `docs/superpowers/architecture/shim-layer.md`:
  - Architecture diagram (ASCII): `cu* shim → CudaRuntimeApi → CudaScheduler → IGpuDriver`
  - Symbol export mechanism (weak attribute + CUDA_DRIVER_APIS list)
  - Thread safety model (mutex per resource type)
  - Known limitations

### D.4: Verify D

- [ ] `ls docs/superpowers/specs/2026-07-02-phase3-mempool-design.md`
- [ ] `ls docs/superpowers/specs/2026-07-02-phase3-stream-capture-design.md`
- [ ] `ls docs/superpowers/architecture/shim-layer.md`

---

## 最终验证

- [ ] **全部测试**: ≥81 cases PASS (49 + 32)
- [ ] **STUB 计数**: ≤54 (原 68)
- [ ] **REAL_IMPL 计数**: ≥90 (原 76)
- [ ] **nm 导出**: 79 (不变)
- [ ] **docs-audit**: 53 PASS (实际基线，不变)
- [ ] **ASan 构建**: clean
- [ ] **GitHub workflow**: 语法有效
- [ ] **pre-commit hook**: 可执行