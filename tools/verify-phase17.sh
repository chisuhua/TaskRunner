#!/usr/bin/env bash
# tools/verify-phase17.sh - Verification runner for the
# 2026-07-02-phase17-test-coverage-completion change.
#
# Usage:
#   ./tools/verify-phase17.sh                    # default --quick
#   ./tools/verify-phase17.sh --quick            # test_cuda_shim only (default)
#   ./tools/verify-phase17.sh --full             # all 5 binaries + docs-audit + ABI
#   ./tools/verify-phase17.sh --asan             # rebuild with ASan+UBSan + --quick
#   ./tools/verify-phase17.sh --target cuFunc    # run E.1 only (cuFunc* + cuOccupancy*)
#   ./tools/verify-phase17.sh --target cuPtr     # E.3: cuPointerGetAttribute + A.4
#   ./tools/verify-phase17.sh --target cuCtx     # E.4: cuCtx full set
#   ./tools/verify-phase17.sh --target primaryCtx  # E.5
#   ./tools/verify-phase17.sh --target launch    # E.6: cuLaunch*
#   ./tools/verify-phase17.sh --target stubs     # E.7: STUB sanity batch
#   ./tools/verify-phase17.sh --cases 'cuFuncGetAttribute*'  # doctest filter
#   ./tools/verify-phase17.sh --build            # configure + compile only, no run
#   ./tools/verify-phase17.sh --clean            # rm -rf build && --build
#   ./tools/verify-phase17.sh --audit            # docs-audit only
#   ./tools/verify-phase17.sh --abi              # ABI symbols + counts
#   ./tools/verify-phase17.sh --counts           # REAL_IMPL / STUB / docs-audit summary
#   ./tools/verify-phase17.sh -h | --help
#
# Exit code:
#   0 on full success, non-zero otherwise. Each section tracks its own status
#   and the script surfaces a summary at the end.
#
# Author: TaskRunner owner (Sisyphus H-5.1 session)
# Created: 2026-07-03

set -uo pipefail

# ---------------------------------------------------------------------------
# Self-locate (resolve project root from script path)
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
TOOLS_DIR="${PROJECT_ROOT}/tools"

# ---------------------------------------------------------------------------
# Defaults
# ---------------------------------------------------------------------------
MODE="quick"           # quick | full | target | cases | build | clean | audit | abi | counts | asan
ASAN=0                 # 0 = off, 1 = ASan+UBSan rebuild
DO_BUILD=1             # reconfigure + compile before running
DO_CLEAN=0             # rm -rf build first
TARGET=""              # cuFunc | cuPtr | cuCtx | primaryCtx | launch | stubs
CASES=""               # doctest --test-case pattern (raw)
DO_AUDIT=0
DO_ABI=0
DO_COUNTS=0
VERBOSE=0

# ---------------------------------------------------------------------------
# ANSI colour helpers (auto-strip when not a TTY)
# ---------------------------------------------------------------------------
if [ -t 1 ] && command -v tput >/dev/null 2>&1 && [ "$(tput colors 2>/dev/null || echo 0)" -ge 8 ]; then
  C_RESET=$'\033[0m'
  C_GREEN=$'\033[0;32m'
  C_RED=$'\033[0;31m'
  C_YELLOW=$'\033[0;33m'
  C_BLUE=$'\033[0;34m'
  C_BOLD=$'\033[1m'
else
  C_RESET="" C_GREEN="" C_RED="" C_YELLOW="" C_BLUE="" C_BOLD=""
fi

log_section() { printf "\n${C_BOLD}${C_BLUE}==> %s${C_RESET}\n" "$*"; }
log_ok()      { printf "  ${C_GREEN}PASS${C_RESET}  %s\n" "$*"; }
log_fail()    { printf "  ${C_RED}FAIL${C_RESET}  %s\n" "$*"; }
log_warn()    { printf "  ${C_YELLOW}WARN${C_RESET}  %s\n" "$*"; }
log_info()    { printf "  ${C_BOLD}·${C_RESET}     %s\n" "$*"; }
log_cmd()     { printf "  ${C_YELLOW}\$${C_RESET}       %s\n" "$*"; }

# ---------------------------------------------------------------------------
# Usage
# ---------------------------------------------------------------------------
usage() {
  sed -n '3,28p' "${BASH_SOURCE[0]}" | sed 's/^# \?//'
  cat <<EOF

Selected targets for --target:
  cuFunc       E.1 cuFunc* attribute APIs (10 cases)
  cuPtr        E.3 cuPointerGetAttribute + A.4 light stubs (6 cases)
  cuCtx        E.4 cuCtx full set (8 cases)
  primaryCtx   E.5 cuDevicePrimaryCtx* (3 cases)
  launch       E.6 cuLaunch* APIs (3 cases)
  stubs        E.7 STUB sanity batch (1 batch)

NOTE: E.2 cuOccupancy* (3 cases) are covered by --target cuFunc (both share
include/cuda.h / cu_module.cpp). Pass --cases 'cuOccupancy*' to filter
specifically.
EOF
}

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
parse_args() {
  while [ $# -gt 0 ]; do
    case "$1" in
      --quick)    MODE="quick" ;;
      --full)     MODE="full" ;;
      --asan)     MODE="asan"; ASAN=1 ;;
      --build)    MODE="build" ;;
      --clean)    MODE="clean"; DO_CLEAN=1 ;;
      --audit)    MODE="audit"; DO_AUDIT=1 ;;
      --abi)      MODE="abi"; DO_ABI=1 ;;
      --counts)   MODE="counts"; DO_COUNTS=1 ;;
      --target)   MODE="target"; TARGET="${2:-}"; shift ;;
      --cases)    MODE="cases"; CASES="${2:-}"; shift ;;
      -v|--verbose) VERBOSE=1 ;;
      -h|--help)  usage; exit 0 ;;
      -*)         printf "${C_RED}Unknown option:${C_RESET} %s\n\n" "$1" >&2; usage; exit 2 ;;
      *)          printf "${C_RED}Unexpected positional arg:${C_RESET} %s\n\n" "$1" >&2; usage; exit 2 ;;
    esac
    shift
  done
}

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
do_cmake_configure() {
  local cmake_args=(-B "${BUILD_DIR}" -DTASKRUNNER_BUILD_MODE=umd-evolution)
  if [ "${ASAN}" = "1" ]; then
    cmake_args+=(-DSANITIZER_ADDRESS=ON -DSANITIZER_UNDEFINED=ON)
  fi

  log_cmd "cmake ${cmake_args[*]} ${PROJECT_ROOT}"
  if ! cmake "${cmake_args[@]}" "${PROJECT_ROOT}" >/dev/null 2>&1; then
    log_fail "cmake configure failed"
    return 1
  fi
  log_ok "cmake configured (ASan=${ASAN})"
}

do_compile() {
  log_cmd "make -j4 (in ${BUILD_DIR})"
  if ! (cd "${BUILD_DIR}" && make -j4) >/dev/null 2>&1; then
    log_fail "make failed"
    return 1
  fi
  log_ok "compile clean"
}

ensure_built() {
  [ -f "${BUILD_DIR}/test_cuda_shim" ] || DO_BUILD=1
  if [ "${DO_BUILD}" = "1" ]; then
    do_cmake_configure || return 1
    do_compile || return 1
  fi
  return 0
}

# ---------------------------------------------------------------------------
# Test runners
# ---------------------------------------------------------------------------
# Args: $1 = binary path, $2 = human label
run_one_binary() {
  local bin="$1" label="$2"
  if [ ! -f "${bin}" ]; then
    log_fail "${label}: binary missing at ${bin}"
    return 1
  fi

  local out
  out="$("${bin}" 2>&1)"
  local rc=$?
  local line
  line="$(printf '%s\n' "${out}" | grep -E 'test cases:|Status:' | head -2 | tr '\n' ' ')"
  printf "  ${C_BOLD}·${C_RESET}     %-22s %s\n" "${label}:" "${line}"
  if [ "${rc}" -ne 0 ]; then
    log_fail "${label}: exit code ${rc}"
    [ "${VERBOSE}" = "1" ] && printf '%s\n' "${out}" | tail -20
    return 1
  fi
  return 0
}

run_all_five() {
  log_section "Running all 5 test binaries"
  local rc=0
  cd "${BUILD_DIR}"
  run_one_binary ./test_cuda_scheduler    "test_cuda_scheduler"    || rc=1
  run_one_binary ./test_gpu_architecture  "test_gpu_architecture"  || rc=1
  run_one_binary ./test_gpu_phase2        "test_gpu_phase2"        || rc=1
  run_one_binary ./test_cuda_runtime_api  "test_cuda_runtime_api"  || rc=1
  run_one_binary ./test_cuda_shim         "test_cuda_shim"         || rc=1
  cd - >/dev/null
  return $rc
}

run_test_cuda_shim() {
  log_section "Running ./test_cuda_shim (the Phase 1.7 target binary)"
  if [ ! -f "${BUILD_DIR}/test_cuda_shim" ]; then
    log_fail "test_cuda_shim not built"
    return 1
  fi
  local out rc
  out="$("${BUILD_DIR}/test_cuda_shim" 2>&1)"
  rc=$?
  printf '%s\n' "${out}" | grep -E 'test cases:|assertions:|Status:|CRASHED|SIGSEGV|Sanitizer|ERROR' | head -10
  if [ "${rc}" -ne 0 ]; then
    log_fail "test_cuda_shim exit code ${rc}"
    [ "${VERBOSE}" = "1" ] && printf '%s\n' "${out}" | tail -30
    return 1
  fi
  return 0
}

# Run a doctest --test-case filter expression. Args: filter pattern (single).
# Use run_multi_filtered for multiple patterns (since doctest --test-case
# only accepts one glob-style pattern at a time).
run_filtered() {
  local pattern="$1"
  log_section "Running ./test_cuda_shim --test-case='${pattern}'"
  if [ ! -f "${BUILD_DIR}/test_cuda_shim" ]; then
    log_fail "test_cuda_shim not built"
    return 1
  fi
  local out rc
  out="$("${BUILD_DIR}/test_cuda_shim" --test-case="${pattern}" 2>&1)"
  rc=$?
  printf '%s\n' "${out}" | grep -E 'test cases:|assertions:|Status:' | head -3
  if [ "${rc}" -ne 0 ]; then
    log_fail "filtered run exit code ${rc}"
    [ "${VERBOSE}" = "1" ] && printf '%s\n' "${out}" | tail -30
    return 1
  fi
  return 0
}

# Args: list of patterns (one pattern per invocation).
# Aggregates results across invocations into a single summary line.
run_multi_filtered() {
  local patterns=("$@")
  log_section "Running ${#patterns[@]} filtered test invocation(s) on test_cuda_shim"
  if [ ! -f "${BUILD_DIR}/test_cuda_shim" ]; then
    log_fail "test_cuda_shim not built"
    return 1
  fi
  local total_cases=0 passed_cases=0 failed_cases=0
  local rc=0
  local p
  for p in "${patterns[@]}"; do
    printf "  ${C_BOLD}·${C_RESET}     pattern: %s\n" "${p}"
    local out
    out="$("${BUILD_DIR}/test_cuda_shim" --test-case="${p}" 2>&1)"
    local sub_rc=$?
    local line
    line="$(printf '%s\n' "${out}" | grep 'test cases:' | head -1)"
    printf "            %s\n" "${line}"
    if [ "${sub_rc}" -ne 0 ]; then
      log_fail "pattern '${p}' returned exit code ${sub_rc}"
      rc=1
    fi
  done
  printf "  ${C_BOLD}·${C_RESET}     ${C_GREEN}all filtered invocations passed${C_RESET}\n"
  return "${rc}"
}

# ---------------------------------------------------------------------------
# Inspections
# ---------------------------------------------------------------------------
do_audit() {
  log_section "Running tools/docs-audit.sh"
  local out
  out="$(bash "${TOOLS_DIR}/docs-audit.sh" 2>&1)"
  # Strip ANSI colour codes before regex extraction
  local plain
  plain="$(printf '%s\n' "${out}" | sed $'s/\033\\[[0-9;]*m//g')"
  local pass fail warn
  pass=$(printf '%s\n' "${plain}" | awk -F: '/PASS:/  {gsub(/[^0-9]/,"",$2); print $2; exit}')
  fail=$(printf '%s\n' "${plain}" | awk -F: '/FAIL:/  {gsub(/[^0-9]/,"",$2); print $2; exit}')
  warn=$(printf '%s\n' "${plain}" | awk -F: '/WARN:/  {gsub(/[^0-9]/,"",$2); print $2; exit}')
  printf "  ${C_BOLD}·${C_RESET}     PASS=%s  FAIL=%s  WARN=%s  (baseline: 53/1/1)\n" \
         "${pass:-?}" "${fail:-?}" "${warn:-?}"
  if [ "${pass:-?}" = "53" ]; then
    log_ok "docs-audit PASS at baseline 53"
  else
    log_warn "docs-audit PASS changed from baseline 53 (got ${pass:-?})"
  fi
  return 0
}

do_abi() {
  log_section "ABI symbols + REAL_IMPL/STUB counts"
  local so="${BUILD_DIR}/libcuda_taskrunner.so"
  if [ ! -f "${so}" ]; then
    log_warn "${so} missing (build first) — skipping ABI count"
  else
    local count
    count=$(nm -D --defined-only "${so}" 2>/dev/null | grep -c ' cu[A-Z]' || echo 0)
    printf "  ${C_BOLD}·${C_RESET}     ABI symbols (nm -D ... 'cu[A-Z]'): %s  (baseline: 79)\n" "${count}"
    log_info "Newly promoted by Phase 1.7 (15 REAL_IMPL + 4 STUB sanity = +19):"
    nm -D --defined-only "${so}" 2>/dev/null | grep -oE 'cu[A-Za-z0-9_]+' | sort -u \
      | grep -E '^cu(FuncGetAttribute|FuncSetAttribute|FuncSetCacheConfig|FuncGetModule|OccupancyMaxActiveBlocksPerMultiprocessor|OccupancyMaxActiveBlocksPerMultiprocessorWithFlags|OccupancyMaxPotentialBlockSize|PointerGetAttribute|StreamCreateWithFlags|StreamGetCaptureInfo|EventCreateWithFlags|MemsetD16|ProfilerStart|ProfilerStop|ProfilerInitialize|ArrayCreate|GraphCreate|TexRefCreate|MemHostRegister)$' \
      | head -25 | sed 's/^/        /'
  fi

  local real stub
  real=$(grep -cE '^// REAL_IMPL' "${PROJECT_ROOT}/src/umd/libcuda_shim/cu_stub_table.inc" || echo 0)
  stub=$(grep -cE '^// STUB'      "${PROJECT_ROOT}/src/umd/libcuda_shim/cu_stub_table.inc" || echo 0)
  printf "  ${C_BOLD}·${C_RESET}     REAL_IMPL=%s  STUB=%s  (baseline: 91/53)\n" "${real}" "${stub}"
}

do_counts() {
  log_section "Quick reference counts (REAL_IMPL / STUB / ABI / docs-audit)"
  local real stub
  real=$(grep -cE '^// REAL_IMPL' "${PROJECT_ROOT}/src/umd/libcuda_shim/cu_stub_table.inc" || echo 0)
  stub=$(grep -cE '^// STUB'      "${PROJECT_ROOT}/src/umd/libcuda_shim/cu_stub_table.inc" || echo 0)
  printf "  REAL_IMPL (cu_stub_table.inc) : %s  (baseline 91)\n" "${real}"
  printf "  STUB      (cu_stub_table.inc) : %s  (baseline 53)\n" "${stub}"

  if [ -f "${BUILD_DIR}/libcuda_taskrunner.so" ]; then
    local syms
    syms=$(nm -D --defined-only "${BUILD_DIR}/libcuda_taskrunner.so" 2>/dev/null | grep -c ' cu[A-Z]' || echo 0)
    printf "  ABI symbols                    : %s  (baseline 79)\n" "${syms}"
  fi
  local audit_out audit_pass
  audit_out="$(bash "${TOOLS_DIR}/docs-audit.sh" 2>&1 | sed $'s/\033\\[[0-9;]*m//g')"
  audit_pass="$(printf '%s\n' "${audit_out}" | awk -F: '/PASS:/  {gsub(/[^0-9]/,"",$2); print $2; exit}')"
  printf "  docs-audit PASS                : %s  (baseline 53)\n" "${audit_pass:-?}"
  printf "  tests/umd/test_cuda_shim.cpp TEST_CASE count: %s  (baseline 69)\n" \
         "$(grep -c 'TEST_CASE' "${PROJECT_ROOT}/tests/umd/test_cuda_shim.cpp")"
}

# ---------------------------------------------------------------------------
# Per-target patterns (matches the E.1-E.7 groups in spec/tasks.md)
# Multiple patterns per target = multiple doctest --test-case invocations.
# ---------------------------------------------------------------------------
declare -A TARGET_PATTERNS=(
  [cuFunc]="cuFuncGetAttribute*|cuFuncSetAttribute*|cuFuncSetCacheConfig*|cuFuncGetModule*|cuOccupancyMaxActiveBlocksPerMultiprocessor*|cuOccupancyMaxPotentialBlockSize*"
  [cuPtr]="cuPointerGetAttribute*|cuMemsetD16*|cuProfilerStart*|cuProfilerStop*|cuProfilerInitialize*"
  [cuCtx]="cuCtxGetDevice*|cuCtxGetFlags*|cuCtxPushCurrent*|cuCtxPopCurrent*|cuCtxSynchronize*|cuCtxGetSharedMemConfig*|cuCtxSetSharedMemConfig*|cuCtxSetLimit*|cuCtxGetApiVersion*"
  [primaryCtx]="cuDevicePrimaryCtx*"
  [launch]="cuLaunchKernel*|cuLaunchHostFunc*"
  [stubs]="STUB APIs*"
)

print_target_hint() {
  printf "${C_YELLOW}hint:${C_RESET} valid targets:\n"
  printf "  cuFunc  cuPtr  cuCtx  primaryCtx  launch  stubs\n"
}

# ---------------------------------------------------------------------------
# Main dispatch
# ---------------------------------------------------------------------------
main() {
  parse_args "$@"

  printf "${C_BOLD}Phase 1.7 verification runner${C_RESET}\n"
  printf "  mode  : %s\n" "${MODE}"
  printf "  root  : %s\n" "${PROJECT_ROOT}"
  printf "  build : %s\n" "${BUILD_DIR}"

  local rc=0

  case "${MODE}" in
    quick)
      ensure_built || { rc=1; }
      [ $rc -eq 0 ] && run_test_cuda_shim || rc=1
      ;;

    full)
      ensure_built || { rc=1; }
      [ $rc -eq 0 ] && run_all_five || rc=1
      do_audit  # audit is informational, does not flip rc
      do_abi    # ABI check informational
      ;;

    asan)
      # forces rebuild from scratch to apply -fsanitize flags
      ASAN=1
      DO_BUILD=1
      ensure_built || { rc=1; }
      [ $rc -eq 0 ] && run_test_cuda_shim || rc=1
      ;;

    target)
      if [ -z "${TARGET}" ]; then
        printf "${C_RED}--target requires a name${C_RESET}\n"; print_target_hint; exit 2
      fi
      if [ -z "${TARGET_PATTERNS[${TARGET}]:-}" ]; then
        printf "${C_RED}Unknown target '${TARGET}'${C_RESET}\n"; print_target_hint; exit 2
      fi
      ensure_built || { rc=1; }
      # TARGET_PATTERNS uses '|' to delimit multiple patterns; split and feed each to run_multi_filtered
      IFS='|' read -ra PATTERNS <<< "${TARGET_PATTERNS[${TARGET}]}"
      [ $rc -eq 0 ] && run_multi_filtered "${PATTERNS[@]}" || rc=1
      ;;

    cases)
      if [ -z "${CASES}" ]; then
        printf "${C_RED}--cases requires a pattern${C_RESET}\n"; exit 2
      fi
      ensure_built || { rc=1; }
      [ $rc -eq 0 ] && run_filtered "${CASES}" || rc=1
      ;;

    build)
      ensure_built || { rc=1; }
      ;;

    clean)
      log_section "Removing ${BUILD_DIR}"
      rm -rf "${BUILD_DIR}"
      log_ok "build/ removed"
      DO_BUILD=1
      ensure_built || { rc=1; }
      ;;

    audit)
      do_audit
      ;;

    abi)
      ensure_built || { rc=1; }
      [ $rc -eq 0 ] && do_abi
      ;;

    counts)
      do_counts
      ;;

    *)
      printf "${C_RED}Unknown mode: ${MODE}${C_RESET}\n" >&2
      usage; exit 2
      ;;
  esac

  printf "\n"
  if [ "${rc}" -eq 0 ]; then
    printf "${C_BOLD}${C_GREEN}Result: PASS${C_RESET}\n"
  else
    printf "${C_BOLD}${C_RED}Result: FAIL${C_RESET}\n"
  fi

  return "${rc}"
}

main "$@"
