#!/usr/bin/env bash
# docs-audit.sh - Validate TaskRunner documentation against H-5 3-scope structure
#
# Adapted for H-5 (2026-06-25): TaskRunner documents are organized into 3 scopes:
#   - test-fixture (1xx) - 当前 shippable 主线
#   - umd-evolution (2xx) - 实验性 UMD 愿景
#   - shared (107 + 3xx) - 跨切面契约
#
# Each scope owns its own docs/{scope}/ sub-tree:
#   docs/test-fixture/{adr,architecture,roadmap,archive,research}/
#   docs/umd-evolution/{adr,architecture,roadmap,archive,research}/
#   docs/shared/{adr,research}/
#
# TADR numbering is split across scopes:
#   1xx - test-fixture TADR
#   2xx - umd-evolution TADR
#   3xx - shared TADR
#   107/108 - shared TADR (boundary + build-mode)
#
# Exit codes:
#   0 - All checks passed (or only warnings in non-strict mode)
#   1 - One or more failures
#
# Author: TaskRunner owner (Sisyphus H-5.1 session)
# Established: 2026-06-25 (H-5.1 scope clarification cleanup)

set -euo pipefail

# ---------------------------------------------------------------------------
# Globals
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
USRLINUXEMU_ROOT="$(realpath "${REPO_ROOT}/../..")"

cd "${REPO_ROOT}"

# Output helpers
PASS=0
FAIL=0
WARN=0
EXIT_CODE=0

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

ok()   { echo -e "${GREEN}✓${NC} $1"; PASS=$((PASS+1)); }
bad()  { echo -e "${RED}✗${NC} $1"; FAIL=$((FAIL+1)); EXIT_CODE=1; }
warn() { echo -e "${YELLOW}⚠${NC} $1"; WARN=$((WARN+1)); }

# Expected 3 scopes (H-5)
SCOPES=("test-fixture" "umd-evolution" "shared")

# Required sub-dirs per scope
SCOPE_SUBDIRS_DEFAULT=("adr")
SCOPE_SUBDIRS_TEST_FIXTURE=("adr" "architecture" "roadmap" "archive")
SCOPE_SUBDIRS_UMD_EVOLUTION=("adr" "architecture" "roadmap" "archive")
SCOPE_SUBDIRS_SHARED=("adr")

# Redirect files: TADR-NNN-redirect.md must have REDIRECT_TO + STATUS: DEPRECATED
# Active TADR files must have SCOPE + STATUS frontmatter

# ---------------------------------------------------------------------------
# Check 1: 3 scope directories exist
# ---------------------------------------------------------------------------
echo "=== 1. Scope Directories ==="
for scope in "${SCOPES[@]}"; do
    if [ -d "docs/$scope" ]; then
        ok "docs/$scope/ exists"
    else
        bad "docs/$scope/ missing"
    fi
done

# Check sub-dirs per scope
echo ""
echo "=== 2. Scope Sub-directories ==="
# adr/ and architecture/ are required for all scopes
for scope in test-fixture umd-evolution; do
    if [ -d "docs/$scope" ]; then
        for sub in adr architecture; do
            if [ -d "docs/$scope/$sub" ]; then
                ok "docs/$scope/$sub/ exists"
            else
                bad "docs/$scope/$sub/ missing"
            fi
        done
        # roadmap/ is required for active scopes (test-fixture)
        # umd-evolution may have roadmap/ but it's optional for PROPOSED-only scopes
        for sub in roadmap; do
            if [ -d "docs/$scope/$sub" ]; then
                ok "docs/$scope/$sub/ exists"
            else
                warn "docs/$scope/$sub/ missing (optional for PROPOSED-only scope)"
            fi
        done
        # archive/ only required for active scopes with deprecated content
        for sub in archive; do
            if [ -d "docs/$scope/$sub" ]; then
                ok "docs/$scope/$sub/ exists"
            else
                warn "docs/$scope/$sub/ missing (no deprecated content yet)"
            fi
        done
    fi
done

if [ -d "docs/shared" ]; then
    if [ -d "docs/shared/adr" ]; then
        ok "docs/shared/adr/ exists"
    else
        bad "docs/shared/adr/ missing"
    fi
fi

# ---------------------------------------------------------------------------
# Check 3: TADR file naming + frontmatter
# ---------------------------------------------------------------------------
echo ""
echo "=== 3. TADR File Integrity ==="

# Validate TADR file naming: tadr-NNN-*.md
# - Redirect files (*-redirect.md): must have REDIRECT_TO + STATUS: DEPRECATED
# - Active files: must have SCOPE: <scope> + STATUS: <status>

for scope in "${SCOPES[@]}"; do
    if [ ! -d "docs/$scope/adr" ]; then
        continue
    fi

    # Expected scope tag for frontmatter validation
    local_scope_upper="$(echo "$scope" | tr '[:lower:]' '[:upper:]' | tr '-' '_')"

    for tadr_file in "docs/$scope/adr/"tadr-*.md; do
        [ -f "$tadr_file" ] || continue

        # Skip template
        if [[ "$(basename "$tadr_file")" == "tadr-000-template.md" ]]; then
            ok "$tadr_file (template, skipped)"
            continue
        fi

        # Validate filename matches tadr-NNN-* pattern
        basename="$(basename "$tadr_file")"
        if [[ ! "$basename" =~ ^tadr-[0-9]{3}-[a-z0-9-]+\.md$ ]]; then
            bad "$tadr_file: filename does not match tadr-NNN-slug.md pattern"
            continue
        fi

        # Check if redirect file
        if [[ "$tadr_file" == *-redirect.md ]]; then
            # Redirect: must have REDIRECT_TO + STATUS: DEPRECATED
            if grep -q "^STATUS: DEPRECATED" "$tadr_file"; then
                if grep -q "^REDIRECT_TO:" "$tadr_file"; then
                    ok "$tadr_file (redirect format OK)"
                else
                    bad "$tadr_file: missing REDIRECT_TO: field"
                fi
            else
                bad "$tadr_file: redirect file missing STATUS: DEPRECATED"
            fi
        else
            # Active TADR: must have SCOPE + STATUS frontmatter
            head_block="$(head -10 "$tadr_file")"
            if echo "$head_block" | grep -q "^SCOPE:"; then
                if echo "$head_block" | grep -q "^STATUS:"; then
                    ok "$tadr_file (frontmatter OK)"
                else
                    bad "$tadr_file: missing STATUS: in frontmatter"
                fi
            else
                bad "$tadr_file: missing SCOPE: in frontmatter"
            fi
        fi
    done
done

# ---------------------------------------------------------------------------
# Check 4: AGENTS.md §Scope Classification section exists
# ---------------------------------------------------------------------------
echo ""
echo "=== 4. AGENTS.md Scope Classification Section ==="

if [ -f "AGENTS.md" ]; then
    if grep -qE "^## Scope Classification" AGENTS.md; then
        ok "AGENTS.md has §Scope Classification section"
    else
        bad "AGENTS.md missing §Scope Classification section"
    fi

    if grep -qE "^### Required Metadata" AGENTS.md; then
        ok "AGENTS.md has §Required Metadata section"
    else
        warn "AGENTS.md missing §Required Metadata section"
    fi

    if grep -qE "^### Build Mode Selection" AGENTS.md; then
        ok "AGENTS.md has §Build Mode Selection section"
    else
        warn "AGENTS.md missing §Build Mode Selection section"
    fi
else
    bad "AGENTS.md not found at repo root"
fi

# ---------------------------------------------------------------------------
# Check 5: docs/shared/adr/README.md is canonical TADR index
# ---------------------------------------------------------------------------
echo ""
echo "=== 5. Canonical TADR Index ==="

if [ -f "docs/shared/adr/README.md" ]; then
    ok "docs/shared/adr/README.md exists"
    # Should reference all 3 scopes
    if grep -q "test-fixture" "docs/shared/adr/README.md"; then
        ok "  README references test-fixture scope"
    else
        bad "  README missing test-fixture scope reference"
    fi
    if grep -q "umd-evolution" "docs/shared/adr/README.md"; then
        ok "  README references umd-evolution scope"
    else
        bad "  README missing umd-evolution scope reference"
    fi
    if grep -q "shared" "docs/shared/adr/README.md"; then
        ok "  README references shared scope"
    else
        bad "  README missing shared scope reference"
    fi
else
    bad "docs/shared/adr/README.md missing"
fi

# ---------------------------------------------------------------------------
# Check 6: UsrLinuxEmu mirror sync
# ---------------------------------------------------------------------------
echo ""
echo "=== 6. UsrLinuxEmu Mirror Sync ==="

MIRROR="${USRLINUXEMU_ROOT}/docs/00_adr/README.md"

if [ -f "$MIRROR" ]; then
    ok "UsrLinuxEmu mirror exists at $MIRROR"

    # Check mirror has TaskRunner TADR mirror section
    if grep -q "TaskRunner TADR" "$MIRROR"; then
        ok "  mirror has 'TaskRunner TADR mirror' section"
    else
        bad "  mirror missing 'TaskRunner TADR mirror' section"
    fi

    # Check expected TADR numbers are present in mirror
    expected_tadrs=(101 102 103 104 105 106 109 201 202 203 204 205 107 108 301 302 303 304)
    missing_in_mirror=0
    for tadr_num in "${expected_tadrs[@]}"; do
        if ! grep -q "tadr-$tadr_num" "$MIRROR"; then
            warn "  mirror missing tadr-$tadr_num"
            missing_in_mirror=$((missing_in_mirror+1))
        fi
    done
    if [ $missing_in_mirror -eq 0 ]; then
        ok "  mirror references all ${#expected_tadrs[@]} expected TADR numbers"
    fi
else
    warn "UsrLinuxEmu mirror not found at $MIRROR (skipped)"
fi

# ---------------------------------------------------------------------------
# Check 7: Source code SCOPE annotations (// SCOPE: <scope>)
# ---------------------------------------------------------------------------
echo ""
echo "=== 7. Source Code SCOPE Annotations ==="

# Check src/ subdirs
for scope_dir in src/test_fixture src/umd src/shared; do
    if [ -d "$scope_dir" ]; then
        n_files=$(find "$scope_dir" -name '*.cpp' -o -name '*.h' -o -name '*.hpp' 2>/dev/null | wc -l)
        n_with_scope=$(grep -rl "^// SCOPE:" "$scope_dir" 2>/dev/null | wc -l)
        if [ $n_files -gt 0 ]; then
            if [ $n_with_scope -eq $n_files ]; then
                ok "$scope_dir: $n_files files, all have // SCOPE: annotation"
            elif [ $n_with_scope -eq 0 ]; then
                warn "$scope_dir: $n_files files, none have // SCOPE: annotation (H-5 future)"
            else
                warn "$scope_dir: $n_files files, only $n_with_scope have // SCOPE: annotation"
            fi
        fi
    fi
done

# ---------------------------------------------------------------------------
# Check 8: No tadr-00X legacy references (should be all remapped to 1xx/2xx)
# ---------------------------------------------------------------------------
echo ""
echo "=== 8. No Legacy tadr-00X References ==="

legacy_count=0
while IFS= read -r f; do
    [ -z "$f" ] && continue
    # Skip untracked research files (intentionally not yet in git)
    if [[ "$f" == *"/research/"* ]]; then
        continue
    fi
    if grep -qE "tadr-00[1-8]-" "$f" 2>/dev/null; then
        # Skip redirect files themselves (they reference old names deliberately)
        if [[ "$(basename "$f")" == *-redirect.md ]]; then
            continue
        fi
        bad "Legacy tadr-00X reference in: $f"
        legacy_count=$((legacy_count+1))
    fi
done < <(find docs include src tests \( -name '*.md' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) 2>/dev/null)

if [ $legacy_count -eq 0 ]; then
    ok "No legacy tadr-00X references found (all remapped)"
fi

# ---------------------------------------------------------------------------
# Check 9: Phase 2 Shim Completeness
# ---------------------------------------------------------------------------
echo ""
echo "=== 9. Phase 2 Shim Completeness ==="

SHIM="${REPO_ROOT}/build/libcuda_taskrunner.so"
if [ ! -f "$SHIM" ]; then
  warn "libcuda_taskrunner.so not built at $SHIM (skipping Phase 2 checks)"
else
  # Critical cu* APIs (from CRITICAL_APIS_IMPL_REQUIRED in tools/generate_cu_stubs.py)
  CRITICAL_APIS=(
    cuInit cuDriverGetVersion
    cuDeviceGetCount cuDeviceGet cuDeviceGetName cuDeviceGetAttribute cuDeviceTotalMem
    cuCtxCreate cuCtxDestroy cuCtxSetCurrent cuCtxGetCurrent
    cuCtxPushCurrent cuCtxPopCurrent cuCtxSynchronize
    cuCtxGetDevice cuCtxGetApiVersion cuCtxGetFlags
    cuDevicePrimaryCtxRetain cuDevicePrimaryCtxRelease cuDevicePrimaryCtxReset
    cuModuleLoad cuModuleUnload cuModuleGetFunction cuModuleGetGlobal
    cuMemAlloc cuMemFree cuMemcpyHtoD cuMemcpyDtoH
    cuMemcpyDtoD cuMemcpy cuMemcpyAsync
    cuMemsetD32 cuMemsetD8 cuMemAllocHost cuMemFreeHost
    cuLaunchKernel
  )

  MISSING=()
  for api in "${CRITICAL_APIS[@]}"; do
    if ! nm -D --defined-only "$SHIM" 2>/dev/null | grep -qw "$api"; then
      MISSING+=("$api")
    fi
  done

  EXPORTED=$(nm -D --defined-only "$SHIM" 2>/dev/null | grep -c "cu[A-Z]" || echo 0)

  echo "Total cu* symbols exported: $EXPORTED (min: 30)"
  if [ "$EXPORTED" -lt 30 ]; then
    bad "Phase 2 shim: only $EXPORTED cu* symbols (need ≥30)"
  else
    ok "Phase 2 shim exports $EXPORTED cu* symbols (≥30)"
  fi

  echo "Critical APIs checked: ${#CRITICAL_APIS[@]}"
  if [ ${#MISSING[@]} -gt 0 ]; then
    bad "Phase 2 shim: ${#MISSING[@]} critical cu* APIs missing: $(printf '%s ' "${MISSING[@]}")"
  else
    ok "Phase 2 shim: all ${#CRITICAL_APIS[@]} critical cu* APIs present"
  fi
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo ""
echo "============================================"
echo "  Summary"
echo "============================================"
echo -e "  ${GREEN}PASS${NC}:   $PASS"
echo -e "  ${RED}FAIL${NC}:   $FAIL"
echo -e "  ${YELLOW}WARN${NC}:   $WARN"
echo ""
if [ $FAIL -eq 0 ]; then
    echo -e "  ${GREEN}Result: PASS${NC}"
    echo "============================================"
    exit 0
else
    echo -e "  ${RED}Result: FAIL${NC}"
    echo "============================================"
    exit 1
fi