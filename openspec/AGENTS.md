# openspec/ - OpenSpec Workflow

OpenSpec change management for the umd-evolution scope. Uses the `openspec` CLI (v1.3.1+) for proposing, applying, and archiving changes.

> ⚠️ **PROJECT IDENTITY** (必读): 本目录位于 TaskRunner 仓 (`/workspace/project/UsrLinuxEmu/external/TaskRunner`)。**Working project = TaskRunner**,父仓 UsrLinuxEmu 仅作 read-only 参考。完整边界与历史反例见 [../AGENTS.md §PROJECT IDENTITY](../AGENTS.md)。简言之: 不要把 openspec 工作流延伸到父仓,不要在父仓跑 `/guide-arch` (skill 默认 cwd 作用于本仓)。

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

### 本仓 skill 适用性矩阵

| Skill | 适用于本仓? | 走本仓 openspec/ 工作流? |
|-------|--------------|--------------------------|
| `/guide-arch` | ❌ 不适用 | — (本仓 H-5 3-scope + 38 TADRs 已完成 arch-done;skill 默认期望 `docs/adr/ADR-*.md`,与本仓 `tadr-*.md` 命名空间不兼容) |
| `/guide-design` | ✅ 适用 | 是 — 提案创建/审查/批准走本仓 `.rddf/improvements/` + `proposal-suggestions.md` |
| `/guide-plan` | ✅ 适用 | 是 — propose 阶段生成 `openspec/changes/<name>/{proposal,design,tasks}.md` |
| `/guide-ship` | ✅ 适用 | 是 — execute/archive/cleanup 阶段直接消费 openspec changes |

**反例** (2026-08-13): 在本仓 cwd 跑 `/guide-arch` → skill 的 ADR-0016 discovery 找不到 `docs/adr/` → 报告 "0 ADR / no roadmap" → 试图在 `docs/adr/` 创建平行 ADR-NNNN → 与现有 TADR-NNN 命名冲突。识别为 skill 不适用并退出是正确行为,不要绕过 discovery (`SPEC_WORKFLOW_ADR_DIR=docs/shared/adr`) 或切换到父仓执行 (后者会污染 UsrLinuxEmu 工作树)。
