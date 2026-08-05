# docs/ - Documentation (3-scope ADR + extras)

Documentation organized by H-5 3-scope. Each scope has its own `adr/`, `architecture/`, `roadmap/`, `archive/` subdirs. Plus extra non-scope dirs (`07-integration/`, `superpowers/`, `ONBOARDING.md`).

## OVERVIEW

| Subdir | Scope | Status | Contents |
|--------|-------|--------|----------|
| `test-fixture/` | TEST-FIXTURE | ACCEPTED | adr(11 numbered + 5 redirect) architecture(7) roadmap(6) archive(4) research(6) coordination(2) |
| `umd-evolution/` | UMD-EVOLUTION | PROPOSED | adr(8 numbered + 3 redirect, 含 tadr-401 promotion) architecture(3) roadmap(8) research(2) gap-analysis vision* README |
| `shared/` | SHARED | ACCEPTED | adr(10, 含 1 模板) research(2) README |
| `07-integration/` | (none) | — | UsrLinuxEmu cross-repo integration |
| `superpowers/` | (none) | — | internal skill notes (architecture, plans, specs, cross-repo-prs) |
| `ONBOARDING.md` | (none) | — | developer onboarding (not scope-tagged) |

## WHERE TO LOOK

| Task | Location |
|------|----------|
| Find TADR by topic | `docs/{test-fixture,umd-evolution,shared}/adr/` — `tadr-NNN-*.md` |
| Find roadmap phase | `docs/{test-fixture,umd-evolution}/roadmap/` |
| Architecture diagrams | `docs/{test-fixture,umd-evolution,shared}/architecture/` |
| Archived ADRs | `docs/test-fixture/archive/` (TADR-001..TADR-099) |
| Gap analysis vs vision | `docs/umd-evolution/gap-analysis.md` |
| Cross-repo integration | `docs/07-integration/` |
| Onboard a new dev | `docs/ONBOARDING.md` |

## CONVENTIONS

- ADR file name: `tadr-NNN-short-slug.md` (lowercase, hyphens, no underscores)
- Numbering: TADR-1xx = test-fixture, TADR-2xx = umd-evolution, TADR-3xx = shared
- Every ADR header:
  ```markdown
  ---
  SCOPE: <test-fixture|umd-evolution|shared>
  STATUS: <ACCEPTED|PROPOSED|DRAFT|DEPRECATED>
  ---
  ```
- Cross-scope reference: use `../umd-evolution/...` and tag inline `[UMD-EVOLUTION SCOPE]`
- TADR-4xx reserved for promotion proposals (TADR-401 = promote umd-evolution → ACCEPTED)
- Verify with `tools/docs-audit.sh` (runs in pre-commit hook)

## ANTI-PATTERNS

- **NEVER** create a doc outside the 3-scope dirs without explicit justification
- **NEVER** mix SCOPE values in a single doc — pick one
- **NEVER** use the `superpowers/` or `ONBOARDING.md` paths from other TADRs (internal-only)
- **NEVER** increment TADR number without checking `docs/{scope}/adr/README.md` INDEX
- **NEVER** mark a TADR as ACCEPTED before implementing + testing

## TADR NUMBERING SCHEME

| Range | Scope | Authority |
|-------|-------|-----------|
| 1xx | test-fixture | High (currently shipped) |
| 2xx | umd-evolution | Medium (proposed/draft) |
| 3xx | shared | High (cross-cutting, dual review) |
| 4xx | promotion | TBD (TADR-401 first use) |

## SYNC PROTOCOL (CROSS-REPO)

When you add a TADR here, also update `UsrLinuxEmu/docs/00_adr/README.md` "TaskRunner TADR mirror" table. See root AGENTS.md "跨仓工作原则" for the 4-step sync protocol.
