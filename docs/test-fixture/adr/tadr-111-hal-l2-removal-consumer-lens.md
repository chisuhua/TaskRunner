---
SCOPE: TEST-FIXTURE
STATUS: PROPOSED
---

# TADR-111: HAL L2 Foundation Removal Consumer-Lens

**状态**: 🔄 Proposed
**日期**: 2026-08-04 (创建)
**提案人**: Chi Suhua
**评审者**: (待定)
**关联 ADR (UsrLinuxEmu)**: (无独立 canonical — 内部 drv→sim 重构,见 5 项 improvement proposals)
**关联 Source**: tests/umd/test_cu_graph*.cpp, test_cu_mem_pool.cpp, test_cu_stream_capture.cpp;
include/test_fixture/gpu_driver_client.h:569-810

---

## Context

UsrLinuxEmu 2026-08-03/04 完成 Stage 4 L2 foundation Phase 2 (commit `489655a`): 27 HAL fn-ptr +
5 headers (`gpu_hal.h` 294 行 + `gpu_hal_handles.h`),并提交 5 项 legacy emu 移除提案
(`63d2162`: `gpu-queue-emu` / `graph` / `hardware-puller-emu` / `mem-pool` / `stream-capture`)。
**ABI CI gate** (08-03, `1ebf8f5`) 验证 ioctl 派发表 37→38 handlers 编号/struct 一致性 → ioctl 契约稳定。
TaskRunner 无代码变更需求 (PR #7 GpuDriverClient forwarding 已落地),但 5 项 emu 移除改变 sim
行为语义,需回归验证 (mock + umd E2E)。

## Decision

1. consumer-lens mirror,与 TADR-110 互补不重复: TADR-110 记录接口映射,本 TADR 聚焦上游重构
   对 TaskRunner 消费路径的行为影响。
2. 影响分类:
   - **直接相关** (TADR-110 已记录): `stream_capture` / `mem_pool` / `graph` emu 移除 —
     Phase 3.1/3.2 测试必须真实路径
   - **间接相关**: `gpu-queue-emu` 移除 — 影响 `submit_batch`/`submit_launch` 的 sim 队列行为
   - **间接相关**: `hardware-puller-emu` 移除 — 影响 Puller semaphore 行为 (与 tadr-302 相关)
3. 验证要求:
   - **mock 测试不受影响** (`tests/test_fixture/mock_gpu_driver.hpp` 独立于 sim)
   - **umd E2E 需重跑**: `test_cu_graph` / `test_cu_mem_pool` / `test_cu_stream_capture`
   - **test-fixture 端无 Phase 3.1/3.2 真实测试** (因 `CudaStub` 未实现,见 TADR-110)
4. STATUS=PROPOSED,实施路径: 重跑 umd E2E + 记录结果 → 通过后升 ACCEPTED。

## Consumer-Lens

不重复 TADR-110 的 ioctl 映射表,聚焦"上游 HAL 重构 → TaskRunner 观察到的行为变化"。
ABI 稳定由 UsrLinuxEmu 端 CI gate 保证,TaskRunner 关注的是 sim 行为语义变化对测试与
shim 降级路径的影响。

## Consequences

### 正面

- 明确上游重构影响范围,避免 TaskRunner 侧盲改
- 记录验证要求,为后续回归测试提供依据
- ABI CI gate 保证契约稳定,降低跨仓维护成本

### 负面 / 风险

- 回归验证需执行时间 (umd E2E 全量重跑)
- 间接影响 (`gpu-queue` / `hardware-puller`) 需进一步分析是否影响现有 submit/fence 测试
- 与 UsrLinuxEmu Stage 5 trigger-gated 后续清理可能产生连锁影响

### 实施路径备注

- HAL fn-ptr 化是 UsrLinuxEmu **内部 drv→sim 边界重构**,不外泄到 ioctl 边界
- 5 项 emu 移除 proposals 状态: queued,未开始实施 (按 Stage 4.7 路线)

## 跨引用

- **tadr-110**: IGpuDriver Phase 3.1/3.2 Real-Path consumer-lens (接口映射)
- **tadr-301**: IGpuDriver 47 方法契约 (shared)
- **tadr-302**: Sync Primitives 抽象 (Puller semaphore 相关)
- **UsrLinuxEmu**: `489655a` (27 fn-ptrs), `63d2162` (5 removal proposals), `1ebf8f5` (ABI CI gate)
- **improvement proposals**: `stage4-l2-foundation-removal-{gpu-queue,graph,hardware-puller,mem-pool,stream-capture}`
- **关联 Source**: `tests/umd/test_cu_graph*.cpp`, `test_cu_mem_pool.cpp`, `test_cu_stream_capture.cpp`;
  `include/test_fixture/gpu_driver_client.h`:569-810

---

**最后更新**: 2026-08-04
