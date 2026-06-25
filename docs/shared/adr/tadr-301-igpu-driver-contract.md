---
SCOPE: SHARED
STATUS: ACCEPTED
DECISION_DATE: 2026-06-24
---

# ADR: IGpuDriver Interface Contract (H-5)

## Context

`IGpuDriver` is the 28-method (基线) abstract interface that decouples TaskRunner from specific GPU implementations. It was introduced in H-2.5 and extended in H-3 to 31 methods (Phase 2 5 方法 + H-3.5 3 调度相关方法，详见 [tadr-109 IGpuDriver 31 方法扩展 + CudaScheduler 抽象泄漏修复](../test-fixture/adr/tadr-109-igpu-driver-uniform-scheduling.md))。It must remain ABI-stable for both test-fixture (current) and umd-evolution (future) consumers.

## Decision

`IGpuDriver` MUST preserve the following invariants:

**Stability rules:**
1. All 31 method signatures (28 baseline + 3 from tadr-109) MUST NOT change without ADR + major version bump
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

## Method Count Evolution

| Version | Methods | Source |
|---------|---------|--------|
| H-2.5 (baseline) | 28 | tadr-102 + UsrLinuxEmu ADR-032 |
| H-3 (Phase 2) | 28 + 5 = 33 (merged later) | tadr-103 + UsrLinuxEmu ADR-033 |
| H-3.5 (current) | **31** | tadr-109 (H-3.5 抽象泄漏修复 3 方法替代 H-3 直接加法) |

**注**：H-3.5 起，由于 tadr-109 重构了 5 个 Phase 2 方法的实现路径（从"加 5 个方法"改为"加 3 个新方法"），最终基线为 **31 方法**（28 + 3）。

## References

- `include/shared/igpu_driver.hpp` — Canonical interface definition
- H-2.5 tadr-102 (original 28-method design)
- H-3 tadr-103 (Phase 2 5 方法 initial proposal)
- H-3.5 tadr-109 (current 31-method baseline)
