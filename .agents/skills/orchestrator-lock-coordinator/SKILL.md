---
name: orchestrator-lock-coordinator
description: 指导 orchestrator 通过 Agent UE Coordinator 管理多个 worker 的 UE 编译、Editor MCP 和通用资源锁。
---

# Orchestrator 锁协调与 UE 任务

使用 Agent UE Coordinator 界面显示的控制 MCP 地址，默认为 `http://127.0.0.1:37992/mcp`。为同一组 worker 指定同一个协调器实例，使所有 UE 请求共享工作区占用和并发配额。

## 首次连接

调用 `register_agent`，传入稳定的 `name`、`role: "admin"` 和桌面程序显示的 `adminToken`。确认返回 `adminGranted: true` 后再执行管理操作。保存返回的 `agentId` 与 `sessionToken`，后续每次调用都携带它们。

## UE 调度与排障

- UE 编译、Editor 启停和 Editor MCP 转发由协调器接管。要求 worker 用 `submit_build` 提交编译，用 `open_ue_mcp` 申请 Editor。`projectFile` 均填当前工作区的 `.uproject` 绝对路径。Editor 就绪后使用返回的 `mcpUrl`。禁止安排 worker 自行编译、启动或结束 Editor，或绕开协调器的 MCP 入口。
- 用 `set_ue_config` 更新 `engineRoot`、`maxBuilds`、`maxBuildActions` 或 `maxEditors`。`engineRoot` 填包含 `Engine` 文件夹的 UE 5.8 安装目录。UBT 动作预算由所有托管构建共享。有排队或活动任务时，先让这些任务结束，再更换引擎目录。
- 用 `list_ue_tasks` 查看所有 UE 任务，用 `ue_task_status` 按 `taskId` 查看状态和日志。收到 `WORKTREE_BUSY` 时按 `blockingTaskId` 查明占用，等任务结束后安排重试。若工具不可用，先检查 worker 是否连到了具备 UE 工具的同一实例。
- 管理员可调用 `cancel_ue_task` 取消任务或关闭 Editor。对已就绪的 Editor 先请求正常退出。确需强制结束时用 `force: true`，这会丢弃未保存的修改。

## 通用资源锁

### 观察流程

- 用 `list_locks` 查看所有资源、当前锁、策略和队列；不要通过反复猜测文件锁状态来维护队列。
- 用 `lock_status` 查看指定请求的状态、队列位置和前方变化。
- 监听 worker 报告或按较长间隔检查状态，只在状态变化、错误或需要管理员处理时介入。

### 管理流程

- 用 `create_resource`、`delete_resource` 和 `set_strategy` 管理资源。策略支持 `fifo`、`priority` 和 `lifo`。
- 用 `set_priority` 调整仍在队列中的请求；不要用取消再重申请伪造优先级。
- 用 `force_remove_lock` 清理确认安全的活动锁或孤儿锁。服务端会先执行匹配的钩子；只有确认外部进程已经结束时才使用 `overrideHooks`。
- 用 `add_hook` 注册通用钩子。内置 `process_absent` 可在 `release`、`force_remove` 或 `expire` 前检查进程名或 PID。

### 约束

- 不能把“查询到空闲”当成已获得锁，只有 `acquire_lock` 返回 `state: "held"` 才能进入临界区。
- 不要代 worker 释放普通锁，也不要删除锁目录；锁服务是唯一授予者，worker 只能释放自己的 lease。
- 通用锁适用于其他共享资源。worker 管理自己在通用锁临界区启动的进程，进程全部结束后释放锁。
- 发现 `orphaned`、钩子拒绝或 lease 过期时，先检查进程和日志，再决定是否强制移除。

