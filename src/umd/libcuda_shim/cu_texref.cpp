// SCOPE: UMD-EVOLUTION
// cu_texref.cpp - CUDA texture reference frontend (Phase 3.3b REAL_IMPL).
//
// Implements 8 cuTexRef* APIs with self-contained atomic + map + mutex state
// table. Matches the shim-local pattern used by cu_stream_capture.cpp /
// cu_graph.cpp / cu_mem_pool.cpp / cu_event.cpp / cu_array.cpp.
//
// Shim does NOT call GpuDriverClient (D-S3-1 decision). Texture references
// are pure frontend state machines; no GPU sampling. Phase 4+ can bridge
// to UsrLinuxEmu for real texture management.
//
// Handle convention:
//   CUtexref = void*  (atomic counter)
//
// Pattern: atomic + map + mutex (D-S3-1)

#include <cuda.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace async_task::umd::shim {
namespace {

struct TexRefRecord {
  unsigned long long address{0};     // bound BO address
  size_t address_offset{0};
  CUarray_format format{CU_AD_FORMAT_FLOAT};
  unsigned int num_channels{1};
  CUarray bound_array{nullptr};      // optional
  unsigned int flags{0};
  bool is_destroyed{false};
};

struct TexRefTable {
  std::atomic<std::uint64_t> next_id{1};
  std::unordered_map<CUtexref, TexRefRecord> texrefs;
  std::mutex mu;
};
TexRefTable g_texrefs;

}  // namespace
}  // namespace async_task::umd::shim

extern "C" CUresult cuTexRefCreate(CUtexref* pTexRef) {
  if (!pTexRef) return CUDA_ERROR_INVALID_VALUE;
  auto& table = async_task::umd::shim::g_texrefs;
  std::lock_guard<std::mutex> lock(table.mu);
  uint64_t id = table.next_id.fetch_add(1);
  *pTexRef = reinterpret_cast<CUtexref>(static_cast<uintptr_t>(id));
  table.texrefs[*pTexRef] = async_task::umd::shim::TexRefRecord{};
  return CUDA_SUCCESS;
}

extern "C" CUresult cuTexRefDestroy(CUtexref hTexRef) {
  if (!hTexRef) return CUDA_ERROR_INVALID_VALUE;
  auto& table = async_task::umd::shim::g_texrefs;
  std::lock_guard<std::mutex> lock(table.mu);
  auto it = table.texrefs.find(hTexRef);
  if (it == table.texrefs.end()) return CUDA_ERROR_INVALID_HANDLE;
  if (it->second.is_destroyed) return CUDA_ERROR_INVALID_HANDLE;
  it->second.is_destroyed = true;
  table.texrefs.erase(it);
  return CUDA_SUCCESS;
}

extern "C" CUresult cuTexRefSetArray(CUtexref hTexRef, CUarray hArray,
                                      unsigned int Flags) {
  if (!hTexRef) return CUDA_ERROR_INVALID_VALUE;
  auto& table = async_task::umd::shim::g_texrefs;
  std::lock_guard<std::mutex> lock(table.mu);
  auto it = table.texrefs.find(hTexRef);
  if (it == table.texrefs.end()) return CUDA_ERROR_INVALID_HANDLE;
  if (it->second.is_destroyed) return CUDA_ERROR_INVALID_HANDLE;
  it->second.bound_array = hArray;
  it->second.flags = Flags;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuTexRefSetAddress(size_t* ByteOffset, CUtexref hTexRef,
                                        CUdeviceptr dptr, size_t bytes) {
  if (!ByteOffset || !hTexRef) return CUDA_ERROR_INVALID_VALUE;
  auto& table = async_task::umd::shim::g_texrefs;
  std::lock_guard<std::mutex> lock(table.mu);
  auto it = table.texrefs.find(hTexRef);
  if (it == table.texrefs.end()) return CUDA_ERROR_INVALID_HANDLE;
  if (it->second.is_destroyed) return CUDA_ERROR_INVALID_HANDLE;
  it->second.address = static_cast<unsigned long long>(dptr);
  it->second.address_offset = *ByteOffset;
  *ByteOffset = 0;  // CUDA convention: byte offset consumed
  (void)bytes;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuTexRefSetFormat(CUtexref hTexRef, CUarray_format fmt,
                                       int NumPackedComponents) {
  if (!hTexRef) return CUDA_ERROR_INVALID_VALUE;
  auto& table = async_task::umd::shim::g_texrefs;
  std::lock_guard<std::mutex> lock(table.mu);
  auto it = table.texrefs.find(hTexRef);
  if (it == table.texrefs.end()) return CUDA_ERROR_INVALID_HANDLE;
  if (it->second.is_destroyed) return CUDA_ERROR_INVALID_HANDLE;
  it->second.format = fmt;
  it->second.num_channels = static_cast<unsigned int>(NumPackedComponents);
  return CUDA_SUCCESS;
}

extern "C" CUresult cuTexRefSetFlags(CUtexref hTexRef, unsigned int Flags) {
  if (!hTexRef) return CUDA_ERROR_INVALID_VALUE;
  auto& table = async_task::umd::shim::g_texrefs;
  std::lock_guard<std::mutex> lock(table.mu);
  auto it = table.texrefs.find(hTexRef);
  if (it == table.texrefs.end()) return CUDA_ERROR_INVALID_HANDLE;
  if (it->second.is_destroyed) return CUDA_ERROR_INVALID_HANDLE;
  it->second.flags = Flags;
  return CUDA_SUCCESS;
}

extern "C" CUresult cuTexRefGetAddress(CUdeviceptr* pdptr, CUtexref hTexRef) {
  if (!pdptr || !hTexRef) return CUDA_ERROR_INVALID_VALUE;
  auto& table = async_task::umd::shim::g_texrefs;
  std::lock_guard<std::mutex> lock(table.mu);
  auto it = table.texrefs.find(hTexRef);
  if (it == table.texrefs.end()) return CUDA_ERROR_INVALID_HANDLE;
  if (it->second.is_destroyed) return CUDA_ERROR_INVALID_HANDLE;
  *pdptr = static_cast<CUdeviceptr>(it->second.address);
  return CUDA_SUCCESS;
}

extern "C" CUresult cuTexRefGetArray(CUarray* phArray, CUtexref hTexRef) {
  if (!phArray || !hTexRef) return CUDA_ERROR_INVALID_VALUE;
  auto& table = async_task::umd::shim::g_texrefs;
  std::lock_guard<std::mutex> lock(table.mu);
  auto it = table.texrefs.find(hTexRef);
  if (it == table.texrefs.end()) return CUDA_ERROR_INVALID_HANDLE;
  if (it->second.is_destroyed) return CUDA_ERROR_INVALID_HANDLE;
  *phArray = it->second.bound_array;
  return CUDA_SUCCESS;
}