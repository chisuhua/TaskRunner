# Change: phase16-cuModuleLoad-demote

> **状态**: 🔄 PROPOSED（2026-07-06）
> **来源**: 拆分自 `archive/2026-07-02-phase16-shim-extension/`（A.2 workstream，4/5 workstreams 已由 phase17-wait-period-work 完成）
> **范围**: 极简，仅 A.2（cuModuleLoad* demote，~30 min 工时）

## Why

### 现状

`tools/generate_cu_stubs.py` 的 `CRITICAL_APIS_IMPL_REQUIRED` 字典当前包含 91 项，其中 3 项是**假 REAL_IMPL**：

```python
"cuModuleLoadData": "cu_module.cpp",
"cuModuleLoadDataEx": "cu_module.cpp",
"cuModuleLoadFatBinary": "cu_module.cpp",
```

但 `cu_module.cpp` 中这 3 个函数实际只返回 `CUDA_ERROR_NOT_IMPLEMENTED`（已 grep 验证）。`tools/generate_cu_stubs.py` 据此生成 `cu_stub_table.inc` 时把它们标为 `// REAL_IMPL in cu_module.cpp`，与实际行为不一致。

### 假 REAL_IMPL 的危害

1. **docs-audit.sh Check 9** 误判这些 API 已实现 → 后续重构 cu_module.cpp 时行为变化不会被发现
2. **误导开发人员**：以为有真实实现可直接调用
3. **cu_stub_table.inc** 不准确 → 跨仓交叉引用时产生错误信息

### Why Now

1. Phase 17 wait-period-work 已将 76→91 REAL_IMPL（含这 3 个假项），同时带来 103 个 E2E 测试覆盖
2. **A.2 是 phase16 唯一未完成的工作**（A.1/A.3/H-3/Phase 3 skeleton 已由 phase17-wait-period-work 完成）
3. 估时 30 min，零风险，可立即清理
4. 不依赖 UsrLinuxEmu Stage 1.0-1.3，独立可推

## What Changes

### A.2: Demote 3 cuModuleLoad APIs to STUB (30 min)

**修改文件 1**: `tools/generate_cu_stubs.py`

**Before**（当前 ~91 项）：
```python
CRITICAL_APIS_IMPL_REQUIRED = {
    "cuModuleLoad": "cu_module.cpp",
    "cuModuleLoadData": "cu_module.cpp",        # ← 移除
    "cuModuleLoadDataEx": "cu_module.cpp",      # ← 移除
    "cuModuleLoadFatBinary": "cu_module.cpp",   # ← 移除
    "cuModuleUnload": "cu_module.cpp",
    ...
}
```

**After**（88 项）：
```python
CRITICAL_APIS_IMPL_REQUIRED = {
    "cuModuleLoad": "cu_module.cpp",
    "cuModuleUnload": "cu_module.cpp",
    ...
}
```

**修改文件 2**: `src/umd/libcuda_shim/cu_stub_table.inc`（自动重新生成）

```bash
cd /workspace/project/UsrLinuxEmu/external/TaskRunner
python3 tools/generate_cu_stubs.py
git diff src/umd/libcuda_shim/cu_stub_table.inc  # 检查变更
```

预期差异：
- `cuModuleLoadData` / `cuModuleLoadDataEx` / `cuModuleLoadFatBinary` 从 `// REAL_IMPL in cu_module.cpp` 改为 `// STUB`
- 注释 `// cuModuleLoadData / cuModuleLoadDataEx / cuModuleLoadFatBinary intentionally` 更新（去除这些行）

### Long-term Followup（不在本 change scope）

`cuModuleLoadData*` 真实实现需要 D-3 lite ELF/CUBIN parser（per phase16-shim-extension A.2 rationale）。这在 UsrLinuxEmu 仓尚未成熟，本 change 不涉及。

## Capabilities

### Modified Capabilities

- **`umd-evolution`**: CRITICAL_APIS_IMPL_REQUIRED 字典反映真实状态（91 → 88 REAL_IMPL，53 → 56 STUB）

## Impact

| 影响项 | 范围 | 风险 |
|--------|------|------|
| 代码 | `tools/generate_cu_stubs.py`（3 行删除）+ 重新生成 `cu_stub_table.inc` | 极低 |
| ABI | 0 变化（cuModuleLoadData/DataEx/FatBinary 仍然导出符号，行为不变） | 零 |
| 测试 | 76+ 回归测试不变 | 零 |
| docs-audit | 3 个 false-positive 消除 | 极低 |
| 跨仓 | 无 | 零 |

**风险缓解**：
- ABI 零变化（只是 cu_stub_table.inc 注释调整）
- cuModuleLoadData/DataEx/FatBinary 行为不变（已返回 NOT_IMPLEMENTED）
- cuModuleLoad（不带 Data）保留为 REAL_IMPL（无回归）
- 76+ 测试基线全过

## Tasks

> 极简 task 列表（~30 min 总实施时间）

- [ ] **T1** 编辑 `tools/generate_cu_stubs.py`：删除 `cuModuleLoadData` / `cuModuleLoadDataEx` / `cuModuleLoadFatBinary` 三行
- [ ] **T2** 重新生成 `cu_stub_table.inc`：
  ```bash
  cd /workspace/project/UsrLinuxEmu/external/TaskRunner
  python3 tools/generate_cu_stubs.py
  ```
- [ ] **T3** 验证：
  ```bash
  git diff src/umd/libcuda_shim/cu_stub_table.inc  # 检查 3 行变化
  grep -c "// REAL_IMPL in" src/umd/libcuda_shim/cu_stub_table.inc  # 期望 88 (原 91 - 3)
  cmake --build build --target taskrunner_umd_stub  # 无 warning
  for t in build/test_cuda_shim build/test_cuda_runtime_api; do
    ./$t 2>&1 | tail -1  # 期望 PASS
  done
  tools/docs-audit.sh --strict  # 期望 exit 0
  ```
- [ ] **T4** 提交 commit：
  ```bash
  git add tools/generate_cu_stubs.py src/umd/libcuda_shim/cu_stub_table.inc
  git commit -m "tools(stubs): demote cuModuleLoadData/DataEx/FatBinary to STUB (A.2 phase16)
  
  Removes 3 fake-REAL_IMPL entries from CRITICAL_APIS_IMPL_REQUIRED dict.
  These APIs currently return CUDA_ERROR_NOT_IMPLEMENTED but were marked
  as REAL_IMPL, misleading docs-audit and developers.
  
  Changes:
  - tools/generate_cu_stubs.py: -3 dict entries
  - src/umd/libcuda_shim/cu_stub_table.inc: regenerated (91 → 88 REAL_IMPL, 53 → 56 STUB)
  
  ABI zero-change. 76+ regression tests pass. docs-audit --strict exit 0.
  
  Refs: openspec/changes/archive/2026-07-02-phase16-shim-extension/ (A.2 remaining)
  Refs: openspec/changes/2026-07-06-phase16-cuModuleLoad-demote/ (this change)"
  ```

## 验收准则（Definition of Done）

- [ ] T1: 3 行从 `CRITICAL_APIS_IMPL_REQUIRED` 删除
- [ ] T2: `cu_stub_table.inc` 重新生成，3 个 cu*Data* 注释从 REAL_IMPL 改为 STUB
- [ ] T3: 全部验证通过（build 无 warning + 76+ tests PASS + docs-audit exit 0）
- [ ] T4: commit + push 成功
- [ ] REAL_IMPL count: 91 → 88（grep 验证）
- [ ] STUB count: 53 → 56（grep 验证）

## 关联 Changes

### TaskRunner 侧
- **来源**（superseded）: `openspec/changes/archive/2026-07-02-phase16-shim-extension/`（A.2 workstream 拆出）
- **独立 parallel**: `openspec/changes/2026-07-06-phase3-step3-shim-and-forwarding/` (Step 3 of Phase 3 coordination)

### 决策编号（TaskRunner 系统）

- **D-UMD-1**: cuModuleLoadData/DataEx/FatBinary demote to STUB（fake-REAL_IMPL 清理，phase16 A.2 后续）