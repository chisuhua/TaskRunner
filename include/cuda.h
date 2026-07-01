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

#ifdef __cplusplus
}
#endif

#endif /* CUDA_H_ */
