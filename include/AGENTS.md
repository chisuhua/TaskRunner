# include/ - Public Headers

C++ headers organized by H-5 3-scope. Headers define the public API surface; implementation is in `src/<scope>/`.

## OVERVIEW

| Subdir | Scope | Files | Notes |
|--------|-------|-------|-------|
| `test_fixture/` | TEST-FIXTURE | 13 | Scheduler + CLI + stub headers |
| `umd/` | UMD-EVOLUTION | 1 | `cuda_runtime_api.hpp` |
| `shared/` | SHARED | 4 | `igpu_driver.hpp`, `sync_primitives.hpp`, `memory_manager.hpp`, `error_handling.hpp` |
| `cuda.h` | UMD-EVOLUTION | 1 | At root (deviation — see note) |

## WHERE TO LOOK

| Task | Header |
|------|--------|
| `TaskRunner` API | `test_fixture/TaskRunner.h` (largest, 489 lines) |
| GPU driver interface | `shared/igpu_driver.hpp` (47 methods per tadr-301, 541 lines) |
| GpuDriverClient (real ioctl) | `test_fixture/gpu_driver_client.h` (901 lines — largest) |
| CudaScheduler (DI) | `test_fixture/cuda_scheduler.hpp` (229 lines) |
| CudaStub (mock) | `test_fixture/cuda_stub.hpp` (231 lines) |
| Runtime API | `umd/cuda_runtime_api.hpp` |
| Sync primitives | `shared/sync_primitives.hpp` (EventQueue, TaskQueue, TaskBuffer, Barrier) |
| Memory manager | `shared/memory_manager.hpp` |
| Result<T> + ErrorCode | `shared/error_handling.hpp` |
| Shim CUDA driver facade | `cuda.h` (at root) |

## CONVENTIONS

- Scheduler headers use `.h` (not `.hpp`) for legacy consistency
- Modern headers (DI, runtime API, shared abstractions) use `.hpp`
- First line: `// SCOPE: <scope>` (uppercase)
- Class declarations live in same-named file (`ClassName.h` or `ClassName.hpp`)
- Forward declarations over includes where possible
- Public API stays minimal — implementation details go in `src/<scope>/`

## ANTI-PATTERNS

- **NEVER** put headers at `include/` root except for legacy/vendor compat (`cuda.h` is the only existing case)
- **NEVER** include a header across scopes without explicit justification (forces scope coupling)
- **NEVER** add a new file to `include/` without matching source in `src/<scope>/`
- **NEVER** use `<cuda.h>` style include — use `"../include/cuda.h"` or proper include path

## KNOWN DEVIATION

`include/cuda.h` is at root (not in `include/umd/`) despite `// SCOPE: UMD-EVOLUTION`. The 16 shim files in `src/umd/libcuda_shim/` include it. Moving it would require updating all 16 includes — out of scope for now. See root AGENTS.md for full deviation list.
