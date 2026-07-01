// SCOPE: UMD-EVOLUTION
// cu_device.cpp - Device enumeration (single-device stub).
//
// Phase 2 PoC: returns single device (ordinal 0) with reasonable defaults.
// Device name, attributes, and total memory are fixed values.

#include <cuda.h>

#include <cstddef>

extern "C" CUresult cuDeviceGetCount(int* count) {
  if (!count) return CUDA_ERROR_INVALID_VALUE;
  *count = 1;  // Phase 2: single device
  return CUDA_SUCCESS;
}

extern "C" CUresult cuDeviceGet(CUdevice* dev, int ordinal) {
  if (!dev) return CUDA_ERROR_INVALID_VALUE;
  if (ordinal < 0 || ordinal >= 1) return CUDA_ERROR_INVALID_VALUE;
  *dev = 0;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuDeviceGetName(char* name, int len, CUdevice dev) {
  if (!name) return CUDA_ERROR_INVALID_VALUE;
  if (len <= 0) return CUDA_ERROR_INVALID_VALUE;
  if (dev != 0) return CUDA_ERROR_INVALID_VALUE;
  const char* src = "TaskRunner CUDA Stub";
  int i;
  for (i = 0; i < len - 1 && src[i]; ++i) name[i] = src[i];
  name[i] = '\0';
  return CUDA_SUCCESS;
}

extern "C" CUresult cuDeviceGetAttribute(int* pi, CUdevice_attribute attrib,
                                         CUdevice dev) {
  if (!pi) return CUDA_ERROR_INVALID_VALUE;
  if (dev != 0) return CUDA_ERROR_INVALID_VALUE;
  *pi = 0;
  switch (attrib) {
    case CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT:
      *pi = 1;
      break;
    case CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_MULTIPROCESSOR:
      *pi = 1024;
      break;
    case CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR:
      *pi = 7;
      break;
    case CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR:
      *pi = 5;
      break;
    case CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK:
      *pi = 1024;
      break;
    case CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_X:
      *pi = 1024;
      break;
    case CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_X:
      *pi = 2147483647;
      break;
    case CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK:
      *pi = 49152;
      break;
    default:
      *pi = 0;
      break;
  }
  return CUDA_SUCCESS;
}

extern "C" CUresult cuDeviceTotalMem(size_t* bytes, CUdevice dev) {
  if (!bytes) return CUDA_ERROR_INVALID_VALUE;
  if (dev != 0) return CUDA_ERROR_INVALID_VALUE;
  *bytes = 8ULL * 1024 * 1024 * 1024;  // 8 GB
  return CUDA_SUCCESS;
}

extern "C" CUresult cuDeviceComputeCapability(int* major, int* minor,
                                              CUdevice dev) {
  if (!major || !minor) return CUDA_ERROR_INVALID_VALUE;
  if (dev != 0) return CUDA_ERROR_INVALID_VALUE;
  *major = 7;
  *minor = 5;
  return CUDA_SUCCESS;
}
