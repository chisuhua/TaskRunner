# Capability Spec: phase17-test-coverage-completion

> **Capability**: `phase17-test-coverage-completion`
> **Change**: `openspec/changes/2026-07-02-phase17-test-coverage-completion/`
> **Status**: PROPOSED (2026-07-03)

---

## Purpose

Close the test coverage gap revealed by the 2026-07-03 coverage review: Phase 1.7 wait-period work added 15 new REAL_IMPL APIs but 0% of them are covered by tests. Add 25-30 E2E tests targeting the new APIs plus cuCtx/PrimaryCtx/Launch coverage, bringing REAL_IMPL coverage from 50.5% to ≥85% and total test count from 69 to ≥95.

---

## Requirements

### Requirement: CUFUNC-ATTRIBUTE-COVERAGE

The system SHALL have E2E tests covering all 4 cuFunc* APIs added in Phase 1.7 A.1.

#### Scenario: cuFuncGetAttribute returns MAX_THREADS_PER_BLOCK=1024

- **WHEN** `cuFuncGetAttribute(&val, CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK, f)` called after cuModuleGetFunction
- **THEN** return `CUDA_SUCCESS` with `val == 1024`

#### Scenario: cuFuncGetAttribute returns SHARED_SIZE_BYTES=48KB

- **WHEN** `cuFuncGetAttribute(&val, CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES, f)` called
- **THEN** return `CUDA_SUCCESS` with `val == 49152`

#### Scenario: cuFuncGetAttribute returns CONST_SIZE_BYTES=64KB

- **WHEN** `cuFuncGetAttribute(&val, CU_FUNC_ATTRIBUTE_CONST_SIZE_BYTES, f)` called
- **THEN** return `CUDA_SUCCESS` with `val == 65536`

#### Scenario: cuFuncGetAttribute returns NUM_REGS=32

- **WHEN** `cuFuncGetAttribute(&val, CU_FUNC_ATTRIBUTE_NUM_REGS, f)` called
- **THEN** return `CUDA_SUCCESS` with `val == 32`

#### Scenario: cuFuncGetAttribute rejects unknown attribute

- **WHEN** `cuFuncGetAttribute(&val, /*unknown attr*/9999, f)` called
- **THEN** return `CUDA_ERROR_INVALID_VALUE`

#### Scenario: cuFuncGetAttribute rejects null function

- **WHEN** `cuFuncGetAttribute(&val, attr, nullptr)` called
- **THEN** return `CUDA_ERROR_INVALID_VALUE`

#### Scenario: cuFuncSetAttribute round-trip

- **WHEN** `cuFuncSetAttribute(f, MAX_THREADS_PER_BLOCK, 512)` then `cuFuncGetAttribute(&val, MAX_THREADS_PER_BLOCK, f)` called
- **THEN** Get returns `val == 512`

#### Scenario: cuFuncSetCacheConfig accepts valid config

- **WHEN** `cuFuncSetCacheConfig(f, CU_FUNC_CACHE_PREFER_L1)` called
- **THEN** return `CUDA_SUCCESS`

#### Scenario: cuFuncGetModule returns owning module

- **WHEN** `cuFuncGetModule(&mod, f)` called after `cuModuleGetFunction(&f, mod, "name")`
- **THEN** return `CUDA_SUCCESS` with `mod == original module handle`

#### Scenario: cuFuncGetModule rejects null function

- **WHEN** `cuFuncGetModule(&mod, nullptr)` called
- **THEN** return `CUDA_ERROR_INVALID_VALUE`

---

### Requirement: CUOCCUPANCY-HEURISTIC-COVERAGE

The system SHALL have E2E tests covering all 3 cuOccupancy* APIs added in Phase 1.7 A.2.

#### Scenario: cuOccupancyMaxActiveBlocksPerMultiprocessor returns >= 1

- **WHEN** `cuOccupancyMaxActiveBlocksPerMultiprocessor(&blocks, f, 256, 0)` called
- **THEN** return `CUDA_SUCCESS` with `blocks >= 1` and `blocks <= 32`

#### Scenario: cuOccupancyMaxActiveBlocksPerMultiprocessorWithFlags delegates correctly

- **WHEN** `cuOccupancyMaxActiveBlocksPerMultiprocessorWithFlags(&blocks, f, 256, 0, 0)` called
- **THEN** return `CUDA_SUCCESS` with `blocks >= 1`

#### Scenario: cuOccupancyMaxPotentialBlockSize returns blockSize=256 minGridSize=80

- **WHEN** `cuOccupancyMaxPotentialBlockSize(&minGridSize, &blockSize, f, nullptr, 0, 0)` called
- **THEN** return `CUDA_SUCCESS` with `blockSize == 256` and `minGridSize == 80`

---

### Requirement: POINTER-ATTRIBUTE-COVERAGE

The system SHALL have E2E tests covering `cuPointerGetAttribute` added in Phase 1.7 A.3.

#### Scenario: cuPointerGetAttribute MEMORY_TYPE returns DEVICE

- **WHEN** `cuPointerGetAttribute(&type, CU_POINTER_ATTRIBUTE_MEMORY_TYPE, dptr)` called after `cuMemAlloc(&dptr, 1024)`
- **THEN** return `CUDA_SUCCESS` with `type == CU_MEMORYTYPE_DEVICE`

#### Scenario: cuPointerGetAttribute DEVICE_POINTER returns identity

- **WHEN** `cuPointerGetAttribute(&ptr, CU_POINTER_ATTRIBUTE_DEVICE_POINTER, dptr)` called
- **THEN** return `CUDA_SUCCESS` with `ptr == dptr`

#### Scenario: cuPointerGetAttribute RANGE_SIZE returns alloc size

- **WHEN** `cuPointerGetAttribute(&size, CU_POINTER_ATTRIBUTE_RANGE_SIZE, dptr)` called after `cuMemAlloc(&dptr, 4096)`
- **THEN** return `CUDA_SUCCESS` with `size == 4096`

#### Scenario: cuPointerGetAttribute CONTEXT returns valid context

- **WHEN** `cuPointerGetAttribute(&ctx, CU_POINTER_ATTRIBUTE_CONTEXT, dptr)` called with current TLS context set
- **THEN** return `CUDA_SUCCESS` (ctx value is implementation-defined, may be nullptr)

---

### Requirement: LIGHT-STUB-COVERAGE

The system SHALL have E2E tests for the 6 light-stub APIs added in Phase 1.7 A.4.

#### Scenario: cuMemsetD16 writes first 16-bit value

- **WHEN** `cuMemsetD16(dptr, 0xABCD, 4)` called after `cuMemAlloc(&dptr, 8)`
- **THEN** return `CUDA_SUCCESS`

#### Scenario: cuProfilerStart returns NOT_SUPPORTED

- **WHEN** `cuProfilerStart()` called
- **THEN** return `CUDA_ERROR_NOT_SUPPORTED`

#### Scenario: cuProfilerStop returns NOT_SUPPORTED

- **WHEN** `cuProfilerStop()` called
- **THEN** return `CUDA_ERROR_NOT_SUPPORTED`

#### Scenario: cuProfilerInitialize returns NOT_SUPPORTED

- **WHEN** `cuProfilerInitialize("config.txt", "output.txt", 0)` called
- **THEN** return `CUDA_ERROR_NOT_SUPPORTED`

#### Scenario: cuStreamCreateWithFlags delegates to cuStreamCreate

- **WHEN** `cuStreamCreateWithFlags(&stream, 0)` called
- **THEN** return `CUDA_SUCCESS` with valid stream handle

#### Scenario: cuStreamGetCaptureInfo returns NONE status

- **WHEN** `cuStreamGetCaptureInfo(stream, &status, &id)` called
- **THEN** return `CUDA_SUCCESS` with `status == CU_STREAM_CAPTURE_STATUS_NONE`

#### Scenario: cuEventCreateWithFlags delegates to cuEventCreate

- **WHEN** `cuEventCreateWithFlags(&event, 0)` called
- **THEN** return `CUDA_SUCCESS` with valid event handle

---

### Requirement: CTX-FULL-COVERAGE

The system SHALL have E2E tests for the cuCtx API surface beyond what Phase 1.6 covered.

#### Scenario: cuCtxGetDevice returns current device ordinal

- **WHEN** `cuCtxGetDevice(&dev)` called with current context set
- **THEN** return `CUDA_SUCCESS` with `dev == 0`

#### Scenario: cuCtxGetFlags returns non-zero flags

- **WHEN** `cuCtxGetFlags(&flags)` called after `cuCtxCreate(&ctx, 0, 0)`
- **THEN** return `CUDA_SUCCESS`

#### Scenario: cuCtxPushCurrent then PopCurrent round-trip

- **WHEN** `cuCtxPushCurrent(ctx)` then `cuCtxPopCurrent(&ctx2)` called
- **THEN** return `CUDA_SUCCESS` from both, and `ctx2 == ctx`

#### Scenario: cuCtxSynchronize returns SUCCESS (no-op)

- **WHEN** `cuCtxSynchronize()` called
- **THEN** return `CUDA_SUCCESS`

#### Scenario: cuCtxGetApiVersion returns CUDA version

- **WHEN** `cuCtxGetApiVersion(ctx, &version)` called
- **THEN** return `CUDA_SUCCESS` with `version >= 11000`

#### Scenario: cuCtxGetSharedMemConfig returns valid config

- **WHEN** `cuCtxGetSharedMemConfig(&config)` called
- **THEN** return `CUDA_SUCCESS` and `config >= 0`

#### Scenario: cuCtxSetSharedMemConfig then Get round-trip

- **WHEN** `cuCtxSetSharedMemConfig(...)` then `cuCtxGetSharedMemConfig(&got)` called
- **THEN** both return `CUDA_SUCCESS`

#### Scenario: cuCtxSetLimit accepts stack size

- **WHEN** `cuCtxSetLimit(CU_LIMIT_STACK_SIZE, 2048)` called
- **THEN** return `CUDA_SUCCESS`

---

### Requirement: PRIMARYCTX-COVERAGE

The system SHALL have E2E tests for the remaining cuDevicePrimaryCtx* APIs.

#### Scenario: cuDevicePrimaryCtxReset returns SUCCESS

- **WHEN** `cuDevicePrimaryCtxReset(0)` called
- **THEN** return `CUDA_SUCCESS`

#### Scenario: cuDevicePrimaryCtxGetState returns active=1

- **WHEN** `cuDevicePrimaryCtxGetState(0, &flags, &active)` called
- **THEN** return `CUDA_SUCCESS` with `active == 1` and `flags == 0`

#### Scenario: cuDevicePrimaryCtxSetFlags accepts flags

- **WHEN** `cuDevicePrimaryCtxSetFlags(0, 0)` called
- **THEN** return `CUDA_SUCCESS`

---

### Requirement: LAUNCH-COVERAGE

The system SHALL have E2E tests for the cuLaunch* API surface.

#### Scenario: cuLaunchKernel with valid registered function returns SUCCESS

- **WHEN** `cuLaunchKernel(f, 1, 1, 1, 1, 1, 1, 0, 0, args, nullptr)` called after `cuModuleLoad` + `cuModuleGetFunction`
- **THEN** return `CUDA_SUCCESS`

#### Scenario: cuLaunchKernelEx with valid registered function returns SUCCESS

- **WHEN** `cuLaunchKernelEx(&config, f, nullptr)` called with valid registered function
- **THEN** return `CUDA_SUCCESS`

#### Scenario: cuLaunchHostFunc returns SUCCESS

- **WHEN** `cuLaunchHostFunc(0, fn, nullptr)` called with valid function pointer
- **THEN** return `CUDA_SUCCESS` (or `CUDA_ERROR_NOT_IMPLEMENTED` if not yet implemented)

---

### Requirement: STUB-SANITY-COVERAGE

The system SHALL have an E2E test verifying that 10 representative STUB APIs (NOT_IMPLEMENTED) return the correct error code.

#### Scenario: STUB APIs return NOT_IMPLEMENTED

- **WHEN** the following APIs are called: `cuModuleLoadData`, `cuModuleLoadDataEx`, `cuModuleLoadFatBinary`, `cuTexRefCreate`, `cuArrayCreate`, `cuGraphCreate`, `cuMemcpyAsync`, `cuMemsetD32`, `cuProfilerStart`, `cuMemHostRegister`
- **THEN** all SHALL return `CUDA_ERROR_NOT_IMPLEMENTED`

---

### Requirement: NON-REGRESSION

The system SHALL NOT regress existing functionality.

#### Scenario: 95+ existing tests pass

- **WHEN** all test binaries run
- **THEN** ≥95 cases SHALL pass in test_cuda_shim (was 69)
- **AND** all 5 binaries SHALL pass (≥134 cases total)

#### Scenario: docs-audit unchanged

- **WHEN** `tools/docs-audit.sh` runs
- **THEN** SHALL report 53 PASS / 1 FAIL（与 baseline 一致，不引入新失败）

#### Scenario: ABI unchanged

- **WHEN** `nm -D --defined-only build/libcuda_taskrunner.so | grep -c " cu[A-Z]"` runs
- **THEN** output SHALL remain 79 symbols (no ABI change)

#### Scenario: STUB and REAL_IMPL counts unchanged

- **WHEN** `grep -c "^// STUB" cu_stub_table.inc` and `grep -c "^// REAL_IMPL" cu_stub_table.inc` run
- **THEN** output SHALL be 53 and 91 respectively (same as baseline)

#### Scenario: ASan+UBSan clean

- **WHEN** `cmake .. -DSANITIZER_ADDRESS=ON -DSANITIZER_UNDEFINED=ON && make -j4 && ./build/test_cuda_shim` runs
- **THEN** all 95+ tests pass with 0 sanitizer errors

---

## Cross-References

- Proposal: [`../proposal.md`](../proposal.md)
- Design: [`../design.md`](../design.md)
- Tasks: [`../tasks.md`](../tasks.md)
- Source change under test: [`../../2026-07-02-phase17-wait-period-work/`](../../2026-07-02-phase17-wait-period-work/)

---

**Capability Status**: PROPOSED
**Next State**: ACCEPTED (after Workstream E complete + 95+ tests pass + ASan clean)
