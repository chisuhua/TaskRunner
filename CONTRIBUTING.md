# Contributing to TaskRunner

> **Status**: Initial draft (2026-06-19) — pending team review
> **Scope**: Cross-repository workflow between TaskRunner and UsrLinuxEmu
> **Audience**: Human contributors to either repo

---

## Overview

TaskRunner is a C++ task execution framework that consumes the GPU driver ABI from UsrLinuxEmu. The two repos evolve together through a **directional ownership sync model** — this document captures how to make cross-repo changes safely.

For AI-agent development conventions (build commands, naming style, etc.), see [AGENTS.md](AGENTS.md). For change proposals following the openspec workflow, see `/docs/02_architecture/` in UsrLinuxEmu.

---

## The Directional Ownership Model

**Rule**: The repo that owns the code leads the openspec; the other repo consumes via submodule pointer update.

| Change type | Lead repo | Why |
|---|---|---|
| **GPU driver ABI change** (`gpu_ioctl.h`, `gpu_types.h`, etc.) | UsrLinuxEmu | TaskRunner only consumes these |
| **CudaScheduler / CudaStub / GpuDriverClient** (in TaskRunner) | TaskRunner | UsrLinuxEmu doesn't depend on these |
| **CMakeLists / build system / CLI** (in TaskRunner) | TaskRunner | TaskRunner-only concern |
| **H-x cross-repo feature** (like H-2.5, H-3) | TaskRunner | Primary consumer; UsrLinuxEmu tracks via openspec archive |

**Example from H-1 closeout**: H-1 was a UsrLinuxEmu-side change (`gpu_ioctl.h` + validation logic). UsrLinuxEmu led, TaskRunner consumed via the submodule pointer update when the closeout change was merged.

---

## The H-x Change Lifecycle

Every cross-repo change follows a 4-phase pattern (established by H-1 closeout, codified in `plans/2026-06-19-h2-5-architecture-foundation/`):

### Phase 1: Skeleton in TaskRunner `plans/`

```bash
plans/<date>-<change-name>/
├── README.md              # DRAFT entry, scope, prerequisites
├── .openspec.yaml         # status: DRAFT
├── proposal.md            # Why + What + Capabilities
├── design.md              # How + decisions + risks
├── tasks.md               # Implementation steps
└── specs/<capability>/    # ADDED Requirements with Scenarios
```

Iterate on the skeleton locally. No UsrLinuxEmu involvement yet.

### Phase 2: Implement in TaskRunner code

- Commit changes in TaskRunner (where the code lives)
- Run TaskRunner tests (doctest + new H-x tests)
- **Do NOT update UsrLinuxEmu submodule pointer yet**

### Phase 3: Cross-repo sync (H-1 closeout pattern)

```bash
# In TaskRunner
git commit -m "feat(<scope>): <H-x summary>"
git push

# In UsrLinuxEmu
git checkout main
git pull
git add external/TaskRunner                          # pointer update (D1)
mv ../external/TaskRunner/plans/<change-name> \
   openspec/changes/<change-name>                     # activate openspec
# Edit files: remove DRAFT markers, update SSOT docs
git add openspec/changes/<change-name>/
git commit -m "feat(<scope>): H-x cross-repo sync + openspec activation"
```

### Phase 4: Verify + archive

```bash
# In UsrLinuxEmu
make -j4 && ctest && tools/docs-audit.sh --strict
openspec archive <change-name>
git add openspec/changes/archive/<date>-<change-name>/
git commit -m "chore(openspec): archive <change-name>"
git push
```

---

## Submodule Pointer Management

**Rule**: Update the submodule pointer (`external/TaskRunner` in UsrLinuxEmu) **only at sync points** — never mid-implementation.

| When | Action |
|---|---|
| During H-x implementation | ❌ **Do NOT** update UsrLinuxEmu pointer |
| After TaskRunner tests pass + commit | ✅ Update pointer ONCE (Phase 3 above) |
| Between H-x changes | ✅ Stable intermediate states OK |

**Why**: An intermediate TaskRunner commit (e.g., half-finished namespace bridge) pinned in UsrLinuxEmu breaks UsrLinuxEmu CI for everyone. Only the final sync commit (when TaskRunner tests are green) should be referenced.

---

## Issue Tracking Boundary

**Rule**: Where the issue lives depends on what it touches.

| Touch surface | Tracker |
|---|---|
| `gpu_ioctl.h` ABI, validation logic, R2 mapping contract | UsrLinuxEmu openspec change + UsrLinuxEmu GitHub issues |
| GpuDriverClient, CudaStub, CudaScheduler, IGpuDriver | TaskRunner GitHub issues |
| CMake build, CLI, TaskRunner tests | TaskRunner GitHub issues |
| Cross-repo contract changes (e.g., new IOCTL numbers) | UsrLinuxEmu openspec + TaskRunner cross-reference |

**Example**: H-3's R2 mapping contract (`stream_id = LOW32(queue_handle)`) lives in **both** repos:
- UsrLinuxEmu: `openspec/changes/.../specs/gpu-pushbuffer-validation/spec.md` (the validation logic)
- TaskRunner: `plans/2026-06-19-h3-phase2-openspec-skeleton/specs/gpu-phase2-management/spec.md` R6 Requirement (the consumer contract)

Both reference `gpgpu_device.cpp:262, 412` as the canonical source.

---

## ADR (Architecture Decision Records)

For upstream design decisions affecting the ABI, UsrLinuxEmu owns the ADR process. TaskRunner can propose ADRs via the UsrLinuxEmu openspec workflow.

**H-7 ADR (current open item)**: 3 owner-flagged upstream issues in UsrLinuxEmu that TaskRunner must work around:
1. `stream_id` u32 vs `queue_handle` u64 type mismatch
2. ioctl path bypasses `GpuQueueEmu`
3. `attached_queues` validation is weak

**Workaround in TaskRunner**: H-3 spec R6 explicitly enforces `stream_id == LOW32(queue_handle)` to comply with R2 mapping contract.

**Decoupling rule**: H-7 ADR does **NOT** block H-2.5/H-3 delivery. They run on parallel tracks. When H-7 lands an ABI change (e.g., widening `stream_id` to u64), TaskRunner opens a follow-up H-x change to consume it — same directional sync model.

---

## Testing Strategy

Two-layer model:

### Layer 1: TaskRunner CI (doctest)
- Runs against `MockGpuDriver` (no real `/dev/gpgpu0` needed)
- Covers IGpuDriver contract + R2 mapping invariant
- Fast, runs every commit
- New H-x tests added to `tests/test_gpu_phase2.cpp` (H-3 introduces this file)

### Layer 2: UsrLinuxEmu CI (Catch2)
- Runs against the simulator (`/dev/gpgpu0` plugin)
- Covers real ioctl path end-to-end
- Runs at cross-repo sync points + UsrLinuxEmu's own commits

### Cross-repo compatibility gate

At each H-x sync point (Phase 4 above), run **both** suites manually before archiving:

```bash
# In TaskRunner
cd build && make -j4 && ./test_cuda_scheduler
# Expected: 8/8 (pre-H-2.5) → 8 + H-2.5 (post-H-2.5) → 8 + H-2.5 + H-3 (post-H-3)

# In UsrLinuxEmu
make -j4 && ctest
# Expected: 34/34 (or current baseline)
```

If either fails, do NOT archive. Fix and retry.

---

## Anti-patterns

Don't do these — they create hidden technical debt or break coordination.

### ❌ Dual-maintain openspec in both repos

Keeps canonical in UsrLinuxEmu `openspec/changes/` only. TaskRunner `plans/` is scratch/DRAFT, never the authoritative copy. If someone edits the TaskRunner `plans/` copy after activation, it silently desyncs.

### ❌ Update submodule pointer mid-implementation

An intermediate TaskRunner commit gets pinned in UsrLinuxEmu, breaking CI for everyone. Only update the pointer at the final sync commit of each H-x change.

### ❌ Block H-2.5/H-3 on H-7 ADR

The R2 mapping workaround already makes TaskRunner functional today. H-7 is a real upstream debt, but conflating its resolution with TaskRunner delivery stalls all TaskRunner progress indefinitely on a decision TaskRunner doesn't own.

### ❌ Skip the skeleton phase for "small" changes

Even small H-x changes benefit from openspec review — the 4 BO signature reconciliations (D6-D8 in H-2.5) are real design decisions that need documented rationale, not ad-hoc fixes in commit messages.

### ❌ Edit files in UsrLinuxEmu's `external/TaskRunner/` submodule directory directly

The submodule is a read-only view from UsrLinuxEmu's perspective. Make changes in the TaskRunner repo, commit, push, then update the pointer in UsrLinuxEmu via Phase 3.

---

## Quick Reference

### I'm starting a new H-x change

1. Read the most recent archived openspec change for format precedent
2. Create skeleton in TaskRunner `plans/<date>-<change-name>/`
3. Iterate on design until reviewable
4. Get review (at minimum: one other team member + check decisions don't conflict with UsrLinuxEmu ABI)
5. Implement in TaskRunner code
6. Test with `MockGpuDriver` (no real device)
7. Follow Phase 3-4 above for cross-repo sync

### I'm fixing a TaskRunner-only bug

1. No openspec needed — commit directly in TaskRunner
2. Add regression test (doctest)
3. Run `./test_cuda_scheduler` before pushing
4. No UsrLinuxEmu sync needed unless you changed ABI headers

### I'm proposing a UsrLinuxEmu ABI change

1. Discuss with UsrLinuxEmu owner FIRST (don't surprise them)
2. Create openspec change in UsrLinuxEmu `openspec/changes/<name>/`
3. TaskRunner gets a corresponding H-x change to consume the new ABI

---

## See Also

- [AGENTS.md](AGENTS.md) — AI agent development conventions (build, naming, code style)
- [`plans/2026-06-19-h2-5-architecture-foundation/`](plans/2026-06-19-h2-5-architecture-foundation/) — H-2.5 openspec skeleton (next major change)
- [`plans/2026-06-19-h3-phase2-openspec-skeleton/`](plans/2026-06-19-h3-phase2-openspec-skeleton/) — H-3 openspec skeleton
- [UsrLinuxEmu openspec workflow](https://github.com/chisuhua/UsrLinuxEmu/tree/main/openspec) — upstream change management
- H-1 closeout precedent: `/workspace/project/UsrLinuxEmu/openspec/changes/archive/2026-06-17-h1-pushbuffer-validation-closeout/`