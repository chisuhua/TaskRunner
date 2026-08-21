---
SCOPE: UMD-EVOLUTION
STATUS: ACTIVE
LAST_UPDATED: 2026-08-20
RELATED: tadr-308 (TaskRunner), ADR-090 v2 (UsrLinuxEmu), ADR-0029 §D8 (PTX-EMU)
ORACLE_R2: ses_fdffa6689ffeQ0vsWd06LuQgbg
TARGET_REPO: chisuhua/UsrLinuxEmu
ISSUE_TYPE: BLOCKING cross-repo coordination
---

# UsrLinuxEmu Issue Draft: Phase 0 BLOCKING GATE for tadr-308

> **⚠️ 状态**: 本文件是 **issue 草稿**，由 TaskRunner owner 在 [TaskRunner #10](https://github.com/chisuhua/TaskRunner/issues/10) 跟踪下起草。
> **owner 必做**：复制下方"Issue Body"到 UsrLinuxEmu 仓创建新 issue（参考 [UsrLinuxEmu #11](https://github.com/chisuhua/UsrLinuxEmu/issues/11) 系列编号模式）。

---

## 📋 Issue Title (推荐)

```
[BLOCKING] tadr-308 (TaskRunner) 需要 6 项 UsrLinuxEmu 协调 — 2 项 HARD blocker
```

## 📋 Issue Body (Markdown)

```markdown
## Context

[TaskRunner tadr-308](https://github.com/chisuhua/TaskRunner/blob/c66b4d0/docs/shared/adr/tadr-308-igpu-driver-vram-load.md)
(IGpuDriver VRAM-Load Extension) 是 [PTX-EMU ADR-0029 §D8 HAL
integration](https://github.com/chisuhua/PTX-EMU/blob/main/docs/adr/ADR-0029-ptxemu-image-executor.md#d8-cp-端集成集成--integrate-load-er)
的 consumer-side 对偶，需在本仓实施 6 项跨仓协调。

本仓 [ADR-090 v2](https://github.com/chisuhua/UsrLinuxEmu/blob/main/docs/00_adr/adr-090-ptxir-via-h2d-dma-v2.md) §D1 已定义相关
HAL/ioctl 契约（`GPU_IOCTL_LOAD_KERNEL_MODULE` 0x27 + `GPU_IOCTL_UNLOAD_KERNEL_MODULE` 0x29），但实际 handler 实现
（特别是 0x29）仍是 stub。

[Oracle session `ses_fdffa6689ffeQ0vsWd06LuQgbg`](https://oracle-sessions/ses_fdffa6689ffeQ0vsWd06LuQgbg)
(2026-08-20 R2 verdict: REVISE further) 揭示：tadr-308 实施依赖 6 项本仓 owner 协调。

## 6 项 Phase 0 协调需求

| # | 项目 | HARD/SOFT | 阻塞 | 估计 |
|---|------|:---------:|------|------|
| **#1** | **定义 `GPU_OP_DISPATCH_KERNEL = 0x10C`** in `plugins/gpu_driver/shared/gpu_types.h` + payload layout contract comment | 🔴 HARD | T-013 (DISPATCH_KERNEL packet) | <1h (1 line + comment) |
| **#2** | **实现 ioctl 0x29 handler** for real：当前 `user_kernel_module_unload` 返 `-ENOSYS` (per `hal_user.cpp:722-726`)，需 fold 到 `hal_mem_free(module_handle)` (~1 line) | 🔴 HARD | T-003 (cuModuleUnload 路径) | <1h (1 line + test) |
| **#3** | **决策 Mode A interim consumer for 0x10C**：选项 (a) puller case returning success-stub, (b) extend `translateLaunch`, (c) 声明 real-path e2e 出 PoC 范围（mock-only）等待 CppTLM Mode B | 🔴 HARD | T-013 e2e | 1-2d owner 决策 |
| **#4** | **Payload layout 写入 HSK-6 joint protocol**，确保 CppTLM Mode B SQ parsing (`ptxemu_image_execute_named` 调用点) 使用相同 layout | 🟡 SOFT | T-013 e2e | <1h (协议文档) |
| **#5** | **kernel_name host-pointer 约定 ack**：user-mode emulation 内合法（CppTLM Mode B 同进程），不可移植到真 KMD | 🟡 SOFT | 文档 ack | <1h |
| **#6** | **文档化 code BO bypass**：`bo_map_`/`handles_` 表（FREE_BO 不接收 code BO）。FREE_BO vs 0x29 不对称目前隐式 | 🟡 SOFT | 文档 | <1h |

## 详细规格 (per item)

### #1 定义 `GPU_OP_DISPATCH_KERNEL = 0x10C`

**位置**：`plugins/gpu_driver/shared/gpu_types.h`

**当前状态**：`gpu_types.h:56-67` 定义 GPU_OP_* 0x100-0x10B（0x10B = PDL_LAUNCH）。无 DISPATCH_KERNEL。

**期望变更**：
```cpp
// append after GPU_OP_PDL_LAUNCH = 0x10B
constexpr uint16_t GPU_OP_DISPATCH_KERNEL = 0x10C;  // Per TaskRunner tadr-308 §1.2 + Oracle R2
```

**Payload layout**（per [PTX-EMU ADR-0023](https://github.com/chisuhua/PTX-EMU/blob/main/docs/adr/ADR-0023-ptxir-binary-format.md) + GPFIFO 约定）：
```cpp
// gpu_gpfifo_entry::payload[7] = 56B total
// payload[0] = vram_addr (u64)         // CUmodule (= module handle from 0x27)
// payload[1] = grid packed             // 复用 gpfifo_translator 约定
// payload[2] = block packed
// payload[3] = shared_mem (u32)
// payload[4] = kernargs GPU VA
// payload[5] = kernel_name HOST pointer (user-mode emulation)
// payload[6] = flags/args_count
```

**Commit 建议**：`feat(gpu): define GPU_OP_DISPATCH_KERNEL = 0x10C + payload contract (tadr-308 HSK-6)`

### #2 实现 ioctl 0x29 handler

**当前状态**：
- `user_kernel_module_unload` (`plugins/gpu_driver/hal/hal_user.cpp:722-726`) 返 `-ENOSYS`
- `handleUnloadKernelModule` (`plugins/gpu_driver/drv/gpgpu_device.cpp:1132-1136`) 转发到 stub
- 注释 "drv/ folds this into FREE_BO" 是**伪实现**，无法工作（FREE_BO 验证 `handles_.valid(handle)` 而 code BO 不在该表）

**期望变更** (`hal_user.cpp`)：
```cpp
// 替换 user_kernel_module_unload 当前实现
static int user_kernel_module_unload(void *ctx, void *args) {
    (void)ctx;
    auto* a = static_cast<gpu_unload_kernel_module_args*>(args);
    // code BO 已在 VRAM (HAL buddy heap 分配); module_handle = vram_addr
    return hal_mem_free(hal_, a->module_handle);  // ~1 line change
}
```

**Commit 建议**：`fix(hal): implement ioctl 0x29 handler for code BO unload (tadr-308 HSK-6)`

**测试**：`tests/test_hal_kernel_module_standalone.cpp` 加 test case `test_unload_kernel_module_vram_addr`

### #3 决策 Mode A interim consumer for 0x10C

**关键事实**：Mode A 当前无 PTXIR executor（PTX-EMU 已移出 UsrLinuxEmu 进程 per ADR-090 §D4 frozen baseline）。

**选项**：
- **(a)** `hardware_puller_emu.cpp` DISPATCH state 加 case 返 success-stub — 最小改动，但 e2e 无 kernel 执行
- **(b)** 扩展 `translateLaunch` 回调（per `sim/hardware/hardware_puller_emu.cpp:273-276`）作为 Mode A interim consumer — 中等改动
- **(c)** 声明 T-013 real-path e2e 出 PoC 范围（mock-only）等待 CppTLM Mode B (P0-P4 9-week timeline per ADR-090 v2 §E)

**推荐**：**(c) mock-only + T-013 mock 测试覆盖**（PoC 阶段足够，real-path e2e 推迟到 CppTLM Mode B 落地后）

**Commit 建议**：`docs(roadmap): declare T-013 real-path e2e mock-only pending CppTLM Mode B`

### #4 Payload layout 写入 HSK-6 joint protocol

**期望**：在 ADR-090 v2 annex §E 跟踪表 + UsrLinuxEmu `docs/05-advanced/adr-090-cross-repo-coordination.md` 追加 payload layout 引用。

**Commit 建议**：`docs(adr-090-annex): record HSK-6 payload layout for GPU_OP_DISPATCH_KERNEL = 0x10C`

### #5 kernel_name host-pointer 约定 ack

**期望**：在 `plugins/gpu_driver/shared/gpu_types.h` 加注释说明 user-mode-emulation-only host pointer semantics（不可移植到真 KMD, per ADR-001）。

**Commit 建议**：`docs(gpu): document kernel_name host pointer as user-mode-emulation only`

### #6 文档化 code BO bypass

**期望**：在 `plugins/gpu_driver/shared/gpu_ioctl.h:740-779` 注释说明 code BO 不进入 `bo_map_`/`handles_` 表，因此 **FREE_BO 不接受 code BO**——必须用 **0x29 UNLOAD_KERNEL_MODULE**。

**Commit 建议**：`docs(gpu_ioctl): document code BO bypass - use ioctl 0x29 not FREE_BO`

## 依赖关系

```
#1 (opcode 定义) ──┐
#4 (payload 协议) ─┤──→ T-013 可实施
#5 (host ptr ack) ─┘
                   │
#3 (Mode A 决策) ──┴──→ T-013 e2e 可启动
                   
#2 (0x29 handler) ────→ T-003 可实施
#6 (code BO 文档) ────→ #2 实施清晰
```

## 跨仓 commit 顺序（per ADR-035 §R5.1）

```
i.   UsrLinuxEmu #2 (0x29 handler) ──→ ii
ii.  UsrLinuxEmu #1 (0x10C 定义) + #4-#6 文档 ──→ iii
iii. TaskRunner tadr-308 Phase 2 实施 (T-001~T-014)
iv.  TaskRunner submodule bump → UsrLinuxEmu
v.   后续: CppTLM Mode B 集成 (HSK-6 protocol)
```

## 参考

- TaskRunner [tadr-308](https://github.com/chisuhua/TaskRunner/blob/c66b4d0/docs/shared/adr/tadr-308-igpu-driver-vram-load.md) (canonical)
- TaskRunner [实施路线图 R2](https://github.com/chisuhua/TaskRunner/blob/4705340/docs/umd-evolution/roadmap/tadr-308-implementation-roadmap.md)
- UsrLinuxEmu [ADR-090 v2](https://github.com/chisuhua/UsrLinuxEmu/blob/main/docs/00_adr/adr-090-ptxir-via-h2d-dma-v2.md) §D1 + §D3.3
- UsrLinuxEmu [annex §E](https://github.com/chisuhua/UsrLinuxEmu/blob/main/docs/05-advanced/adr-090-cross-repo-coordination.md) (tracking table)
- PTX-EMU [ADR-0029 §D8](https://github.com/chisuhua/PTX-EMU/blob/main/docs/adr/ADR-0029-ptxemu-image-executor.md#d8-cp-端集成集成--integrate-load-er) (pending amendment per ADR-090 v2 §C4)
- Oracle session [`ses_fdffa6689ffeQ0vsWd06LuQgbg`](https://oracle-sessions/ses_fdffa6689ffeQ0vsWd06LuQgbg) (R2 verdict)

## owner 必做（请尽快）

1. **确认 issue 内容**（上方 Issue Body）
2. **owner 评估 6 项**（特别是 #1 #2 #3 HARD blocker）
3. **分配每项 owner + 估计完工日期**
4. **回复 TaskRunner issue #10**（[link](https://github.com/chisuhua/TaskRunner/issues/10)）关联此 issue

## 后续跟踪

- TaskRunner [tadr-308 implementation roadmap R2](https://github.com/chisuhua/TaskRunner/blob/4705340/docs/umd-evolution/roadmap/tadr-308-implementation-roadmap.md) §Phase 0 BLOCKING GATE
- TaskRunner [tadr-308 tasks.md](https://github.com/chisuhua/TaskRunner/blob/cc59cf8/openspec/changes/2026-08-18-tadr-308-igpu-driver-vram-load/tasks.md) (15+ 任务待 Phase 0 通过)
```

---

## 📋 owner 操作清单

```bash
# 1. 复制上方"Issue Body"内容（两个 ```markdown 块之间）
# 2. 创建 UsrLinuxEmu issue：
gh issue create --repo chisuhua/UsrLinuxEmu \
  --title "[BLOCKING] tadr-308 (TaskRunner) 需要 6 项 UsrLinuxEmu 协调 — 2 项 HARD blocker" \
  --body-file issue-body.md \
  --label "cross-repo,blocking,hal,gpu-driver"
# 3. 记录 issue 编号到本地跟踪（本文档）
# 4. 关联 TaskRunner #10 评论："已创建 UsrLinuxEmu issue #N，请 owner 评估 6 项"
```

## 跟踪记录

| 日期 | 事件 | 链接 |
|---|---|---|
| 2026-08-20 | TaskRunner issue #10 跟踪 tadr-308 状态 | [TaskRunner #10](https://github.com/chisuhua/TaskRunner/issues/10) |
| 2026-08-20 | 本文起草（per Oracle R2 + Metis review） | 本文件 |
| ⏳ | UsrLinuxEmu issue 创建 | ⏳ owner 待 file |
| ⏳ | #1 #2 #3 owner 决策 | ⏳ HARD blocker |
| ⏳ | Phase 0 完成 | ⏳ T-013/T-003 实施解锁 |