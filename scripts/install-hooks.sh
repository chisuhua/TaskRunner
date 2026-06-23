#!/bin/sh
# Install TaskRunner git hooks
#
# Usage: scripts/install-hooks.sh
# Or:    cp scripts/hooks/pre-commit .git/hooks/pre-commit && chmod +x .git/hooks/pre-commit

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SOURCE_HOOKS_DIR="${SCRIPT_DIR}/hooks"

if [ -f "${REPO_ROOT}/.git" ]; then
    GITDIR_REL=$(grep -E '^gitdir:' "${REPO_ROOT}/.git" | sed 's/^gitdir:[[:space:]]*//')
    GITDIR=$(readlink -m "${REPO_ROOT}/${GITDIR_REL}")
    HOOKS_DIR="${GITDIR}/hooks"
elif [ -d "${REPO_ROOT}/.git" ]; then
    HOOKS_DIR="${REPO_ROOT}/.git/hooks"
else
    echo "Error: neither .git file nor .git directory found in ${REPO_ROOT}"
    exit 1
fi

if [ ! -d "${SOURCE_HOOKS_DIR}" ]; then
    echo "Error: ${SOURCE_HOOKS_DIR} not found"
    exit 1
fi

if [ ! -d "${HOOKS_DIR}" ]; then
    echo "Error: hooks directory ${HOOKS_DIR} not found"
    exit 1
fi

echo "Installing TaskRunner git hooks from ${SOURCE_HOOKS_DIR} to ${HOOKS_DIR}..."

for hook in pre-commit; do
    if [ -f "${SOURCE_HOOKS_DIR}/${hook}" ]; then
        cp "${SOURCE_HOOKS_DIR}/${hook}" "${HOOKS_DIR}/${hook}"
        chmod +x "${HOOKS_DIR}/${hook}"
        echo "  Installed ${hook}"
    else
        echo "  Skipped ${hook} (source not found)"
    fi
done

echo ""
echo "Done. To skip docs-audit temporarily: SKIP_DOCS_AUDIT=1 git commit ..."
echo "To uninstall: rm ${HOOKS_DIR}/pre-commit"