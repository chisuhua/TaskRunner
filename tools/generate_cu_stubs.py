#!/usr/bin/env python3
"""Generate CUDA Driver API stub declarations for libcuda_taskrunner.so.

Reads CUDA Driver API symbol list and generates ~150 inline stub declarations
for the LD_PRELOAD shim. Critical APIs (cuInit, cuDeviceGet, etc.) are NOT
stubbed -- they have real implementations in src/umd/libcuda_shim/cu_*.cpp.

Auto-generated; do not edit manually.
"""

import sys
from pathlib import Path

# APIs implemented in real cu_*.cpp files. These MUST NOT be stubbed.
# Each maps to the source file containing the implementation.
CRITICAL_APIS_IMPL_REQUIRED = {
    # Initialization & Version
    "cuInit": "cu_init.cpp",
    "cuDriverGetVersion": "cu_query.cpp",
    "cuDriverGet": "cu_query.cpp",
    # Device
    "cuDeviceGetCount": "cu_device.cpp",
    "cuDeviceGet": "cu_device.cpp",
    "cuDeviceGetName": "cu_query.cpp",
    "cuDeviceGetAttribute": "cu_query.cpp",
    "cuDeviceTotalMem": "cu_query.cpp",
    # Context
    "cuCtxCreate": "cu_ctx.cpp",
    "cuCtxDestroy": "cu_ctx.cpp",
    "cuCtxSetCurrent": "cu_ctx.cpp",
    "cuCtxGetCurrent": "cu_ctx.cpp",
    "cuCtxPushCurrent": "cu_ctx.cpp",
    "cuCtxPopCurrent": "cu_ctx.cpp",
    "cuCtxSynchronize": "cu_ctx.cpp",
    "cuCtxGetDevice": "cu_ctx.cpp",
    "cuCtxGetApiVersion": "cu_query.cpp",
    "cuCtxGetFlags": "cu_ctx.cpp",
    "cuDevicePrimaryCtxRetain": "cu_query.cpp",
    "cuDevicePrimaryCtxRelease": "cu_query.cpp",
    "cuDevicePrimaryCtxReset": "cu_query.cpp",
    # Module
    "cuModuleLoad": "cu_module.cpp",
    "cuModuleUnload": "cu_module.cpp",
    "cuModuleGetFunction": "cu_module.cpp",
    "cuModuleGetGlobal": "cu_module.cpp",
    # Memory
    "cuMemAlloc": "cu_mem.cpp",
    "cuMemFree": "cu_mem.cpp",
    "cuMemcpyHtoD": "cu_mem.cpp",
    "cuMemcpyDtoH": "cu_mem.cpp",
    "cuMemcpyDtoD": "cu_mem.cpp",
    "cuMemcpy": "cu_mem.cpp",
    "cuMemcpyAsync": "cu_mem.cpp",
    "cuMemsetD32": "cu_mem.cpp",
    "cuMemsetD8": "cu_mem.cpp",
    "cuMemAllocHost": "cu_mem.cpp",
    "cuMemFreeHost": "cu_mem.cpp",
    # Launch
    "cuLaunchKernel": "cu_launch.cpp",
}

# All CUDA Driver APIs (~200). Those in CRITICAL_APIS_IMPL_REQUIRED become
# real implementations; the rest become stubs.
CUDA_DRIVER_APIS = [
    # Initialization & Version
    "cuInit", "cuDriverGetVersion", "cuDriverGet",
    # Device enumeration
    "cuDeviceGetCount", "cuDeviceGet", "cuDeviceGetName",
    "cuDeviceGetAttribute", "cuDeviceTotalMem", "cuDeviceComputeCapability",
    # Context Management
    "cuCtxCreate", "cuCtxDestroy", "cuCtxSetCurrent", "cuCtxGetCurrent",
    "cuCtxPushCurrent", "cuCtxPopCurrent", "cuCtxSynchronize",
    "cuCtxGetDevice", "cuCtxGetFlags", "cuCtxGetApiVersion",
    "cuCtxGetCacheConfig", "cuCtxSetCacheConfig",
    "cuCtxGetSharedMemConfig", "cuCtxSetSharedMemConfig",
    "cuCtxGetLimit", "cuCtxSetLimit",
    "cuDevicePrimaryCtxRetain", "cuDevicePrimaryCtxRelease",
    "cuDevicePrimaryCtxReset", "cuDevicePrimaryCtxSetFlags",
    # Module Loading
    "cuModuleLoad", "cuModuleLoadData", "cuModuleLoadDataEx",
    "cuModuleLoadFatBinary", "cuModuleUnload", "cuModuleGetFunction",
    "cuModuleGetTexRef", "cuModuleGetSurfRef", "cuModuleGetGlobal",
    # Memory Management
    "cuMemAlloc", "cuMemFree", "cuMemAllocHost", "cuMemFreeHost",
    "cuMemHostAlloc", "cuMemHostGetDevicePointer", "cuMemHostGetFlags",
    "cuMemAllocManaged", "cuMemAllocPitch",
    "cuMemcpyHtoD", "cuMemcpyDtoH", "cuMemcpyDtoD",
    "cuMemcpyHtoDAsync", "cuMemcpyDtoHAsync", "cuMemcpyDtoDAsync",
    "cuMemcpy2D", "cuMemcpy2DAsync", "cuMemcpy3D", "cuMemcpy3DAsync",
    "cuMemcpy", "cuMemcpyAsync", "cuMemsetD32", "cuMemsetD16",
    "cuMemsetD8", "cuMemsetD32Async",
    "cuMemAddressFree", "cuMemAddressReserve",
    "cuMemExportToShareableHandle", "cuMemImportFromShareableHandle",
    "cuMemGetInfo", "cuMemGetAddressRange",
    # Stream & Event
    "cuStreamCreate", "cuStreamDestroy", "cuStreamSynchronize",
    "cuStreamQuery", "cuStreamWaitEvent", "cuStreamWaitValue32",
    "cuStreamWriteValue32", "cuStreamAddCallback",
    "cuStreamCreateWithPriority", "cuStreamCreateWithFlags",
    "cuStreamGetPriority", "cuStreamGetFlags", "cuStreamGetCaptureInfo",
    "cuStreamBeginCapture", "cuStreamEndCapture",
    "cuEventCreate", "cuEventDestroy", "cuEventRecord",
    "cuEventSynchronize", "cuEventElapsedTime", "cuEventQuery",
    "cuEventCreateWithFlags",
    # Kernel Launch
    "cuLaunchKernel", "cuLaunchKernelEx", "cuLaunchHostFunc",
    "cuLaunchCooperativeKernel",
    # Texture/Surface
    "cuTexRefCreate", "cuTexRefDestroy", "cuTexRefSetAddress",
    "cuTexRefSetAddress2D", "cuTexRefSetFormat",
    "cuTexRefGetAddress", "cuTexRefGetArray",
    "cuSurfRefCreate", "cuSurfRefDestroy", "cuSurfRefSetFormat",
    "cuArrayCreate", "cuArrayDestroy", "cuArray3DCreate",
    # Graph
    "cuGraphCreate", "cuGraphDestroy", "cuGraphInstantiate",
    "cuGraphInstantiateWithFlags",
    "cuGraphLaunch", "cuGraphUpload",
    "cuGraphAddKernelNode", "cuGraphAddMemcpyNode",
    "cuGraphAddMemsetNode", "cuGraphAddHostNode",
    "cuGraphAddEmptyNode", "cuGraphExecLaunch", "cuGraphExecDestroy",
    "cuGraphExecUpdate", "cuGraphExecKernelNodeSetParams",
    # Linking & Compilation
    "cuLinkAddData", "cuLinkAddFile", "cuLinkComplete",
    "cuLinkDestroy", "cuLinkCreate",
    # Unified Addressing
    "cuMemHostUnregister", "cuMemHostRegister",
    # Occupancy
    "cuOccupancyMaxActiveBlocksPerMultiprocessor",
    "cuOccupancyMaxPotentialBlockSize",
    "cuOccupancyMaxActiveBlocksPerMultiprocessorWithFlags",
    # Profiler
    "cuProfilerStart", "cuProfilerStop",
    "cuProfilerInitialize",
    # Error / Version
    "cuGetErrorName", "cuGetErrorString",
    "cuFuncGetAttribute", "cuFuncSetAttribute",
    "cuFuncSetCacheConfig", "cuFuncGetModule",
    # Misc
    "cuPointerGetAttribute",
]


def main():
    # Dedup while preserving order
    seen = set()
    apis = []
    for a in CUDA_DRIVER_APIS:
        if a not in seen:
            seen.add(a)
            apis.append(a)

    impl_apis = sorted(CRITICAL_APIS_IMPL_REQUIRED.keys())

    lines = []
    lines.append("// Auto-generated by tools/generate_cu_stubs.py")
    lines.append("// DO NOT EDIT MANUALLY.")
    lines.append("// Generated stub declarations for libcuda_taskrunner.so")
    lines.append("// Returns CUDA_ERROR_NOT_IMPLEMENTED for unhandled cu* APIs.")
    lines.append("")
    lines.append("#pragma once")
    lines.append("#include <cuda.h>")
    lines.append("")
    lines.append("#ifdef __cplusplus")
    lines.append('extern "C" {')
    lines.append("#endif")
    lines.append("")

    for fn in apis:
        is_impl = fn in CRITICAL_APIS_IMPL_REQUIRED
        annotation = f"// REAL_IMPL in {CRITICAL_APIS_IMPL_REQUIRED[fn]}" if is_impl else "// STUB"
        lines.append(f"{annotation}")
        lines.append(f"__attribute__((weak, visibility(\"default\")))")
        lines.append(f"CUresult {fn}(void);")
        lines.append("")

    lines.append("#ifdef __cplusplus")
    lines.append("}")
    lines.append("#endif")
    lines.append("")

    output_path = Path(__file__).parent.parent / "src/umd/libcuda_shim/cu_stub_table.inc"
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(lines))

    # Summary
    total = len(apis)
    impl = sum(1 for a in apis if a in CRITICAL_APIS_IMPL_REQUIRED)
    stubs = total - impl
    print(f"Wrote {output_path}")
    print(f"  Total APIs: {total}")
    print(f"  Real implementations (in cu_*.cpp): {impl}")
    print(f"  Stubs (NOT_IMPLEMENTED): {stubs}")
    print()
    print("CRITICAL APIs that MUST be implemented (not stubbed):")
    for a in impl_apis:
        in_list = a in apis
        marker = "OK" if in_list else "MISSING"
        print(f"  [{marker}] {a} -> {CRITICAL_APIS_IMPL_REQUIRED[a]}")


if __name__ == "__main__":
    main()
