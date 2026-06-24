---
SCOPE: SHARED
STATUS: ACCEPTED
DECISION_DATE: 2026-06-24
---

# ADR: Sync Primitives Abstraction (H-5)

## Context

TaskRunner needs cross-cutting synchronization primitives (MPSC queue, atomic counter, mutex wrappers) used by both test-fixture (current) and umd-evolution (future) scopes.

## Decision

`include/shared/sync_primitives.hpp` provides:

- `MpscQueue<T>` — Multi-producer single-consumer lock-free queue
- `AtomicCounter` — 64-bit atomic counter with relaxed/acquire/release semantics
- `Mutex` — Thin wrapper around `std::mutex` (test-fixture only) or platform-specific (umd-evolution)
- `ConditionVariable` — Companion to Mutex

**Rules:**
- All primitives MUST be header-only (template-based) for inlining
- All primitives MUST be `noexcept`
- All primitives MUST support both single-threaded (test) and multi-threaded (prod) usage

## References

- `include/shared/sync_primitives.hpp` — Canonical header
- TaskRunner original `EventQueue` implementation (reference)
