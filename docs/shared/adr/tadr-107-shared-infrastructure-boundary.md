---
SCOPE: SHARED
STATUS: ACCEPTED
DECISION_DATE: 2026-06-24
---

# ADR: Shared Infrastructure Boundary (H-5)

## Context

The TaskRunner codebase has cross-cutting abstractions (IGpuDriver interface, sync primitives, error handling) that are used by both the test-fixture scope and the (future) umd-evolution scope. Without a clear boundary, changes to shared abstractions can unilaterally break one scope.

## Decision

Define **shared scope** as the cross-cutting abstractions, with the following rules:

**In scope (shared):**
- `IGpuDriver` interface contract (28 methods)
- Sync primitives (`include/shared/sync_primitives.hpp`)
- Error handling abstractions (`include/shared/error_handling.hpp`)
- Memory manager (`include/shared/memory_manager.hpp`)

**Out of scope (scope-specific):**
- Business logic (test-fixture's CudaStub/Scheduler, future umd's CUDA API surface)
- Hardware-specific code (real CUDA Runtime, doorbell MMIO)

## Review Requirements

**Dual approval required for any shared-scope change:**
1. At least 1 test-fixture scope maintainer
2. At least 1 umd-evolution scope maintainer (or designee)

This prevents unilateral breakage.

## References

- tadr-301 through tadr-303 (specific shared contracts)
- H-5 design.md §Decision 4: "Shared area review 严格化"
