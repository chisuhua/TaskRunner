# src/test_fixture/ - Scheduler + CLI + Stub

The default-shippable scope. Compiles in both `test-fixture` and `umd-evolution` build modes.

## OVERVIEW

Three sub-systems: (1) **scheduler** — singleton `TaskRunner` + `CmdProcessor` workers; (2) **CLI** — `cmd_buffer_v2_main()` dispatcher; (3) **stub** — pure-CPU `CudaStub` for testing without GPU.

## FILES

| File | LOC | Role |
|------|-----|------|
| `TaskRunner.cpp` | 8 | Singleton definition (header-only logic) |
| `CmdProcessor.cpp` | 195 | Worker thread, event loop, work stealing |
| `cmd_buffer_v2.cpp` | 210 | V2 cmd buffer with ordered/unordered |
| `cli_main.cpp` | 60 | `main()` entry, dispatches to `cmd_buffer_v2_main()` |
| `cmd_cuda.cpp` | 580 | CLI subcommands (`cuda_alloc`, `cuda_memcpy`, `cuda_va_space`, `cuda_queue`) |
| `cuda_scheduler.cpp` | 401 | DI-injected scheduler; uses `IGpuDriver` |
| `cuda_stub.cpp` | 507 | Pure-CPU stub (no ioctl) |
| `gpu_driver_client.cpp` | 33 | `GpuDriverClient` impl (only consumer of `GPU_IOCTL_*`) |

## WHERE TO LOOK

| Task | Start here |
|------|------------|
| Add CLI subcommand | `cmd_cuda.cpp` (add `cmd_*` function + register in `cmd_buffer_v2_main`) |
| Add new scheduler primitive | Header in `include/test_fixture/`, impl in same-name `.cpp` |
| Test scheduler w/o GPU | Use `CudaStub` via DI; see `tests/test_fixture/test_cuda_scheduler.cpp` |
| Wire real GPU | Use `GpuDriverClient` via DI; see `tests/test_fixture/test_kfd_e2e_bridge.cpp` |
| Mock driver for unit tests | `tests/test_fixture/mock_gpu_driver.hpp` (in `IGpuDriver` interface) |

## CONVENTIONS

- Headers: `include/test_fixture/ClassName.h` (PascalCase.h, not .hpp)
- One class per file unless tightly coupled (e.g., `EventQueue` + `TaskQueue` work-stealing pair)
- `gpu_driver_client.cpp` MUST NOT call `ioctl` directly — wrap via `GpuDriverClient` methods
- All CLI commands return `int` (exit code), print to `stdout`/`stderr`
- Barrier types: RELEASE / ACQUIRE / WAIT / GROUP (see `include/test_fixture/Barrier.h`)

## ANTI-PATTERNS

- **NEVER** call `ioctl` from `cuda_scheduler.cpp` — go through `GpuDriverClient` or DI mock
- **NEVER** instantiate `GpuDriverClient` directly in tests — inject `MockGpuDriver` from `tests/test_fixture/mock_gpu_driver.hpp`
- **NEVER** add new CLI command without updating `cmd_cuda.h` (header is the registry)
- **NEVER** use `CUDA_IOCTL_*` — use `GPU_IOCTL_*` only
