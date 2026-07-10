---
SCOPE: SHARED
STATUS: SUPERSEDED
DECISION_DATE: 2026-06-25
SUPERSEDED_BY: openspec/changes/umd-evolution-build-default-on
SUPERSEDED_DATE: 2026-07-09
---

# ADR: Build Mode Selection (H-5)

> ## ⚠️ SUPERSEDE NOTICE (2026-07-09)
>
> This ADR is **SUPERSEDED** by `openspec/changes/umd-evolution-build-default-on`.
> The original decision ("default = test-fixture") is reversed: the new default
> is **umd-evolution** (i.e., `cmake -B build` now builds the UMD shim and
> tests/umd/ by default). `TASKRUNNER_BUILD_MODE=test-fixture` is preserved as
> an explicit opt-out.
>
> **Underlying technical rationale remains valid** — the analysis of CMake
> option design, opt-in/opt-out semantics, and shared-scope coupling in this
> ADR still applies. Only the choice of which mode is the *default* is
> reversed, driven by the UMD-EVOLUTION → ACCEPTED promotion track.
>
> See `openspec/changes/umd-evolution-build-default-on/` for full rationale.

## Context

H-5 引入三 scope 分离（test-fixture / shared / umd-evolution）。`umd-evolution` scope 是**实验性** future-evolution 路径（CUDA Runtime / User-Mode Driver 雏形），其代码骨架仅占位，**不应污染主构建**。但 shared-scope 头文件（如 `IGpuDriver` 31 方法接口契约）必须同时被 `test-fixture` 和 `umd-evolution` 引用——需要一种显式的构建模式选择机制，让默认构建只包含 `test-fixture`，opt-in 开启 `umd-evolution`。

`AGENTS.md` §Scope Classification (lines 161–197) 已经定义了三 scope 的目录布局和元数据规则，但缺少对应的**构建模式规范**——本 TADR 补齐这一层。

## Decision

> **STATUS: SUPERSEDED** (see top-of-file notice). The decision below is preserved
> for historical reference; the **current behavior** is in
> `openspec/changes/umd-evolution-build-default-on/`.

`CMakeLists.txt` 暴露单一 CMake option `TASKRUNNER_BUILD_MODE`，二选一字符串：

| 值 | 行为 | 默认 |
|----|------|------|
| `test-fixture` | 编译 `src/test_fixture/` + `include/test_fixture/` + `src/shared/` + `include/shared/` + `tests/test_fixture/` | ✅ 是（变更前）/ ❌ opt-out（变更后） |
| `umd-evolution` | 在 `test-fixture` 基础上**额外**编译 `src/umd/` + `include/umd/` + `tests/umd/` | ❌ 否（变更前）/ ✅ **默认**（变更后，2026-07-09 起） |

**关键规则：**

1. ~~**default = `test-fixture`**~~ → **default = `umd-evolution`**（2026-07-09 反转，见 openspec/changes/umd-evolution-build-default-on）
2. `TASKRUNNER_BUILD_MODE=test-fixture` 是显式 opt-out，用于不需要 shim 的用户
3. `TASKRUNNER_BUILD_MODE=umd-evolution` 现为默认的别名（保留向后兼容）
4. **shared-scope 始终编译**：与 `TASKRUNNER_BUILD_MODE` 无关（shared 是 test-fixture 和 umd-evolution 的共同依赖）
5. **范围检查**：未知值必须 `FATAL_ERROR` 拒绝（如 `cmake -DTASKRUNNER_BUILD_MODE=foo` 应 fail）

**CMake 实现示意**（canonical 在 `CMakeLists.txt`）：

```cmake
set(TASKRUNNER_BUILD_MODE "test-fixture" CACHE STRING "Build mode: test-fixture | umd-evolution")
set_property(CACHE TASKRUNNER_BUILD_MODE PROPERTY STRINGS "test-fixture" "umd-evolution")

if(TASKRUNNER_BUILD_MODE STREQUAL "umd-evolution")
    add_definitions(-DTASKRUNNER_UMD_EVOLUTION=1)
    include_directories(${CMAKE_SOURCE_DIR}/include/umd)
    # ... glob src/umd/ + tests/umd/
elseif(TASKRUNNER_BUILD_MODE STREQUAL "test-fixture")
    # 默认：仅编译 test-fixture + shared
else()
    message(FATAL_ERROR "Unknown TASKRUNNER_BUILD_MODE=${TASKRUNNER_BUILD_MODE}")
endif()
```

## Consequences

### 正面

- **零侵入 umd-evolution**：默认构建下 `umd-evolution` 占位代码完全不参与编译
- **显式 opt-in**：CI / 开发者主动选择 umd-evolution 才编译
- **shared 始终安全**：shared-scope 头文件改动立即在 default 构建生效（避免 silent regression）

### 负面 / 风险

- **CMake 复杂度增加**：从单模式 → 双模式 + 范围检查
- **开发者必须理解 scope 边界**：`src/umd/*.cpp` 内的 `// SCOPE: UMD-EVOLUTION` 注释是软约束（CMake 不强制）

## Alternatives Considered

**A. 单一 `test-fixture` 模式（无 option）** — 拒绝：umd-evolution 永远不可达，违反 H-5 §Decision 5 "umd-evolution 代码骨架仅占位"。

**B. `dlopen()` 动态加载 umd-evolution** — 拒绝：(1) 增加运行时复杂度（符号解析、ABI 兼容）；(2) TaskRunner 是 C++ header-only shared-scope 抽象，动态加载无收益；(3) CI 难做静态分析。

**C. 三个 mode（test-fixture / umd-evolution / all）** — 拒绝：第三个值无实际语义（umd-evolution 本来就隐含 test-fixture）。

## References

- `AGENTS.md` lines 161–197 — §Scope Classification (H-5) 元数据规则
- `AGENTS.md` lines 189–197 — §Build Mode Selection (CMake 命令示例)
- [tadr-107-shared-infrastructure-boundary.md](./tadr-107-shared-infrastructure-boundary.md) — Shared scope 边界定义
- [tadr-106-test-fixture-scope-clarification.md](../test-fixture/adr/tadr-106-test-fixture-scope-clarification.md) — test-fixture scope 状态规则
- [tadr-204-umd-evolution-scope-clarification.md](../umd-evolution/adr/tadr-204-umd-evolution-scope-clarification.md) — umd-evolution scope 状态规则（`STATUS: PROPOSED`）+ build mode 引用
- `CMakeLists.txt` — `TASKRUNNER_BUILD_MODE` option 实际定义位置
- UsrLinuxEmu [ADR-036](../../../docs/00_adr/adr-036-three-way-separation.md) — 3-Way Architectural Separation
