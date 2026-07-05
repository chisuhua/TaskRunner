# Design: phase17-wait-period-work

> **Change 类型**: 4 个独立 workstream（A 代码实现 / B CI 基础设施 / C 测试覆盖 / D 文档）
> **依赖**: commit d988393（Phase 1.6）✅
> **Scope**: umd-evolution + build infrastructure

## 数据流（Workstream A 的 STUB → REAL_IMPL 映射）

```
Workstream A 实施的 14 个 STUB 在 cu_stub_table.inc 中的变化:

cuProfilerStart        → cu_mem.cpp  (state flag)
cuProfilerStop         → cu_mem.cpp  (state flag)
cuProfilerInitialize   → cu_mem.cpp  (state flag)
cuFuncGetAttribute     → cu_module.cpp (static query from CUfunction handle)
cuFuncSetAttribute     → cu_module.cpp (store and return)
cuFuncSetCacheConfig   → cu_module.cpp (store and return)
cuFuncGetModule        → cu_module.cpp (function→module reverse lookup)
cuOccupancyMaxActiveBlocksPerMultiprocessor          → cu_module.cpp (heuristic)
cuOccupancyMaxActiveBlocksPerMultiprocessorWithFlags → cu_module.cpp (heuristic)
cuOccupancyMaxPotentialBlockSize                     → cu_module.cpp (heuristic)
cuPointerGetAttribute  → cu_mem.cpp  (new BO ptr→info map in shim layer)
cuStreamCreateWithFlags → cu_stream.cpp (delegate to cuStreamCreate)
cuStreamGetCaptureInfo → cu_stream.cpp (state: not capturing)
cuEventCreateWithFlags → cu_event.cpp (delegate to cuEventCreate)
cuMemsetD16            → cu_mem.cpp  (delegate to cuMemsetD32, 8→16 bit)
```

## 关键决策

### D1: Workstream A 独立实施，不创建跨仓依赖

**理由**: 14 个 API 全部仅需 TaskRunner 侧 bookkeeping：
- `cuFuncGetAttribute/Set` — 需 `CUfunction` 对象新增 `std::unordered_map` 属性存储
- `cuPointerGetAttribute` — 需 shim 层新增 `std::unordered_map<CUdeviceptr, ...>` 跟踪已分配指针
- `cuOccupancyMax*` — 纯启发式计算（硬件 warp size = 32，shared mem = 48KB，regs = 32）
- `cuProfiler*` — 单 bool flag
- 变体 API — 直接 delegate 到已有实现

### D2: cuPointerGetAttribute 的 BO 映射表位置

**选 A**: shim 层全局 map（`cu_mem.cpp` 中加 static map，lock 保护）
**选 B**: CudaScheduler 级映射（需跨层接口）
**选 C**: 仅返回 `CUDA_ERROR_NOT_IMPLEMENTED`（不改，留到 Phase 3）

**决策**: **选 A**（shim 层全局 map + mutex）。简单、隔离、不跨层。

### D3: CI 工作流策略

**选项 A**：GitHub Actions（push/PR trigger）
**选项 B**：GitLab CI
**选项 C**：本地 pre-commit 仅

**决策**: **选 A + C 组合**。GitHub Actions 是工业标准；pre-commit 防止本地未测试代码被 push。

### D4: sanitizer 构建模式

**选项 A**：`cmake -DSANITIZER=address` 仅 ASan
**选项 B**：`cmake -DSANITIZER=address,undefined,thread` 三合一
**选项 C**：不添加（保持现状）

**决策**: **选 B 变体**（三个独立 option，而非一个三合一选项）。

注意：ASan 与 TSan **不能同时编译**（LLVM 限制：`-fsanitize=address` 和 `-fsanitize=thread` 冲突）。CMake 需加互斥检查。

- ASan + UBSan：可以组合（推荐 CI 使用）
- TSan：单独使用（shim 多线程场景有价值）
- ASan 只需编译器 flag，不需 linker flag（跳过 `.so`）；TSan 需同时设 linker flag

ASan 捕获内存错误，UBSan 捕获未定义行为，TSan 捕获数据竞争。shim 是多线程环境（cuStream/cuEvent 用 mutex），TSan 特别有价值。

### D5: 测试优先级（Workstream C）

按 ROI 排序：
1. Threading tests（最高风险，当前 0 个）— 多线程 cuCtx/cuStream 并发
2. cuDeviceGetAttribute 全属性（80+ values）— 当前仅 6
3. OOM 模拟 — malloc = NULL → cuMemAlloc 返回 OUT_OF_MEMORY
4. 错误路径 — NULL 指针、非法 handle、cuModuleLoad 失败
5. cuStream/cuEvent 重复 destroy（double-free 风险）

## 验证矩阵

| Workstream | 验证 | 预期 |
|-----------|------|------|
| A | `grep -c "^// STUB" cu_stub_table.inc` | ≤54（原 68） |
| A | `grep -c "^// REAL_IMPL" cu_stub_table.inc` | ≥90（原 76） |
| A | `./build/test_cuda_shim` | ≥60 PASS（原 49，+11） |
| B | `cmake .. -DSANITIZER=address && make -j4 && ./build/test_cuda_shim` | ASan 0 errors |
| B | `.github/workflows/shim.yml` 存在且语法有效 | `yamllint` pass |
| C | `./build/test_cuda_shim` | ≥70 PASS（原 49，+21） |
| D | `ls docs/superpowers/specs/*-phase3-stream-design.md` | 存在 |
| D | `ls docs/superpowers/specs/*-phase3-mempool-design.md` | 存在 |
| D | `ls docs/superpowers/architecture/shim-layer.md` | 存在 |
| All | `nm -D --defined-only build/libcuda_taskrunner.so | grep -c " cu[A-Z]"` | 79（不变） |
| All | `./tools/docs-audit.sh` | 53 PASS / 1 FAIL（实际基线） |