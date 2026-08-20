---
SCOPE: UMD-EVOLUTION
STATUS: ACTIVE
LAST_UPDATED: 2026-08-20
HEAD_COMMIT: 40ff0bb (TaskRunner, 6 atomic commits) + 25d3fec (UsrLinuxEmu submodule bump)
SOURCE_CHANGE: openspec/changes/2026-08-18-tadr-308-igpu-driver-vram-load
SOURCE_TADR: docs/shared/adr/tadr-308-igpu-driver-vram-load.md (🔄 Proposed)
SOURCE_TADR_STALE: docs/shared/adr/tadr-307-igpu-driver-kernel-module-extension.md (🚫 STALE)
ORACLE_SESSION: ses_fe0443831ffenUxpEQxZqWE8Cp (2026-08-20 A' 决策)
RELATED_PHASE: UMD-EVOLUTION Phase 5 (tadr-308 是 Phase 5 的 IGpuDriver 扩展核心)
---

# TADR-308 实施路线图（专项）

> 本文档是 **tadr-308 (IGpuDriver VRAM-Load Extension)** 的实施专项路线图。
> 跟踪 [openspec/changes/2026-08-18-tadr-308-igpu-driver-vram-load](../changes/2026-08-18-tadr-308-igpu-driver-vram-load/tasks.md) 中所有任务的执行状态、依赖关系与里程碑。
>
> **本路线图与共享设计文档分离**：
> - **设计文档**（`docs/shared/adr/tadr-308-igpu-driver-vram-load.md`）— 决策、上下文、影响（scope: shared）
> - **本路线图**（本文）— 实施任务清单、时间线、状态（scope: umd-evolution，因为实施落在 shim 层）

---

## TL;DR

**当前状态**：**设计已完成，待实施**。

- 7 个 atomic commit 已落地（6 在 TaskRunner，1 在 UsrLinuxEmu mirror + submodule bump）
- 5 项 owner 决策已落地（C1/C2/C5/M11/M8/MF-2/MF-4 全部解决）
- Oracle A′ 最终决策已采纳（kernel_name 应用传入 + image_size 24B header 推断）
- **待 owner**: push 到 origin + 召集 shared scope dual-ack + 启动实施

**实施未启动**。本路线图定义 4 个阶段（前置 / 实施 / 验证 / 维护），总工时约 **26-30 工作小时（3-4 工作日）**。

**关键瓶颈**：T-013 NEW（DISPATCH_KERNEL packet 携带 vram_addr）是 G1 缺口的核心架构工作，需 6-8 小时。

---

## 已落地 commit 清单（设计阶段）

### TaskRunner 仓（HEAD = 40ff0bb）

| # | Commit | 描述 |
|---|---|---|
| 1 | `fadcc7d` | docs(tadr-307): slim STALE body to 30-line historical summary + redirect（per ADR-035 §R2.4）|
| 2 | `83abae0` | docs(tadr-308): §A fix real 24B PTXIR format + §1.5 A′ opt-in validate + §1.2 CUmodule=VA redefine |
| 3 | `de94c48` | docs(tadr-308): §References cite ADR-090 v2 (canonical) + ADR-0023/0028 + fix v1→v2 references |
| 4 | `fb521c9` | docs(tadr-308): M1 narrow F4 + Gate #22-#24 + Cross-Sync v2 update |
| 5 | `84ec36d` | docs(openspec/tadr-308): T-000c decision + T-011 §A 24B + T-003 G2 + T-009b + T-010 DROP + T-013 NEW (G1) + T-014 placeholder |
| 6 | `40ff0bb` | docs(shared/adr/README): tadr-308 描述修订 + tadr-307 状态更新 + 最后更新时间戳 |

### UsrLinuxEmu 仓（HEAD = 25d3fec）

| # | Commit | 描述 |
|---|---|---|
| 7 | `25d3fec` | docs(adr-090-mirror): tadr-307 STALE slimmed + tadr-308 行新增 + submodule bump (40ff0bb) |

---

## 阶段一：前置准备（owner 必做，~2h）

### 1.1 Push 到 origin

| 仓 | 状态 | 操作 |
|---|---|---|
| TaskRunner | 6 commits 本地，未 push | `git push origin main` (HEAD = `40ff0bb`) |
| UsrLinuxEmu | 1 commit 本地，未 push | `git push origin main` (HEAD = `25d3fec`) |

**owner 决策 push 时机**：
- 建议**先 TaskRunner push，等 CI（lsp_diagnostics + ctest）+ 1-2 天 review window** → 再 UsrLinuxEmu push
- 或**两仓同时 push**（如果 reviewer 已知情）

### 1.2 修复 docs-audit 跨仓 FAIL

**失败 detection**：`docs/07-integration/taskrunner-index.md` 中 tadr-307 引用需更新（per Commit 7 跳过 docs-audit）

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
- ⚠️ 涉及 ABI 契约变更需通知 UsrLinuxEmu maintainer（**T-013 G1 DISPATCH_KERNEL packet 即触发**）

**建议时间**：阶段二实施前 **1 周** 通知，让 maintainer review tadr-308 + tasks.md。

### 1.4 跨仓 owner 协调（T-013 G1 关键路径）

T-013 G1 DISPATCH_KERNEL packet 涉及：
- UsrLinuxEmu 端：HAL #66 派发表接受新 `GPU_OP_DISPATCH_KERNEL = 0x04` opcode
- UsrLinuxEmu 端：`hal_user.cpp` 接受 packet payload `{vram_addr(u64), kernel_name(str), grid/block/args/smem}`
- CppTLM 端（Mode B）：SQ/CQ doorbell 接收后调 `ptxemu_image_execute_named`

**协调动作**：发起 issue 到 UsrLinuxEmu 仓（[UsrLinuxEmu #10](https://github.com/chisuhua/TaskRunner/issues/10) 跟踪 tadr-308 状态），请求 UsrLinuxEmu owner 评估 opcode 命名空间 + struct layout。

---

## 阶段二：实施（T-001 ~ T-008 + T-013 NEW，~26-30h）

### 2.1 任务依赖图

```
T-009b (PTX-EMU fixture 验证, 验证 PTXIR 24B 格式)
   │
   ├─→ T-001 (IGpuDriver 默认 -ENOSYS)
   ├─→ T-002 (cuModuleLoadData 适配)
   ├─→ T-003 (cuModuleUnload + G2 VA 翻译)
   ├─→ T-004 (GpuDriverClient 并发安全)
   ├─→ T-005 (错误注入)
   ├─→ T-005b (cuModuleUnload 清理 func_to_*/func_to_module, F4 缩窄)
   ├─→ T-008 (func_to_attrs/func_to_module 测试覆盖)
   ├─→ T-011 §B (cuModuleLoad VRAM-load + 独立 next_func_id 计数器)
   │
   └─→ T-013 NEW (G1: DISPATCH_KERNEL packet payload, 核心架构)
        │
        └─→ T-014 (unload_kernel_module 评估, PoC 选 G2 路径, 无代码)

T-000a / T-000b (Gate #7/#8 HARD 前置, 可与 T-009b 并行)
```

### 2.2 任务清单（按依赖顺序）

| Task | 描述 | 估计工时 | 依赖 | 风险 |
|---|---|---|---|---|
| **T-009b** | 用 PTX-EMU `tests/ptxir/fixtures/{multi_kernel_basic,cute_rmsnorm}.ptxir` 验证 24B header 格式 + string_table_ tail 推断 + MANIFEST parse | 2-3h | 无前置 | 低 |
| **T-000a** | `CudaRuntimeApi` 扩 `load_kernel_module` 方法（Gate #7 HARD）| 1h | 无前置 | 低 |
| **T-000b** | `cuda_error_from_errno` helper 定义（Gate #8 HARD）| 1h | 无前置 | 低 |
| **T-001** | `IGpuDriver::load_kernel_module` 默认 -ENOSYS（TDD 5 步）| 2h | 无前置 | 低 |
| **T-002** | `cuModuleLoadData` 接入 `load_kernel_module` (T-009b 验证后) | 3h | T-009b | 中 |
| **T-003** | `cuModuleUnload` 接入 `free_bo`（per G2 VA 翻译修订：get_bo_gpu_va 反查 + free_bo）| 3h | T-000a/b, T-005b | 中 |
| **T-004** | `GpuDriverClient::load_kernel_module` 并发安全（M3 修订：mutex 在 GpuDriverClient 私有成员，不在 IGpuDriver）| 2h | T-001 | 低 |
| **T-005** | 错误注入测试（M3 修订）| 2h | T-003/T-004 | 低 |
| **T-005b** | `cuModuleUnload` 清理 `func_to_attrs`/`func_to_module` (F4 缩窄范围: func_to_name 已清理) | 1.5h | T-003 | 低 |
| **T-008** | 补 `func_to_attrs`/`func_to_module` 映射测试（MF-2：CUDA_ERROR_NOT_SUPPORTED 字面量对齐 + 注释别名）| 1h | T-005b | 低 |
| **T-011 §B** | `cuModuleLoad` 改走 VRAM-load 路径 + 独立 `next_func_id` 计数器（D6 owner 决策）| 2h | T-003 | 中 |
| **T-013 NEW** | DISPATCH_KERNEL packet 携带 `vram_addr + kernel_name` (G1 launch 路径修复) | **6-8h** | T-001/T-009b | **高（核心架构）** |
| **T-014** | `unload_kernel_module` 评估（PoC 选 G2 路径, 无代码）| 0.5h | 无前置 | 低 |
| **T-006** | tadr-307 STALE 标注（已部分完成 per Commit 1, verify）| 0.5h | 无前置 | 低 |
| **T-007** | openspec change 归档 (Phase 3, T-001~T-008 全部 PASS 后) | 1h | T-005/T-008 | 低 |

**总工时估算**：**约 26-30 工作小时（3-4 工作日）**

### 2.3 关键路径（Critical Path）

```
T-009b (2-3h) → T-002 启用 (3h) ─→ T-013 G1 (6-8h) ─→ T-006/T-007 归档 (1.5h)
                                  ↘
                  T-001/T-011 §B/T-005b (并行 ~5h) ─→ T-003 (3h) → T-008 (1h)
```

**瓶颈**：T-013 G1 DISPATCH_KERNEL packet 是核心架构决策，需：
- 详细设计 `dispatch_kernel_packet` struct（建议 `include/shared/dispatch_kernel_packet.hpp`）
- `cu_launch.cpp` 重构以构造 packet
- `GpuDriverClient::submit_batch` 新增 opcode handling（**注意**：impl 在 `include/test_fixture/gpu_driver_client.h` inline，非 `.cpp`）
- UsrLinuxEmu HAL 准备接受新 opcode（需协调 UsrLinuxEmu owner per 1.4）
- E2E 测试（mock + 单测）

### 2.4 风险与缓解

| 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|
| **G1 DISPATCH_KERNEL 与 UsrLinuxEmu 现有 GPFIFO 提交路径冲突** | 中 | 高 | 阶段一 1.4: 与 UsrLinuxEmu owner 同步 opcode 命名空间 (`GPU_OP_DISPATCH_KERNEL = 0x04`) |
| **PTX-EMU fixture 版本不匹配**（per `ptxir_format.h:16` version=4，而 §A 旧假设 1/2）| 低 | 中 | T-009b 已有（用真实 fixture 验证）|
| **CUmodule=VA 与现有 `free_bo` BO handle 子类型混淆** | 中 | 中 | T-003 G2 用 `get_bo_gpu_va` 反查 |
| **shared scope dual-ack 延迟** | 中 | 中 | 阶段一 1.3 提前 1 周通知 maintainer |
| **T-013 G1 实施超 6-8h** | 中 | 中 | mock-only E2E 测试，真实 UsrLinuxEmu 集成延后 |
| **`free_bo` 调用链误用**（CUmodule=VA 转型后类型不匹配）| 中 | 中 | T-003 G2 已设计 `get_bo_gpu_va` 反查路径 |

### 2.5 T-013 G1 详细设计（核心架构）

**任务来源**：Oracle session `ses_fe0443831...` Part B G1（重大发现）

**问题**：现行 `cuLaunchKernel` → `runtime()->launch_kernel(name, ...)` → `CudaScheduler::submit_launch(stream_id, kernel_index, ...)` → `IGpuDriver::submit_launch` (`igpu_driver.hpp:219`)
**VRAM 加载的 image 在 `vram_addr` 永远不会被 launch 路径引用**。

**修复方向**（per Oracle Part D Recommendation 2）：
**不新增 IGpuDriver 方法**（per ADR-023 append-only），而是扩展 `submit_batch` (`igpu_driver.hpp:191`) 携带新 packet payload：

```cpp
namespace async_task::shared {
constexpr uint32_t GPU_OP_DISPATCH_KERNEL = 0x04;  // 新增 opcode

struct dispatch_kernel_packet {
  uint64_t vram_addr;      // cuModuleLoadData 返回的 vram_addr (= CUmodule)
  uint32_t grid_x, grid_y, grid_z;
  uint32_t block_x, block_y, block_z;
  uint32_t shared_mem_bytes;
  uint64_t args_ptr;       // 用户态 void** kernel_args
  uint64_t args_count;
  char     kernel_name[256]; // 应用在 cuModuleGetFunction 传入
};
}
```

**集成**（`cu_launch.cpp:62-95` 重构）：
```cpp
extern "C" CUresult cuLaunchKernel(CUfunction f, uint32_t grid_x, ...) {
  // ... 现有 grid/block/args 校验 ...
  std::string kernel_name = resolve_func_name_impl(f);
  CUmodule mod = (CUmodule)0;
  cuFuncGetModule(&mod, f);  // 反查 func_to_module[f]

  // G1 修复: 构造 DISPATCH_KERNEL packet 携带 vram_addr (= mod) + name
  dispatch_kernel_packet pkt{};
  pkt.vram_addr = reinterpret_cast<uint64_t>(mod);  // ★ vram_addr 关键
  // ... grid/block/args/smem 填充 ...
  std::strncpy(pkt.kernel_name, kernel_name.c_str(), 255);
  pkt.kernel_name[255] = '\0';

  int rc = runtime()->submit_batch(
      default_stream_id_,
      &pkt, sizeof(pkt),
      GPU_OP_DISPATCH_KERNEL);
  return cuda_error_from_errno(rc);
}
```

**E2E 测试**（TDD）：
```cpp
TEST_CASE("cuLaunchKernel after cuModuleLoadData submits DISPATCH_KERNEL with vram_addr") {
  // 1. load PTXIR image → vram_addr
  CUmodule mod;
  const uint8_t ptxir[] = {/* real PTXIR fixture bytes */};
  REQUIRE(cuModuleLoadData(&mod, ptxir) == CUDA_SUCCESS);
  uint64_t expected_vram = (uint64_t)mod;

  // 2. get function
  CUfunction fn;
  REQUIRE(cuModuleGetFunction(&fn, mod, "my_kernel") == CUDA_SUCCESS);

  // 3. launch — expect submit_batch with DISPATCH_KERNEL packet
  void* args[] = {nullptr};
  REQUIRE(cuLaunchKernel(fn, 1, 1, 1, 1, 1, 1, 0, nullptr, args, nullptr) == CUDA_SUCCESS);

  // 4. verify mock received DISPATCH_KERNEL with vram_addr + name
  auto& mock = *mock_drv();
  REQUIRE(mock.last_submit_batch_packet_type() == GPU_OP_DISPATCH_KERNEL);
  REQUIRE(mock.last_submit_batch_vram_addr() == expected_vram);
  REQUIRE(mock.last_submit_batch_kernel_name() == "my_kernel");
}
```

---

## 阶段三：验证与归档（~3h）

### 3.1 验证清单

| 验证项 | 命令 | 通过标准 |
|---|---|---|
| 单测 | `cd build && ctest -R tadr-308` | 100% PASS |
| E2E (mock) | `ctest -R e2e` | PASS |
| LSP 诊断 | `lsp_diagnostics` on changed files | 0 errors |
| docs-audit | `tools/docs-audit.sh --strict` | 70/70 PASS |
| TaskRunner ↔ UsrLinuxEmu ABI 对齐 | `cd UsrLinuxEmu && ctest -R gpu_ioctl_kernel_module` | 0x27 新 struct 一致 |
| 真实 PTXIR fixture | `tests/umd/test_ptxir_format` + `tools/verify_ptxir_format.sh` | 24B header + string_table tail 验证 |

### 3.2 归档流程 (Phase 3)

```
T-007: openspec change 归档
  Step 1: cd openspec/changes/2026-08-18-tadr-308-igpu-driver-vram-load
  Step 2: openspec archive 2026-08-18-tadr-308-igpu-driver-vram-load
  Step 3: tadr-308 状态: 🔄 Proposed → ✅ Accepted (per ADR-035)
  Step 4: 同步 mirror 到 UsrLinuxEmu docs/00_adr/README.md (已 per Commit 7)
```

### 3.3 跨仓 submodule bump（per ADR-035 §R5.1 Step 2）

实施完成后，UsrLinuxEmu owner 需：
```bash
cd /workspace/project/UsrLinuxEmu
git add external/TaskRunner
git commit -m "chore(submodule): bump TaskRunner to <NEW HEAD> for tadr-308 implementation"
git push origin main
```

### 3.4 PTX-EMU 协调（后置）

per tadr-308 §Cross-Repo Sync 表，需要：
- PTX-EMU owner 接受 ADR-0029 §D8 amendment（per ADR-090 v2 §C4 + F2 修订）
- PTX-EMU HSK-6 公告发出
- PTX-EMU owner 关闭 [PTX-EMU #12](https://github.com/chisuhua/PTX-EMU/issues/12)（已 closed）

---

## 阶段四：维护性工作（可选，post-Phase 3）

### 4.1 后续改进

| 改进 | 描述 | 优先级 |
|---|---|---|
| `cuModuleLoadDataEx` 支持 | 当前 PoC 返 `CUDA_ERROR_INVALID_VALUE`，PyTorch 用 fatbin 需补 story | 高 |
| §C MANIFEST 验证 (typo 检测) | 当 owner 决策需要 `CUDA_ERROR_NOT_FOUND` 提前错误时 | 中 |
| `unload_kernel_module` 升级 | 如 ioctl 0x29 获更明确语义，发起独立 sub-change | 低 |
| ADR-0029 §D8 amendment | 协调 PTX-EMU owner 重新描述 HAL 集成路径 | 中 |
| CUBIN 嵌入 PTXIR 支持 | 集成 PTX-EMU ADR-0024 PTXIR-Embedded CUBIN loader 决策 | 低 |

### 4.2 文档维护

- tadr-308 示例代码引用 file:line 同步（cu_module.cpp / cu_launch.cpp 一旦修改）
- AGENTS.md 补充 shared scope dual-ack 提交前 checklist
- 阶段一完成后建 issue `tadr-308-cleanup-hal-mock-injection-helpers`（per ADR-090 v1 §M9）

---

## 关键里程碑 (Milestone)

| 里程碑 | 触发条件 | 后续动作 | 估计时间 |
|---|---|---|---|
| **M1**: Push 完成 | 7 commits 落地 origin | 召集 dual-ack | T+0d |
| **M2**: dual-ack 通过 | dual reviewer approve tadr-308 + tasks.md | 启动 T-009b | T+1w |
| **M3**: T-001~T-008 PASS | ctest -R tadr-308 全过 | T-007 归档 | T+1w |
| **M4**: tadr-308 Accepted | 归档完成 | UsrLinuxEmu submodule bump | T+1w |
| **M5**: Mode B E2E | CppTLM SM executor + PTX-EMU 集成完成 | 真实 GPU 端到端测试 | T+3-6m（跨仓协调）|

---

## 立即建议（next 24h）

1. **owner push 6 commits + 1 commit**（TaskRunner + UsrLinuxEmu）— 决策时机
2. **修复 docs-audit FAIL** — owner 决策单独 commit 或与下批合并
3. **发起 T-009b 任务** — 创建 issue 跟踪 fixture 验证
4. **召集 shared scope dual-ack** — 提前 1 周通知 maintainer
5. **协调 UsrLinuxEmu owner**（T-013 G1 DISPATCH_KERNEL opcode 命名空间）— 发起 [TaskRunner #10](https://github.com/chisuhua/TaskRunner/issues/10) 评论

---

## 引用

- **设计 canonical**：[tadr-308-igpu-driver-vram-load.md](../shared/adr/tadr-308-igpu-driver-vram-load.md)
- **任务详细**：[tasks.md](../changes/2026-08-18-tadr-308-igpu-driver-vram-load/tasks.md)（14 tasks：T-000a/b/c、T-001~T-008、T-009、T-009b NEW、T-010 DROPPED、T-011 §A/§B/§C、T-013 NEW、T-014 placeholder）
- **历史 STALE**：[tadr-307-igpu-driver-kernel-module-extension.md](../shared/adr/tadr-307-igpu-driver-kernel-module-extension.md)
- **Oracle 决策**：[Oracle session `ses_fe0443831ffenUxpEQxZqWE8Cp`](https://oracle-sessions/ses_fe0443831ffenUxpEQxZqWE8Cp)（2026-08-20 A′ 决策 + F1-F5 验证 + Part A-E 报告）
- **跨仓 ADR**：
  - UsrLinuxEmu [ADR-090 v2 §D1](https://github.com/chisuhua/UsrLinuxEmu/blob/main/docs/00_adr/adr-090-ptxir-via-h2d-dma-v2.md) (canonical ✅ Accepted)
  - PTX-EMU [ADR-0023](https://github.com/chisuhua/PTX-EMU/blob/main/docs/adr/ADR-0023-ptxir-binary-format.md) (24B header)
  - PTX-EMU [ADR-0028](https://github.com/chisuhua/PTX-EMU/blob/main/docs/adr/ADR-0028-multi-kernel-manifest.md) (kernels[])
  - PTX-EMU [ADR-0029 §D1](https://github.com/chisuhua/PTX-EMU/blob/main/docs/adr/ADR-0029-ptxemu-image-executor.md) (cpptlm_module.h)
- **同步协议**：[ADR-035 §R5.1 4 步](https://github.com/chisuhua/UsrLinuxEmu/blob/main/docs/00_adr/adr-035-governance-policy.md)
- **本仓同期路线图**：
  - [`README.md`](./README.md) - umd-evolution roadmap index
  - [`current-status.md`](./current-status.md) - master snapshot

---

**最后更新**: 2026-08-20 (Sisyphus, per Oracle session `ses_fe0443831ffenUxpEQxZqWE8Cp` A' 决策 + 7 atomic commits 落地)

**变更记录**:
- 2026-08-20: 初始版本（per Oracle A' + 5 atomic commits + Commit 7 跨仓 mirror + submodule bump）