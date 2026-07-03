// SCOPE: UMD-EVOLUTION
// Minimal CUDA Driver API compatibility header for libcuda_taskrunner.so.
//
// Provides type definitions and enums needed by the LD_PRELOAD shim.
// Does NOT provide a real CUDA toolkit — only what the shim needs to compile.

#ifndef CUDA_H_
#define CUDA_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Handle types (all opaque pointers in user-mode) --- */
typedef void* CUcontext;
typedef void* CUmodule;
typedef void* CUfunction;
typedef void* CUstream;
typedef void* CUevent;
typedef void* CUgraph;
typedef void* CUtexref;
typedef void* CUsurfref;
typedef void* CUarray;
typedef int   CUdevice;
typedef unsigned long long CUdeviceptr;
typedef uint32_t cuuint32_t;
typedef unsigned long long cuuint64_t;

/* --- Result type --- */
typedef enum CUresult_enum {
  CUDA_SUCCESS                              = 0,
  CUDA_ERROR_INVALID_VALUE                  = 1,
  CUDA_ERROR_OUT_OF_MEMORY                  = 2,
  CUDA_ERROR_NOT_READY                      = 600,
  CUDA_ERROR_INVALID_HANDLE                 = 400,
  CUDA_ERROR_NOT_SUPPORTED                  = 801,
  /* NOTE: NOT_IMPLEMENTED is aliased to NOT_SUPPORTED (both value 801 in CUDA 12.x).
     We define it as a macro alias to avoid duplicate case value errors in switch(). */
  CUDA_ERROR_UNKNOWN                        = 999
} CUresult;

#define CUDA_ERROR_NOT_IMPLEMENTED CUDA_ERROR_NOT_SUPPORTED

/* --- Device attributes --- */
typedef enum CUdevice_attribute_enum {
  CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK         = 1,
  CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_X               = 2,
  CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Y               = 3,
  CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Z               = 4,
  CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_X                = 5,
  CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Y                = 6,
  CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Z                = 7,
  CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK   = 8,
  CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT          = 16,
  CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_MULTIPROCESSOR = 39,
  CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR       = 75,
  CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR       = 76,
  CU_DEVICE_ATTRIBUTE_CAN_MAP_HOST_MEMORY            = 62,
  CU_DEVICE_ATTRIBUTE_INTEGRATED                     = 72,
  CU_DEVICE_ATTRIBUTE_KERNEL_EXEC_TIMEOUT             = 85,
  CU_DEVICE_ATTRIBUTE_MAX                          = 100
} CUdevice_attribute;

/* --- Stream capture mode --- */
typedef enum CUstreamCaptureMode_enum {
  CU_STREAM_CAPTURE_MODE_GLOBAL      = 0,
  CU_STREAM_CAPTURE_MODE_THREAD_LOCAL = 1,
  CU_STREAM_CAPTURE_MODE_RELAXED     = 2
} CUstreamCaptureMode;

/* --- Stream callback --- */
typedef void (*CUstreamCallback)(CUstream hStream, CUresult status,
                                  void* userData);

/* --- Launch config (CUDA 12.x) --- */
typedef struct CUlaunchConfig_st {
  unsigned int gridDimX;
  unsigned int gridDimY;
  unsigned int gridDimZ;
  unsigned int blockDimX;
  unsigned int blockDimY;
  unsigned int blockDimZ;
  unsigned int sharedMemBytes;
  CUstream     hStream;
  void**       kernelParams;
  void**       extra;
} CUlaunchConfig;

/* --- Function attribute enum (cuFuncGetAttribute/SetAttribute) --- */
typedef enum CUfunction_attribute_enum {
  CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK = 0,
  CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES    = 1,
  CU_FUNC_ATTRIBUTE_CONST_SIZE_BYTES     = 2,
  CU_FUNC_ATTRIBUTE_NUM_REGS             = 3,
  CU_FUNC_ATTRIBUTE_MAX                  = 100
} CUfunction_attribute;

/* --- Pointer attribute enum (cuPointerGetAttribute) --- */
typedef enum CUpointer_attribute_enum {
  CU_POINTER_ATTRIBUTE_CONTEXT          = 1,
  CU_POINTER_ATTRIBUTE_MEMORY_TYPE      = 2,
  CU_POINTER_ATTRIBUTE_DEVICE_POINTER   = 3,
  CU_POINTER_ATTRIBUTE_HOST_POINTER     = 4,
  CU_POINTER_ATTRIBUTE_RANGE_START_ADDR = 10,
  CU_POINTER_ATTRIBUTE_RANGE_SIZE       = 11,
  CU_POINTER_ATTRIBUTE_MAX              = 16
} CUpointer_attribute;

/* --- Memory type enum (used by cuPointerGetAttribute) --- */
typedef enum CUresourcetype_enum {
  CU_MEMORYTYPE_HOST   = 1,
  CU_MEMORYTYPE_DEVICE = 2,
  CU_MEMORYTYPE_ARRAY  = 3,
  CU_MEMORYTYPE_UNIFIED = 4
} CUresourcetype;

/* --- Stream capture status (cuStreamGetCaptureInfo) --- */
typedef enum CUstreamCaptureStatus_enum {
  CU_STREAM_CAPTURE_STATUS_NONE        = 0,
  CU_STREAM_CAPTURE_STATUS_ACTIVE      = 1,
  CU_STREAM_CAPTURE_STATUS_INVALIDATED = 2
} CUstreamCaptureStatus;

/* --- Function cache config enum --- */
typedef enum CUfunc_cache_enum {
  CU_FUNC_CACHE_PREFER_NONE   = 0,
  CU_FUNC_CACHE_PREFER_SHARED = 1,
  CU_FUNC_CACHE_PREFER_L1     = 2,
  CU_FUNC_CACHE_PREFER_EQUAL  = 3
} CUfunc_cache;

/* --- Occupancy callback type (cuOccupancyMaxPotentialBlockSize) --- */
typedef size_t (*CUoccupancyB2DSize)(int blockSize);

/* --- Context/function attribute types (opaque int enums) --- */
typedef int CUfunc_cacheConfig;
typedef int CUsharedconfig;
typedef int CUlimit;

// Limit enum values (used as int, not typed enum).
#define CU_LIMIT_STACK_SIZE 0
#define CU_LIMIT_PRINTF_FIFO_SIZE 1
#define CU_LIMIT_MALLOC_HEAP_SIZE 2
typedef int CUgraphExec;
typedef int CUgraphNode;

/* --- Function prototypes for libcuda_taskrunner.so --- */
CUresult cuInit(unsigned int Flags);
CUresult cuDriverGetVersion(int* version);
CUresult cuDriverGet(int* driver);

CUresult cuDeviceGetCount(int* count);
CUresult cuDeviceGet(CUdevice* dev, int ordinal);
CUresult cuDeviceGetName(char* name, int len, CUdevice dev);
CUresult cuDeviceGetAttribute(int* pi, CUdevice_attribute attrib, CUdevice dev);
CUresult cuDeviceTotalMem(size_t* bytes, CUdevice dev);
CUresult cuDeviceComputeCapability(int* major, int* minor, CUdevice dev);

CUresult cuCtxCreate(CUcontext* pctx, unsigned int flags, CUdevice dev);
CUresult cuCtxDestroy(CUcontext ctx);
CUresult cuCtxSetCurrent(CUcontext ctx);
CUresult cuCtxGetCurrent(CUcontext* pctx);
CUresult cuCtxPushCurrent(CUcontext ctx);
CUresult cuCtxPopCurrent(CUcontext* pctx);
CUresult cuCtxSynchronize(void);
CUresult cuCtxGetDevice(CUdevice* device);
CUresult cuCtxGetFlags(unsigned int* flags);
CUresult cuCtxGetApiVersion(CUcontext ctx, unsigned int* version);
CUresult cuCtxGetCacheConfig(CUfunc_cache* pconfig);
CUresult cuCtxSetCacheConfig(CUfunc_cache config);
CUresult cuCtxGetSharedMemConfig(CUsharedconfig* pConfig);
CUresult cuCtxSetSharedMemConfig(CUsharedconfig config);
CUresult cuCtxGetLimit(size_t* pvalue, CUlimit limit);
CUresult cuCtxSetLimit(CUlimit limit, size_t value);

CUresult cuDevicePrimaryCtxRetain(CUcontext* pctx, CUdevice dev);
CUresult cuDevicePrimaryCtxRelease(CUdevice dev);
CUresult cuDevicePrimaryCtxReset(CUdevice dev);
CUresult cuDevicePrimaryCtxSetFlags(CUdevice dev, unsigned int flags);
CUresult cuDevicePrimaryCtxGetState(CUdevice dev, unsigned int* flags, int* active);

CUresult cuModuleLoad(CUmodule* module, const char* fname);
CUresult cuModuleLoadData(CUmodule* module, const void* image);
CUresult cuModuleLoadDataEx(CUmodule* module, const void* image,
                            unsigned int numOptions, int* options,
                            void** optionValues);
CUresult cuModuleLoadFatBinary(CUmodule* module, const void* fatCubin);
CUresult cuModuleUnload(CUmodule hmod);
CUresult cuModuleGetFunction(CUfunction* hfunc, CUmodule hmod, const char* name);
CUresult cuModuleGetGlobal(void** dptr, size_t* bytes, CUmodule hmod, const char* name);
CUresult cuModuleGetTexRef(CUtexref* pTexRef, CUmodule hmod, const char* name);
CUresult cuModuleGetSurfRef(CUsurfref* pSurfRef, CUmodule hmod, const char* name);

CUresult cuMemAlloc(CUdeviceptr* dptr, size_t bytesize);
CUresult cuMemFree(CUdeviceptr dptr);
CUresult cuMemAllocHost(void** pp, size_t bytesize, unsigned int Flags);
CUresult cuMemFreeHost(void* p);
CUresult cuMemAllocManaged(CUdeviceptr* dptr, size_t bytesize, unsigned int flags);
CUresult cuMemAllocPitch(CUdeviceptr* dptr, size_t* pitch,
                         size_t width, size_t height,
                         unsigned int elementSizeBytes);
CUresult cuMemcpyHtoD(CUdeviceptr dstDevice, const void* srcHost, size_t ByteCount);
CUresult cuMemcpyDtoH(void* dstHost, CUdeviceptr srcDevice, size_t ByteCount);
CUresult cuMemcpyDtoD(CUdeviceptr dstDevice, CUdeviceptr srcDevice, size_t ByteCount);
CUresult cuMemcpy(CUdeviceptr dst, CUdeviceptr src, size_t ByteCount);
CUresult cuMemcpyAsync(CUdeviceptr dst, CUdeviceptr src,
                       size_t ByteCount, CUstream hStream);
CUresult cuMemsetD32(CUdeviceptr dstDevice, unsigned int ui, size_t N);
CUresult cuMemsetD8(CUdeviceptr dstDevice, unsigned char uc, size_t N);
CUresult cuMemGetInfo(size_t* free, size_t* total);
CUresult cuMemGetAddressRange(CUdeviceptr* base, size_t* size, CUdeviceptr dptr);

CUresult cuStreamCreate(CUstream* phStream, unsigned int flags);
CUresult cuStreamCreateWithPriority(CUstream* phStream, unsigned int flags,
                                    int priority);
CUresult cuStreamDestroy(CUstream hStream);
CUresult cuStreamSynchronize(CUstream hStream);
CUresult cuStreamQuery(CUstream hStream);
CUresult cuStreamWaitEvent(CUstream hStream, CUevent hEvent, unsigned int Flags);
CUresult cuStreamGetFlags(CUstream hStream, unsigned int* flags);
CUresult cuStreamGetPriority(CUstream hStream, int* priority);
CUresult cuStreamAddCallback(CUstream hStream, CUstreamCallback callback,
                             void* userData, unsigned int flags);
CUresult cuStreamBeginCapture(CUstream hStream, CUstreamCaptureMode mode);
CUresult cuStreamEndCapture(CUstream hStream, CUgraph* phGraph);
CUresult cuStreamWriteValue32(CUstream hStream, CUdeviceptr addr,
                              cuuint32_t value, unsigned int flags);
CUresult cuStreamWaitValue32(CUstream hStream, CUdeviceptr addr,
                             cuuint32_t value, unsigned int flags);

CUresult cuEventCreate(CUevent* phEvent, unsigned int Flags);
CUresult cuEventDestroy(CUevent hEvent);
CUresult cuEventRecord(CUevent hEvent, CUstream hStream);
CUresult cuEventSynchronize(CUevent hEvent);
CUresult cuEventQuery(CUevent hEvent);
CUresult cuEventElapsedTime(float* pMilliseconds, CUevent hStart, CUevent hEnd);

CUresult cuLaunchKernel(CUfunction f,
                        unsigned int gridDimX, unsigned int gridDimY,
                        unsigned int gridDimZ,
                        unsigned int blockDimX, unsigned int blockDimY,
                        unsigned int blockDimZ,
                        unsigned int sharedMemBytes, CUstream hStream,
                        void** kernelParams, void** extra);
CUresult cuLaunchKernelEx(const CUlaunchConfig* config, CUfunction f,
                          void** extraParams);
CUresult cuLaunchHostFunc(CUstream hStream, void (*fn)(void*), void* userData);
CUresult cuLaunchCooperativeKernel(CUfunction f,
                                   unsigned int gridDimX, unsigned int gridDimY,
                                   unsigned int gridDimZ,
                                   unsigned int blockDimX, unsigned int blockDimY,
                                   unsigned int blockDimZ,
                                   unsigned int sharedMemBytes, CUstream hStream,
                                   void** kernelParams);

/* --- Phase 1.7 A.1: cuFunc* 属性 API --- */
CUresult cuFuncGetAttribute(int* val, CUfunction_attribute attr, CUfunction f);
CUresult cuFuncSetAttribute(CUfunction f, CUfunction_attribute attr, int val);
CUresult cuFuncSetCacheConfig(CUfunction f, CUfunc_cache config);
CUresult cuFuncGetModule(CUmodule* mod, CUfunction f);

/* --- Phase 1.7 A.2: cuOccupancy* 启发式 API --- */
CUresult cuOccupancyMaxActiveBlocksPerMultiprocessor(int* blocks, CUfunction f,
                                                     int blockSize,
                                                     size_t dynSMem);
CUresult cuOccupancyMaxActiveBlocksPerMultiprocessorWithFlags(
    int* blocks, CUfunction f, int blockSize, size_t dynSMem,
    unsigned int flags);
CUresult cuOccupancyMaxPotentialBlockSize(int* minGridSize, int* blockSize,
                                          CUfunction f,
                                          CUoccupancyB2DSize blockSizeToDynamicSMem,
                                          size_t dynSMemPerBlock,
                                          int blockSizeLimit);

/* --- Phase 1.7 A.3: cuPointerGetAttribute --- */
CUresult cuPointerGetAttribute(void* data, CUpointer_attribute attr,
                               CUdeviceptr ptr);

/* --- Phase 1.7 A.4: 轻量 stub API --- */
CUresult cuStreamCreateWithFlags(CUstream* phStream, unsigned int flags);
CUresult cuStreamGetCaptureInfo(CUstream hStream,
                                CUstreamCaptureStatus* captureStatus,
                                cuuint64_t* id);
CUresult cuEventCreateWithFlags(CUevent* phEvent, unsigned int flags);
CUresult cuMemsetD16(CUdeviceptr dstDevice, unsigned short us, size_t N);
CUresult cuProfilerStart(void);
CUresult cuProfilerStop(void);
CUresult cuProfilerInitialize(const char* configFile, const char* outputFile,
                              unsigned int outputMode);

/* --- Phase 1.7 STUB sanity API (cuArrayCreate/cuGraphCreate/etc) --- */
CUresult cuArrayCreate(CUarray* pHandle, const void* allocSize);
CUresult cuGraphCreate(CUgraph* phGraph, unsigned int flags);
CUresult cuTexRefCreate(CUtexref* pTexRef);
CUresult cuMemHostRegister(void* p, size_t bytesize, unsigned int Flags);

CUresult cuGetErrorName(CUresult error, const char** pstr);
CUresult cuGetErrorString(CUresult error, const char** pstr);

#ifdef __cplusplus
}
#endif

#endif /* CUDA_H_ */
