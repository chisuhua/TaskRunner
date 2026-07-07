---
SCOPE: shared (tooling)
STATUS: ACTIVE
DATE: 2026-07-07
---

# OpenSpec Tooling Issues

Known bugs and limitations in the `openspec` CLI (installed via pnpm globally;
upstream source, not vendored into this repo). Each issue tracks symptom,
impact, workaround, and suggested upstream fix.

> **Maintenance**: Before fixing locally, check the upstream `openspec` changelog
> to see if the issue has shipped a fix. After upgrading `openspec`, re-test the
> commands under "Workaround" — if they succeed, mark the issue as RESOLVED and
> add a reference to the upgrade commit here.

| ID       | Title                                                          | Severity | Status | Discovered   |
|----------|----------------------------------------------------------------|----------|--------|--------------|
| TOOL-001 | `--change` rejects names with date prefix (`YYYY-MM-DD-...`)  | High     | Open   | 2026-07-07   |

---

## TOOL-001: `--change` rejects names with date prefix

### Severity
**High** — affects every archive operation in this project. Every change
created in TaskRunner follows the `YYYY-MM-DD-<slug>` convention, and every
change has had its archive workflow blocked by this bug.

### Symptom

```
$ openspec status --change "2026-07-06-phase3-step3-shim-and-forwarding" --json
✖ Error: Invalid change name '2026-07-06-phase3-step3-shim-and-forwarding':
          Change name must start with a letter

$ openspec status --change "phase3-step3-shim-and-forwarding" --json
✖ Error: Change 'phase3-step3-shim-and-forwarding' not found.
          Available changes: 2026-07-06-phase3-step3-shim-and-forwarding
```

`openspec list --json` correctly returns the full date-prefixed name, so the
directory layout is recognized — but the name validator in `status`, `archive`,
`show`, and presumably all other `--change <name>` accepting subcommands rejects
any name whose first character is a digit.

### Impact
- Cannot use `openspec status <name>` to check completion of any in-flight change
- Cannot use `openspec archive <name>` (its validator short-circuits on the same
  regex, then aborts before any rename/move logic runs); spec-format validation
  issues in the change also surface here when bypassing
- `openspec show <name>` likely shares the constraint (untested at filing time)

### Workaround (used 2026-07-07 for archive of `phase3-step3-shim-and-forwarding`)

Re-implement the [`openspec-archive-change` skill](../../.opencode/skills/openspec-archive-change/SKILL.md)
step 5 manually:

```bash
cd /workspace/project/UsrLinuxEmu/external/TaskRunner

# 1. Tick tasks (the status counter reads `- [x]` from tasks.md)
grep -c '^\- \[ \]' openspec/changes/<change-name>/tasks.md   # remaining
# (edit to flip all to `- [x]` and update header status to ✅ DONE)
grep -c '^\- \[x\]' openspec/changes/<change-name>/tasks.md   # = total

# 2. Ensure archive dir exists, then bypass openspec CLI for the move
mkdir -p openspec/changes/archive
mv openspec/changes/<change-name> openspec/changes/archive/<change-name>

# 3. Verify rename detection (tasks.md tick changes appear as R077)
git add -A
git diff --staged -M --diff-filter=R --name-status

# 4. Commit + push (PR-merge-style)
git commit -m "openspec(archive): <change-name> (PR #N applied)"
git push origin main
```

Reference commit demonstrating the workaround on TaskRunner `main`:
`76f14e0` — `openspec(archive): phase3-step3-shim-and-forwarding (PR #7 applied)`
(2026-07-07, merged PR #7 + submodule bump).

### Suggested fix (upstream)

Either of these restores the documented CLI ergonomics:

1. **Loosen the regex** — accept any leading character, including digits; the
   directory-vs-name lookup is what matters, not the format.
2. **Strip leading `YYYY-MM-DD-` prefix when matching** the `--change` argument
   against the directory `name` field returned by `list`. This way both
   `2026-07-06-foo` (full) and `phase3-step3-foo` (slug-only) work.

A local fallback (no upstream change needed): drop the date prefix from change
directory names in this repo. Cost: loss of automatic chronological sort in
`openspec list` output. Benefit: unblocks every archive command and aligns with
upstream CLI expectations.

### Affected changes (TaskRunner `openspec/changes/`)

| Change name                                           | State   | Archived via |
|-------------------------------------------------------|---------|--------------|
| 2026-07-02-phase16-shim-extension                     | archive | (pre-fix; CLI check not in repo yet) |
| 2026-07-02-phase17-test-coverage-completion           | archive | (pre-fix) |
| 2026-07-02-phase17-wait-period-work                   | archive | (pre-fix) |
| 2026-07-02-umd-shim-coverage-hardening                | archive | (pre-fix) |
| 2026-07-05-phase3-1-igpu-driver-extension             | archive | (pre-fix) |
| 2026-07-06-phase3-step3-shim-and-forwarding           | archive | **manual workaround (`76f14e0`)** |

Every future change following the date-prefix convention inherits this bug.

### Discovered during
Archive workflow for `phase3-step3-shim-and-forwarding` after TaskRunner
PR #7 merged + UsrLinuxEmu submodule bumped (post Step 4 of ADR-035 §R5.1).

### Reporter
TaskRunner owner (`chisuhua`); filed in-repo per AGENTS.md guidance
("Tooling issues → `openspec/TOOLING_ISSUES.md`") to avoid scattering
workarounds across PRs.

### Resolution status
**Open** as of 2026-07-07. Re-test after each `openspec` upgrade:
```bash
openspec status --change "2026-07-05-phase3-1-igpu-driver-extension" --json
# If returns JSON instead of "Invalid change name", TOOL-001 is fixed.
```

---

## Adding new issues

Use the format above. Required fields:
- **Severity**: High / Medium / Low (based on workflow impact, not user pain)
- **Symptom**: exact error output, copy-pasted
- **Impact**: list which repo commands are affected and which changes are stuck
- **Workaround**: copy-pasteable command sequence, with a reference commit
- **Suggested fix**: upstream-first; local fallback second
- **Affected changes**: table of every change blocked by this bug
- **Resolution status**: Open / RESOLVED (with upstream version that fixed it)
