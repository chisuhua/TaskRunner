# openspec/ - OpenSpec Workflow

OpenSpec change management for the umd-evolution scope. Uses the `openspec` CLI (v1.3.1+) for proposing, applying, and archiving changes.

## OVERVIEW

| Item | Status |
|------|--------|
| Active changes | 0 (all 16 are in `archive/`) |
| Active specs | 2 (`cu-graph-async-fence-testing`, `shim-default-init-fallback`) |
| Config | `config.yaml` (5 tools-related issues noted in `TOOLING_ISSUES.md`) |

## STRUCTURE

```
openspec/
├── config.yaml                    # OpenSpec config (project name, schema)
├── TOOLING_ISSUES.md              # Known issues with openspec CLI
├── changes/                       # Active change proposals
│   └── archive/                   # 16 archived changes (per change_id)
│       ├── 2026-07-07-phase3-real-impl-bridge/
│       └── ... (15 more)
└── specs/                         # Main specs (delta + base)
    ├── cu-graph-async-fence-testing/   # Active spec delta
    └── shim-default-init-fallback/     # Active spec delta
```

## WHERE TO LOOK

| Task | Location |
|------|----------|
| Propose a new change | Create `openspec/changes/<change-id>/` with `proposal.md`, `tasks.md`, `design.md` (if needed), `specs/*.md` |
| Apply a change | `openspec apply <change-id>` (use `openspec-apply-change` skill) |
| Archive a completed change | `openspec archive <change-id>` (use `openspec-archive-change` skill) |
| Sync delta specs to main | `openspec sync <spec-id>` (use `openspec-sync-specs` skill) |
| Find archived change history | `openspec/changes/archive/<change-id>/` |
| Investigate tooling issues | `TOOLING_ISSUES.md` |

## CONVENTIONS

- Change ID format: `YYYY-MM-DD-short-slug` (e.g., `2026-07-07-phase3-real-impl-bridge`)
- Each change directory contains:
  - `proposal.md` — **REQUIRED** — what & why
  - `tasks.md` — **REQUIRED** — task breakdown with `[ ]`/`[x]`
  - `design.md` — optional, for complex changes
  - `specs/` — optional delta specs (when adding new requirements)
- After implementation: archive moves the dir to `changes/archive/`
- Spec deltas use `## ADDED Requirements`, `## MODIFIED Requirements`, `## REMOVED Requirements`

## ANTI-PATTERNS

- **NEVER** create a change without `proposal.md` + `tasks.md` (these are required)
- **NEVER** archive an incomplete change (CI gate catches this)
- **NEVER** mix multiple changes in one dir (1 change = 1 dir)
- **NEVER** skip the sync step when adding a spec delta (main specs will drift)
- **NEVER** commit `openspec/` without first running `openspec validate`

## SKILLS (PROJECT-LEVEL)

Available via the `skill` tool:
- `openspec-explore` — explore mode for thinking through problems
- `openspec-propose` — generate all artifacts in one step
- `openspec-apply-change` — implement tasks from a change
- `openspec-archive-change` — archive a completed change
- `openspec-sync-specs` — sync delta specs to main without archiving

## RELATIONSHIP TO RDDF WORKFLOW

OpenSpec is the new (v3+) change management system; the older `rdd-workflow` skills (`guide-arch`, `guide-design`, `guide-plan`, `guide-ship`) drive a higher-level state machine that may use OpenSpec under the hood. See `openspec/TOOLING_ISSUES.md` for known integration issues.
