---
name: worker-lock-coordinator
description: 让 worker 通过 Agent Lock Coordinator 自行申请、观察、续租和释放共享资源锁。
---

# Worker 锁协调

连接桌面程序显示的本机 MCP endpoint，例如 `http://127.0.0.1:37991/mcp`。本服务只管理逻辑锁，不会代你运行命令。

## 使用顺序

1. 调用 `register_agent`，传入自己的稳定名称和 `role: "worker"`。
2. 需要共享宿主前调用 `acquire_lock`，至少提供 `resource`、`agentId` 和 `sessionToken`。可以附带 `reason`、`metadata`、`priority` 和 `leaseSeconds`。
3. 返回 `state: "held"` 且包含 `lock.leaseToken` 后，才可以进入临界区。返回 `queued` 时记录 `requestId`，用 `lock_status` 查看状态和队列位置；不要自己轮询文件或争抢。
4. 若设置了 `leaseSeconds`，临界区运行期间定期调用 `heartbeat`。
5. 所有由你启动的进程结束后，使用 `release_lock` 携带 `lockId` 和 `leaseToken` 释放。若钩子拒绝释放，先停止仍在运行的进程，再重试。

## UE 示例边界

可以把 `ue-build`、`ue-editor` 或项目约定的资源名作为锁名。编译命令仍由 worker 自己启动；UBT 可额外使用 `-WaitMutex`。锁服务不会检查命令是否真的启动，因此不要在进程仍运行时释放锁。

## 失败处理

- `queued` 是正常等待状态，不是失败；不要重复申请同一资源。
- `hook_rejected` 表示服务端检测到仍有受保护进程或检查器失败，按 fail-closed 处理。
- `forbidden` 通常表示 agent/session、lock owner 或 leaseToken 不匹配；不要请求 orchestrator 代为删除。
- 会话断开后重新注册会得到新 session token；仍需持有原 lease token 才能释放自己的锁。若原 token 丢失，向管理员报告，由管理员核验进程后处理孤儿锁。

