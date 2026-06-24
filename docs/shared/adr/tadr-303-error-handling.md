---
SCOPE: SHARED
STATUS: ACCEPTED
DECISION_DATE: 2026-06-24
---

# ADR: Error Handling Abstraction (H-5)

## Context

TaskRunner needs a consistent error handling pattern across scopes. Current code mixes Linux error codes (return values), exceptions (none currently), and custom enums.

## Decision

`include/shared/error_handling.hpp` provides:

- `Result<T>` — Tagged union of `T` value or `ErrorCode`
- `ErrorCode` enum — Linux-style error codes (EINVAL, ENOMEM, EREMOTEIO, etc.)
- `make_error(ErrorCode)` — Factory function for error Results

**Rules:**
- All public APIs MUST return `Result<T>` for fallible operations
- All public APIs MUST return `T` directly for infallible operations
- Exceptions are FORBIDDEN in shared-scope code (test-fixture follows same rule)
- Linux error codes MUST be preserved (no custom enum mapping)

## References

- `include/shared/error_handling.hpp` — Canonical header
- Linux kernel error code conventions
