#!/usr/bin/env bash
# tools/coverage.sh - Generate code coverage report for TaskRunner
#
# Usage:
#   ./tools/coverage.sh                    # default: test-fixture mode
#   ./tools/coverage.sh --mode=umd-evolution
#   ./tools/coverage.sh --skip-html        # skip genhtml, just capture lcov info
#
# Prerequisites:
#   - GCC-compatible compiler with --coverage support
#   - lcov, genhtml
#
# Output:
#   build/coverage/index.html  — HTML report root
#   build/coverage.info        — Raw lcov tracefile
#
# Author: TaskRunner owner (Sisyphus H-5.1 session)
# Created: 2026-07-03

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build"
COVERAGE_DIR="${BUILD_DIR}/coverage"
TRACEFILE="${BUILD_DIR}/coverage.info"

# ── Parse arguments ───────────────────────────────────────────────────────
BUILD_MODE="test-fixture"
SKIP_HTML=false
for arg in "$@"; do
  case "$arg" in
    --mode=test-fixture)    BUILD_MODE="test-fixture" ;;
    --mode=umd-evolution)   BUILD_MODE="umd-evolution" ;;
    --skip-html)            SKIP_HTML=true ;;
    --help)
      echo "Usage: $0 [--mode=test-fixture|umd-evolution] [--skip-html]"
      exit 0
      ;;
    *)
      echo "Unknown argument: $arg"
      echo "Usage: $0 [--mode=test-fixture|umd-evolution] [--skip-html]"
      exit 1
      ;;
  esac
done

echo "=== TaskRunner Coverage Report ==="
echo "Build mode:      ${BUILD_MODE}"
echo "Build directory: ${BUILD_DIR}"

# ── Step 0: Check prerequisites ──────────────────────────────────────────
if ! command -v lcov &>/dev/null; then
  echo "ERROR: lcov not found. Install: sudo apt-get install lcov"
  exit 1
fi

# ── Step 1: Clean previous coverage data ─────────────────────────────────
echo ""
echo "--- Step 1: Cleaning previous coverage data ---"
rm -rf "${COVERAGE_DIR}" "${TRACEFILE}"
find "${BUILD_DIR}" -name '*.gcda' -o -name '*.gcno' -o -name '*.gcov' \
  2>/dev/null | xargs rm -f 2>/dev/null || true

# ── Step 2: Configure with coverage flags ─────────────────────────────────
echo ""
echo "--- Step 2: Configuring (BUILD_MODE=${BUILD_MODE}) ---"
cmake -B "${BUILD_DIR}" \
  -DCMAKE_CXX_FLAGS="--coverage -g -O0" \
  -DCMAKE_EXE_LINKER_FLAGS="--coverage" \
  -DCMAKE_SHARED_LINKER_FLAGS="--coverage" \
  -DTASKRUNNER_BUILD_MODE="${BUILD_MODE}" \
  -DCMAKE_BUILD_TYPE=Debug

# ── Step 3: Build ─────────────────────────────────────────────────────────
echo ""
echo "--- Step 3: Building ---"
cmake --build "${BUILD_DIR}" -j"$(nproc)"

# ── Step 4: Run all test binaries ─────────────────────────────────────────
echo ""
echo "--- Step 4: Running test binaries ---"

# Find all test executables (skip non-test targets)
TEST_BINS=()
while IFS= read -r -d '' bin; do
  bname="$(basename "$bin")"
  if [[ "$bname" == test_* ]]; then
    TEST_BINS+=("$bin")
  fi
done < <(find "${BUILD_DIR}" -maxdepth 1 -type f -executable -print0 2>/dev/null)

if [ ${#TEST_BINS[@]} -eq 0 ]; then
  echo "WARNING: No test binaries found, falling back to ctest"
  cd "${BUILD_DIR}"
  ctest --output-on-failure -j"$(nproc)" || true
  cd "${REPO_ROOT}"
else
  for bin in "${TEST_BINS[@]}"; do
    bname="$(basename "$bin")"
    echo "  Running: ${bname}"
    if "$bin" > /dev/null 2>&1; then
      echo "    PASS"
    else
      echo "    FAIL (exit code $?) — coverage data still captured"
    fi
  done
fi

# ── Step 5: lcov capture ──────────────────────────────────────────────────
echo ""
echo "--- Step 5: Capturing coverage data ---"

lcov --capture \
     --directory "${BUILD_DIR}" \
     --output-file "${TRACEFILE}" \
     --rc lcov_branch_coverage=1 \
     2>&1 | tail -5

# Remove system/external/doctest headers from tracefile for cleaner report
lcov --remove "${TRACEFILE}" \
     '/usr/*' \
     '*/doctest/*' \
     '*/tests/*' \
     --output-file "${TRACEFILE}" \
     --rc lcov_branch_coverage=1 \
     2>&1 | tail -3

# Summary
echo ""
echo "--- Coverage Summary ---"
lcov --summary "${TRACEFILE}" --rc lcov_branch_coverage=1 2>&1 | tail -10

# ── Step 6: Generate HTML report ──────────────────────────────────────────
if [ "${SKIP_HTML}" = true ]; then
  echo ""
  echo "Skipping HTML generation (--skip-html)"
  echo "Tracefile: ${TRACEFILE}"
else
  echo ""
  echo "--- Step 6: Generating HTML report ---"
  mkdir -p "${COVERAGE_DIR}"
  genhtml "${TRACEFILE}" \
          --output-directory "${COVERAGE_DIR}" \
          --rc lcov_branch_coverage=1 \
          --title "TaskRunner Coverage (${BUILD_MODE})" \
          2>&1 | tail -5

  echo ""
  echo "HTML report: ${COVERAGE_DIR}/index.html"
fi

echo ""
echo "=== Done ==="