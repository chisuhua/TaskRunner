# src/ - Source Layout

C++ source organized by H-5 3-scope. **Every `.cpp`/`.hpp`/`.h` MUST have `// SCOPE: <scope>` as first line** (verified by `tools/docs-audit.sh`).

## OVERVIEW

3 subdirectories map 1:1 to build scopes. Default build (umd-evolution) compiles all 3; test-fixture opt-out skips `src/umd/libcuda_shim/`.

## STRUCTURE

| Dir | Scope | Status | Compiled when |
|---|---|---|---|
| `test_fixture/` | TEST-FIXTURE | ACCEPTED | always |
| `umd/` | UMD-EVOLUTION | PROPOSED | always (CLI), only `libcuda_shim/` in umd-evolution mode |
| `shared/` | SHARED | ACCEPTED | always |

## WHERE TO LOOK

| Task | File |
|------|------|
| Scheduler core | `test_fixture/TaskRunner.cpp`, `CmdProcessor.cpp` |
| CLI entry | `test_fixture/cli_main.cpp` (line 33: `main()` → `cmd_buffer_v2_main()`) |
| CLI commands | `test_fixture/cmd_cuda.cpp` (largest, 580 lines) |
| GPU ioctl wrapper | `test_fixture/gpu_driver_client.cpp` (only consumer of `GPU_IOCTL_*`) |
| CUDA scheduler | `test_fixture/cuda_scheduler.cpp` (DI-injected) |
| Stub mode | `test_fixture/cuda_stub.cpp` (pure-CPU) |
| V2 cmd buffer | `test_fixture/cmd_buffer_v2.cpp` |
| Runtime API | `umd/cuda_runtime_api.cpp` (links into both CLI and shim) |
| Shim (LD_PRELOAD) | `umd/libcuda_shim/cu_*.cpp` (16 files + 1 auto-generated `cu_stub_table.inc`, weak→strong override) |
| Memory manager | `shared/memory_manager.cpp` |
| Sync primitives | `shared/sync_primitives.cpp` |

## CONVENTIONS

- First line of every file: `// SCOPE: <scope>` (uppercase value)
- File naming: `ClassName.cpp` (PascalCase for class impls)
- Each subdir has its own AGENTS.md with scope-specific guidance
- `src/umd/libcuda_shim/cu_stub_table.inc` is the only file without SCOPE line (auto-generated)

## ANTI-PATTERNS

- **NEVER** place files at `src/` root — pick a scope subdir
- **NEVER** use `CUDA_IOCTL_*` (magic='C') — use `GPU_IOCTL_*` (magic='G')
- **NEVER** skip the `// SCOPE:` first line — pre-commit hook blocks this
- **NEVER** reach across scopes without explicit cross-reference note in commit message
