---
name: worker-lock-coordinator
description: 指导 worker 通过 Agent UE Coordinator 提交 UE 编译、申请 Editor MCP 和使用通用资源锁。
---

# Worker 锁协调与 UE 任务

连接 Agent UE Coordinator 界面显示的控制 MCP 地址，默认为 `http://127.0.0.1:37992/mcp`。调用 `register_agent`，传入自己的稳定名称 `name` 和 `role: "worker"`。后续调用协调器工具均携带返回的 `agentId` 和 `sessionToken`。

## UE 编译与 Editor MCP

**UE 编译、Editor 启停和 Editor MCP 转发已由协调器接管。Worker 必须通过控制 MCP 请求这些操作。禁止自行编译 UE，包括在命令行、IDE 或脚本中运行 `Build.bat`、UBT 或触发 Live Coding。禁止自行启停 Editor 或其 MCP 服务，也禁止直连 Editor 后端端口。** 协调器统一处理工作区互斥、排队、并发参数和日志。

1. **编译**时优先通过 `powershell -NoProfile -ExecutionPolicy Bypass -File "Source/Tools/Coordinator/Invoke-CoordinatorTask.ps1"` 调用仓库封装脚本（先执行 `-Action Register`，再执行 `-Action Build`），由脚本持久化会话并在内部阻塞等待直至 `succeeded`、`failed` 或 `cancelled`，避免在模型轮次里反复手动轮询 `ue_task_status`。若直接调用 MCP 工具 `submit_build`，需提供 `projectFile`（本地 `.uproject` 绝对路径）、`target`、`platform: "Win64"` 和 `configuration`（如 `Development`），记录返回的 `task.taskId`，用 `ue_task_status` 查询状态；失败时查看 `message`、`exitCode` 和 `logPath`，主日志尚未生成时查看 `stdoutLogPath`。
2. **申请 Editor MCP** 时优先运行 `Invoke-CoordinatorTask.ps1 -Action OpenEditor`（仅使用无需渲染的工具时可加 `-Headless`），脚本会将 `activeEditorTaskId` 与就绪后的 `mcpUrl` 自动写入 `Saved/Coordinator/coord_state.json`（防止上下文压缩丢失 `taskId`）。跑自动化测试时优先使用 `powershell -NoProfile -ExecutionPolicy Bypass -File "Source/Tools/Coordinator/Invoke-UeMcpTests.ps1"` 按 `-TestRegex` 或 `-TestNames` 阻塞执行并输出精简汇总。若直接调用 `open_ue_mcp`，等到 `ready` 后用原生 Streamable HTTP MCP 客户端连接任务返回的 `mcpUrl`；若任务提前结束，报告状态和日志。提前配置客户端时可用 `ue_mcp_endpoint` 查询或预留同一工作区的固定地址。Editor 重启后，在该地址重新初始化 MCP 会话。
3. **取消或关闭**自己提交的任务时，运行 `Invoke-CoordinatorTask.ps1 -Action CloseTask`（或直接调用 `cancel_ue_task` 并传入 `taskId` 并确认进入终态）。协调器会在进程树退出后释放工作区。仅断开 MCP 客户端连接时，Editor 会继续占用工作区。

- `queued` 是正常排队，按 `taskId` 查询 `queuePosition`。提交编译或 Editor 请求时可附上 `requestKey`。若因通信问题重试，请沿用原会话、参数和 `requestKey`，避免重复提交。
- 收到 `WORKTREE_BUSY` 时报告 `blockingTaskId`、`blockingType` 和 `blockingState`。等占用任务结束后再提交。同一目录的编译和 Editor 共用占用。
- 若控制 MCP 不可用、缺少 UE 工具或引擎配置有误，向 orchestrator 报告控制地址和错误，将该 UE 操作标记为受阻。禁止改用命令行、其他入口或通用资源锁继续执行 UE 操作。

## 通用资源锁

1. 使用其他共享资源前调用 `acquire_lock`，至少提供 `resource`、`agentId` 和 `sessionToken`。可以附带 `reason`、`metadata`、`priority` 和 `leaseSeconds`。
2. 返回 `state: "held"` 且包含 `lock.leaseToken` 后，才可以进入临界区。返回 `queued` 时记录 `requestId`，用 `lock_status` 查看状态和队列位置。不要重复申请同一资源或轮询文件锁。
3. 若设置了 `leaseSeconds`，临界区运行期间定期调用 `heartbeat`。
4. 临界区内由你启动的进程全部结束后，使用 `release_lock` 携带 `lockId` 和 `leaseToken` 释放。若钩子拒绝释放，先核对进程是否还在运行或检查器是否失败，再重试。

- `forbidden` 通常表示资源锁所有者或 `leaseToken` 不匹配。核对原会话与令牌。原会话失效或令牌丢失时，报告管理员核验进程和锁状态。

