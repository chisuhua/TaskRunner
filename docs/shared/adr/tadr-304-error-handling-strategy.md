---
SCOPE: SHARED
STATUS: ACCEPTED
DECISION_DATE: 2026-06-25
---

# ADR: Error Handling Strategy (H-5)

## Context

TaskRunner 横跨 3 scope（test-fixture / umd-evolution / shared），需要在所有 scope 间使用**统一**的错误处理策略。`include/shared/error_handling.hpp`（定义在 [tadr-303](./tadr-303-error-handling.md)）提供了**抽象层**——`Result<T>` tagged-union + `ErrorCode` enum，但**未规定**：

1. **错误码语义**：何时用 `-ENOSYS` vs `-EINVAL` vs `-EAGAIN`？
2. **错误传播规则**：跨 scope 调用时是否要 wrap？还是直接 propagate？
3. **错误日志集成**：错误是否要自动 log？log 级别？
4. **致命 vs 可恢复错误**：哪些错误应该 abort 进程？哪些允许 retry？

没有统一策略，不同 scope 自行选择错误码导致**跨仓调试困难**（UsrLinuxEmu 端 `errno` 与 TaskRunner 端 `ErrorCode` 含义不一致）。

## Decision

本 TADR **扩展自 [tadr-303](./tadr-303-error-handling.md)**——303 定义 `Result<T>` / `ErrorCode` 抽象，本 TADR 定义**策略层**（semantics + propagation + logging）。

### 错误码语义（Linux 错误码约定）

**强制使用 Linux 错误码**（`<errno.h>`），**禁止自定义错误码**：

| 错误码 | 语义 | 何时使用 |
|--------|------|---------|
| `0` | 成功 | 所有 public API |
| `-EINVAL` | 无效参数 | 参数越界、handle 非法、flags 不识别 |
| `-ENOMEM` | 内存不足 | `alloc_bo` / `mmap` / `va_space_create` 失败 |
| `-ENOSPC` | 设备空间不足 | 显存分配超额 |
| `-ENOENT` | 资源不存在 | handle 不在 `handle_map_` |
| `-EBUSY` | 资源忙 | 销毁正在使用的 `va_space` |
| `-EAGAIN` | 重试 | 队列满、临时状态冲突 |
| `-ENOSYS` | 不支持 | umd-evolution scope 中尚未实现的 Phase D 特性 |
| `-ENODEV` | 设备未初始化 | `open()` 前调用 `submit_*` |
| `-EIO` | I/O 错误 | `ioctl` 失败、driver plugin 异常 |
| `-ETIMEDOUT` | 超时 | `wait_fence` 超时（不是 0 timeout） |
| `-EINTR` | 中断 | `wait_fence` 被 signal 中断 |
| `-ECANCELED` | 取消 | `destroy_queue` 强制取消 pending 操作 |
| `-EPERM` | 权限不足 | 跨进程 handle 越权访问 |

### 错误传播规则

**核心规则：跨 scope 边界时保持错误码不变（NO wrap）**。

```cpp
// ✅ 正确：直接 propagate
IGpuDriver::Result<int> GpuDriverClient::submit_batch(...) {
    int ret = ioctl(GPU_IOCTL_PUSHBUFFER_SUBMIT_BATCH, &args);
    if (ret < 0) {
        return make_error(static_cast<ErrorCode>(-ret));  // 直接传 Linux errno
    }
    return args.fence_id;
}

// ❌ 错误：wrap 自定义错误
return make_error(ErrorCode::GPU_IOCTL_FAILED);  // 丢失原始 errno
```

**caller 端决策**：
1. **可恢复错误**（`EAGAIN` / `EINTR`）：retry loop（最多 3 次 + backoff）
2. **参数错误**（`EINVAL` / `ENOENT`）：直接 fail，不再 retry
3. **资源错误**（`ENOMEM` / `ENOSPC`）：返回 caller 决定（OOM kill / graceful degradation）
4. **未知错误**：log `WARNING` + 返回原始错误码

### 错误日志集成

**强制规则**（shared-scope 内）：

```cpp
#define TR_LOG_ERR(code, fmt, ...) \
    do { \
        if (code != 0) { \
            fprintf(stderr, "[TaskRunner] %s:%d: " fmt " (errno=%d)\n", \
                __FILE__, __LINE__, ##__VA_ARGS__, -(code)); \
        } \
    } while (0)
```

- **`ErrorCode != 0`**：自动 log `stderr`（含文件名 + 行号 + errno 数值）
- **`ErrorCode == 0`**：无 log（避免噪音）
- **log 级别**：错误用 `stderr`（不可关闭），警告用 `stderr`（可关闭 by env var `TASKRUNNER_QUIET=1`）

### 致命 vs 可恢复

| 错误类型 | 策略 | 理由 |
|---------|------|------|
| `ENOSYS` | **可恢复** + log WARNING | umd-evolution 占位代码，预期场景 |
| `EINVAL` / `ENOENT` | **可恢复** + log ERROR | caller bug，开发者应立即看到 |
| `ENOMEM` / `ENOSPC` | **可恢复** + log ERROR | 系统资源耗尽，由 caller 决定 |
| `EIO` from ioctl | **可恢复** + log ERROR | 驱动异常，但不应 kill TaskRunner |
| Internal invariant violation (assert) | **致命** + abort | 不可恢复的程序 bug |

**禁止**直接 `abort()` 应对 EIO / EINVAL 等可恢复错误（违反 Linux 进程模型）。

## Consequences

### 正面

- **跨 scope 一致性**：test-fixture / umd-evolution / shared 使用相同错误码语义
- **调试友好**：log 自动包含文件 + 行号 + errno 数值
- **跨仓可移植**：TaskRunner 与 UsrLinuxEmu 共享 Linux errno 空间（无需转换表）
- **上层 caller 易处理**：可恢复 vs 致命有清晰分类

### 负面 / 风险

- **强制 stderr log**：单元测试可能受干扰（需用 `TASKRUNNER_QUIET=1`）
- **Linux errno 空间有限**：超过 256 种错误码时需要新增 enum（但目前够用）
- **错误码语义主观**：`EINVAL` vs `EFAULT` 边界模糊（需 PR review 强制）

## Alternatives Considered

**A. 自定义 ErrorCode enum（不映射 Linux errno）** — 拒绝：(1) 跨仓需要维护 errno 转换表；(2) 上层 caller 习惯 Linux 错误码（POSIX 兼容）；(3) 失去 Linux 内核 `strerror()` 等工具集成。

**B. 异常（std::exception）** — 拒绝：(1) [tadr-303](./tadr-303-error-handling.md) 明确禁止异常（与 Linux 内核模型不一致）；(2) 性能开销（throw/catch）；(3) shared-scope 是 header-only，异常 ABI 风险。

**C. HRESULT 风格（32-bit composite）** — 拒绝：(1) Linux 生态不习惯；(2) 高 16 位 facility code 在 TaskRunner 无意义。

## References

- [tadr-303-error-handling.md](./tadr-303-error-handling.md) — **基础抽象**（`Result<T>` + `ErrorCode` enum 模板定义）
- [tadr-301-igpu-driver-contract.md](./tadr-301-igpu-driver-contract.md) — IGpuDriver 28/31 方法的错误返回约定（"Return 0 on success / negative Linux error code on failure"）
- [tadr-107-shared-infrastructure-boundary.md](./tadr-107-shared-infrastructure-boundary.md) — Shared scope review 要求（本 TADR 属于 shared 范畴，dual-ack 适用）
- `include/shared/error_handling.hpp` — canonical `Result<T>` + `ErrorCode` 实现
- UsrLinuxEmu [ADR-035 governance-policy](../../../docs/00_adr/adr-035-governance-policy.md) — 跨仓错误处理一致性要求
