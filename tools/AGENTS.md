# tools/ - Tooling Scripts

4 scripts for docs validation, coverage, shim stub generation, and phase verification.

## OVERVIEW

| Script | Type | Role |
|--------|------|------|
| `docs-audit.sh` | bash (14194 bytes) | H-5 3-scope structure validator — checks SCOPE annotations, ADR headers, TADR numbering, cross-repo mirror |
| `coverage.sh` | bash (5369 bytes) | Test coverage report generator (gcov-based) |
| `generate_cu_stubs.py` | python (13013 bytes) | Regenerates `src/umd/libcuda_shim/cu_stub_table.inc` from CUDA driver headers |
| `verify-phase17.sh` | bash (16856 bytes) | Phase 1.7 milestone verifier (e2e + architecture validation) |

## WHERE TO LOOK

| Task | Tool |
|------|------|
| Pre-commit docs check | `docs-audit.sh` (called from `.githooks/pre-commit`) |
| Generate test coverage | `coverage.sh` (produces HTML report in `build/coverage/`) |
| Update shim stub table | `python3 tools/generate_cu_stubs.py` (regenerates `.inc`) |
| Verify Phase 1.7 milestone | `bash tools/verify-phase17.sh` (runs e2e + arch tests) |

## CONVENTIONS

- Scripts are executable (`chmod +x`); `*.sh` is bash, `*.py` is python3
- `docs-audit.sh` is the source of truth for H-5 structure (if it's a pass, structure is valid)
- `generate_cu_stubs.py` MUST be re-run when adding a new `cu_*.cpp` file
- `verify-phase17.sh` exits non-zero on any failure (CI gates on this)

## ANTI-PATTERNS

- **NEVER** edit `cu_stub_table.inc` by hand — always regenerate via `generate_cu_stubs.py`
- **NEVER** skip `docs-audit.sh` in pre-commit hook (it catches scope drift)
- **NEVER** add a tool that depends on `build/` artifacts (must work from source tree)
- **NEVER** introduce Python dependencies in `generate_cu_stubs.py` without checking Python 3.11+ is available
- **NEVER** delete a tool without checking the pre-commit hook + CI workflow first

## PRE-COMMIT INTEGRATION

The `.githooks/pre-commit` hook (when installed via `scripts/install-hooks.sh`) calls:
1. `bash tools/docs-audit.sh` — docs validation
2. `clang-format` on staged `.cpp`/`.hpp`/`.h` files

Install with: `git config core.hooksPath .githooks` (or use `scripts/install-hooks.sh`).
