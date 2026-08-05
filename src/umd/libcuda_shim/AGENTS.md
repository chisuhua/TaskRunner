# src/umd/libcuda_shim/ - CUDA Driver Shim (LD_PRELOAD)

16-file LD_PRELOAD shim (`cu_*.cpp`) + 1 auto-generated `cu_stub_table.inc` producing `libcuda_taskrunner.so`. **SCOPE: UMD-EVOLUTION**, built only when `TASKRUNNER_BUILD_MODE=umd-evolution`.

## OVERVIEW

Each `cu_*.cpp` file provides a strong symbol that overrides the weak symbol in the CUDA driver static lib. When a CUDA application is launched with `LD_PRELOAD=libcuda_taskrunner.so`, our shim intercepts all `cu*()` calls and routes them through `CudaRuntimeApi` (which dispatches to the real GPU via `IGpuDriver`).

## FILE INVENTORY

| File | LOC | Provides |
|------|-----|----------|
| `cu_init.cpp` | — | `cuInit()` + `runtime()` lazy accessor (entry point) |
| `cu_device.cpp` | — | `cuDeviceGet*`, `cuDeviceGetCount` |
| `cu_ctx.cpp` | 202 | `cuCtxCreate*`, `cuCtxDestroy`, `cuCtxPushCurrent` |
| `cu_module.cpp` | 272 | `cuModuleLoad*`, `cuModuleGetFunction` |
| `cu_mem.cpp` | 257 | `cuMemAlloc*`, `cuMemFree`, `cuMemcpy*` |
| `cu_mem_pool.cpp` | — | Memory pool ops |
| `cu_stream.cpp` | 188 | `cuStreamCreate*`, `cuStreamDestroy`, `cuStreamSynchronize` |
| `cu_stream_capture.cpp` | — | Stream capture (`cuStreamBeginCapture` etc.) |
| `cu_event.cpp` | — | `cuEventCreate*`, `cuEventRecord`, `cuEventSynchronize` |
| `cu_graph.cpp` | 188 | `cuGraphCreate*`, `cuGraphInstantiate*` |
| `cu_graph_node.cpp` | — | `cuGraphAddNode*` |
| `cu_graph_exec.cpp` | — | `cuGraphLaunch`, `cuGraphExec*` |
| `cu_array.cpp` | 131 | `cuArrayCreate*`, `cuArray3DCreate*` |
| `cu_texref.cpp` | — | Texture reference ops |
| `cu_query.cpp` | — | Query ops (`cuDriverGetVersion` etc.) |
| `cu_launch.cpp` | — | `cuLaunchKernel` |
| `cu_stub_table.inc` | — | Auto-generated stub dispatch table |

## WHERE TO LOOK

| Task | Start here |
|------|------------|
| Add new CUDA driver API | Look up the entry in `cu_stub_table.inc`; add to nearest `cu_*.cpp` (or create new) |
| Understand override pattern | `cu_init.cpp` + `cuda_driver_accessor.hpp` (RAII for `runtime()`) |
| Stream/Event sync | `cu_stream.cpp`, `cu_event.cpp` + `stream_fence_registry.hpp` |
| Async fences | `stream_fence_registry.hpp` (batched fence tracking) |
| Generate stub table | `tools/generate_cu_stubs.py` (regenerates `cu_stub_table.inc`) |

## CONVENTIONS

- One `cu_*.cpp` per CUDA driver domain (mem, stream, event, etc.)
- All public symbols are `extern "C"` (C linkage — they MUST match CUDA driver ABI)
- Each file delegates to `runtime().<api>()` — NEVER call `CudaRuntimeApi` directly
- The `runtime()` accessor in `cuda_driver_accessor.hpp` is a Meyers singleton (lazy init, thread-safe)
- Stream/fence tracking is centralized in `stream_fence_registry.hpp`

## ANTI-PATTERNS

- **NEVER** call `IGpuDriver` methods directly from shim files — go through `CudaRuntimeApi::instance()` (or `runtime()` accessor)
- **NEVER** add C++ exceptions to `extern "C"` functions — they cannot unwind through C frames
- **NEVER** edit `cu_stub_table.inc` by hand — regenerate via `python3 tools/generate_cu_stubs.py`
- **NEVER** introduce a dependency on `src/test_fixture/` from shim — shim is independent
- **NEVER** mark shim file as `// SCOPE: TEST-FIXTURE` — all 17 entries (16 `cu_*.cpp` + 1 auto-generated `.inc`) are UMD-EVOLUTION

## TESTING

- Unit tests: `tests/umd/test_cuda_shim.cpp` (1057 lines — the largest test file)
- E2E standalone: `tests/umd/test_cu_graph_e2e_standalone.cpp`, `tests/umd/test_cuda_e2e_real.cpp`
- Test fixture pattern: each test links against the shim + uses `mock_gpu_driver.hpp` for DI
