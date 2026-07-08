# Change: phase3-3-event-texture-impl

> **Status**: 🔄 PROPOSED（2026-07-08, kicked off via worktree phase3-3-event-texture）
> **Type**: UMD-EVOLUTION sub-system（implementation workstream）
> **Phase**: 3.3
> **Subplans**: 3.3a (Event timing precision, 1w) + 3.3b (Texture/Surface frontend, 2w)
> **Estimated effort**: 3 weeks (1w + 2w)
> **Backend dependency**: None (frontend-only, CudaStub clock sufficient)
> **Triggered by**: docs/superpowers/plans/2026-07-05-umd-phase3.3-event-texture.md (ACCEPTED 2026-07-08)
> **Precondition**: Phase 1.7 ✅ close (commit 869bd25, 2026-07-08)
> **Worktree**: .rddf/wt/phase3-3-event-texture (branch phase3-3-event-texture)
> **Author**: Sisyphus session

## Why

Phase 1.7 (commit 869bd25) 已 close。Phase 3.3 (Event timing precision + Texture/Surface frontend) 是 sync-plan §5.3 行 260 的 backlog 项目：
- "🟢 可立即开始 (独立), DRAFT plan"
- 已 DRAFT plan 写好 (547 lines, 2026-07-05)
- 现 kick off 为 ACTIVE openspec change

### 业务价值

1. **Phase 3.3a**: 修复 `cuEvent*` 现有 precision bug（Phase 2 PoC 遗留），添加 flag validation + 严格 timing semantics。影响 6 个 cu* API，2 处 bug fix。
2. **Phase 3.3b**: 升级 11 个 cuTexRef*/cuArray* API 从 STUB → REAL_IMPL，提供完整 frontend（无实际 GPU sampling，纯 frontend 状态机）。
3. **总**: shim API 覆盖深度提升，REAL_IMPL 113 → 120+。

## What Changes

### Phase 3.3a (1w) — Event timing precision

**修改**:
- `src/umd/libcuda_shim/cu_event.cpp` — 重构 EventTable（添加 EventRecord struct），修复 cuEventRecord 覆盖 created_at bug
- `include/cuda.h` — 添加 `cuEventRecordWithFlags` 声明
- `tools/generate_cu_stubs.py` — 标记 cuEventCreateWithFlags 从 STUB 升 REAL_IMPL

**新增**:
- `tests/umd/test_event_timing.cpp` — 23 cases
- `tests/umd/test_event_timing.cpp` 注册到 cmake/UMDEvolution.cmake

### Phase 3.3b (2w) — Texture/Surface frontend

**修改**:
- `tools/generate_cu_stubs.py` — 11 个 cuTexRef*/cuArray* API 从 STUB 升 REAL_IMPL
- `cmake/UMDEvolution.cmake` — 注册新 .cpp
- `docs/umd-evolution/architecture/runtime-layering.md` — 添加 Texture/Surface 段落

**新增**:
- `src/umd/libcuda_shim/cu_texref.cpp` — 200-300 行（11 cuTexRef* API）
- `src/umd/libcuda_shim/cu_array.cpp` — 150-200 行（3 cuArray* API）
- `tests/umd/test_texture_surface.cpp` — 25 cases

## Impact

| 影响项 | 数量 | 风险 |
|---|---|---|
| 源文件 | 1 modify (cu_event.cpp) + 2 new (cu_texref.cpp, cu_array.cpp) | Low (frontend-only) |
| Header | 1 modify (cuda.h, +1 declaration) | Low |
| Test 文件 | 2 new (test_event_timing.cpp, test_texture_surface.cpp) | Low |
| Test case 增加 | +48 (23 + 25) | None (purely additive) |
| STUB → REAL_IMPL | +12 (cuEventCreateWithFlags + 11 cuTexRef*/cuArray*) | Low (state machine) |
| REAL_IMPL 计数 | 113 → 125 | n/a |
| docs-audit | 1 modify (runtime-layering.md) | None |
| UsrLinuxEmu 跨仓 | 0 lines | None (frontend-only) |

**风险缓解**:
- Frontend-only, no UsrLinuxEmu backend dep
- 现有 270/270 tests 必须保持 → 0 regression acceptance
- ASan + UBSan + TSan 三 sanitizer 跑过
- 工作在独立 worktree, 失败易 revert

## Cross-Spec Sync

- **Precondition**: Phase 1.7 close (commit 869bd25)
- **Reference**: DRAFT plan `docs/superpowers/plans/2026-07-05-umd-phase3.3-event-texture.md` (547 lines)
- **Worktree**: `.rddf/wt/phase3-3-event-texture/`
- **TADR 影响**: 无 (frontend-only, 不影响 IGpuDriver interface)

## Out of Scope (deferred)

- 实际 GPU sampling (需 D-3 ELF parse，超出 Phase 3.3 scope)
- `cuTexRefSetAddress2D` (3D addressing，复杂)
- `cuTexRefSetBorderColor` / `cuTexRefSetFilterMode` (CUDA Runtime API)
- `cudaCreate*` / `cudaDestroy*` (已由 Phase 1 CudaRuntimeApi 覆盖)

## Refs

- DRAFT plan: `docs/superpowers/plans/2026-07-05-umd-phase3.3-event-texture.md`
- Precondition: commit `869bd25` (Phase 1.7 close)
- AGENTS.md 2026-07-06 worktree convention
