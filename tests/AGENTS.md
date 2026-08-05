# tests/ - Doctest Framework

Doctest-based unit + integration tests, organized by H-5 3-scope. `tests/shared/` is a stub (README only, no code).

## OVERVIEW

| Subdir | Scope | Tests | Built when |
|--------|-------|-------|------------|
| `test_fixture/` | TEST-FIXTURE | 4 (+ 1 `.disabled`) | always |
| `umd/` | UMD-EVOLUTION | 10 | only `TASKRUNNER_BUILD_MODE=umd-evolution` |
| `shared/` | SHARED | 0 (README.md only) | — |

## WHERE TO LOOK

| Task | Look in |
|------|---------|
| Scheduler behavior | `test_fixture/test_cuda_scheduler.cpp` (297 lines) |
| GPU architecture/Phase 2 | `test_fixture/test_gpu_architecture.cpp`, `test_gpu_phase2.cpp` |
| KFD E2E bridge | `test_fixture/test_kfd_e2e_bridge.cpp` (real GPU integration) |
| Mock GPU driver | `test_fixture/mock_gpu_driver.hpp` (461 lines — largest test header) |
| Shim symbol behavior | `umd/test_cuda_shim.cpp` (1057 lines — largest test file) |
| CUDA graph API | `umd/test_cu_graph.cpp`, `umd/test_cu_graph_e2e_standalone.cpp` |
| Memory pool | `umd/test_cu_mem_pool.cpp` (477 lines) |
| Stream capture | `umd/test_cu_stream_capture.cpp` |
| Event/timing | `umd/test_event_timing.cpp` |
| Texture/surface | `umd/test_texture_surface.cpp` |
| Shim default init | `umd/test_shim_default_init.cpp` |
| Real GPU E2E | `umd/test_cuda_e2e_real.cpp` (189 lines, needs UsrLinuxEmu) |
| Runtime API | `umd/test_cuda_runtime_api.cpp` |

## CONVENTIONS

- Framework: doctest (`#include <doctest/doctest.h>`)
- Test cases: `TEST_CASE("description")` with `CHECK`/`REQUIRE` macros
- Test fixture pattern: DI inject `MockGpuDriver` for unit tests; use real `GpuDriverClient` for E2E
- Test files in same dir as the unit they test (mirrors `src/<scope>/`)
- Each test executable is a separate target in `cmake/{TestFixture,UMDEvolution}.cmake`
- `test_taskrunner.cpp.disabled` exists but is excluded from build (`.disabled` suffix)

## ANTI-PATTERNS

- **NEVER** call real `ioctl` from unit tests — inject `MockGpuDriver` from `test_fixture/mock_gpu_driver.hpp`
- **NEVER** skip `REQUIRE` for preconditions — `CHECK` is for non-fatal verification
- **NEVER** put umd tests in `tests/test_fixture/` or vice versa
- **NEVER** add a test without matching it to a TADR/test-plan in `openspec/changes/<name>/`
- **NEVER** commit a `*.cpp.disabled` test without a tracking issue (delete or fix instead)

## RUNNING

```bash
# All tests (umd-evolution mode)
cd build && ctest --output-on-failure

# Single test
./build/test_cuda_scheduler -tc=*specific_test_case*

# With sanitizers (rebuild required)
cmake -B build -DSANITIZER_ADDRESS=ON && cmake --build build
```

## SANITIZER COMBINATIONS

Defined in top-level `CMakeLists.txt`. Mutually exclusive except ASan+UBSan:

| Flag | Combines with |
|------|---------------|
| `SANITIZER_ADDRESS=ON` | `SANITIZER_UNDEFINED=ON` |
| `SANITIZER_UNDEFINED=ON` | `SANITIZER_ADDRESS=ON` |
| `SANITIZER_THREAD=ON` | **standalone only** (LLVM limitation) |
