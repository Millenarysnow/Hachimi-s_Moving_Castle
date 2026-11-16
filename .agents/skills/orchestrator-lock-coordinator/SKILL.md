---
name: orchestrator-lock-coordinator
description: 让 orchestrator 通过 Agent Lock Coordinator 观察和管理多个 worker 的共享资源锁、队列、策略和钩子。
---

# Orchestrator 锁协调

这是一个本机锁协调服务，不执行构建或编辑器命令。默认 MCP 地址由桌面程序显示，例如 `http://127.0.0.1:37991/mcp`。

## 首次连接

调用 `register_agent`，传入稳定的 agent 名称和 `role: "admin"`。只有同时提供桌面程序显示的管理员引导令牌时才会获得管理员角色；令牌错误会安全降级为 worker。保存返回的 `agentId` 与 `sessionToken`，后续每次调用都携带它们。

## 观察流程

- 用 `list_locks` 查看所有资源、当前锁、策略和队列；不要通过反复猜测文件锁状态来维护队列。
- 用 `lock_status` 查看指定请求的状态、队列位置和前方变化。
- 监听 worker 报告或按较长间隔检查状态，只在状态变化、错误或需要管理员处理时介入。

## 管理流程

- 用 `create_resource`、`delete_resource` 和 `set_strategy` 管理资源。策略支持 `fifo`、`priority` 和 `lifo`。
- 用 `set_priority` 调整仍在队列中的请求；不要用取消再重申请伪造优先级。
- 用 `force_remove_lock` 清理确认安全的活动锁或孤儿锁。服务端会先执行匹配的钩子；只有确认外部进程已经结束时才使用 `overrideHooks`。
- 用 `add_hook` 注册通用钩子。内置 `process_absent` 可在 `release`、`force_remove` 或 `expire` 前检查进程名或 PID。

## 约束

- 不能把“查询到空闲”当成已获得锁，只有 `acquire_lock` 返回 `state: "held"` 才能进入临界区。
- 不要代 worker 释放普通锁，也不要删除锁目录；锁服务是唯一授予者，worker 只能释放自己的 lease。
- 锁服务不启动 UBT、Editor 或测试进程。worker 仍需自行管理进程并在进程结束后释放锁。
- 发现 `orphaned`、钩子拒绝或 lease 过期时，先检查进程和日志，再决定是否强制移除。

