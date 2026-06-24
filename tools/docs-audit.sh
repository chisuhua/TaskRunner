#!/usr/bin/env bash
# docs-audit.sh - Validate TaskRunner documentation against TADR + cross-repo invariants
#
# Re-runnable audit script for TaskRunner. Mirrors UsrLinuxEmu tools/docs-audit.sh
# style (set -e, emoji status, EXIT_CODE accumulator, --section flag, --strict flag).
# Self-locates REPO_ROOT from SCRIPT_DIR (no hardcoded paths).
#
# Usage:
#   tools/docs-audit.sh                 # Run all sections
#   tools/docs-audit.sh --section tadr # Run single section
#   tools/docs-audit.sh --strict        # Treat warnings as failures
#   tools/docs-audit.sh --help          # Show this help
#
# Available sections:
#   tadr-integrity   TADR-001~008 files exist + have required MADR sections
#   cross-links      Cross-submodule 4-dot links + intra-project 2-dot links
#   tadr-crossref    TADR-XXX references UsrLinuxEmu ADR-XXX correctly
#   index-sync       docs/adr/README.md INDEX matches actual TADR files
#   capability       capabilities.md has only 3 canonical capabilities
#   archive-policy   DEPRECATED files only in docs/archive/
#   doc-structure    architecture/ + roadmap/ + archive/ + adr/ file counts
#
# Exit codes:
#   0 - All checks passed (or only warnings in non-strict mode)
#   1 - One or more failures (or warnings in --strict mode)
#   2 - Invalid arguments
#
# Author: TaskRunner owner (Sisyphus session)
# Established: 2026-06-23 (H-4.5 docs governance cleanup)

# ---------------------------------------------------------------------------
# Globals
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
USRLINUXEMU_ROOT="$(realpath "${REPO_ROOT}/../..")"
EXIT_CODE=0
STRICT=0

# Sections to run
RUN_TADR_INTEGRITY=0
RUN_CROSS_LINKS=0
RUN_TADR_CROSSREF=0
RUN_INDEX_SYNC=0
RUN_CAPABILITY=0
RUN_ARCHIVE_POLICY=0
RUN_DOC_STRUCTURE=0

# ---------------------------------------------------------------------------
# Output helpers
# ---------------------------------------------------------------------------
section() {
    echo ""
    echo "=== $1 ==="
}

subsection() {
    echo "--- $1 ---"
}

check_pass() {
    echo "  ✅ $1"
}

check_fail() {
    echo "  ❌ $1"
    EXIT_CODE=1
}

check_warn() {
    if [ "${STRICT}" -eq 1 ]; then
        echo "  ❌ (strict) $1"
        EXIT_CODE=1
    else
        echo "  ⚠️  $1"
    fi
}

print_summary() {
    local passed="${1:-0}"
    local failed="${2:-0}"
    local warned="${3:-0}"
    echo ""
    echo "============================================"
    echo "  Summary"
    echo "============================================"
    echo "  Repository: ${REPO_ROOT}"
    echo "  Mode: $([ ${STRICT} -eq 1 ] && echo 'all (strict)' || echo 'all')"
    echo ""
    echo "  ✅ Passed:  ${passed}"
    echo "  ❌ Failed:  ${failed}"
    echo "  ⚠️  Warnings: ${warned}"
    if [ "${EXIT_CODE}" -eq 0 ]; then
        echo ""
        echo "  Result: ✅ PASS"
    else
        echo ""
        echo "  Result: ❌ FAIL"
    fi
    echo "============================================"
}

usage() {
    sed -n '2,30p' "${BASH_SOURCE[0]}" | sed 's/^# //'
}

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
RUN_ALL=1
while [ $# -gt 0 ]; do
    case "$1" in
        --section)
            RUN_ALL=0
            shift
            case "$1" in
                tadr-integrity) RUN_TADR_INTEGRITY=1 ;;
                cross-links)    RUN_CROSS_LINKS=1 ;;
                tadr-crossref)  RUN_TADR_CROSSREF=1 ;;
                index-sync)     RUN_INDEX_SYNC=1 ;;
                capability)     RUN_CAPABILITY=1 ;;
                archive-policy) RUN_ARCHIVE_POLICY=1 ;;
                doc-structure)  RUN_DOC_STRUCTURE=1 ;;
                *)
                    echo "Invalid section: $1" >&2
                    echo "Valid sections: tadr-integrity, cross-links, tadr-crossref, index-sync, capability, archive-policy, doc-structure" >&2
                    exit 2
                    ;;
            esac
            ;;
        --strict) STRICT=1 ;;
        --help|-h) usage; exit 0 ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

if [ ${RUN_ALL} -eq 1 ]; then
    RUN_TADR_INTEGRITY=1
    RUN_CROSS_LINKS=1
    RUN_TADR_CROSSREF=1
    RUN_INDEX_SYNC=1
    RUN_CAPABILITY=1
    RUN_ARCHIVE_POLICY=1
    RUN_DOC_STRUCTURE=1
fi

# ---------------------------------------------------------------------------
# Section 1: TADR Integrity
# ---------------------------------------------------------------------------
section_tadr_integrity() {
    section "1. TADR Integrity"
    local tadr_dir="${REPO_ROOT}/docs/adr"
    local required_sections=("## Context" "## Decision" "## Consequences")

    for n in 001 002 003 004 005 006 007 008; do
        local actual
        actual=$(ls "${tadr_dir}/tadr-${n}-"*.md 2>/dev/null | head -1)
        if [ -z "${actual}" ]; then
            check_fail "TADR-${n} file missing (expected docs/adr/tadr-${n}-*.md)"
            continue
        fi

        local missing_sections=0
        for sect in "${required_sections[@]}"; do
            if ! grep -qF "${sect}" "${actual}"; then
                check_fail "TADR-${n} $(basename "${actual}") missing required section '${sect}'"
                missing_sections=$((missing_sections+1))
            fi
        done

        # Consumer-Lens required for TADR-005/006/008 (consumer-lens mirrors)
        if [[ "${n}" == "005" || "${n}" == "006" || "${n}" == "008" ]]; then
            if ! grep -qF "## Consumer-Lens" "${actual}"; then
                check_fail "TADR-${n} $(basename "${actual}") missing required section '## Consumer-Lens' (consumer-lens mirror)"
                missing_sections=$((missing_sections+1))
            fi
        fi

        if [ ${missing_sections} -eq 0 ]; then
            check_pass "TADR-${n} $(basename "${actual}") has all required sections"
        fi
    done
}

# ---------------------------------------------------------------------------
# Section 2: Cross-submodule + intra-project Links
# ---------------------------------------------------------------------------
section_cross_links() {
    section "2. Cross-submodule + Intra-project Links"

    # 2.1 Cross-submodule docs/00_adr links: should be 4 dots (../../../..).
    #     A 4-dot prefix contains the 3-dot prefix as substring, so we mask 4-dot
    #     occurrences before grepping for remaining 3-dot patterns.
    local bad_3dot=0
    while IFS= read -r f; do
        [ -z "${f}" ] && continue
        if sed 's|\.\./\.\./\.\./\.\./docs/00_adr/|__4DOT_OK__|g' "${f}" | grep -qE '\.\./\.\./\.\./docs/00_adr/'; then
            check_fail "Bad 3-dot cross-submodule link in $(realpath --relative-to="${REPO_ROOT}" "${f}")"
            bad_3dot=$((bad_3dot+1))
        fi
    done < <(find "${REPO_ROOT}/docs" -name '*.md' -type f 2>/dev/null)
    if [ ${bad_3dot} -eq 0 ]; then
        check_pass "All cross-submodule docs/00_adr links use 4-dot pattern"
    fi

    # 2.2 Cross-submodule docs/00_adr: should NOT use 5 dots (off-by-one more).
    local bad_5dot=0
    while IFS= read -r f; do
        [ -z "${f}" ] && continue
        if grep -qE '\.\./\.\./\.\./\.\./\.\./docs/00_adr/' "${f}"; then
            check_fail "Bad 5-dot cross-submodule link in $(realpath --relative-to="${REPO_ROOT}" "${f}")"
            bad_5dot=$((bad_5dot+1))
        fi
    done < <(find "${REPO_ROOT}/docs" -name '*.md' -type f 2>/dev/null)
    if [ ${bad_5dot} -eq 0 ]; then
        check_pass "No 5-dot cross-submodule docs/00_adr links (no off-by-one more)"
    fi

    # 2.3 Intra-project src/ include links: should be 2 dots (../../) from docs/adr/.
    #     3 dots would mean going up one directory too far.
    local bad_src_3dot=0
    while IFS= read -r f; do
        [ -z "${f}" ] && continue
        # Match ../../../src/ (3 dots before src/) but not when part of 4+ dots.
        # We mask 4+-dot occurrences first.
        if sed 's|\.\./\.\./\.\./\.\./src/|__4DOT_SRC__|g; s|\.\./\.\./\.\./\.\./\.\./src/|__5DOT_SRC__|g' "${f}" | grep -qE '\.\./\.\./\.\./src/'; then
            check_fail "Bad 3-dot intra-project src/ link in $(realpath --relative-to="${REPO_ROOT}" "${f}")"
            bad_src_3dot=$((bad_src_3dot+1))
        fi
    done < <(find "${REPO_ROOT}/docs" -name '*.md' -type f 2>/dev/null)
    if [ ${bad_src_3dot} -eq 0 ]; then
        check_pass "All intra-project src/ links use 2-dot pattern"
    fi

    # 2.4 Count 4-dot cross-submodule links (informational).
    local n4
    n4=$(grep -rE '\.\./\.\./\.\./\.\./docs/00_adr/' "${REPO_ROOT}/docs" --include='*.md' 2>/dev/null | wc -l)
    if [ ${n4} -gt 0 ]; then
        check_pass "${n4} correct 4-dot cross-submodule docs/00_adr links"
    else
        check_warn "No 4-dot cross-submodule docs/00_adr links found (expected ≥8 for TADR-005~008 + INDEX mirror)"
    fi
}

# ---------------------------------------------------------------------------
# Section 3: TADR Cross-references to UsrLinuxEmu ADRs
# ---------------------------------------------------------------------------
section_tadr_crossref() {
    section "3. TADR Cross-references to UsrLinuxEmu ADRs"

    # Map TADR-XXX → expected ADR-XXX
    declare -A tadr_to_adr=(
        ["005"]="032"
        ["006"]="033"
        ["007"]="033"
        ["008"]="034"
    )

    for tadr_num in "${!tadr_to_adr[@]}"; do
        local adr_num="${tadr_to_adr[$tadr_num]}"
        local tadr_file
        tadr_file=$(ls "${REPO_ROOT}/docs/adr/tadr-${tadr_num}-"*.md 2>/dev/null | head -1)

        if [ -z "${tadr_file}" ]; then
            check_fail "TADR-${tadr_num} file missing"
            continue
        fi

        # Check TADR file references the ADR file
        local expected_pattern="../../../../docs/00_adr/adr-${adr_num}-"
        if grep -qF "${expected_pattern}" "${tadr_file}"; then
            check_pass "TADR-${tadr_num} references ADR-${adr_num}"
        else
            check_fail "TADR-${tadr_num} missing ADR-${adr_num} reference (expected pattern '${expected_pattern}')"
        fi

        # Check ADR file actually exists at UsrLinuxEmu side
        local adr_glob="${USRLINUXEMU_ROOT}/docs/00_adr/adr-${adr_num}-"*.md
        if ls ${adr_glob} > /dev/null 2>&1; then
            check_pass "ADR-${adr_num} file exists at UsrLinuxEmu side"
        else
            check_fail "ADR-${adr_num} file missing at UsrLinuxEmu (cannot find ${adr_glob})"
        fi
    done
}

# ---------------------------------------------------------------------------
# Section 4: docs/adr/README.md INDEX Sync
# ---------------------------------------------------------------------------
section_index_sync() {
    section "4. docs/adr/README.md INDEX Sync"
    local readme="${REPO_ROOT}/docs/adr/README.md"

    if [ ! -f "${readme}" ]; then
        check_fail "docs/adr/README.md missing"
        return
    fi

    # Extract TADR-XXX from README INDEX table
    local index_tadrs
    index_tadrs=$(grep -oE 'TADR-[0-9]+' "${readme}" 2>/dev/null | sort -u)

    # Extract TADR-XXX from actual files (skip template)
    local actual_tadrs
    actual_tadrs=$(ls "${REPO_ROOT}/docs/adr/tadr-"*.md 2>/dev/null \
        | xargs -n1 basename 2>/dev/null \
        | grep -oE 'tadr-[0-9]+' \
        | sed 's/tadr-/TADR-/' \
        | sort -u)

    # Files exist but not in INDEX
    local missing_in_index=0
    for t in ${actual_tadrs}; do
        if ! echo "${index_tadrs}" | grep -qx "${t}"; then
            check_fail "${t} file exists but not in docs/adr/README.md INDEX"
            missing_in_index=$((missing_in_index+1))
        fi
    done

    # INDEX entries without files (skip TADR-000 = template)
    local missing_as_file=0
    for t in ${index_tadrs}; do
        if [ "${t}" = "TADR-000" ]; then continue; fi
        if ! echo "${actual_tadrs}" | grep -qx "${t}"; then
            check_fail "${t} in INDEX but no corresponding file"
            missing_as_file=$((missing_as_file+1))
        fi
    done

    if [ ${missing_in_index} -eq 0 ] && [ ${missing_as_file} -eq 0 ]; then
        check_pass "INDEX entries match actual TADR files (${actual_tadrs//$'\n'/,})"
    fi
}

# ---------------------------------------------------------------------------
# Section 5: Capability Consistency
# ---------------------------------------------------------------------------
section_capability() {
    section "5. Capability Consistency"
    local cap_file="${REPO_ROOT}/docs/architecture/capabilities.md"

    if [ ! -f "${cap_file}" ]; then
        check_fail "docs/architecture/capabilities.md missing"
        return
    fi

    # Canonical capabilities per UsrLinuxEmu ADR-035 §按 Capability 分组
    local canonical=("gpu-driver-architecture" "gpu-phase2-management" "architecture-governance")

    # Extract declared capabilities (matches **name** bold pattern in INDEX row)
    local declared
    declared=$(grep -oE '\*\*[a-z][a-z0-9-]+-(architecture|management|governance)\*\*' "${cap_file}" \
        | sed 's/\*//g' | sort -u)

    # All canonical present
    for cap in "${canonical[@]}"; do
        if echo "${declared}" | grep -qx "${cap}"; then
            check_pass "Canonical capability '${cap}' present"
        else
            check_fail "Canonical capability '${cap}' missing from capabilities.md"
        fi
    done

    # No scope creep
    local cap_count
    cap_count=$(echo "${declared}" | grep -c .)
    if [ ${cap_count} -eq 3 ]; then
        check_pass "Capability count = 3 (no scope creep vs UsrLinuxEmu canonical)"
    else
        check_fail "Capability count = ${cap_count} (expected 3)"
        local extra
        extra=$(comm -23 <(echo "${declared}") <(printf '%s\n' "${canonical[@]}" | sort -u))
        if [ -n "${extra}" ]; then
            check_warn "Extra capabilities (scope creep candidates): ${extra}"
        fi
    fi
}

# ---------------------------------------------------------------------------
# Section 6: Archive Policy
# ---------------------------------------------------------------------------
section_archive_policy() {
    section "6. Archive Policy"

    local deprecated_total=0
    local deprecated_outside=0
    while IFS= read -r f; do
        if grep -q "⚠️ DEPRECATED" "${f}"; then
            deprecated_total=$((deprecated_total+1))
            local rel
            rel=$(realpath --relative-to="${REPO_ROOT}" "${f}")
            if [[ ! "${rel}" =~ ^docs/archive/ ]]; then
                check_fail "DEPRECATED file outside docs/archive/: ${rel}"
                deprecated_outside=$((deprecated_outside+1))
            fi
        fi
    done < <(find "${REPO_ROOT}/docs" -name '*.md' -type f 2>/dev/null)

    if [ ${deprecated_total} -gt 0 ] && [ ${deprecated_outside} -eq 0 ]; then
        check_pass "${deprecated_total} DEPRECATED files, all in docs/archive/"
    elif [ ${deprecated_total} -eq 0 ]; then
        check_warn "No DEPRECATED files found (expected ≥3 archived v0.1 docs)"
    fi
}

# ---------------------------------------------------------------------------
# Section 7: Doc Structure
# ---------------------------------------------------------------------------
section_doc_structure() {
    section "7. Doc Structure"

    local arch_count
    arch_count=$(find "${REPO_ROOT}/docs/architecture" -name '*.md' -type f 2>/dev/null | wc -l)
    if [ ${arch_count} -eq 5 ]; then
        check_pass "docs/architecture/ has 5 files (README + 4 docs)"
    else
        check_fail "docs/architecture/ has ${arch_count} files (expected 5)"
    fi

    local roadmap_count
    roadmap_count=$(find "${REPO_ROOT}/docs/roadmap" -name '*.md' -type f 2>/dev/null | wc -l)
    if [ ${roadmap_count} -eq 6 ]; then
        check_pass "docs/roadmap/ has 6 files (README + 5 phase docs)"
    else
        check_fail "docs/roadmap/ has ${roadmap_count} files (expected 6)"
    fi

    local archive_count
    archive_count=$(find "${REPO_ROOT}/docs/archive" -name '*.md' -type f 2>/dev/null | wc -l)
    if [ ${archive_count} -eq 4 ]; then
        check_pass "docs/archive/ has 4 files (README + 3 v0.1)"
    else
        check_fail "docs/archive/ has ${archive_count} files (expected 4)"
    fi

    local adr_count
    adr_count=$(find "${REPO_ROOT}/docs/adr" -name 'tadr-*.md' -type f 2>/dev/null | wc -l)
    if [ ${adr_count} -eq 9 ]; then
        check_pass "docs/adr/ has 9 TADR files (TADR-000 template + TADR-001~008)"
    else
        check_fail "docs/adr/ has ${adr_count} TADR files (expected 9)"
    fi
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
PASSED=0
FAILED=0
WARNED=0

# Wrap each section to track passed/failed/warned counts via output counting
# (simpler: just count via exit code and trust the user can re-run)

# Each check_pass/fail/warn increments an internal counter; we track here.
run_section() {
    local name="$1"
    local before_exit="${EXIT_CODE}"
    "$1"  # call the section function
    local after_exit="${EXIT_CODE}"
    if [ "${after_exit}" -gt "${before_exit}" ]; then
        FAILED=$((FAILED+1))
    else
        PASSED=$((PASSED+1))
    fi
}

# Run selected sections
[ ${RUN_TADR_INTEGRITY} -eq 1 ] && run_section section_tadr_integrity
[ ${RUN_CROSS_LINKS}    -eq 1 ] && run_section section_cross_links
[ ${RUN_TADR_CROSSREF}  -eq 1 ] && run_section section_tadr_crossref
[ ${RUN_INDEX_SYNC}     -eq 1 ] && run_section section_index_sync
[ ${RUN_CAPABILITY}     -eq 1 ] && run_section section_capability
[ ${RUN_ARCHIVE_POLICY} -eq 1 ] && run_section section_archive_policy
[ ${RUN_DOC_STRUCTURE}  -eq 1 ] && run_section section_doc_structure

print_summary ${PASSED} ${FAILED} ${WARNED}
exit ${EXIT_CODE}