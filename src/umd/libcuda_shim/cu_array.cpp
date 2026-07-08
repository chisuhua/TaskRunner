// SCOPE: UMD-EVOLUTION
// cu_array.cpp - CUDA array management (Phase 3.3b REAL_IMPL).
//
// Implements 3 cuArray* APIs with self-contained atomic + map + mutex state
// table. Matches the shim-local pattern used by cu_stream_capture.cpp /
// cu_graph.cpp / cu_mem_pool.cpp / cu_event.cpp.
//
// Shim does NOT call GpuDriverClient (D-S3-1 decision). CUarray allocation
// uses virtual backing memory (vector<uint8_t>), no real GPU allocation.
// Phase 4+ can bridge to UsrLinuxEmu IOCTL for real VA reservation.
//
// Handle convention:
//   CUarray = void*  (atomic counter)
//
// Pattern: atomic + map + mutex (D-S3-1)

#include <cuda.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace async_task::umd::shim {
namespace {

struct ArrayDescriptor {
  CUarray_format format;      // CU_AD_FORMAT_*
  unsigned int num_channels;  // 1, 2, 4
  size_t width, height;
  size_t total_size_bytes;
  std::vector<std::uint8_t> backing;  // virtual backing memory
  bool is_destroyed{false};
};

struct ArrayTable {
  std::atomic<std::uint64_t> next_id{1};
  std::unordered_map<CUarray, std::unique_ptr<ArrayDescriptor>> arrays;
  std::mutex mu;
};
ArrayTable g_arrays;

}  // namespace
}  // namespace async_task::umd::shim

extern "C" CUresult cuArrayCreate(CUarray* pHandle,
                                  const CUDA_ARRAY_DESCRIPTOR* pAllocateArray) {
  if (!pHandle || !pAllocateArray) return CUDA_ERROR_INVALID_VALUE;
  if (pAllocateArray->Width == 0) return CUDA_ERROR_INVALID_VALUE;

  auto& table = async_task::umd::shim::g_arrays;
  std::lock_guard<std::mutex> lock(table.mu);

  auto desc = std::make_unique<async_task::umd::shim::ArrayDescriptor>();
  desc->format = pAllocateArray->Format;
  desc->num_channels = pAllocateArray->NumChannels;
  desc->width = pAllocateArray->Width;
  desc->height = (pAllocateArray->Height == 0) ? 1 : pAllocateArray->Height;
  desc->total_size_bytes = desc->width * desc->height * desc->num_channels;
  // Calculate element size from format
  unsigned int elem_bytes = 1;
  switch (desc->format) {
    case CU_AD_FORMAT_FLOAT:
    case CU_AD_FORMAT_SIGNED_INT32:
    case CU_AD_FORMAT_UNSIGNED_INT32:
      elem_bytes = 4; break;
    case CU_AD_FORMAT_SIGNED_INT16:
    case CU_AD_FORMAT_UNSIGNED_INT16:
    case CU_AD_FORMAT_HALF:
      elem_bytes = 2; break;
    default: break;  // 1 byte
  }
  desc->total_size_bytes *= elem_bytes;
  desc->backing.resize(desc->total_size_bytes, 0);

  uint64_t id = table.next_id.fetch_add(1);
  *pHandle = reinterpret_cast<CUarray>(static_cast<uintptr_t>(id));
  table.arrays[*pHandle] = std::move(desc);
  return CUDA_SUCCESS;
}

extern "C" CUresult cuArrayGetDescriptor(CUDA_ARRAY_DESCRIPTOR* pArrayDescriptor,
                                          CUarray hArray) {
  if (!pArrayDescriptor || !hArray) return CUDA_ERROR_INVALID_VALUE;
  auto& table = async_task::umd::shim::g_arrays;
  std::lock_guard<std::mutex> lock(table.mu);
  auto it = table.arrays.find(hArray);
  if (it == table.arrays.end()) return CUDA_ERROR_INVALID_HANDLE;
  if (it->second->is_destroyed) return CUDA_ERROR_INVALID_HANDLE;

  const auto& d = *it->second;
  pArrayDescriptor->Format = d.format;
  pArrayDescriptor->NumChannels = d.num_channels;
  pArrayDescriptor->Width = d.width;
  pArrayDescriptor->Height = (d.height == 1) ? 0 : d.height;  // CUDA convention
  return CUDA_SUCCESS;
}

extern "C" CUresult cuArrayDestroy(CUarray hArray) {
  if (!hArray) return CUDA_ERROR_INVALID_VALUE;
  auto& table = async_task::umd::shim::g_arrays;
  std::lock_guard<std::mutex> lock(table.mu);
  auto it = table.arrays.find(hArray);
  if (it == table.arrays.end()) return CUDA_ERROR_INVALID_HANDLE;
  if (it->second->is_destroyed) return CUDA_ERROR_INVALID_HANDLE;
  it->second->is_destroyed = true;
  table.arrays.erase(it);
  return CUDA_SUCCESS;
}