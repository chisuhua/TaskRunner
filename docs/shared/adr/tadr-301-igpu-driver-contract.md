---
SCOPE: SHARED
STATUS: ACCEPTED
DECISION_DATE: 2026-06-24
---

# ADR: IGpuDriver Interface Contract (H-5)

## Context

`IGpuDriver` is the 28-method abstract interface that decouples TaskRunner from specific GPU implementations. It was introduced in H-2.5 and extended in H-3. It must remain ABI-stable for both test-fixture (current) and umd-evolution (future) consumers.

## Decision

`IGpuDriver` MUST preserve the following invariants:

**Stability rules:**
1. All 28 method signatures MUST NOT change without ADR + major version bump
2. Adding new methods is allowed (backward-compatible)
3. Deprecating methods is allowed (must provide alternative)
4. Removing methods is FORBIDDEN

**Naming conventions:**
- snake_case for all methods (e.g., `alloc_bo`, `submit_batch`)
- UPPER_SNAKE_CASE for constants (e.g., `GPU_IOCTL_*`)
- CamelCase for classes (e.g., `GpuDriverClient`)

**Error handling:**
- Return 0 on success
- Return negative Linux error code on failure (e.g., -EINVAL, -ENOMEM)
- Never throw exceptions

## References

- `include/shared/igpu_driver.hpp` — Canonical interface definition
- H-2.5 tadr-102 (original design)
- H-3 tadr-103 (Phase 2 extension)
