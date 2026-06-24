---
SCOPE: SHARED
STATUS: ACTIVE
CROSS_REFS: [ADR-036](../../../docs/00_adr/adr-036-three-way-separation.md) (3-Way Architectural Separation), [ADR-035](../../../docs/00_adr/adr-035-governance-policy.md) (Governance Policy)
---

# shared Scope

This directory contains **shared infrastructure** that both test-fixture and umd-evolution scopes depend on.

## Cross-Repo ABI Contract (ADR-036 Alignment)

The TaskRunner `include/shared/` layer is the **TaskRunner-side implementation** of the shared ABI contract defined by [UsrLinuxEmu ADR-036 §Decision](../../../docs/00_adr/adr-036-three-way-separation.md) §Decision line 41:

> **shared**: ABI 契约 — `plugins/gpu_driver/shared/`（`gpu_ioctl.h`, `gpu_types.h`, `gpu_queue.h`）— TaskRunner 与 UsrLinuxEmu 共享头文件 — 既不属于 ② 也不属于 ③，仅作为可移植驱动与外部 consumer 之间的契约。

| TaskRunner side | UsrLinuxEmu side | Contract role |
|---|---|---|
| `include/shared/igpu_driver.hpp` | `plugins/gpu_driver/shared/gpu_ioctl.h` (via UsrLinuxEmu symlink) | IGpuDriver 28-method interface ↔ ioctl 派发表 |
| `include/shared/sync_primitives.hpp` | (no direct counterpart) | TaskRunner-internal cross-cutting sync primitives |
| `include/shared/error_handling.hpp` (H-5 placeholder) | (no direct counterpart) | Result<T> + ErrorCode enum |
| `include/shared/memory_manager.hpp` (H-5 v2) | (no direct counterpart) | Memory manager shared by test-fixture and umd-evolution |

**Cross-repo modification policy**: Per ADR-036 §Risk 表 "shared 头文件双方不同步" — TaskRunner and UsrLinuxEmu maintainers must perform **dual-ack** on every shared contract change. The existing UsrLinuxEmu symlink (`external/TaskRunner/UsrLinuxEmu/`) triggers immediate diff visibility for cross-repo reviewers.

## In Scope

- `IGpuDriver` interface contract (28 methods) — see `adr/tadr-301-igpu-driver-contract.md`
- Sync primitives (MPSC queue, atomic counter, mutex wrappers) — see `adr/tadr-302-sync-primitives.md`
- Error handling abstractions (Result<T> + ErrorCode enum) — see `adr/tadr-303-error-handling.md`
- Memory manager (H-5 v2 addition, used by `cuda_scheduler`) — see `../../include/shared/memory_manager.hpp`

## Review Requirements (Dual Approval)

**Any shared-scope change requires dual approval:**

1. At least 1 test-fixture scope maintainer
2. At least 1 umd-evolution scope maintainer (or designee if no active maintainer)
3. **For ABI-contract-affecting changes** (anything in `igpu_driver.hpp` or related to `gpu_ioctl.h`): also notify UsrLinuxEmu maintainer per ADR-036 cross-repo policy

This prevents unilateral breakage of either scope.

## Subdirectories

- `adr/` — Shared scope TADRs (tadr-107 shared boundary + tadr-301~303 contracts)

## Cross-Scope References

- For test-fixture: see `../test-fixture/README.md`
- For umd-evolution: see `../umd-evolution/README.md`
- For UsrLinuxEmu 3-way architectural principle: see [ADR-036](../../../docs/00_adr/adr-036-three-way-separation.md)
