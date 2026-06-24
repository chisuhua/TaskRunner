---
SCOPE: UMD-EVOLUTION
STATUS: PROPOSED
DECISION_DATE: 2026-06-24
IMPLEMENTED: NO
---

# ADR: umd-evolution Scope Clarification (H-5)

## Context

The umd-evolution scope describes a future evolution path toward a CUDA Runtime / User-Mode Driver. Without clear scope rules, contributors may assume unimplemented features are available.

## Decision

**Status rules:**
- All umd-evolution content MUST have `STATUS: PROPOSED` or `STATUS: DRAFT`
- **`STATUS: ACCEPTED` is FORBIDDEN** for unimplemented features
- All `.cpp`/`.hpp` files in `src/umd/` and `include/umd/` MUST have `// SCOPE: UMD-EVOLUTION` header comment

**Build mode:**
- Default `TASKRUNNER_BUILD_MODE=test-fixture` does NOT compile umd-evolution code
- Opt-in `TASKRUNNER_BUILD_MODE=umd-evolution` compiles experimental skeleton

## Consequences

- Contributors cannot accidentally depend on unimplemented UMD features
- umd-evolution code is isolated from main test-fixture build

## References

- H-5 design.md §Decision 5: "umd-evolution 代码骨架仅占位"
- tadr-205-umd-evolution-poc-roadmap (deferred PoC phasing)
