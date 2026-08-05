# cmake/ - Build System

3 cmake module files driving the top-level `CMakeLists.txt`. Mode dispatch via `TASKRUNNER_BUILD_MODE` cache var (default `umd-evolution`).

## FILES

| File | LOC | Defines |
|------|-----|---------|
| `Shared.cmake` | 17 | `taskrunner_shared` (always built: memory_manager + sync_primitives) |
| `TestFixture.cmake` | 72 | `taskrunner_test_fixture` + CLI `taskrunner` + 4 test executables |
| `UMDEvolution.cmake` | 136 | Includes TestFixture; adds `taskrunner_umd_stub` + `cuda_taskrunner` (libcuda_taskrunner.so) + 9 test executables |

## WHERE TO LOOK

| Task | File |
|------|------|
| Add a shared library source | `Shared.cmake` |
| Add a test-fixture test executable | `TestFixture.cmake` |
| Add a UMD shim test executable | `UMDEvolution.cmake` |
| Add a new CLI subcommand binary | `TestFixture.cmake` (CLI is built here) |
| Change build mode default | Top-level `CMakeLists.txt` (line 47) |
| Add a sanitizer flag | Top-level `CMakeLists.txt` (SANITIZER_* options) |

## CONVENTIONS

- Mode dispatch in top-level `CMakeLists.txt` includes exactly one of `TestFixture.cmake` or `UMDEvolution.cmake`
- `UMDEvolution.cmake` includes `TestFixture.cmake` first (so do NOT re-include in top-level)
- Each test executable follows pattern:
  ```cmake
  add_executable(test_X test_X.cpp)
  target_link_libraries(test_X PRIVATE doctest_with_main taskrunner_xxx)
  doctest_discover_tests(test_X)
  ```
- `cuda_taskrunner` is the LD_PRELOAD shim — output is `libcuda_taskrunner.so`
- `taskrunner_umd_stub` is a SHARED library wrapping `cuda_runtime_api.cpp` (consumed by tests/CLI)

## ANTI-PATTERNS

- **NEVER** add build logic directly in top-level `CMakeLists.txt` — put it in the appropriate module file
- **NEVER** include `TestFixture.cmake` from `UMDEvolution.cmake` AND top-level (double-include breaks targets)
- **NEVER** set `TASKRUNNER_BUILD_MODE` in cmake files — only via `-D` at configure time
- **NEVER** link `cuda_taskrunner` against `taskrunner_test_fixture` — shim is independent
- **NEVER** introduce a dependency on `build/` (the project is source-relative, not build-relative)

## SANITIZERS (TOP-LEVEL ONLY)

Defined and constrained in top-level `CMakeLists.txt` (lines 11-40):

- `SANITIZER_ADDRESS=ON` — AddressSanitizer (combines with UBSan)
- `SANITIZER_UNDEFINED=ON` — UndefinedBehaviorSanitizer
- `SANITIZER_THREAD=ON` — ThreadSanitizer (**standalone only**, mutually exclusive with ASan)

CI consumes these via `-DSANITIZER_*` flags in `.github/workflows/shim.yml`.
