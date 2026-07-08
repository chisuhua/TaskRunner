# Tasks: phase3-3-event-texture-impl

> **Status**: 🔄 PROPOSED（2026-07-08, kicked off via worktree phase3-3-event-texture）
> **Type**: 2 sub-plan implementation (3.3a 1w + 3.3b 2w)
> **Total tasks**: 10 (5 per sub-plan)
> **Total tests**: 48 new (23 + 25)
> **Worktree**: .rddf/wt/phase3-3-event-texture

## Phase 3.3a — Event timing precision (Week 1)

### A.1: 重构 EventTable (EventRecord struct)

**Files:**
- Modify: `src/umd/libcuda_shim/cu_event.cpp`

- [ ] A.1.1 添加 EventRecord struct（含 created_at, recorded_at optional, flags, is_destroyed）
- [ ] A.1.2 EventTable 改用 `unordered_map<CUevent, EventRecord>`
- [ ] A.1.3 保留 g_events 静态对象接口

### A.2: cuEventCreate fix (flag validation)

**Files:**
- Modify: `src/umd/libcuda_shim/cu_event.cpp`

- [ ] A.2.1 添加 `kValidFlags = DEFAULT | BLOCKING_SYNC | DISABLE_TIMING | INTERPROCESS`
- [ ] A.2.2 拒绝 reserved bits set → `CUDA_ERROR_INVALID_VALUE`
- [ ] A.2.3 保存 flags 到 EventRecord

### A.3: cuEventRecord fix (write to recorded_at, not created_at)

**Files:**
- Modify: `src/umd/libcuda_shim/cu_event.cpp`

- [ ] A.3.1 检查 handle 存在 + !is_destroyed
- [ ] A.3.2 写 `it->second.recorded_at = now()` (而非 created_at)
- [ ] A.3.3 hStream no-op (Phase 3.3a 决策)

### A.4: cuEventElapsedTime fix (strict semantics)

**Files:**
- Modify: `src/umd/libcuda_shim/cu_event.cpp`

- [ ] A.4.1 校验两个 event 都已 record (recorded_at has_value)
- [ ] A.4.2 未 record → `CUDA_ERROR_NOT_PERMITTED` (Phase 3.3a 严格决策)
- [ ] A.4.3 计算 diff (microseconds → float ms)
- [ ] A.4.4 clamp to 0 if negative

### A.5: Tests (23 cases) + docs audit

**Files:**
- New: `tests/umd/test_event_timing.cpp`
- Modify: `cmake/UMDEvolution.cmake`
- Modify: `tools/generate_cu_stubs.py`

- [ ] A.5.1 T-EVT-Basic-1 (5 cases): create/destroy/null/double-destroy
- [ ] A.5.2 T-EVT-Flags-1 (6 cases): valid flags + reserved bits reject
- [ ] A.5.3 T-EVT-Record-1 (4 cases): record normal + secondary + invalid + destroyed
- [ ] A.5.4 T-EVT-ElapsedTime-1 (5 cases): happy + reverse + unrecord + null + invalid
- [ ] A.5.5 T-EVT-Stub-Sanity (3 cases): EventQuery/Sync no-op + destroyed → INVALID
- [ ] A.5.6 注册到 cmake/UMDEvolution.cmake
- [ ] A.5.7 修 docs-audit.sh 7 false-positive FAIL (cuEvent* 升 REAL_IMPL)
- [ ] A.5.8 build + ctest → 23/23 pass, 0 regression (270 → 293)
- [ ] A.5.9 commit + push to phase3-3-event-texture branch + PR

## Phase 3.3b — Texture/Surface frontend (Week 2-3)

### B.1: 数据结构设计 (ArrayDescriptor, TexRefTable)

**Files:**
- New: `src/umd/libcuda_shim/cu_array.cpp`
- New: `src/umd/libcuda_shim/cu_texref.cpp`

- [ ] B.1.1 ArrayDescriptor struct (format, num_channels, width/height/depth, pitch, total_size, backing vector, is_destroyed)
- [ ] B.1.2 ArrayTable (atomic next_id, unordered_map<CUarray, unique_ptr<ArrayDescriptor>>, mutex)
- [ ] B.1.3 TexRefTable (atomic next_id, unordered_map<CUtexref, TexRefRecord>, mutex)
- [ ] B.1.4 TexRefRecord struct (flags, address, format_desc, array, is_destroyed)

### B.2: cuArray* 3 API REAL_IMPL

**Files:**
- New: `src/umd/libcuda_shim/cu_array.cpp`

- [ ] B.2.1 cuArrayCreate (CU_ARRAY_CREATE_INFO parsing → ArrayDescriptor)
- [ ] B.2.2 cuArrayGetDescriptor (返回创建时 descriptor)
- [ ] B.2.3 cuArrayDestroy (erase from map)

### B.3: cuTexRef* 8 API REAL_IMPL

**Files:**
- New: `src/umd/libcuda_shim/cu_texref.cpp`

- [ ] B.3.1 cuTexRefCreate (handle alloc)
- [ ] B.3.2 cuTexRefDestroy (erase from map)
- [ ] B.3.3 cuTexRefSetArray (绑定 CUarray)
- [ ] B.3.4 cuTexRefSetAddress (绑定 bo + offset)
- [ ] B.3.5 cuTexRefSetFormat (set format descriptor)
- [ ] B.3.6 cuTexRefSetFlags (set flags)
- [ ] B.3.7 cuTexRefGetAddress (返回 address)
- [ ] B.3.8 cuTexRefGetArray (返回 array)

### B.4: tests/generate_cu_stubs.py + cmake

**Files:**
- Modify: `tools/generate_cu_stubs.py` (11 API 升 REAL_IMPL)
- Modify: `cmake/UMDEvolution.cmake` (注册 2 个新 .cpp)

- [ ] B.4.1 11 API 从 STUB 移到 REAL_IMPL
- [ ] B.4.2 cmake register cu_array.cpp + cu_texref.cpp

### B.5: Tests (25 cases) + docs

**Files:**
- New: `tests/umd/test_texture_surface.cpp`
- Modify: `docs/umd-evolution/architecture/runtime-layering.md`

- [ ] B.5.1 T-TEX-Basic-1 (5 cases): create/destroy/null/double-destroy/array
- [ ] B.5.2 T-TEX-SetAddress (4 cases): null + valid + offset
- [ ] B.5.3 T-TEX-SetFormat (3 cases): valid + invalid
- [ ] B.5.4 T-TEX-GetAddress (3 cases): unbind + bind + null
- [ ] B.5.5 T-TEX-SetArray (4 cases): null + valid + destroyed + null array
- [ ] B.5.6 T-ARRAY-GetDescriptor (3 cases): unbind + bind + null
- [ ] B.5.7 T-ARRAY-Lifecycle (3 cases): create + alloc + destroy + get
- [ ] B.5.8 docs: runtime-layering.md 添加 Texture/Surface 段落
- [ ] B.5.9 build + ctest → 25/25 pass, 0 regression (293 → 318)
- [ ] B.5.10 commit + push + PR + cross-repo UsrLinuxEmu submodule bump

## Acceptance Criteria

- [ ] 48 new tests pass (23 + 25)
- [ ] 0 regression (270 → 318)
- [ ] ASan + UBSan + TSan clean
- [ ] docs-audit.sh: 7 false-positive FAIL 修复
- [ ] REAL_IMPL: 113 → 125
- [ ] STUB: 45 → 33
- [ ] nm libcuda_taskrunner.so: 158 → 170 exported symbols
- [ ] UsrLinuxEmu submodule bump: TaskRunner HEAD 同步
- [ ] Worktree branch phase3-3-event-texture 合并到 main
- [ ] 旧 archive/2026-07-02-phase17-test-coverage-completion/ 不变 (legacy record)
