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

}  // namespace
