---
SCOPE: UMD-EVOLUTION
STATUS: ACTIVE
LAST_UPDATED: 2026-08-20 (R2 revision per Oracle session ses_fdffa6689ffeQ0vsWd06LuQgbg)
HEAD_COMMIT: c66b4d0 (TaskRunner, 7 atomic commits) + 25d3fec (UsrLinuxEmu mirror + submodule bump)
SOURCE_CHANGE: openspec/changes/2026-08-18-tadr-308-igpu-driver-vram-load
SOURCE_TADR: docs/shared/adr/tadr-308-igpu-driver-vram-load.md (🔄 Proposed)
SOURCE_TADR_STALE: docs/shared/adr/tadr-307-igpu-driver-kernel-module-extension.md (🚫 STALE)
ORACLE_SESSION_R1: ses_fe0443831ffenUxpEQxZqWE8Cp (2026-08-20 A' 决策)
ORACLE_SESSION_R2: ses_fdffa6689ffeQ0vsWd06LuQgbg (2026-08-20 验证 + REVISE further verdict)
METIS_SESSION: ses_fe0048690ffeOIUPxgOHrfUI1s (2026-08-20 发现 3 CRITICAL 架构错误)
RELATED_PHASE: UMD-EVOLUTION Phase 5 (tadr-308 是 Phase 5 的 IGpuDriver 扩展核心)
---

# TADR-308 实施路线图（专项，v2 修订版）

> **本路线图是 v2 修订版**（per Oracle R2 verdict: REVISE further）。
>
> **R1 → R2 关键变化**：
> - T-013 G1 v1 → **v2**: 改用 `gpu_gpfifo_entry`（`method=0x10C` + 56B payload + kernel_name 通过 host pointer）
> - T-003 G2 v1（reverse lookup）→ **v2**（ioctl 0x29 路径）→ **v3**（新增 append-only `IGpuDriver::unload_kernel_module`，因 0x29 handler 当前是 -ENOSYS stub）
> - 新增 **T-000d**（CudaRuntimeApi 扩 `unload_kernel_module`）+ **T-000e**（CudaRuntimeApi 扩 `dispatch_kernel` typed wrapper）
> - 新增 **Phase 0 BLOCKING GATE**（6 项 UsrLinuxEmu owner 协调，2 项 HARD blocker）
> - 总工时从 26-30h 上调至 **37-52h 工程 + 不定 calendar time 等待 UsrLinuxEmu owner**

---

## TL;DR

**当前状态**：**设计阶段 v2 完成，待 Phase 0 BLOCKING GATE 通过后才能进入实施**。

- 7 个 atomic commit 已落地（6 在 TaskRunner，1 在 UsrLinuxEmu mirror + submodule bump）
- 5 项 owner 决策已落地（C1/C2/C5/M11/M8/MF-2/MF-4 全部解决）
- Oracle R1 A' 决策已采纳（kernel_name 应用传入 + image_size 24B header 推断）
- Oracle R2 + Metis 共同揭示 **3 个 CRITICAL 架构错误** + **6 项跨仓协调需求**
- **待 owner**: 6 项跨仓协调（**2 项 HARD blocker**：ioctl 0x29 handler 实现 + Mode A consumer 决策）+ push 到 origin + 召集 dual-ack

**Phase 0 BLOCKING GATE**：6 项 UsrLinuxEmu owner 协调必须先完成（特别是 #2 ioctl 0x29 handler 和 #3 Mode A consumer）。没有这些实施无法启动。

---

## 已落地 commit 清单（设计阶段 v1 + v2 修订）

### TaskRunner 仓（HEAD = c66b4d0）

| # | Commit | 描述 |
|---|---|---|
| 1 | `fadcc7d` | docs(tadr-307): slim STALE body to 30-line historical summary + redirect（per ADR-035 §R2.4）|
| 2 | `83abae0` | docs(tadr-308): §A fix real 24B PTXIR format + §1.5 A′ opt-in validate + §1.2 CUmodule=VA redefine |
| 3 | `de94c48` | docs(tadr-308): §References cite ADR-090 v2 (canonical) + ADR-0023/0028 + fix v1→v2 references |
| 4 | `fb521c9` | docs(tadr-308): M1 narrow F4 + Gate #22-#24 + Cross-Sync v2 update |
| 5 | `84ec36d` | docs(openspec/tadr-308): T-000c decision + T-011 §A 24B + T-003 G2 + T-009b + T-010 DROP + T-013 NEW (G1) + T-014 placeholder |
| 6 | `40ff0bb` | docs(shared/adr/README): tadr-308 描述修订 + tadr-307 状态更新 + 最后更新时间戳 |
| 7 | `c66b4d0` | docs(roadmap/tadr-308): 添加 TADR-308 实施专项路线图 v1 |

### UsrLinuxEmu 仓（HEAD = 25d3fec）

| # | Commit | 描述 |
|---|---|---|
| 8 | `25d3fec` | docs(adr-090-mirror): tadr-307 STALE slimmed + tadr-308 行新增 + submodule bump (40ff0bb) |

---

## Phase 0: BLOCKING GATE（**必须先完成**才能实施，~1-2w calendar）

### 6 项 UsrLinuxEmu owner 协调（per Oracle R2 Part D）

| # | 项目 | 类型 | 阻塞 |
|---|------|------|------|
| **1** | **定义 `GPU_OP_DISPATCH_KERNEL = 0x10C`** in `plugins/gpu_driver/shared/gpu_types.h` + payload layout contract comment | 共享 ABI（dual-ack per ADR-036）| 🔴 HARD for T-013 |
| **2** | **实现 ioctl 0x29 handler** for real：当前 `user_kernel_module_unload` 返 `-ENOSYS`（per `hal_user.cpp:722-726`），需 fold 到 `hal_mem_free(module_handle)` (~1 line)。当前 `handleFreeBo` 验证 `handles_.valid(handle)` 而 code BO 不在 handles 表内，无法复用 FREE_BO | 父仓 commit | 🔴 HARD for T-003 |
| **3** | **决策 Mode A interim consumer for 0x10C**：选项 (a) puller case returning success-stub, (b) extend `translateLaunch`, (c) 声明 real-path e2e 出 PoC 范围（mock-only）等待 CppTLM Mode B | 父仓架构决策 | 🔴 HARD for T-013 e2e |
| **4** | **Payload layout 写入 HSK-6 joint protocol**，确保 CppTLM Mode B SQ parsing（`ptxemu_image_execute_named` 调用点）使用相同 layout | 跨仓 HSK-6 协议 | 🟡 SOFT for T-013 |
| **5** | **kernel_name host-pointer 约定 ack**：user-mode emulation 内合法（CppTLM Mode B 同进程），不可移植到真 KMD，per ADR-001 可接受 | 文档 ack | 🟡 SOFT |
| **6** | **文档化 code BO bypass**：`bo_map_`/`handles_` 表（FREE_BO 不接收 code BO）。**FREE_BO vs 0x29 不对称** 目前隐式 | 文档 | 🟡 SOFT |

### Phase 0 触发机制

- **发起 issue 到 UsrLinuxEmu 仓**：参考 [TaskRunner #10](https://github.com/chisuhua/TaskRunner/issues/10) 模式
- **跨仓 owner 会议**：召集 UsrLinuxEmu + PTX-EMU + CppTLM maintainers
- **预期 calendar time**：1-2 周（项目 #2 + #3 涉及 owner 决策）
- **完成标志**：6 项全部 ✅ + UsrLinuxEmu commits 落地（最关键：#2 ioctl 0x29 实现 + #3 Mode A consumer 决策）

### Phase 0 阻塞期间可并行的 TaskRunner 工作

- ✅ **T-009b** PTX-EMU fixture 验证（不依赖 UsrLinuxEmu）
- ✅ **T-000a/b** CudaRuntimeApi 扩 `load_kernel_module`（已含 T-000a）
- ✅ **T-001** IGpuDriver 默认 -ENOSYS
- ✅ **T-005** 错误注入测试
- ✅ **T-007** openspec change 归档（最后阶段）
- ✅ **T-009** 父仓契约核验（文档级）

### Phase 0 阻塞期间 BLOCKED 的 TaskRunner 工作

- ❌ **T-002/T-003**（依赖 ioctl 0x27/0x29 真实工作）
- ❌ **T-004/T-005b/T-008**（依赖 T-002/T-003）
- ❌ **T-011 §B** `cuModuleLoad` 改走 VRAM-load（依赖 T-002 + 父仓契约）
- ❌ **T-013** DISPATCH_KERNEL packet（依赖 Phase 0 #1 + #3 + #4）
- ❌ **T-014** `unload_kernel_module`（依赖 Phase 0 #2）

---

## 阶段一：前置准备（owner 必做，~2h）

### 1.1 Push 到 origin

| 仓 | 状态 | 操作 |
|---|---|---|
| TaskRunner | 7 commits 本地，未 push | `git push origin main` (HEAD = `c66b4d0`) |
| UsrLinuxEmu | 1 commit 本地，未 push | `git push origin main` (HEAD = `25d3fec`) |

**owner 决策 push 时机**：
- 建议**先 TaskRunner push，等 CI（lsp_diagnostics + ctest）+ 1-2 天 review window** → 再 UsrLinuxEmu push
- 或**两仓同时 push**（如果 reviewer 已知情）

### 1.2 修复 docs-audit 跨仓 FAIL

**失败 detection**：`docs/07-integration/taskrunner-index.md` 中 tadr-307 引用需更新（per Commit 8 跳过 docs-audit）

**修复路径**：
```bash
# 1. 找到所有 tadr-307 引用点
grep -rn "tadr-307" /workspace/project/UsrLinuxEmu/

# 2. 替换描述 "3 方法方案" → "STALE 历史摘要"

# 3. 重新验证
cd /workspace/project/UsrLinuxEmu && tools/docs-audit.sh --strict
# 期望: 70/70 PASS
```

### 1.3 shared scope dual-ack 召集

按 `docs/shared/README.md` policy（**dual review required**）：
- ✅ 1 名 test-fixture scope maintainer
- ✅ 1 名 umd-evolution scope maintainer（或其指定人）
- ⚠️ 涉及 ABI 契约变更需通知 UsrLinuxEmu maintainer（**T-013 G1 v2 + T-014 `unload_kernel_module` 都触发**）

**建议时间**：Phase 0 BLOCKING GATE 期间（1-2 周）通知，让 maintainer review tadr-308 v2 + tasks.md v2。

### 1.4 启动 Phase 0 跨仓协调（per Oracle R2 Part D 6 项）

**立即动作**：
1. **发起 issue 到 UsrLinuxEmu 仓**（参考 [TaskRunner #10](https://github.com/chisuhua/TaskRunner/issues/10) 模式）
2. **请求 UsrLinuxEmu owner 评审 6 项**（特别是 #2 ioctl 0x29 handler + #3 Mode A consumer 决策）
3. **跨仓 owner 会议**：召集 UsrLinuxEmu + PTX-EMU + CppTLM maintainers

---

## 阶段二：实施（依赖 Phase 0 完成，~37-52h 工程 + 不定 calendar）

### 2.1 任务依赖图（v2）

```
Phase 0 BLOCKING GATE 完成 (6 项 UsrLinuxEmu 协调)
   │
   ├─→ T-009b (PTX-EMU fixture 验证, 24B format 验证)
   │
   ├─→ T-000a (CudaRuntimeApi::load_kernel_module) [parallel with Phase 0]
   ├─→ T-000b (cuda_error_from_errno helper) [parallel]
   ├─→ T-000d (CudaRuntimeApi::unload_kernel_module, NEW per R2)
   ├─→ T-000e (CudaRuntimeApi::dispatch_kernel typed wrapper, NEW per R2)
   │
   ├─→ T-001 (IGpuDriver 默认 -ENOSYS)
   ├─→ T-014 (IGpuDriver::unload_kernel_module append-only, NEW per R2)
   │
   ├─→ T-004 (GpuDriverClient::load_kernel_module 并发安全)
   ├─→ T-002 (cuModuleLoadData 接入)
   ├─→ T-003 (cuModuleUnload → unload_kernel_module via T-000d)
   ├─→ T-005 (错误注入)
   ├─→ T-005b (cuModuleUnload 清理 func_to_attrs/func_to_module)
   ├─→ T-008 (func_to_attrs/func_to_module 测试覆盖)
   ├─→ T-011 §B (cuModuleLoad VRAM-load + 独立 next_func_id 计数器)
   │
   └─→ T-013 (DISPATCH_KERNEL packet payload, G1 v2: 0x10C + 56B payload + kernel_name host pointer)
        │
        └─→ T-007 (openspec change 归档, 最后阶段)
```

### 2.2 任务清单（v2，按依赖顺序）

| Task | 描述 | 估计工时 | 依赖 | 风险 |
|---|---|---|---|---|
| **T-009b** | 用 PTX-EMU `tests/ptxir/fixtures/{multi_kernel_basic,cute_rmsnorm}.ptxir}` 验证 24B header 格式 + string_table_ tail 推断 + MANIFEST parse | 2-3h | 无前置（与 Phase 0 并行）| 低 |
| **T-000a** | `CudaRuntimeApi` 扩 `load_kernel_module` 方法（Gate #7 HARD）| 1h | 无前置 | 低 |
| **T-000b** | `cuda_error_from_errno` helper 定义（Gate #8 HARD）| 1h | 无前置 | 低 |
| **T-000d** NEW | `CudaRuntimeApi::unload_kernel_module(vram_addr)`（per Oracle R2, 替代 free_bo reverse lookup）| 1h | Phase 0 #2 (ioctl 0x29 实现) | 中 |
| **T-000e** NEW | `CudaRuntimeApi::dispatch_kernel(...)` typed wrapper（per Oracle R2 Part C, 避免 raw submit_batch pass-through）| 2h | Phase 0 #1 (`GPU_OP_DISPATCH_KERNEL = 0x10C` 定义) | 中 |
| **T-001** | `IGpuDriver::load_kernel_module` 默认 -ENOSYS（TDD 5 步）| 2h | 无前置 | 低 |
| **T-014** 升级 | `IGpuDriver::unload_kernel_module(vram_addr)` 默认 -ENOSYS（TDD 5 步，per ADR-023 append-only）| 1.5h | Phase 0 #2 | 低 |
| **T-002** | `cuModuleLoadData` 接入 `load_kernel_module` (T-009b 验证后) | 3h | T-009b, T-000a/b | 中 |
| **T-003** v3 | `cuModuleUnload` 接入 `unload_kernel_module`（per T-000d: CudaRuntimeApi::unload_kernel_module → IGpuDriver::unload_kernel_module → ioctl 0x29）| 2h | T-000d, T-014, T-005b | 中 |
| **T-004** | `GpuDriverClient::load_kernel_module` 并发安全（M3 修订：mutex 在 GpuDriverClient 私有成员）| 2h | T-001 | 低 |
| **T-005** | 错误注入测试（M3 修订）| 2h | T-003/T-004 | 低 |
| **T-005b** | `cuModuleUnload` 清理 `func_to_attrs`/`func_to_module` (F4 缩窄范围: func_to_name 已清理) | 1.5h | T-003 | 低 |
| **T-008** | 补 `func_to_attrs`/`func_to_module` 映射测试（MF-2：CUDA_ERROR_NOT_SUPPORTED 字面量对齐 + 注释别名）| 1h | T-005b | 低 |
| **T-011 §B** | `cuModuleLoad` 改走 VRAM-load 路径 + 独立 `next_func_id` 计数器（D6 owner 决策）| 2h | T-003 | 中 |
| **T-013** v2 | DISPATCH_KERNEL packet 携带 `vram_addr + kernel_name` (G1 v2: 0x10C + 56B payload + kernel_name host pointer + dispatch_kernel wrapper) | **14-20h** | Phase 0 #1/#3/#4, T-000e | **高（核心架构）** |
| **T-006** | tadr-307 STALE 标注（已部分完成 per Commit 1, verify）| 0.5h | 无前置 | 低 |
| **T-007** | openspec change 归档 (Phase 3, T-001~T-008 全部 PASS 后) | 1h | T-005/T-008 | 低 |

**总工时估算**：**约 37-52 工作小时（5-7 工作工作日）**
- 工程时间：T-009b (3) + T-000a/b/d/e (5) + T-001 (2) + T-014 (1.5) + T-002/3 (5) + T-004/5 (4) + T-005b/8 (2.5) + T-011§B (2) + T-013 (14-20) + T-006/7 (1.5) = ~37-52h
- 跨仓等待（Phase 0）：1-2 周 calendar

### 2.3 关键路径（v2 Critical Path）

```
Phase 0 完成 (1-2w calendar)
   │
   ├─→ T-009b (3h) [parallel]                    ─┐
   ├─→ T-001 (2h) [parallel]                       │
   ├─→ T-014 (1.5h) [parallel]                     ├─→ T-002 (3h) ─→ T-003 v3 (2h)
   ├─→ T-000a/b/d/e (5h) [parallel]                │                              ↓
   └─→ T-000 (1h) [parallel]                       │                  T-005 (2h) → T-008 (1h)
                                                    │                              ↓
                                                    └─→ T-013 v2 (14-20h) ─────────┴─→ T-007 归档 (1h)
```

**瓶颈**：T-013 v2 DISPATCH_KERNEL packet 是核心架构决策，需：
- 详细设计 `dispatch_kernel_packet` layout（56B payload per Oracle R2 Part A）
- `CudaRuntimeApi::dispatch_kernel()` typed wrapper (per Oracle R2 Part C)
- `cu_launch.cpp` 重构构造 `dispatch_kernel` 调用
- `GpuDriverClient::submit_batch` 适配新 method=0x10C（**注意**：impl 在 `include/test_fixture/gpu_driver_client.h` inline）
- UsrLinuxEmu HAL 接受新 opcode（Phase 0 #1 决策 + #3 Mode A consumer）
- E2E 测试（mock-only per Phase 0 #3）

### 2.4 T-013 v2 详细设计（核心架构）

**任务来源**：Oracle R2 session `ses_fdffa6689...` Part A + Part C

**修正 v1 → v2**（per Metis + Oracle R2）：
- ❌ v1 `dispatch_kernel_packet` 自定义 struct 调 `submit_batch`（签名不兼容）
- ✅ v2 使用 `gpu_gpfifo_entry`（`method=0x10C`）+ 56B payload + kernel_name host pointer

**payload layout**（per `gpu_gpfifo_entry::payload[7]` 56B 总空间）：
```cpp
// payload[0] = vram_addr (u64)            // CUmodule (= module handle from 0x27)
// payload[1] = grid packed                // 复用 gpfifo_translator 约定: grid_x | grid_y<<16 | grid_z<<24
// payload[2] = block packed               // block_x | block_y<<8 | block_z<<16
// payload[3] = shared_mem (u32)           // 复用约定
// payload[4] = kernargs GPU VA            // 类似 gpu_pdl_payload
// payload[5] = kernel_name HOST pointer   // user-space emulation (precedent: image_ptr host ptr)
// payload[6] = flags/args_count
```

**集成**（`cu_launch.cpp:62-95` 重构）：
```cpp
extern "C" CUresult cuLaunchKernel(CUfunction f, uint32_t grid_x, ...) {
  // ... 现有 grid/block/args 校验 ...
  std::string kernel_name = resolve_func_name_impl(f);
  CUmodule mod = (CUmodule)0;
  cuFuncGetModule(&mod, f);  // 反查 func_to_module[f]

  // T-013 v2: 调用 CudaRuntimeApi::dispatch_kernel typed wrapper
  // (不直接构造 gpfifo_entry; 包装在 CudaRuntimeApi 内)
  int rc = runtime()->dispatch_kernel(
      reinterpret_cast<uint64_t>(mod),  // vram_addr (= CUmodule)
      kernel_name.c_str(),              // kernel_name (host pointer)
      /*grid, block, smem, args, stream*/
      );
  // kernel_name 生命周期: cuLaunchKernel 同步语义, string 由调用者保证存活至返回
  return cuda_error_from_errno(rc);
}
```

**CudaRuntimeApi::dispatch_kernel 实现**（per Oracle R2 Part C）：
```cpp
CudaError CudaRuntimeApi::dispatch_kernel(
    uint64_t vram_addr,
    const char* kernel_name,
    Dim3 grid, Dim3 block,
    size_t shared_mem,
    uint64_t kernargs_va,
    uint32_t stream_id) {
  // 1. 构造 gpu_gpfifo_entry (method=0x10C)
  gpu_gpfifo_entry entry{};
  entry.method = GPU_OP_DISPATCH_KERNEL;  // 0x10C (Phase 0 #1 决策)
  entry.payload[0] = vram_addr;
  entry.payload[1] = pack_grid(grid);          // 复用 translator 约定
  entry.payload[2] = pack_block(block);
  entry.payload[3] = static_cast<uint32_t>(shared_mem);
  entry.payload[4] = kernargs_va;
  entry.payload[5] = reinterpret_cast<uint64_t>(kernel_name);  // host pointer
  entry.payload[6] = pack_flags_args(/*...*/);
  // semaphore_va / ts_query 等其他字段按需填

  // 2. 调 IGpuDriver::submit_batch (raw pass-through, 因为构造在 CudaRuntimeApi 内)
  int64_t fence_id = scheduler_->driver()->submit_batch(
      stream_id, &entry, 1, GPU_SUBMIT_FENCE);
  return (fence_id < 0) ? cuda_error_from_errno(fence_id) : CudaError::OK;
}
```

**E2E 测试**（TDD）：
```cpp
TEST_CASE("cuLaunchKernel submits DISPATCH_KERNEL with 0x10C method") {
  // 1. load PTXIR image → vram_addr
  CUmodule mod;
  const uint8_t ptxir[] = {/* real PTXIR fixture bytes */};
  REQUIRE(cuModuleLoadData(&mod, ptxir) == CUDA_SUCCESS);
  uint64_t expected_vram = (uint64_t)mod;

  // 2. get function
  CUfunction fn;
  REQUIRE(cuModuleGetFunction(&fn, mod, "my_kernel") == CUDA_SUCCESS);

  // 3. launch
  void* args[] = {nullptr};
  REQUIRE(cuLaunchKernel(fn, 1, 1, 1, 1, 1, 1, 0, nullptr, args, NULL) == CUDA_SUCCESS);

  // 4. verify mock received DISPATCH_KERNEL entry with 0x10C method
  auto& mock = *mock_drv();
  REQUIRE(mock.last_gpfifo_entry().method == 0x10C);  // GPU_OP_DISPATCH_KERNEL
  REQUIRE(mock.last_gpfifo_entry().payload[0] == expected_vram);
  REQUIRE(std::string(reinterpret_cast<const char*>(mock.last_gpfifo_entry().payload[5]))
          == "my_kernel");
}
```

### 2.5 T-003 v3 详细设计

**任务来源**：Oracle R2 session `ses_fdffa6689...` Part B + Part C

**修正 v1 → v2 → v3**（per Metis + Oracle R2）：
- ❌ v1 reverse lookup via `get_bo_gpu_va`（该方法仅正向）
- ❌ v2 直接调 `GPU_IOCTL_UNLOAD_KERNEL_MODULE`（handler 当前是 -ENOSYS stub）
- ✅ v3 新增 append-only `IGpuDriver::unload_kernel_module(vram_addr)`（per ADR-023 §D4 / tadr-301 前例）

**实现**（per Oracle R2 Part C）：
```cpp
// IGpuDriver 新增 (per T-014, append-only, 默认 -ENOSYS)
virtual int unload_kernel_module(uint64_t vram_addr) { return -ENOSYS; }

// GpuDriverClient 实现（依赖 Phase 0 #2：父仓 0x29 handler 已实现）
inline int GpuDriverClient::unload_kernel_module(uint64_t vram_addr) {
  gpu_unload_kernel_module_args args{};
  args.module_handle = vram_addr;
  return ioctl(fd_, GPU_IOCTL_UNLOAD_KERNEL_MODULE, &args) == 0
         ? 0 : -errno;
}

// CudaRuntimeApi::unload_kernel_module (per T-000d, 替代 v1 free_bo reverse)
CudaError CudaRuntimeApi::unload_kernel_module(uint64_t vram_addr) {
  int rc = scheduler_->driver()->unload_kernel_module(vram_addr);
  return cuda_error_from_errno(rc);
}

// cu_module.cpp cuModuleUnload (per T-003 v3)
CUresult cuModuleUnload(CUmodule module) {
  // ... 现有清理 (T-005b) ...
  uint64_t vram_addr = reinterpret_cast<uint64_t>(module);
  int rc = runtime()->unload_kernel_module(vram_addr);  // → CudaRuntimeApi::unload_kernel_module
  return cuda_error_from_errno(rc);
}
```

**Phase 0 #2 BLOCKER 关键**：父仓 owner 必须先实现 ioctl 0x29 handler（`user_kernel_module_unload` 当前返 `-ENOSYS`，需 fold 到 `hal_mem_free(module_handle)`，约 1 行代码）。

### 2.6 风险与缓解（v2）

| 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|
| **Phase 0 BLOCKING GATE 6 项跨仓协调延迟** | 中 | 高 | 1-2w calendar; 期间可并行 T-009b/T-001/T-014/T-000a/b/d/e |
| **G1 DISPATCH_KERNEL 与 UsrLinuxEmu GPFIFO 路径不兼容** | 低 | 中 | Phase 0 #1 (opcode 0x10C 定义) + #4 (HSK-6 协议) 解决 |
| **ioctl 0x29 handler 实现延迟** | 中 | 高 | Phase 0 #2 HARD blocker; 需父仓 owner commit |
| **Mode A 无 PTXIR executor** | 中 | 中 | Phase 0 #3 决策: mock-only + e2e 等待 CppTLM Mode B |
| **kernel_name host pointer lifetime**（async submit 后失效）| 中 | 中 | PoC 同步 submit+FENCE+wait；未来异步需所有权策略 |
| **T-013 v2 估计 14-20h 仍可能低估** | 中 | 中 | mock-only 测试, real-path e2e 跨仓延后 |
| **`CUDA_ERROR_FILE_NOT_FOUND = 500` 未定义** | 中 | 中 | 在 `include/cuda.h` 正式定义（owner 决策）|
| **grid packing lossy** (grid_x 仅 16 bits) | 低 | 低 | PoC 足够; 后续按需扩展 |

---

## 阶段三：验证与归档（~3h）

### 3.1 验证清单（v2）

| 验证项 | 命令 | 通过标准 |
|---|---|---|
| 单测 | `cd build && ctest -R tadr-308` | 100% PASS |
| E2E (mock) | `ctest -R e2e` | PASS（含 DISPATCH_KERNEL mock 捕获测试）|
| LSP 诊断 | `lsp_diagnostics` on changed files | 0 errors |
| docs-audit | `tools/docs-audit.sh --strict` | 70/70 PASS |
| TaskRunner ↔ UsrLinuxEmu ABI 对齐 | `cd UsrLinuxEmu && ctest -R test_hal_kernel_module_standalone` | 0x27 + 0x29 新 struct 一致 |
| PTXIR fixture 验证 | `tests/umd/test_ptxir_format` + `tools/verify_ptxir_format.sh` | 24B header + string_table tail 验证 |
| **跨仓 ABI 字节级 diff** | `sizeof(dispatch_kernel_packet)` + `offsetof` 双端比对 | 一致 |
| **ioctl 0x29 handler 实现验证** | `test_unload_kernel_module_vram_addr` | 0x29 返 0（非 -ENOSYS）|

### 3.2 归档流程 (Phase 3)

```
T-007: openspec change 归档
  Step 1: cd openspec/changes/2026-08-18-tadr-308-igpu-driver-vram-load
  Step 2: openspec archive 2026-08-18-tadr-308-igpu-driver-vram-load
  Step 3: tadr-308 状态: 🔄 Proposed → ✅ Accepted (per ADR-035)
  Step 4: 同步 mirror 到 UsrLinuxEmu docs/00_adr/README.md (已 per Commit 8)
  Step 5: 验证 Phase 0 #1/#2 已落地 (opcode 定义 + ioctl 0x29 handler)
```

### 3.3 跨仓 submodule bump（per ADR-035 §R5.1 Step 2）

实施完成后，UsrLinuxEmu owner 需：
```bash
cd /workspace/project/UsrLinuxEmu
git add external/TaskRunner
git commit -m "chore(submodule): bump TaskRunner to <NEW HEAD> for tadr-308 v2 implementation"
git push origin main
```

---

## 阶段四：维护性工作（可选，post-Phase 3）

### 4.1 后续改进

| 改进 | 描述 | 优先级 |
|---|---|---|
| `cuModuleLoadDataEx` 支持 | 当前 PoC 返 `CUDA_ERROR_INVALID_VALUE`，PyTorch 用 fatbin 需补 story | 高 |
| §C MANIFEST 验证 (typo 检测) | 当 owner 决策需要 `CUDA_ERROR_NOT_FOUND` 提前错误时 | 中 |
| **kernel_name lifetime 升级** | 异步 submit 场景需 heap copy + fence 关联 | 中 |
| **grid packing 扩展** | 当前 lossy 16-bit grid_x; 真 workload 需更大 grids | 低 |
| ADR-0029 §D8 amendment | 协调 PTX-EMU owner 重新描述 HAL 集成路径 | 中 |
| CUBIN 嵌入 PTXIR 支持 | 集成 PTX-EMU ADR-0024 PTXIR-Embedded CUBIN loader 决策 | 低 |

### 4.2 文档维护

- tadr-308 v2 文档同步（GPU_OP_DISPATCH_KERNEL opcode + payload layout contract）
- AGENTS.md 补充 shared scope dual-ack 提交前 checklist
- 阶段一完成后建 issue `tadr-308-cleanup-hal-mock-injection-helpers`（per ADR-090 v1 §M9）

---

## 关键里程碑 (Milestone v2)

| 里程碑 | 触发条件 | 后续动作 | 估计时间 |
|---|---|---|---|
| **M0**: Phase 0 BLOCKING 完成 | 6 项 UsrLinuxEmu owner 协调 ✅ | 启动 T-009b/T-001/T-014/T-000 系列 | T+1-2w |
| **M1**: Push 完成 | 8 commits 落地 origin | 召集 dual-ack | T+0d |
| **M2**: dual-ack 通过 | dual reviewer approve tadr-308 v2 + tasks.md v2 | 启动 Phase 2 实施 | T+1w |
| **M3**: T-001~T-008 + T-014 PASS | ctest -R tadr-308 全过 | T-007 归档 | T+1w after M2 |
| **M4**: T-013 v2 PASS | ctest 含 DISPATCH_KERNEL mock 测试 | UsrLinuxEmu submodule bump | T+2w after M2 |
| **M5**: tadr-308 v2 Accepted | 归档完成 | UsrLinuxEmu submodule bump | T+2w after M2 |
| **M6**: Mode B E2E | CppTLM SM executor + PTX-EMU 集成完成 | 真实 GPU 端到端测试 | T+3-6m（跨仓协调）|

---

## 立即建议（next 24h）

1. **owner push 7 commits + 1 commit**（TaskRunner + UsrLinuxEmu）— 决策时机
2. **修复 docs-audit FAIL** — owner 决策单独 commit 或与下批合并
3. **🆕 启动 Phase 0 BLOCKING GATE**：发起 UsrLinuxEmu issue + 召集 owner 会议（**最关键**）
4. **召集 shared scope dual-ack** — 提前 1 周通知 maintainer
5. **同步通知 UsrLinuxEmu owner 6 项 Phase 0 协调**（特别是 #2 ioctl 0x29 + #3 Mode A consumer）

---

## 引用

- **设计 canonical**：[tadr-308-igpu-driver-vram-load.md](../shared/adr/tadr-308-igpu-driver-vram-load.md)
- **任务详细**：[tasks.md](../changes/2026-08-18-tadr-308-igpu-driver-vram-load/tasks.md)（**待 v2 修订**：T-000d/e/T-014 实施细节，T-003/T-013 v2/v3 代码）
- **历史 STALE**：[tadr-307-igpu-driver-kernel-module-extension.md](../shared/adr/tadr-307-igpu-driver-kernel-module-extension.md)
- **Oracle 决策**：
  - [R1: `ses_fe0443831ffenUxpEQxZqWE8Cp`](https://oracle-sessions/ses_fe0443831ffenUxpEQxZqWE8Cp)（A' 决策：kernel_name 应用传入 + 24B header + CUmodule=VA）
  - [R2: `ses_fdffa6689ffeQ0vsWd06LuQgbg`](https://oracle-sessions/ses_fdffa6689ffeQ0vsWd06LuQgbg)（验证 + REVISE further: T-013 v2 (0x10C + payload) + T-003 v3 (新方法) + 6 项跨仓）
- **Metis 审查**：[`ses_fe0048690ffeOIUPxgOHrfUI1s`](https://oracle-sessions/ses_fe0048690ffeOIUPxgOHrfUI1s)（揭示 3 CRITICAL 架构错误触发 Oracle R2 重新设计）
- **跨仓 ADR**：
  - UsrLinuxEmu [ADR-090 v2 §D1](https://github.com/chisuhua/UsrLinuxEmu/blob/main/docs/00_adr/adr-090-ptxir-via-h2d-dma-v2.md) (canonical ✅ Accepted)
  - UsrLinuxEmu [`gpu_ioctl.h:770-774`](https://github.com/chisuhua/UsrLinuxEmu/blob/main/plugins/gpu_driver/shared/gpu_ioctl.h) (gpu_unload_kernel_module_args struct)
  - UsrLinuxEmu [`hal_user.cpp:722-726`](https://github.com/chisuhua/UsrLinuxEmu/blob/main/plugins/gpu_driver/hal/hal_user.cpp) (当前 -ENOSYS stub，需实现)
  - PTX-EMU [ADR-0023](https://github.com/chisuhua/PTX-EMU/blob/main/docs/adr/ADR-0023-ptxir-binary-format.md) (24B header)
  - PTX-EMU [ADR-0028](https://github.com/chisuhua/PTX-EMU/blob/main/docs/adr/ADR-0028-multi-kernel-manifest.md) (kernels[])
  - PTX-EMU [ADR-0029 §D1](https://github.com/chisuhua/PTX-EMU/blob/main/docs/adr/ADR-0029-ptxemu-image-executor.md) (cpptlm_module.h)
- **同步协议**：[ADR-035 §R5.1 4 步](https://github.com/chisuhua/UsrLinuxEmu/blob/main/docs/00_adr/adr-035-governance-policy.md)
- **本仓同期路线图**：
  - [`README.md`](./README.md) - umd-evolution roadmap index
  - [`current-status.md`](./current-status.md) - master snapshot

---

**最后更新**: 2026-08-20 R2 (Sisyphus, per Oracle session `ses_fdffa6689ffeQ0vsWd06LuQgbg` R2 verdict + Metis session `ses_fe0048690ffeOIUPxgOHrfUI1s` 审查 + 8 atomic commits 落地)

**变更记录**:
- 2026-08-20 (R1): 初始版本（per Oracle A' + 7 atomic commits + Commit 8 跨仓 mirror + submodule bump）
- 2026-08-20 (R2): 重大修订（per Metis + Oracle R2）
  - T-013 v1 → v2: `submit_batch` 任意 packet → `gpu_gpfifo_entry method=0x10C` + 56B payload + kernel_name host pointer
  - T-003 v1 → v3: `get_bo_gpu_va` reverse lookup → 新增 `IGpuDriver::unload_kernel_module(vram_addr)` (append-only per ADR-023)
  - 新增 T-000d (CudaRuntimeApi::unload_kernel_module) + T-000e (CudaRuntimeApi::dispatch_kernel typed wrapper)
  - 新增 Phase 0 BLOCKING GATE（6 项 UsrLinuxEmu owner 协调，2 项 HARD blocker）
  - T-013 G1 时间估计 6-8h → 14-20h
  - T-014 从 placeholder 升级为实际实施任务
  - 总工时 26-30h → 37-52h 工程 + 不定 calendar (Phase 0)