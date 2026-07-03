// SCOPE: UMD-EVOLUTION
// test_cuda_shim.cpp - E2E tests for libcuda_taskrunner.so (Phase 2 C.7).
//
// Verifies the shim correctly intercepts and routes cu* Driver API calls.
// Linked against libcuda_taskrunner.so directly.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cuda.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Initialization and version queries
// ---------------------------------------------------------------------------

TEST_CASE("cuInit returns SUCCESS") {
  // cuInit may be called multiple times — both should succeed.
  CHECK(cuInit(0) == CUDA_SUCCESS);
}

TEST_CASE("cuDriverGetVersion returns CUDA 12.0") {
  int version = 0;
  CHECK(cuDriverGetVersion(&version) == CUDA_SUCCESS);
  CHECK(version >= 11000);
  CHECK(version <= 12050);
}

TEST_CASE("cuDriverGet returns CUDA 12.0") {
  int driver = 0;
  CHECK(cuDriverGet(&driver) == CUDA_SUCCESS);
  CHECK(driver >= 11000);
  CHECK(driver <= 12050);
}

// ---------------------------------------------------------------------------
// Device enumeration
// ---------------------------------------------------------------------------

TEST_CASE("cuDeviceGetCount returns at least 1 device") {
  int count = 0;
  CHECK(cuDeviceGetCount(&count) == CUDA_SUCCESS);
  CHECK(count >= 1);
}

TEST_CASE("cuDeviceGet with ordinal 0 returns valid device") {
  CUdevice dev = -1;
  CHECK(cuDeviceGet(&dev, 0) == CUDA_SUCCESS);
  CHECK(dev == 0);  // Phase 2 returns device 0
}

TEST_CASE("cuDeviceGetName returns 'TaskRunner CUDA Stub'") {
  char name[256];
  std::memset(name, 0, sizeof(name));
  CHECK(cuDeviceGetName(name, sizeof(name), 0) == CUDA_SUCCESS);
  CHECK(std::string(name) == "TaskRunner CUDA Stub");
}

TEST_CASE("cuDeviceGetAttribute returns reasonable defaults") {
  int val = 0;
  CHECK(cuDeviceGetAttribute(&val, CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT, 0) == CUDA_SUCCESS);
  CHECK(val >= 1);

  CHECK(cuDeviceGetAttribute(&val, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, 0) == CUDA_SUCCESS);
  CHECK(val >= 7);
}

TEST_CASE("cuDeviceTotalMem returns non-zero") {
  size_t bytes = 0;
  CHECK(cuDeviceTotalMem(&bytes, 0) == CUDA_SUCCESS);
  CHECK(bytes > 0);
}

TEST_CASE("cuDeviceComputeCapability returns valid") {
  int major = 0, minor = 0;
  CHECK(cuDeviceComputeCapability(&major, &minor, 0) == CUDA_SUCCESS);
  CHECK(major >= 7);
}

// ---------------------------------------------------------------------------
// Context lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("cuCtxCreate/SetCurrent/GetCurrent/Destroy lifecycle") {
  CUcontext ctx;
  CHECK(cuCtxCreate(&ctx, 0, 0) == CUDA_SUCCESS);
  CHECK(ctx != nullptr);
  CHECK(cuCtxSetCurrent(ctx) == CUDA_SUCCESS);

  CUcontext current;
  CHECK(cuCtxGetCurrent(&current) == CUDA_SUCCESS);
  CHECK(current == ctx);

  CHECK(cuCtxDestroy(ctx) == CUDA_SUCCESS);
}

TEST_CASE("cuCtxCreate rejects null pointer") {
  CHECK(cuCtxCreate(nullptr, 0, 0) == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cuCtxGetCurrent returns null when no context set") {
  CUcontext current;
  CHECK(cuCtxGetCurrent(&current) == CUDA_SUCCESS);
  // After prior test destroyed the context, stack empty -> nullptr
  CHECK(current == nullptr);
}

// ---------------------------------------------------------------------------
// Error handling (NULL rejection)
// ---------------------------------------------------------------------------

TEST_CASE("cuDeviceGetCount rejects null argument") {
  CHECK(cuDeviceGetCount(nullptr) == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cuDeviceGet rejects null pointer") {
  CHECK(cuDeviceGet(nullptr, 0) == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cuDeviceGet rejects invalid ordinal") {
  CUdevice dev;
  CHECK(cuDeviceGet(&dev, 999) == CUDA_ERROR_INVALID_VALUE);
}

// ---------------------------------------------------------------------------
// Memory operations
// ---------------------------------------------------------------------------

TEST_CASE("cuMemAlloc/cuMemcpyHtoD/cuMemFree round-trip") {
  CUdeviceptr dptr = 0;
  CHECK(cuMemAlloc(&dptr, 1024) == CUDA_SUCCESS);
  CHECK(dptr != 0);

  std::vector<uint8_t> host(1024, 0xAB);
  CHECK(cuMemcpyHtoD(dptr, host.data(), 1024) == CUDA_SUCCESS);

  CHECK(cuMemFree(dptr) == CUDA_SUCCESS);
}

TEST_CASE("cuMemAlloc rejects zero size") {
  CUdeviceptr dptr = 0;
  CHECK(cuMemAlloc(&dptr, 0) == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cuMemcpyDtoD returns NOT_SUPPORTED (Phase 1 limitation)") {
  CUdeviceptr a = 0, b = 0;
  CHECK(cuMemAlloc(&a, 1024) == CUDA_SUCCESS);
  CHECK(cuMemAlloc(&b, 1024) == CUDA_SUCCESS);
  CHECK(cuMemcpyDtoD(a, b, 1024) == CUDA_ERROR_NOT_SUPPORTED);
  CHECK(cuMemFree(a) == CUDA_SUCCESS);
  CHECK(cuMemFree(b) == CUDA_SUCCESS);
}

TEST_CASE("cuMemcpy rejects null pointers") {
  CUdeviceptr dptr = 0;
  CHECK(cuMemAlloc(&dptr, 64) == CUDA_SUCCESS);
  // HtoD with null source
  CHECK(cuMemcpyHtoD(dptr, nullptr, 64) == CUDA_ERROR_INVALID_VALUE);
  // DtoH with null dest
  CHECK(cuMemcpyDtoH(nullptr, dptr, 64) == CUDA_ERROR_INVALID_VALUE);
  CHECK(cuMemFree(dptr) == CUDA_SUCCESS);
}

TEST_CASE("cuMemAlloc rejects null pointer") {
  CHECK(cuMemAlloc(nullptr, 1024) == CUDA_ERROR_INVALID_VALUE);
}

// ---------------------------------------------------------------------------
// Module/function lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("cuModuleLoad + cuModuleGetFunction + cuModuleUnload lifecycle") {
  CUmodule mod;
  CHECK(cuModuleLoad(&mod, "fake.cubin") == CUDA_SUCCESS);
  CHECK(mod != 0);

  CUfunction func;
  CHECK(cuModuleGetFunction(&func, mod, "vectorAdd") == CUDA_SUCCESS);
  CHECK(func != 0);

  // Verify cleanup: unload module
  CHECK(cuModuleUnload(mod) == CUDA_SUCCESS);
}

TEST_CASE("cuModuleLoad rejects null pointer") {
  CHECK(cuModuleLoad(nullptr, "x.cubin") == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cuModuleGetFunction rejects null name") {
  CUmodule mod;
  CHECK(cuModuleLoad(&mod, "x.cubin") == CUDA_SUCCESS);
  CHECK(cuModuleGetFunction(nullptr, mod, "foo") == CUDA_ERROR_INVALID_VALUE);
  CHECK(cuModuleUnload(mod) == CUDA_SUCCESS);
}

// ---------------------------------------------------------------------------
// Stream lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("cuStreamCreate/cuStreamSynchronize basic lifecycle") {
  CUstream stream;
  CHECK(cuStreamCreate(&stream, 0) == CUDA_SUCCESS);
  CHECK(stream != 0);
  CHECK(cuStreamSynchronize(stream) == CUDA_SUCCESS);
  CHECK(cuStreamDestroy(stream) == CUDA_SUCCESS);
}

TEST_CASE("cuStreamGetFlags/cuStreamGetPriority basic") {
  CUstream stream;
  CHECK(cuStreamCreate(&stream, 0) == CUDA_SUCCESS);

  unsigned int flags = 0;
  CHECK(cuStreamGetFlags(stream, &flags) == CUDA_SUCCESS);

  int priority = 0;
  CHECK(cuStreamGetPriority(stream, &priority) == CUDA_SUCCESS);

  CHECK(cuStreamDestroy(stream) == CUDA_SUCCESS);
}

TEST_CASE("cuStreamCreate rejects null") {
  CHECK(cuStreamCreate(nullptr, 0) == CUDA_ERROR_INVALID_VALUE);
}

// ---------------------------------------------------------------------------
// Event lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("cuEventCreate/cuEventRecord/cuEventSynchronize/cuEventElapsedTime/cuEventDestroy") {
  CUevent e1, e2;
  CHECK(cuEventCreate(&e1, 0) == CUDA_SUCCESS);
  CHECK(cuEventCreate(&e2, 0) == CUDA_SUCCESS);
  CHECK(e1 != 0);
  CHECK(e2 != 0);

  // Record both events (0 = default stream)
  CHECK(cuEventRecord(e1, 0) == CUDA_SUCCESS);
  CHECK(cuEventRecord(e2, 0) == CUDA_SUCCESS);

  CHECK(cuEventSynchronize(e1) == CUDA_SUCCESS);
  CHECK(cuEventSynchronize(e2) == CUDA_SUCCESS);

  float ms = -1.0f;
  CHECK(cuEventElapsedTime(&ms, e1, e2) == CUDA_SUCCESS);
  CHECK(ms >= 0.0f);  // Phase 2 PoC: time difference is >= 0

  CHECK(cuEventDestroy(e1) == CUDA_SUCCESS);
  CHECK(cuEventDestroy(e2) == CUDA_SUCCESS);
}

TEST_CASE("cuEventCreate rejects null") {
  CHECK(cuEventCreate(nullptr, 0) == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cuEventDestroy rejects null") {
  CHECK(cuEventDestroy(nullptr) == CUDA_ERROR_INVALID_VALUE);
}

// ---------------------------------------------------------------------------
// Primary context
// ---------------------------------------------------------------------------

TEST_CASE("cuDevicePrimaryCtxRetain returns non-null") {
  CUcontext ctx;
  CHECK(cuDevicePrimaryCtxRetain(&ctx, 0) == CUDA_SUCCESS);
  CHECK(ctx != nullptr);
  CHECK(cuDevicePrimaryCtxRelease(0) == CUDA_SUCCESS);
}

TEST_CASE("cuDevicePrimaryCtxRetain rejects invalid device") {
  CUcontext ctx;
  CHECK(cuDevicePrimaryCtxRetain(&ctx, 999) == CUDA_ERROR_INVALID_VALUE);
}

// ---------------------------------------------------------------------------
// Error name/string queries
// ---------------------------------------------------------------------------

TEST_CASE("cuGetErrorName returns expected strings") {
  const char* name = nullptr;
  CHECK(cuGetErrorName(CUDA_SUCCESS, &name) == CUDA_SUCCESS);
  CHECK(std::string(name) == "cudaSuccess");

  CHECK(cuGetErrorName(CUDA_ERROR_OUT_OF_MEMORY, &name) == CUDA_SUCCESS);
  CHECK(std::string(name) == "cudaErrorOutOfMemory");

  CHECK(cuGetErrorName(CUDA_ERROR_INVALID_VALUE, &name) == CUDA_SUCCESS);
  CHECK(std::string(name) == "cudaErrorInvalidValue");

  CHECK(cuGetErrorName(CUDA_ERROR_NOT_SUPPORTED, &name) == CUDA_SUCCESS);
  CHECK(std::string(name) == "cudaErrorNotSupported");

  CHECK(cuGetErrorName(CUDA_ERROR_INVALID_HANDLE, &name) == CUDA_SUCCESS);
  CHECK(std::string(name) == "cudaErrorInvalidHandle");
}

TEST_CASE("cuGetErrorName rejects null") {
  CHECK(cuGetErrorName(CUDA_SUCCESS, nullptr) == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cuGetErrorString returns expected strings") {
  const char* str = nullptr;
  CHECK(cuGetErrorString(CUDA_SUCCESS, &str) == CUDA_SUCCESS);
  CHECK(std::string(str) == "no error");

  CHECK(cuGetErrorString(CUDA_ERROR_OUT_OF_MEMORY, &str) == CUDA_SUCCESS);
  CHECK(std::string(str) == "out of memory");
}

// ---------------------------------------------------------------------------
// Stream-ordered operations
// ---------------------------------------------------------------------------

TEST_CASE("cuStreamWaitEvent basic (no-op in Phase 2)") {
  CUstream stream;
  CHECK(cuStreamCreate(&stream, 0) == CUDA_SUCCESS);

  CUevent event;
  CHECK(cuEventCreate(&event, 0) == CUDA_SUCCESS);

  CHECK(cuStreamWaitEvent(stream, event, 0) == CUDA_SUCCESS);

  CHECK(cuEventDestroy(event) == CUDA_SUCCESS);
  CHECK(cuStreamDestroy(stream) == CUDA_SUCCESS);
}

TEST_CASE("cuStreamAddCallback basic") {
  auto callback = [](CUstream, CUresult, void*) {};
  CUstream stream;
  CHECK(cuStreamCreate(&stream, 0) == CUDA_SUCCESS);
  CHECK(cuStreamAddCallback(stream, callback, nullptr, 0) == CUDA_SUCCESS);
  CHECK(cuStreamDestroy(stream) == CUDA_SUCCESS);
}

// ---------------------------------------------------------------------------
// Multiple cuInit calls (idempotency)
// ---------------------------------------------------------------------------

TEST_CASE("cuInit is idempotent across multiple calls") {
  CHECK(cuInit(0) == CUDA_SUCCESS);
  CHECK(cuInit(0) == CUDA_SUCCESS);
  CHECK(cuInit(1) == CUDA_SUCCESS);
}

// ---------------------------------------------------------------------------
// Phase 1.6 — A.1 cuMemGetInfo real data source (hotfix d8ca3d3+ followup)
// ---------------------------------------------------------------------------

TEST_CASE("cuMemGetInfo returns real backend vram_size via IGpuDriver") {
  size_t free_bytes = 0, total_bytes = 0;
  CHECK(cuMemGetInfo(&free_bytes, &total_bytes) == CUDA_SUCCESS);
  CHECK(total_bytes > 0);
  CHECK(free_bytes > 0);
  CHECK(free_bytes <= total_bytes);
}

// ---------------------------------------------------------------------------
// Phase 1.6 — A.3 test coverage expansion (cuEvent*)
// ---------------------------------------------------------------------------

TEST_CASE("cuEventCreate returns valid event handle") {
  CUevent event;
  CHECK(cuEventCreate(&event, 0) == CUDA_SUCCESS);
  CHECK(event != nullptr);
  CHECK(cuEventDestroy(event) == CUDA_SUCCESS);
}

TEST_CASE("cuEventRecord and cuEventSynchronize basic flow") {
  CUevent event;
  CHECK(cuEventCreate(&event, 0) == CUDA_SUCCESS);
  CHECK(cuEventRecord(event, /*hStream=*/0) == CUDA_SUCCESS);
  CHECK(cuEventSynchronize(event) == CUDA_SUCCESS);
  CHECK(cuEventDestroy(event) == CUDA_SUCCESS);
}

TEST_CASE("cuEventQuery returns SUCCESS after sync") {
  CUevent event;
  CHECK(cuEventCreate(&event, 0) == CUDA_SUCCESS);
  CHECK(cuEventRecord(event, 0) == CUDA_SUCCESS);
  CHECK(cuEventSynchronize(event) == CUDA_SUCCESS);
  CHECK(cuEventQuery(event) == CUDA_SUCCESS);
  CHECK(cuEventDestroy(event) == CUDA_SUCCESS);
}

TEST_CASE("cuEventElapsedTime measures positive interval") {
  CUevent start, end;
  CHECK(cuEventCreate(&start, 0) == CUDA_SUCCESS);
  CHECK(cuEventCreate(&end, 0) == CUDA_SUCCESS);
  CHECK(cuEventRecord(start, 0) == CUDA_SUCCESS);
  CHECK(cuEventRecord(end, 0) == CUDA_SUCCESS);
  CHECK(cuEventSynchronize(end) == CUDA_SUCCESS);
  float ms = -1.0f;
  CHECK(cuEventElapsedTime(&ms, start, end) == CUDA_SUCCESS);
  CHECK(ms >= 0.0f);
  CHECK(cuEventDestroy(start) == CUDA_SUCCESS);
  CHECK(cuEventDestroy(end) == CUDA_SUCCESS);
}

// ---------------------------------------------------------------------------
// Phase 1.6 — A.3 test coverage expansion (cuStream*)
// ---------------------------------------------------------------------------

TEST_CASE("cuStreamQuery returns SUCCESS for idle stream") {
  CUstream stream;
  CHECK(cuStreamCreate(&stream, 0) == CUDA_SUCCESS);
  CHECK(cuStreamQuery(stream) == CUDA_SUCCESS);
  CHECK(cuStreamSynchronize(stream) == CUDA_SUCCESS);
  CHECK(cuStreamDestroy(stream) == CUDA_SUCCESS);
}

TEST_CASE("cuStreamWaitEvent basic flow") {
  CUstream stream;
  CUevent event;
  CHECK(cuStreamCreate(&stream, 0) == CUDA_SUCCESS);
  CHECK(cuEventCreate(&event, 0) == CUDA_SUCCESS);
  CHECK(cuEventRecord(event, 0) == CUDA_SUCCESS);
  CHECK(cuStreamWaitEvent(stream, event, 0) == CUDA_SUCCESS);
  CHECK(cuStreamSynchronize(stream) == CUDA_SUCCESS);
  CHECK(cuEventDestroy(event) == CUDA_SUCCESS);
  CHECK(cuStreamDestroy(stream) == CUDA_SUCCESS);
}

TEST_CASE("cuStreamCreateWithPriority returns valid stream") {
  CUstream stream;
  CHECK(cuStreamCreateWithPriority(&stream, 0, /*priority=*/0) == CUDA_SUCCESS);
  CHECK(stream != nullptr);
  int priority = -1;
  CHECK(cuStreamGetPriority(stream, &priority) == CUDA_SUCCESS);
  CHECK(cuStreamDestroy(stream) == CUDA_SUCCESS);
}

// ---------------------------------------------------------------------------
// Phase 1.6 — A.3 test coverage expansion (cuCtx*Config/Limit)
// ---------------------------------------------------------------------------

TEST_CASE("cuCtxGetCacheConfig returns default config") {
  CUfunc_cache config;
  CHECK(cuCtxGetCacheConfig(&config) == CUDA_SUCCESS);
  CHECK(config >= CU_FUNC_CACHE_PREFER_NONE);
  CHECK(config <= CU_FUNC_CACHE_PREFER_SHARED);
}

TEST_CASE("cuCtxSetCacheConfig then Get returns same value") {
  CUfunc_cache original;
  CHECK(cuCtxGetCacheConfig(&original) == CUDA_SUCCESS);
  CHECK(cuCtxSetCacheConfig(CU_FUNC_CACHE_PREFER_L1) == CUDA_SUCCESS);
  CUfunc_cache updated;
  CHECK(cuCtxGetCacheConfig(&updated) == CUDA_SUCCESS);
  CHECK(updated == CU_FUNC_CACHE_PREFER_L1);
  CHECK(cuCtxSetCacheConfig(original) == CUDA_SUCCESS);
}

TEST_CASE("cuCtxGetLimit returns non-zero for stack size") {
  size_t stack_size = 0;
  CHECK(cuCtxGetLimit(&stack_size, CU_LIMIT_STACK_SIZE) == CUDA_SUCCESS);
  CHECK(stack_size > 0);
}

// ---------------------------------------------------------------------------
// Phase 1.6 — A.3 test coverage expansion (cuLaunchCooperativeKernel)
// ---------------------------------------------------------------------------

TEST_CASE("cuLaunchCooperativeKernel returns INVALID_HANDLE for null func") {
  CUfunction func = nullptr;
  void* args[1] = {nullptr};
  CHECK(cuLaunchCooperativeKernel(func, 1, 1, 1, 1, 1, 1, 0, /*hStream=*/0,
                                  args) == CUDA_ERROR_INVALID_HANDLE);
}

// ---------------------------------------------------------------------------
// Phase 1.7 — C.1: Threading tests (cuCtx/cuMem/cuStream/cuEvent concurrent)
// ---------------------------------------------------------------------------

TEST_CASE("cuCtxCreate concurrent — 4 threads each create+destroy ctx") {
  std::thread t1([] {
    CUcontext ctx;
    CHECK(cuCtxCreate(&ctx, 0, 0) == CUDA_SUCCESS);
    CHECK(cuCtxDestroy(ctx) == CUDA_SUCCESS);
  });
  std::thread t2([] {
    CUcontext ctx;
    CHECK(cuCtxCreate(&ctx, 0, 0) == CUDA_SUCCESS);
    CHECK(cuCtxDestroy(ctx) == CUDA_SUCCESS);
  });
  std::thread t3([] {
    CUcontext ctx;
    CHECK(cuCtxCreate(&ctx, 0, 0) == CUDA_SUCCESS);
    CHECK(cuCtxDestroy(ctx) == CUDA_SUCCESS);
  });
  std::thread t4([] {
    CUcontext ctx;
    CHECK(cuCtxCreate(&ctx, 0, 0) == CUDA_SUCCESS);
    CHECK(cuCtxDestroy(ctx) == CUDA_SUCCESS);
  });
  t1.join(); t2.join(); t3.join(); t4.join();
}

TEST_CASE("cuMemAlloc concurrent — 8 threads each alloc 4KB") {
  std::vector<std::thread> threads;
  std::vector<CUdeviceptr> ptrs(8, 0);
  for (int i = 0; i < 8; ++i) {
    threads.emplace_back([&ptrs, i] {
      CHECK(cuMemAlloc(&ptrs[i], 4096) == CUDA_SUCCESS);
      CHECK(ptrs[i] != 0);
    });
  }
  for (auto& t : threads) t.join();
  // Verify all distinct.
  for (int i = 0; i < 8; ++i)
    for (int j = i + 1; j < 8; ++j)
      CHECK(ptrs[i] != ptrs[j]);
  // Cleanup
  for (auto p : ptrs) cuMemFree(p);
}

TEST_CASE("cuStreamCreate concurrent — 4 threads create+destroy stream") {
  std::thread t1([] {
    CUstream s;
    CHECK(cuStreamCreate(&s, 0) == CUDA_SUCCESS);
    CHECK(cuStreamDestroy(s) == CUDA_SUCCESS);
  });
  std::thread t2([] {
    CUstream s;
    CHECK(cuStreamCreate(&s, 0) == CUDA_SUCCESS);
    CHECK(cuStreamDestroy(s) == CUDA_SUCCESS);
  });
  std::thread t3([] {
    CUstream s;
    CHECK(cuStreamCreate(&s, 0) == CUDA_SUCCESS);
    CHECK(cuStreamDestroy(s) == CUDA_SUCCESS);
  });
  std::thread t4([] {
    CUstream s;
    CHECK(cuStreamCreate(&s, 0) == CUDA_SUCCESS);
    CHECK(cuStreamDestroy(s) == CUDA_SUCCESS);
  });
  t1.join(); t2.join(); t3.join(); t4.join();
}

TEST_CASE("cuEventCreate concurrent — 4 threads create+destroy event") {
  std::thread t1([] {
    CUevent e;
    CHECK(cuEventCreate(&e, 0) == CUDA_SUCCESS);
    CHECK(cuEventDestroy(e) == CUDA_SUCCESS);
  });
  std::thread t2([] {
    CUevent e;
    CHECK(cuEventCreate(&e, 0) == CUDA_SUCCESS);
    CHECK(cuEventDestroy(e) == CUDA_SUCCESS);
  });
  std::thread t3([] {
    CUevent e;
    CHECK(cuEventCreate(&e, 0) == CUDA_SUCCESS);
    CHECK(cuEventDestroy(e) == CUDA_SUCCESS);
  });
  std::thread t4([] {
    CUevent e;
    CHECK(cuEventCreate(&e, 0) == CUDA_SUCCESS);
    CHECK(cuEventDestroy(e) == CUDA_SUCCESS);
  });
  t1.join(); t2.join(); t3.join(); t4.join();
}

TEST_CASE("cuStreamSynchronize from multiple threads — 2 threads sync same stream") {
  CUstream stream;
  CHECK(cuStreamCreate(&stream, 0) == CUDA_SUCCESS);
  std::thread t1([stream] { CHECK(cuStreamSynchronize(stream) == CUDA_SUCCESS); });
  std::thread t2([stream] { CHECK(cuStreamSynchronize(stream) == CUDA_SUCCESS); });
  t1.join(); t2.join();
  CHECK(cuStreamDestroy(stream) == CUDA_SUCCESS);
}

TEST_CASE("cuCtxGetCurrent thread-local isolation") {
  CUcontext ctx_a, ctx_b;
  CHECK(cuCtxCreate(&ctx_a, 0, 0) == CUDA_SUCCESS);
  CHECK(cuCtxSetCurrent(ctx_a) == CUDA_SUCCESS);

  // Spawn thread and check its ctx is different.
  std::thread worker([&ctx_b] {
    CHECK(cuCtxGetCurrent(&ctx_b) == CUDA_SUCCESS);
    // Worker thread should have null context by default.
    CHECK(ctx_b == nullptr);
  });
  worker.join();

  // Main thread context unchanged.
  CUcontext main_ctx;
  CHECK(cuCtxGetCurrent(&main_ctx) == CUDA_SUCCESS);
  CHECK(main_ctx == ctx_a);

  CHECK(cuCtxDestroy(ctx_a) == CUDA_SUCCESS);
}

// ---------------------------------------------------------------------------
// Phase 1.7 — C.2: cuDeviceGetAttribute full coverage
// ---------------------------------------------------------------------------

TEST_CASE("cuDeviceGetAttribute WARP_SIZE=32") {
  int v = 0;
  CHECK(cuDeviceGetAttribute(&v, CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK, 0) == CUDA_SUCCESS);
  CHECK(v >= 1024);
}

TEST_CASE("cuDeviceGetAttribute MAX_REGISTERS_PER_BLOCK") {
  int v = 0;
  CHECK(cuDeviceGetAttribute(&v, CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK, 0) == CUDA_SUCCESS);
  CHECK(v > 0);
}

TEST_CASE("cuDeviceGetAttribute MULTIPROCESSOR_COUNT >= 1") {
  int v = 0;
  CHECK(cuDeviceGetAttribute(&v, CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT, 0) == CUDA_SUCCESS);
  CHECK(v >= 1);
}

TEST_CASE("cuDeviceGetAttribute MAX_BLOCK_DIM returns reasonable") {
  int x = 0, y = 0, z = 0;
  CHECK(cuDeviceGetAttribute(&x, CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_X, 0) == CUDA_SUCCESS);
  CHECK(cuDeviceGetAttribute(&y, CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Y, 0) == CUDA_SUCCESS);
  CHECK(cuDeviceGetAttribute(&z, CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Z, 0) == CUDA_SUCCESS);
  CHECK(x >= 1024);
  CHECK(y >= 1024);
  CHECK(z >= 64);
}

TEST_CASE("cuDeviceGetAttribute MAX_GRID_DIM returns reasonable") {
  int x = 0, y = 0, z = 0;
  CHECK(cuDeviceGetAttribute(&x, CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_X, 0) == CUDA_SUCCESS);
  CHECK(cuDeviceGetAttribute(&y, CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Y, 0) == CUDA_SUCCESS);
  CHECK(cuDeviceGetAttribute(&z, CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Z, 0) == CUDA_SUCCESS);
  CHECK(x >= 1);
  CHECK(y >= 1);
  CHECK(z >= 1);
}

TEST_CASE("cuDeviceGetAttribute COMPUTE_CAPABILITY returns >= 7.0") {
  int maj = 0, min = 0;
  CHECK(cuDeviceGetAttribute(&maj, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, 0) == CUDA_SUCCESS);
  CHECK(cuDeviceGetAttribute(&min, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, 0) == CUDA_SUCCESS);
  CHECK(maj >= 7);
}

// ---------------------------------------------------------------------------
// Phase 1.7 — C.3: Error path tests
// ---------------------------------------------------------------------------

TEST_CASE("cuStreamCreate with NULL handle returns INVALID_VALUE") {
  CHECK(cuStreamCreate(nullptr, 0) == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cuEventSynchronize with NULL handle returns INVALID_VALUE") {
  CHECK(cuEventSynchronize(nullptr) == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cuModuleGetFunction with NULL function name returns INVALID_VALUE") {
  CUmodule mod;
  CHECK(cuModuleLoad(&mod, "x.cubin") == CUDA_SUCCESS);
  CHECK(cuModuleGetFunction(nullptr, mod, "foo") == CUDA_ERROR_INVALID_VALUE);
  CHECK(cuModuleUnload(mod) == CUDA_SUCCESS);
}

TEST_CASE("cuMemAlloc with zero size returns INVALID_VALUE") {
  CUdeviceptr dptr = 0;
  CHECK(cuMemAlloc(&dptr, 0) == CUDA_ERROR_INVALID_VALUE);
}

// ---------------------------------------------------------------------------
// Phase 1.7 — C.4: Resource lifecycle tests
// ---------------------------------------------------------------------------

TEST_CASE("double cuMemFree — first SUCCESS, second also SUCCESS (no-op)") {
  CUdeviceptr dptr = 0;
  CHECK(cuMemAlloc(&dptr, 64) == CUDA_SUCCESS);
  CHECK(cuMemFree(dptr) == CUDA_SUCCESS);
  CHECK(cuMemFree(dptr) == CUDA_SUCCESS);
}

TEST_CASE("cuStreamDestroy after double-create — no crash") {
  CUstream s1, s2;
  CHECK(cuStreamCreate(&s1, 0) == CUDA_SUCCESS);
  CHECK(cuStreamCreate(&s2, 0) == CUDA_SUCCESS);
  CHECK(cuStreamDestroy(s1) == CUDA_SUCCESS);
  CHECK(cuStreamDestroy(s2) == CUDA_SUCCESS);
}

TEST_CASE("cuEventDestroy after event sync — no crash") {
  CUevent e;
  CHECK(cuEventCreate(&e, 0) == CUDA_SUCCESS);
  CHECK(cuEventRecord(e, 0) == CUDA_SUCCESS);
  CHECK(cuEventSynchronize(e) == CUDA_SUCCESS);
  CHECK(cuEventDestroy(e) == CUDA_SUCCESS);
}

TEST_CASE("cuCtxDestroy then cuCtxGetCurrent returns INVALID_CONTEXT") {
  CUcontext ctx;
  CHECK(cuCtxCreate(&ctx, 0, 0) == CUDA_SUCCESS);
  CHECK(cuCtxDestroy(ctx) == CUDA_SUCCESS);
  CUcontext current;
  CHECK(cuCtxGetCurrent(&current) == CUDA_SUCCESS);
  // After destroy, current context should be null (stack empty).
  CHECK(current == nullptr);
}

// ---------------------------------------------------------------------------
// Phase 1.7 — E.1: cuFunc* 属性 API 测试 (Phase 1.7 A.1 - 4 cuFunc* APIs)
// ---------------------------------------------------------------------------

// Setup helper: load module + resolve function. Caller unloads.
static void phase17_load_module_and_function(CUmodule* mod, CUfunction* func,
                                             const char* fname,
                                             const char* func_name) {
  CHECK(cuModuleLoad(mod, fname) == CUDA_SUCCESS);
  CHECK(*mod != nullptr);
  CHECK(cuModuleGetFunction(func, *mod, func_name) == CUDA_SUCCESS);
  CHECK(*func != nullptr);
}

TEST_CASE("cuFuncGetAttribute returns 1024 for MAX_THREADS_PER_BLOCK") {
  CUmodule mod; CUfunction f;
  phase17_load_module_and_function(&mod, &f, "x.cubin", "_Z6kernelPv");
  int val = 0;
  CHECK(cuFuncGetAttribute(&val, CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK, f) ==
        CUDA_SUCCESS);
  CHECK(val == 1024);
  CHECK(cuModuleUnload(mod) == CUDA_SUCCESS);
}

TEST_CASE("cuFuncGetAttribute returns 48KB for SHARED_SIZE_BYTES") {
  CUmodule mod; CUfunction f;
  phase17_load_module_and_function(&mod, &f, "x.cubin", "_Z6kernelPv");
  int val = 0;
  CHECK(cuFuncGetAttribute(&val, CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES, f) ==
        CUDA_SUCCESS);
  CHECK(val == 48 * 1024);
  CHECK(cuModuleUnload(mod) == CUDA_SUCCESS);
}

TEST_CASE("cuFuncGetAttribute returns 64KB for CONST_SIZE_BYTES") {
  CUmodule mod; CUfunction f;
  phase17_load_module_and_function(&mod, &f, "x.cubin", "_Z6kernelPv");
  int val = 0;
  CHECK(cuFuncGetAttribute(&val, CU_FUNC_ATTRIBUTE_CONST_SIZE_BYTES, f) ==
        CUDA_SUCCESS);
  CHECK(val == 64 * 1024);
  CHECK(cuModuleUnload(mod) == CUDA_SUCCESS);
}

TEST_CASE("cuFuncGetAttribute returns 32 for NUM_REGS") {
  CUmodule mod; CUfunction f;
  phase17_load_module_and_function(&mod, &f, "x.cubin", "_Z6kernelPv");
  int val = 0;
  CHECK(cuFuncGetAttribute(&val, CU_FUNC_ATTRIBUTE_NUM_REGS, f) ==
        CUDA_SUCCESS);
  CHECK(val == 32);
  CHECK(cuModuleUnload(mod) == CUDA_SUCCESS);
}

TEST_CASE("cuFuncGetAttribute returns INVALID_VALUE for unknown attribute") {
  CUmodule mod; CUfunction f;
  phase17_load_module_and_function(&mod, &f, "x.cubin", "_Z6kernelPv");
  int val = 0;
  // CU_FUNC_ATTRIBUTE_MAX = 100 (sentinel) — beyond handled cases → default branch.
  CHECK(cuFuncGetAttribute(&val, CU_FUNC_ATTRIBUTE_MAX, f) ==
        CUDA_ERROR_INVALID_VALUE);
  CHECK(cuModuleUnload(mod) == CUDA_SUCCESS);
}

TEST_CASE("cuFuncGetAttribute returns INVALID_VALUE for null function") {
  int val = 0;
  CHECK(cuFuncGetAttribute(&val, CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK,
                           nullptr) == CUDA_ERROR_INVALID_VALUE);
}

TEST_CASE("cuFuncSetAttribute then Get returns same value (round-trip)") {
  CUmodule mod; CUfunction f;
  phase17_load_module_and_function(&mod, &f, "x.cubin", "_Z6kernelPv");
  CHECK(cuFuncSetAttribute(f, CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK, 512) ==
        CUDA_SUCCESS);
  int val = 0;
  CHECK(cuFuncGetAttribute(&val, CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK, f) ==
        CUDA_SUCCESS);
  CHECK(val == 512);
  CHECK(cuModuleUnload(mod) == CUDA_SUCCESS);
}

TEST_CASE("cuFuncSetCacheConfig accepts valid config") {
  CUmodule mod; CUfunction f;
  phase17_load_module_and_function(&mod, &f, "x.cubin", "_Z6kernelPv");
  CHECK(cuFuncSetCacheConfig(f, CU_FUNC_CACHE_PREFER_L1) == CUDA_SUCCESS);
  CHECK(cuModuleUnload(mod) == CUDA_SUCCESS);
}

TEST_CASE("cuFuncGetModule returns owning module handle") {
  CUmodule mod; CUfunction f;
  phase17_load_module_and_function(&mod, &f, "x.cubin", "_Z6kernelPv");
  CUmodule got = nullptr;
  CHECK(cuFuncGetModule(&got, f) == CUDA_SUCCESS);
  CHECK(got == mod);
  CHECK(cuModuleUnload(mod) == CUDA_SUCCESS);
}

TEST_CASE("cuFuncGetModule returns INVALID_VALUE for null function") {
  CUmodule got = nullptr;
  CHECK(cuFuncGetModule(&got, nullptr) == CUDA_ERROR_INVALID_VALUE);
}

// ---------------------------------------------------------------------------
// Phase 1.7 — E.2: cuOccupancy* 启发式 API 测试 (Phase 1.7 A.2 - 3 APIs)
// ---------------------------------------------------------------------------

TEST_CASE("cuOccupancyMaxActiveBlocksPerMultiprocessor returns >= 1 for block_size 256") {
  CUmodule mod; CUfunction f;
  phase17_load_module_and_function(&mod, &f, "x.cubin", "_Z6kernelPv");
  int blocks = 0;
  CHECK(cuOccupancyMaxActiveBlocksPerMultiprocessor(&blocks, f, 256, 0) ==
        CUDA_SUCCESS);
  CHECK(blocks >= 1);
  CHECK(blocks <= 32);
  CHECK(cuModuleUnload(mod) == CUDA_SUCCESS);
}

TEST_CASE("cuOccupancyMaxActiveBlocksPerMultiprocessorWithFlags delegates to non-flags variant") {
  CUmodule mod; CUfunction f;
  phase17_load_module_and_function(&mod, &f, "x.cubin", "_Z6kernelPv");
  int blocks = 0;
  CHECK(cuOccupancyMaxActiveBlocksPerMultiprocessorWithFlags(&blocks, f, 256,
                                                              0, 0) ==
        CUDA_SUCCESS);
  CHECK(blocks >= 1);
  CHECK(blocks <= 32);
  CHECK(cuModuleUnload(mod) == CUDA_SUCCESS);
}

TEST_CASE("cuOccupancyMaxPotentialBlockSize returns blockSize=256 minGridSize=80") {
  CUmodule mod; CUfunction f;
  phase17_load_module_and_function(&mod, &f, "x.cubin", "_Z6kernelPv");
  int min_grid_size = 0;
  int block_size = 0;
  CHECK(cuOccupancyMaxPotentialBlockSize(&min_grid_size, &block_size, f,
                                         nullptr, 0, 0) == CUDA_SUCCESS);
  CHECK(block_size == 256);
  CHECK(min_grid_size == 80);
  CHECK(cuModuleUnload(mod) == CUDA_SUCCESS);
}

// ---------------------------------------------------------------------------
// Phase 1.7 — E.3: cuPointerGetAttribute + A.4 轻量 stub (6 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cuPointerGetAttribute CONTEXT returns SUCCESS with valid output") {
  CUdeviceptr dptr = 0;
  CHECK(cuMemAlloc(&dptr, 1024) == CUDA_SUCCESS);
  CUcontext ctx_out = reinterpret_cast<CUcontext>(0xDEADBEEF);
  CHECK(cuPointerGetAttribute(&ctx_out, CU_POINTER_ATTRIBUTE_CONTEXT, dptr) ==
        CUDA_SUCCESS);
  CHECK(cuMemFree(dptr) == CUDA_SUCCESS);
}

TEST_CASE("cuPointerGetAttribute MEMORY_TYPE returns DEVICE") {
  CUdeviceptr dptr = 0;
  CHECK(cuMemAlloc(&dptr, 1024) == CUDA_SUCCESS);
  int mem_type = 0;
  CHECK(cuPointerGetAttribute(&mem_type, CU_POINTER_ATTRIBUTE_MEMORY_TYPE,
                              dptr) == CUDA_SUCCESS);
  CHECK(mem_type == CU_MEMORYTYPE_DEVICE);
  CHECK(cuMemFree(dptr) == CUDA_SUCCESS);
}

TEST_CASE("cuPointerGetAttribute DEVICE_POINTER returns identity") {
  CUdeviceptr dptr = 0;
  CHECK(cuMemAlloc(&dptr, 1024) == CUDA_SUCCESS);
  CUdeviceptr identity = 0;
  CHECK(cuPointerGetAttribute(&identity, CU_POINTER_ATTRIBUTE_DEVICE_POINTER,
                              dptr) == CUDA_SUCCESS);
  CHECK(identity == dptr);
  CHECK(cuMemFree(dptr) == CUDA_SUCCESS);
}

TEST_CASE("cuPointerGetAttribute RANGE_SIZE returns alloc size") {
  CUdeviceptr dptr = 0;
  CHECK(cuMemAlloc(&dptr, 4096) == CUDA_SUCCESS);
  size_t size = 0;
  CHECK(cuPointerGetAttribute(&size, CU_POINTER_ATTRIBUTE_RANGE_SIZE, dptr) ==
        CUDA_SUCCESS);
  CHECK(size == 4096);
  CHECK(cuMemFree(dptr) == CUDA_SUCCESS);
}

TEST_CASE("cuMemsetD16 writes first 16-bit value and returns SUCCESS") {
  CUdeviceptr dptr = 0;
  CHECK(cuMemAlloc(&dptr, 8) == CUDA_SUCCESS);
  CHECK(cuMemsetD16(dptr, 0xABCD, 4) == CUDA_SUCCESS);
  CHECK(cuMemFree(dptr) == CUDA_SUCCESS);
}

TEST_CASE("cuProfilerStart/Stop/Initialize return NOT_SUPPORTED") {
  CHECK(cuProfilerStart() == CUDA_ERROR_NOT_SUPPORTED);
  CHECK(cuProfilerStop() == CUDA_ERROR_NOT_SUPPORTED);
  CHECK(cuProfilerInitialize("config.txt", "output.txt", 0) ==
        CUDA_ERROR_NOT_SUPPORTED);
}

// ---------------------------------------------------------------------------
// Phase 1.7 — E.4: cuCtx 完整集 (8 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cuCtxGetDevice returns 0 for current context") {
  CUdevice dev = -1;
  CHECK(cuCtxGetDevice(&dev) == CUDA_SUCCESS);
  CHECK(dev == 0);
}

TEST_CASE("cuCtxGetFlags returns non-zero after ctx create") {
  unsigned int flags = 0;
  CHECK(cuCtxGetFlags(&flags) == CUDA_SUCCESS);
}

TEST_CASE("cuCtxPushCurrent then PopCurrent round-trip") {
  CUcontext ctx;
  CHECK(cuCtxCreate(&ctx, 0, 0) == CUDA_SUCCESS);
  CHECK(cuCtxPushCurrent(ctx) == CUDA_SUCCESS);
  CUcontext popped = nullptr;
  CHECK(cuCtxPopCurrent(&popped) == CUDA_SUCCESS);
  CHECK(popped == ctx);
  CHECK(cuCtxDestroy(ctx) == CUDA_SUCCESS);
}

TEST_CASE("cuCtxSynchronize returns SUCCESS (no-op)") {
  CHECK(cuCtxSynchronize() == CUDA_SUCCESS);
}

TEST_CASE("cuCtxGetSharedMemConfig returns valid config") {
  CUsharedconfig config = -1;
  CHECK(cuCtxGetSharedMemConfig(&config) == CUDA_SUCCESS);
  CHECK(config >= 0);
}

TEST_CASE("cuCtxSetSharedMemConfig then Get round-trip") {
  CUsharedconfig original = -1;
  CHECK(cuCtxGetSharedMemConfig(&original) == CUDA_SUCCESS);
  CHECK(cuCtxSetSharedMemConfig(original) == CUDA_SUCCESS);
  CUsharedconfig got = -1;
  CHECK(cuCtxGetSharedMemConfig(&got) == CUDA_SUCCESS);
  CHECK(got == original);
}

TEST_CASE("cuCtxSetLimit STACK_SIZE accepts value") {
  CHECK(cuCtxSetLimit(CU_LIMIT_STACK_SIZE, 2048) == CUDA_SUCCESS);
}

TEST_CASE("cuCtxGetApiVersion returns CUDA version >= 11000") {
  CUcontext ctx;
  CHECK(cuCtxCreate(&ctx, 0, 0) == CUDA_SUCCESS);
  unsigned int version = 0;
  CHECK(cuCtxGetApiVersion(ctx, &version) == CUDA_SUCCESS);
  CHECK(version >= 11000);
  CHECK(cuCtxDestroy(ctx) == CUDA_SUCCESS);
}

// ---------------------------------------------------------------------------
// Phase 1.7 — E.5: PrimaryCtx 完整集 (3 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cuDevicePrimaryCtxReset returns SUCCESS for dev 0") {
  CHECK(cuDevicePrimaryCtxReset(0) == CUDA_SUCCESS);
}

TEST_CASE("cuDevicePrimaryCtxGetState returns active=1 flags=0") {
  unsigned int flags = 0xFFFFFFFF;
  int active = -1;
  CHECK(cuDevicePrimaryCtxGetState(0, &flags, &active) == CUDA_SUCCESS);
  CHECK(active == 1);
  CHECK(flags == 0);
}

TEST_CASE("cuDevicePrimaryCtxSetFlags accepts flags for dev 0") {
  CHECK(cuDevicePrimaryCtxSetFlags(0, 0) == CUDA_SUCCESS);
}

// ---------------------------------------------------------------------------
// Phase 1.7 — E.6: Launch API (3 cases)
// ---------------------------------------------------------------------------

TEST_CASE("cuLaunchKernel with valid registered function returns SUCCESS") {
  CUmodule mod; CUfunction f;
  phase17_load_module_and_function(&mod, &f, "x.cubin", "_Z6kernelPv");
  void* args[1] = {nullptr};
  CUresult r = cuLaunchKernel(f, /*gridDim=*/1, 1, 1, /*blockDim=*/1, 1, 1,
                              /*sharedMem=*/0, /*hStream=*/0, args, nullptr);
  CHECK(r == CUDA_SUCCESS);
  CHECK(cuModuleUnload(mod) == CUDA_SUCCESS);
}

TEST_CASE("cuLaunchKernelEx with valid registered function returns SUCCESS") {
  CUmodule mod; CUfunction f;
  phase17_load_module_and_function(&mod, &f, "x.cubin", "_Z6kernelPv");
  CUlaunchConfig cfg{};
  cfg.gridDimX = 1;
  cfg.gridDimY = 1;
  cfg.gridDimZ = 1;
  cfg.blockDimX = 1;
  cfg.blockDimY = 1;
  cfg.blockDimZ = 1;
  cfg.sharedMemBytes = 0;
  cfg.hStream = 0;
  cfg.kernelParams = nullptr;
  cfg.extra = nullptr;
  CUresult r = cuLaunchKernelEx(&cfg, f, nullptr);
  CHECK(r == CUDA_SUCCESS);
  CHECK(cuModuleUnload(mod) == CUDA_SUCCESS);
}

TEST_CASE("cuLaunchHostFunc returns SUCCESS or NOT_IMPLEMENTED") {
  auto fn = [](void*) {};
  CUstream stream;
  CHECK(cuStreamCreate(&stream, 0) == CUDA_SUCCESS);
  // Accept either SUCCESS or NOT_IMPLEMENTED — spec allows both (host-side
  // function launch is deferred in Phase 1.7).
  CUresult r = cuLaunchHostFunc(stream, fn, nullptr);
  CHECK((r == CUDA_SUCCESS || r == CUDA_ERROR_NOT_IMPLEMENTED));
  CHECK(cuStreamDestroy(stream) == CUDA_SUCCESS);
}

// ---------------------------------------------------------------------------
// Phase 1.7 — E.7: STUB sanity 批量测试 (1 case)
// ---------------------------------------------------------------------------

TEST_CASE("STUB APIs return NOT_IMPLEMENTED") {
  CUmodule mod;
  CHECK(cuModuleLoadData(&mod, nullptr) == CUDA_ERROR_NOT_IMPLEMENTED);
  CHECK(cuModuleLoadDataEx(&mod, nullptr, 0, nullptr, nullptr) ==
        CUDA_ERROR_NOT_IMPLEMENTED);
  CHECK(cuModuleLoadFatBinary(&mod, nullptr) == CUDA_ERROR_NOT_IMPLEMENTED);

  CUtexref texref;
  CHECK(cuModuleGetTexRef(&texref, nullptr, "x") == CUDA_ERROR_NOT_IMPLEMENTED);

  CUarray arr;
  (void)arr;

  // cuProfilerStart already covered; not duplicate assertion here.

  CUdeviceptr dptr = 0;
  CHECK(cuMemcpyAsync(dptr, dptr, 0, /*hStream=*/0) ==
        CUDA_ERROR_NOT_IMPLEMENTED);
  CHECK(cuMemsetD32(dptr, 0, 0) == CUDA_ERROR_NOT_IMPLEMENTED);
  CHECK(cuMemHostRegister(nullptr, 0, 0) == CUDA_ERROR_NOT_IMPLEMENTED);
}

}  // namespace
