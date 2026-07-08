# Design: phase3-3-event-texture-impl

> **Status**: 🔄 PROPOSED（2026-07-08, kicked off via worktree phase3-3-event-texture）
> **Phase**: 3.3 (2 sub-plans)
> **Scope**: Frontend-only shim improvements
> **Backend dependency**: None (CudaStub clock sufficient)

## §1. Background

DRAFT plan `docs/superpowers/plans/2026-07-05-umd-phase3.3-event-texture.md` (547 lines, 2026-07-05) 已写详细 design。本设计文档是 DRAFT plan 的**精炼 + 决策 lock**，供 openspec change ACTIVE 实施用。

### 当前 baseline (2026-07-08)

- shim REAL_IMPL: 113
- shim STUB: 45
- test_cuda_shim: 103 cases
- 全测试套件: 270/270 pass
- IGpuDriver: 47 methods (不变)
- Phase 1.7: ✅ close (commit 869bd25)

## §2. Architecture

```
Phase 3.3a (Week 1)             Phase 3.3b (Week 2-3)
       │                                │
       ▼                                ▼
  cu_event.cpp                    cu_array.cpp (new)
  (EventTable refactor)           cu_texref.cpp (new)
       │                                │
       ▼                                ▼
  test_event_timing.cpp           test_texture_surface.cpp
  (23 cases)                      (25 cases)
       │                                │
       └────────────┬───────────────────┘
                    ▼
        Worktree phase3-3-event-texture
        (branch phase3-3-event-texture)
                    │
                    ▼
            PR + cross-repo sync
                    │
                    ▼
        main + UsrLinuxEmu submodule bump
```

**Frontend-only**: 无 UsrLinuxEmu sim 层改动。Shim 内部 atomic + map + mutex 状态机（per cu_stream_capture / cu_graph / cu_mem_pool 既有 pattern）。

## §3. Phase 3.3a Design (Event timing precision)

### §3.1 Data Structure Refactor

```cpp
// Before (Phase 1.7 baseline, buggy)
struct EventTable {
  std::atomic<std::uint64_t> next_id{1};
  std::unordered_map<CUevent, std::chrono::steady_clock::time_point> created;
  std::mutex mu;
};

// After (Phase 3.3a)
struct EventRecord {
  unsigned int flags;
  std::chrono::steady_clock::time_point created_at;
  std::optional<std::chrono::steady_clock::time_point> recorded_at;  // nullopt until Record
  bool is_destroyed{false};
};
struct EventTable {
  std::atomic<std::uint64_t> next_id{1};
  std::unordered_map<CUevent, EventRecord> events;
  std::mutex mu;
};
```

### §3.2 Decision: cuEventCreate flag validation

- **Strict**: 拒绝 reserved bits → `CUDA_ERROR_INVALID_VALUE`
- Valid flags: `CU_EVENT_DEFAULT | CU_EVENT_BLOCKING_SYNC | CU_EVENT_DISABLE_TIMING | CU_EVENT_INTERPROCESS`
- (CUDA 12.x spec compliance)

### §3.3 Decision: cuEventRecord writes recorded_at (not created_at)

- Bug: 当前实现覆盖 created_at，导致 recorded 与 created 不可区分
- Fix: 写 recorded_at (optional<time_point>)
- Phase 3.3a: hStream no-op (stream 不影响时间, CUDA spec)

### §3.4 Decision: cuEventElapsedTime strict semantics

- 严格 (Phase 3.3a 决策): 未 record → `CUDA_ERROR_NOT_PERMITTED`
- 之前 (Phase 2 PoC): fallback to created_at (向后兼容)
- 决策: 严格更符合 CUDA spec 12.x

## §4. Phase 3.3b Design (Texture/Surface frontend)

### §4.1 Data Structures

```cpp
// cu_array.cpp
struct ArrayDescriptor {
  CUarray_format format;          // CU_AD_FORMAT_*
  unsigned int num_channels;      // 1, 2, 4
  size_t width, height, depth;    // 1D/2D/3D
  size_t pitch;                   // bytes per row
  size_t total_size_bytes;
  std::vector<std::uint8_t> backing;  // virtual backing (no real GPU alloc)
  bool is_destroyed{false};
};
struct ArrayTable {
  std::atomic<std::uint64_t> next_id{1};
  std::unordered_map<CUarray, std::unique_ptr<ArrayDescriptor>> arrays;
  std::mutex mu;
};

// cu_texref.cpp
struct TexRefRecord {
  unsigned long long address;     // bound BO address
  size_t address_offset;
  CUarray_format format;
  unsigned int num_channels;
  CUarray bound_array;            // optional
  unsigned int flags;
  bool is_destroyed{false};
};
struct TexRefTable {
  std::atomic<std::uint64_t> next_id{1};
  std::unordered_map<CUtexref, TexRefRecord> texrefs;
  std::mutex mu;
};
```

### §4.2 API Surface (11 cuTexRef*/cuArray*)

| API | Implementation | Notes |
|---|---|---|
| `cuArrayCreate` | B.2.1 | parse CUDA_ARRAY3_DESCRIPTOR → ArrayDescriptor |
| `cuArrayGetDescriptor` | B.2.2 | 返回创建时的 descriptor 字段 |
| `cuArrayDestroy` | B.2.3 | erase + mark destroyed |
| `cuTexRefCreate` | B.3.1 | atomic next_id alloc |
| `cuTexRefDestroy` | B.3.2 | erase + mark |
| `cuTexRefSetArray` | B.3.3 | 绑定 array handle |
| `cuTexRefSetAddress` | B.3.4 | 绑定 bo + offset |
| `cuTexRefSetFormat` | B.3.5 | set format + num_channels |
| `cuTexRefSetFlags` | B.3.6 | set flags |
| `cuTexRefGetAddress` | B.3.7 | 返回 address 字段 |
| `cuTexRefGetArray` | B.3.8 | 返回 bound_array |

**不需要实现** (deferred):
- `cuTexRefSetAddress2D` (3D addressing, 复杂)
- `cuTexRefSetBorderColor` / `cuTexRefSetFilterMode` (CUDA Runtime API, 不在 shim scope)
- 实际 GPU sampling (需 D-3 ELF parse)

### §4.3 Virtual backing memory

CUarray 分配虚拟 backing memory (vector<uint8_t>)，不实际 GPU 分配：
- `total_size_bytes = width * num_channels * sizeof(format) * (height || 1) * (depth || 1)`
- `backing.resize(total_size_bytes, 0)`
- Phase 3.3b 不实际渲染，仅占位

## §5. Test Strategy

### §5.1 Phase 3.3a (23 cases)

| Test Group | Count | Coverage |
|---|---|---|
| T-EVT-Basic-1 | 5 | create/destroy happy + null + double destroy |
| T-EVT-Flags-1 | 6 | 4 valid flags + reserved bits reject + interprocess |
| T-EVT-Record-1 | 4 | normal + secondary + invalid + destroyed |
| T-EVT-ElapsedTime-1 | 5 | happy + reverse + unrecord + null + invalid |
| T-EVT-Stub-Sanity | 3 | Query/Sync no-op + destroyed → INVALID |

### §5.2 Phase 3.3b (25 cases)

| Test Group | Count | Coverage |
|---|---|---|
| T-TEX-Basic-1 | 5 | create/destroy/null/double-destroy/array |
| T-TEX-SetAddress | 4 | null + valid + offset + null ptr |
| T-TEX-SetFormat | 3 | valid + invalid format |
| T-TEX-GetAddress | 3 | unbind + bind + null |
| T-TEX-SetArray | 4 | null + valid + destroyed + null array |
| T-ARRAY-GetDescriptor | 3 | unbind + bind + null |
| T-ARRAY-Lifecycle | 3 | create + alloc + destroy + get |

### §5.3 Sanitizer Validation

每个 sub-plan 完成后:
```bash
cmake -B build_asan -DSANITIZER_ADDRESS=ON && cmake --build build_asan -j4 && ctest -j4
cmake -B build_ubsan -DSANITIZER_UNDEFINED=ON && cmake --build build_ubsan -j4 && ctest -j4
cmake -B build_tsan -DSANITIZER_THREAD=ON && cmake --build build_tsan -j4 && ctest -j4
```

Expected: 0 sanitizer errors.

## §6. Files Affected

### Modify
- `src/umd/libcuda_shim/cu_event.cpp` (Phase 3.3a)
- `include/cuda.h` (Phase 3.3a: add cuEventRecordWithFlags)
- `tools/generate_cu_stubs.py` (Phase 3.3a + 3.3b: 12 API STUB→REAL_IMPL)
- `cmake/UMDEvolution.cmake` (Phase 3.3b: register 2 new .cpp)
- `docs/umd-evolution/architecture/runtime-layering.md` (Phase 3.3b: Texture section)
- `openspec/changes/phase3-3-event-texture-impl/{proposal,tasks,design}.md` (kickoff)

### New
- `src/umd/libcuda_shim/cu_array.cpp` (Phase 3.3b, ~150-200 lines)
- `src/umd/libcuda_shim/cu_texref.cpp` (Phase 3.3b, ~200-300 lines)
- `tests/umd/test_event_timing.cpp` (Phase 3.3a, ~300 lines)
- `tests/umd/test_texture_surface.cpp` (Phase 3.3b, ~300 lines)

## §7. Cross-Repo Sync (Phase 3.3b 完成后)

1. Worktree phase3-3-event-texture: PR → TaskRunner main merge
2. UsrLinuxEmu: submodule bump to TaskRunner HEAD
3. UsrLinuxEmu 端: `make -j4 && ./bin/test_gpu_plugin` 验证

## §8. Refs

- DRAFT plan: `docs/superpowers/plans/2026-07-05-umd-phase3.3-event-texture.md`
- Precondition: commit `869bd25` (Phase 1.7 close)
- Worktree: `.rddf/wt/phase3-3-event-texture/`
- AGENTS.md 2026-07-06 worktree convention
- TADR-301 (IGpuDriver 47 methods, 不变)
