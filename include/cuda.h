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
typedef int   CUdevice;
typedef unsigned long long CUdeviceptr;
typedef uint32_t cuuint32_t;

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

/* --- Context/function attribute types (opaque int enums) --- */
typedef int CUfunc_cacheConfig;
typedef int CUsharedconfig;
typedef int CUlimit;
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
CUresult cuCtxGetCacheConfig(CUfunc_cacheConfig* pconfig);
CUresult cuCtxSetCacheConfig(CUfunc_cacheConfig config);
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

CUresult cuGetErrorName(CUresult error, const char** pstr);
CUresult cuGetErrorString(CUresult error, const char** pstr);

#ifdef __cplusplus
}
#endif

#endif /* CUDA_H_ */
