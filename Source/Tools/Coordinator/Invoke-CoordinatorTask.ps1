<#
.SYNOPSIS
    Agent UE Coordinator 阻塞式任务封装脚本（编译、托管 Editor 启停与会话状态持久化）。

.DESCRIPTION
    本脚本面向 Worker 与 Orchestrator 会话，通过 Streamable HTTP JSON-RPC 调用本机
    Agent UE Coordinator（默认 http://127.0.0.1:37992/mcp），提供以下能力：
    1. Register   ：注册协调器会话并将 agentId / sessionToken 持久化到 Saved/Coordinator/coord_state.json；
    2. Build      ：提交 UE 编译任务并在脚本内部阻塞等待至终态（succeeded / failed / cancelled），避免模型多轮轮询；
    3. OpenEditor ：申请受管 Unreal Editor MCP 实例并阻塞等待至 ready，自动持久化 activeEditorTaskId 与 mcpUrl，防止上下文压缩丢失 taskId；
    4. Status     ：查询当前或指定 taskId 的协调器任务状态，或列出当前会话可见任务；
    5. CloseTask  ：请求关闭当前或指定 taskId 的受管任务，并等待进程树完全退出、工作区锁释放后返回；
    6. Disconnect ：注销当前协调器会话并清理本地会话令牌记录。

    严格遵守项目规则：不做任何静默兜底、吞错或无效参数自动修正；任何配置缺失、HTTP 错误、WORKTREE_BUSY 或构建失败均明确报错并以非零退出码退出。
#>
[CmdletBinding()]
param(
    # 要执行的协调器动作
    [Parameter(Mandatory = $true)]
    [ValidateSet('Register', 'Build', 'OpenEditor', 'Status', 'CloseTask', 'Disconnect')]
    [string]$Action,

    # 当前工作区的 .uproject 绝对路径（用于定位工作区及默认状态文件路径）
    [Parameter(Mandatory = $true)]
    [string]$ProjectFile,

    # Agent UE Coordinator 的 MCP 地址
    [Parameter(Mandatory = $false)]
    [string]$CoordinatorUrl = 'http://127.0.0.1:37992/mcp',

    # Register 动作必填：稳定易辨识的会话名称
    [Parameter(Mandatory = $false)]
    [string]$AgentName,

    # Register 动作可选：角色（worker 或 admin）
    [Parameter(Mandatory = $false)]
    [ValidateSet('worker', 'admin')]
    [string]$Role = 'worker',

    # Register 为 admin 时必填的管理员令牌
    [Parameter(Mandatory = $false)]
    [string]$AdminToken,

    # Build 动作参数：构建目标名称
    [Parameter(Mandatory = $false)]
    [string]$Target = 'Moving_CastleEditor',

    # Build 动作参数：目标平台
    [Parameter(Mandatory = $false)]
    [string]$Platform = 'Win64',

    # Build 动作参数：构建配置
    [Parameter(Mandatory = $false)]
    [ValidateSet('Debug', 'DebugGame', 'Development', 'Shipping', 'Test')]
    [string]$Configuration = 'Development',

    # OpenEditor 动作参数：是否启用无头模式（NullRHI，仅限无需渲染的操作）
    [Parameter(Mandatory = $false)]
    [switch]$Headless,

    # Status / CloseTask 可选：显式指定目标任务 ID（未传时读取状态文件中的 activeEditorTaskId）
    [Parameter(Mandatory = $false)]
    [string]$TaskId,

    # CloseTask 可选：是否强制终止进程树（丢弃未保存修改）
    [Parameter(Mandatory = $false)]
    [switch]$Force,

    # 幂等请求键（可选）
    [Parameter(Mandatory = $false)]
    [string]$RequestKey,

    # 阻塞等待轮询间隔（秒）
    [Parameter(Mandatory = $false)]
    [ValidateRange(1, 120)]
    [int]$PollIntervalSeconds = 4,

    # 阻塞等待最大超时时间（秒）
    [Parameter(Mandatory = $false)]
    [ValidateRange(10, 7200)]
    [int]$TimeoutSeconds = 1200,

    # 可选的自定义状态文件路径（默认存于 <ProjectDir>/Saved/Coordinator/coord_state.json）
    [Parameter(Mandatory = $false)]
    [string]$StateFile
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)

# 本机回环地址直连，避免系统 HTTP_PROXY（如本机代理端口）拦截 127.0.0.1 导致 502
$coordUriObj = [System.Uri]$CoordinatorUrl
if ($coordUriObj.Host -in @('127.0.0.1', 'localhost', '::1')) {
    $env:NO_PROXY = '127.0.0.1,localhost'
    $env:HTTP_PROXY = ''
    $env:HTTPS_PROXY = ''
    [System.Net.WebRequest]::DefaultWebProxy = $null
}

# 校验 .uproject 文件路径真实存在且后缀正确
if (-not (Test-Path -LiteralPath $ProjectFile -PathType Leaf)) {
    Write-Error "[Invoke-CoordinatorTask] 项目文件不存在: '$ProjectFile'"
    exit 1
}
$resolvedProjectFile = (Resolve-Path -LiteralPath $ProjectFile).Path
if ([System.IO.Path]::GetExtension($resolvedProjectFile) -ne '.uproject') {
    Write-Error "[Invoke-CoordinatorTask] ProjectFile 必须指向 .uproject 文件: '$resolvedProjectFile'"
    exit 1
}

$projectDir = Split-Path -Parent $resolvedProjectFile
if ([string]::IsNullOrWhiteSpace($StateFile)) {
    $StateFile = Join-Path $projectDir 'Saved\Coordinator\coord_state.json'
}

# 脚本内递增的 JSON-RPC 请求编号与 MCP 会话缓存
$script:RpcId = 0
$script:McpSessionId = $null
$script:Initialized = $false

<#
.SYNOPSIS
    解析 Streamable HTTP 返回的 SSE（data: ...）或纯 JSON 响应体。
#>
function ConvertFrom-McpResponseBody {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RawContent
    )

    if ([string]::IsNullOrWhiteSpace($RawContent)) {
        return $null
    }

    $lines = $RawContent -split "`r?`n"
    $dataPayloads = @(
        $lines | Where-Object { $_ -match '^data:\s*(.+)$' } | ForEach-Object { $Matches[1] }
    )
    if ($dataPayloads.Count -gt 0) {
        return (($dataPayloads | Select-Object -Last 1) | ConvertFrom-Json)
    }
    return ($RawContent | ConvertFrom-Json)
}

<#
.SYNOPSIS
    向 Agent UE Coordinator 发送底层 JSON-RPC 请求并检查协议级错误。
#>
function Invoke-CoordinatorRpc {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Method,

        [Parameter(Mandatory = $true)]
        [hashtable]$Params,

        [Parameter(Mandatory = $false)]
        [int]$HttpTimeoutSec = 30,

        [Parameter(Mandatory = $false)]
        [switch]$IsNotification
    )

    $headers = @{
        'Accept'       = 'application/json, text/event-stream'
        'Content-Type' = 'application/json'
    }
    if (-not [string]::IsNullOrWhiteSpace($script:McpSessionId)) {
        $headers['Mcp-Session-Id'] = $script:McpSessionId
    }

    if ($IsNotification) {
        $payloadObj = @{
            jsonrpc = '2.0'
            method  = $Method
            params  = $Params
        }
    }
    else {
        $script:RpcId++
        $payloadObj = @{
            jsonrpc = '2.0'
            id      = $script:RpcId
            method  = $Method
            params  = $Params
        }
    }

    $bodyJson = $payloadObj | ConvertTo-Json -Depth 12 -Compress
    $resp = Invoke-WebRequest -Uri $CoordinatorUrl -Method Post -Headers $headers -Body $bodyJson -UseBasicParsing -TimeoutSec $HttpTimeoutSec

    $respSid = $resp.Headers['Mcp-Session-Id']
    if ($respSid -is [array]) {
        $respSid = $respSid[0]
    }
    if (-not [string]::IsNullOrWhiteSpace($respSid)) {
        $script:McpSessionId = [string]$respSid
    }

    if ($IsNotification) {
        return $null
    }

    $parsed = ConvertFrom-McpResponseBody -RawContent $resp.Content
    if ($null -eq $parsed) {
        throw "协调器返回空响应: url=$CoordinatorUrl, method=$Method"
    }
    if ($parsed.PSObject.Properties['error'] -and $null -ne $parsed.error) {
        $errJson = $parsed.error | ConvertTo-Json -Depth 8 -Compress
        throw "协调器 JSON-RPC 错误 ($Method): $errJson"
    }
    return $parsed
}

<#
.SYNOPSIS
    确保已完成 MCP initialize 握手，再调用具体的协调器工具并解包返回结构。
#>
function Invoke-CoordinatorTool {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ToolName,

        [Parameter(Mandatory = $true)]
        [hashtable]$Arguments,

        [Parameter(Mandatory = $false)]
        [int]$HttpTimeoutSec = 30
    )

    if (-not $script:Initialized) {
        $null = Invoke-CoordinatorRpc -Method 'initialize' -Params @{
            protocolVersion = '2025-03-26'
            capabilities    = @{}
            clientInfo      = @{
                name    = 'moving-castle-coord-cli'
                version = '1.0.0'
            }
        } -HttpTimeoutSec 20
        $null = Invoke-CoordinatorRpc -Method 'notifications/initialized' -Params @{} -HttpTimeoutSec 15 -IsNotification
        $script:Initialized = $true
    }

    $rpcResp = Invoke-CoordinatorRpc -Method 'tools/call' -Params @{
        name      = $ToolName
        arguments = $Arguments
    } -HttpTimeoutSec $HttpTimeoutSec

    $resultObj = $rpcResp.result
    if ($null -eq $resultObj) {
        throw "协调器工具 '$ToolName' 未返回 result 字段。"
    }

    # 优先读取 structuredContent，其次解析 content[0].text
    $payload = $null
    if ($resultObj.PSObject.Properties['structuredContent'] -and $null -ne $resultObj.structuredContent) {
        $payload = $resultObj.structuredContent
    }
    elseif ($resultObj.PSObject.Properties['content'] -and $resultObj.content.Count -gt 0) {
        $firstText = $resultObj.content[0].text
        $payload = $firstText | ConvertFrom-Json
    }
    else {
        throw "协调器工具 '$ToolName' 返回内容为空。"
    }

    # 检查工具级错误（例如 WORKTREE_BUSY、unauthorized 等）
    $isToolErr = ($resultObj.PSObject.Properties['isError'] -and [bool]$resultObj.isError)
    $isOkFalse = ($payload.PSObject.Properties['ok'] -and (-not [bool]$payload.ok))
    if ($isToolErr -or $isOkFalse) {
        $detail = $payload | ConvertTo-Json -Depth 10 -Compress
        throw "协调器工具 '$ToolName' 返回失败: $detail"
    }

    return $payload
}

<#
.SYNOPSIS
    读取持久化的协调器会话状态文件；缺失或字段不完整时直接报错。
#>
function Read-CoordinatorState {
    if (-not (Test-Path -LiteralPath $StateFile -PathType Leaf)) {
        throw "协调器状态文件不存在: '$StateFile'。请先运行 -Action Register 注册会话。"
    }
    $raw = Get-Content -LiteralPath $StateFile -Raw -Encoding UTF8
    $state = $raw | ConvertFrom-Json
    if ([string]::IsNullOrWhiteSpace($state.agentId) -or [string]::IsNullOrWhiteSpace($state.sessionToken)) {
        throw "协调器状态文件 '$StateFile' 缺少有效的 agentId 或 sessionToken，请重新执行 -Action Register。"
    }
    return $state
}

<#
.SYNOPSIS
    将协调器会话与任务状态原子写入本地状态文件（位于 git 忽略的 Saved/ 目录）。
#>
function Write-CoordinatorState {
    param(
        [Parameter(Mandatory = $true)]
        [hashtable]$StateData
    )

    $stateDir = Split-Path -Parent $StateFile
    if (-not (Test-Path -LiteralPath $stateDir -PathType Container)) {
        $null = New-Item -ItemType Directory -Path $stateDir -Force
    }
    $StateData['updatedAt'] = (Get-Date).ToString('o')
    $json = $StateData | ConvertTo-Json -Depth 8
    [System.IO.File]::WriteAllText($StateFile, $json, [System.Text.UTF8Encoding]::new($false))
}

<#
.SYNOPSIS
    将 PSObject 安全转换为 Hashtable 以便更新后写回。
#>
function ConvertTo-StateHashtable {
    param(
        [Parameter(Mandatory = $true)]
        $InputObject
    )

    $ht = @{}
    foreach ($prop in $InputObject.PSObject.Properties) {
        $ht[$prop.Name] = $prop.Value
    }
    return $ht
}

<#
.SYNOPSIS
    从任务对象或状态响应中提取嵌套 task 字段（兼容返回顶层或返回 .task 子对象两种结构）。
#>
function Get-TaskRecord {
    param(
        [Parameter(Mandatory = $true)]
        $ResponsePayload
    )

    if ($ResponsePayload.PSObject.Properties['task'] -and $null -ne $ResponsePayload.task) {
        return $ResponsePayload.task
    }
    return $ResponsePayload
}

<#
.SYNOPSIS
    输出日志文件末尾若干行，辅助定位编译或 Editor 启动失败原因。
#>
function Get-LogTailText {
    param(
        [Parameter(Mandatory = $false)]
        [string]$PrimaryLogPath,

        [Parameter(Mandatory = $false)]
        [string]$FallbackStdoutPath,

        [Parameter(Mandatory = $false)]
        [int]$TailLines = 40
    )

    # 按协调器规范：主日志已生成时读 logPath，主日志尚未生成时读 stdoutLogPath
    $targetLog = $null
    if (-not [string]::IsNullOrWhiteSpace($PrimaryLogPath) -and (Test-Path -LiteralPath $PrimaryLogPath -PathType Leaf)) {
        $targetLog = $PrimaryLogPath
    }
    elseif (-not [string]::IsNullOrWhiteSpace($FallbackStdoutPath) -and (Test-Path -LiteralPath $FallbackStdoutPath -PathType Leaf)) {
        $targetLog = $FallbackStdoutPath
    }

    if ($null -eq $targetLog) {
        return "（未找到可读取的日志文件: logPath='$PrimaryLogPath', stdoutLogPath='$FallbackStdoutPath'）"
    }

    $tail = Get-Content -LiteralPath $targetLog -Tail $TailLines -Encoding UTF8 -ErrorAction Stop
    return ("--- 日志尾部 ($targetLog) ---`n" + ($tail -join "`n"))
}

# ==================== 各动作分支执行 ====================
switch ($Action) {
    'Register' {
        if ([string]::IsNullOrWhiteSpace($AgentName)) {
            Write-Error "[Invoke-CoordinatorTask] -Action Register 必须提供 -AgentName。"
            exit 1
        }
        if ($Role -eq 'admin' -and [string]::IsNullOrWhiteSpace($AdminToken)) {
            Write-Error "[Invoke-CoordinatorTask] 请求 admin 角色时必须提供 -AdminToken。"
            exit 1
        }

        $regArgs = @{
            name = $AgentName
            role = $Role
        }
        if (-not [string]::IsNullOrWhiteSpace($AdminToken)) {
            $regArgs['adminToken'] = $AdminToken
        }

        $regResp = Invoke-CoordinatorTool -ToolName 'register_agent' -Arguments $regArgs
        if ($Role -eq 'admin' -and (-not [bool]$regResp.adminGranted)) {
            Write-Error "[Invoke-CoordinatorTask] 申请 admin 角色未获授权，请检查 -AdminToken 是否正确。"
            exit 1
        }

        # 若此前状态文件中保留有 activeEditorTaskId，继承保存以免覆盖丢失
        $prevEditorTaskId = $null
        $prevMcpUrl = $null
        if (Test-Path -LiteralPath $StateFile -PathType Leaf) {
            $oldObj = (Get-Content -LiteralPath $StateFile -Raw -Encoding UTF8) | ConvertFrom-Json
            if ($oldObj.PSObject.Properties['activeEditorTaskId']) { $prevEditorTaskId = $oldObj.activeEditorTaskId }
            if ($oldObj.PSObject.Properties['mcpUrl']) { $prevMcpUrl = $oldObj.mcpUrl }
        }

        $newState = @{
            coordinatorUrl     = $CoordinatorUrl
            projectFile        = $resolvedProjectFile
            agentName          = $AgentName
            role               = $regResp.role
            adminGranted       = [bool]$regResp.adminGranted
            agentId            = [string]$regResp.agentId
            sessionToken       = [string]$regResp.sessionToken
            activeEditorTaskId = $prevEditorTaskId
            mcpUrl             = $prevMcpUrl
            lastBuildTaskId    = $null
        }
        Write-CoordinatorState -StateData $newState

        Write-Output ("[Register] 成功注册会话: agentId={0}, role={1}, stateFile={2}" -f $newState.agentId, $newState.role, $StateFile)
        exit 0
    }

    'Build' {
        $stateObj = Read-CoordinatorState
        $stateHt = ConvertTo-StateHashtable -InputObject $stateObj

        $buildArgs = @{
            agentId       = [string]$stateHt['agentId']
            sessionToken  = [string]$stateHt['sessionToken']
            projectFile   = $resolvedProjectFile
            target        = $Target
            platform      = $Platform
            configuration = $Configuration
        }
        if (-not [string]::IsNullOrWhiteSpace($RequestKey)) {
            $buildArgs['requestKey'] = $RequestKey
        }

        $submitResp = Invoke-CoordinatorTool -ToolName 'submit_build' -Arguments $buildArgs
        $task = Get-TaskRecord -ResponsePayload $submitResp
        $buildTaskId = [string]$task.taskId
        if ([string]::IsNullOrWhiteSpace($buildTaskId)) {
            Write-Error "[Invoke-CoordinatorTask] submit_build 未返回有效的 taskId。"
            exit 1
        }

        $stateHt['lastBuildTaskId'] = $buildTaskId
        Write-CoordinatorState -StateData $stateHt
        Write-Output ("[Build] 已提交编译任务 taskId={0} ({1} {2} {3})，开始阻塞等待..." -f $buildTaskId, $Target, $Platform, $Configuration)

        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        $lastSummary = ''
        while ((Get-Date) -lt $deadline) {
            $stResp = Invoke-CoordinatorTool -ToolName 'ue_task_status' -Arguments @{
                agentId      = [string]$stateHt['agentId']
                sessionToken = [string]$stateHt['sessionToken']
                taskId       = $buildTaskId
            }
            $cur = Get-TaskRecord -ResponsePayload $stResp
            $curState = [string]$cur.state
            $qPos = if ($cur.PSObject.Properties['queuePosition']) { $cur.queuePosition } else { $null }
            $summary = "state=$curState, queuePosition=$qPos"
            if ($summary -ne $lastSummary) {
                Write-Output ("[{0}] [Build {1}] {2}" -f (Get-Date -Format 'HH:mm:ss'), $buildTaskId.Substring(0, [Math]::Min(8, $buildTaskId.Length)), $summary)
                $lastSummary = $summary
            }

            if ($curState -eq 'succeeded') {
                $logPath = if ($cur.PSObject.Properties['logPath']) { [string]$cur.logPath } else { '' }
                Write-Output ("[Build] 编译成功: taskId={0}, exitCode={1}, logPath={2}" -f $buildTaskId, $cur.exitCode, $logPath)
                exit 0
            }
            if ($curState -in @('failed', 'cancelled')) {
                $logPath = if ($cur.PSObject.Properties['logPath']) { [string]$cur.logPath } else { '' }
                $stdoutPath = if ($cur.PSObject.Properties['stdoutLogPath']) { [string]$cur.stdoutLogPath } else { '' }
                $msg = if ($cur.PSObject.Properties['message']) { [string]$cur.message } else { '' }
                $tailText = Get-LogTailText -PrimaryLogPath $logPath -FallbackStdoutPath $stdoutPath -TailLines 40
                Write-Error ("[Invoke-CoordinatorTask] 编译任务未成功: taskId={0}, state={1}, exitCode={2}, message={3}`n{4}" -f $buildTaskId, $curState, $cur.exitCode, $msg, $tailText)
                exit 1
            }

            Start-Sleep -Seconds $PollIntervalSeconds
        }

        Write-Error ("[Invoke-CoordinatorTask] 等待编译任务超时 ({0}s): taskId={1}" -f $TimeoutSeconds, $buildTaskId)
        exit 1
    }

    'OpenEditor' {
        $stateObj = Read-CoordinatorState
        $stateHt = ConvertTo-StateHashtable -InputObject $stateObj

        $openArgs = @{
            agentId      = [string]$stateHt['agentId']
            sessionToken = [string]$stateHt['sessionToken']
            projectFile  = $resolvedProjectFile
            headless     = [bool]$Headless
        }
        if (-not [string]::IsNullOrWhiteSpace($RequestKey)) {
            $openArgs['requestKey'] = $RequestKey
        }

        $openResp = Invoke-CoordinatorTool -ToolName 'open_ue_mcp' -Arguments $openArgs
        $task = Get-TaskRecord -ResponsePayload $openResp
        $editorTaskId = [string]$task.taskId
        if ([string]::IsNullOrWhiteSpace($editorTaskId)) {
            Write-Error "[Invoke-CoordinatorTask] open_ue_mcp 未返回有效的 taskId。"
            exit 1
        }

        # 立即持久化 activeEditorTaskId，防止启动等待途中中断或上下文压缩丢失 taskId
        $stateHt['activeEditorTaskId'] = $editorTaskId
        $stateHt['mcpUrl'] = $null
        Write-CoordinatorState -StateData $stateHt
        Write-Output ("[OpenEditor] 已提交托管 Editor 请求 taskId={0} (headless={1})，等待就绪..." -f $editorTaskId, [bool]$Headless)

        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        $lastSummary = ''
        while ((Get-Date) -lt $deadline) {
            $stResp = Invoke-CoordinatorTool -ToolName 'ue_task_status' -Arguments @{
                agentId      = [string]$stateHt['agentId']
                sessionToken = [string]$stateHt['sessionToken']
                taskId       = $editorTaskId
            }
            $cur = Get-TaskRecord -ResponsePayload $stResp
            $curState = [string]$cur.state
            $qPos = if ($cur.PSObject.Properties['queuePosition']) { $cur.queuePosition } else { $null }
            $summary = "state=$curState, queuePosition=$qPos"
            if ($summary -ne $lastSummary) {
                Write-Output ("[{0}] [Editor {1}] {2}" -f (Get-Date -Format 'HH:mm:ss'), $editorTaskId.Substring(0, [Math]::Min(8, $editorTaskId.Length)), $summary)
                $lastSummary = $summary
            }

            if ($curState -eq 'ready') {
                $mcpUrl = if ($cur.PSObject.Properties['mcpUrl']) { [string]$cur.mcpUrl } else { '' }
                if ([string]::IsNullOrWhiteSpace($mcpUrl)) {
                    Write-Error ("[Invoke-CoordinatorTask] Editor 已 ready 但未返回 mcpUrl: taskId={0}" -f $editorTaskId)
                    exit 1
                }
                $logPath = if ($cur.PSObject.Properties['logPath']) { [string]$cur.logPath } else { '' }
                $stateHt['activeEditorTaskId'] = $editorTaskId
                $stateHt['mcpUrl'] = $mcpUrl
                Write-CoordinatorState -StateData $stateHt

                Write-Output ("[OpenEditor] 托管 Editor 已就绪: taskId={0}, mcpUrl={1}, logPath={2}" -f $editorTaskId, $mcpUrl, $logPath)
                exit 0
            }

            if ($curState -in @('failed', 'cancelled', 'succeeded', 'exited')) {
                $logPath = if ($cur.PSObject.Properties['logPath']) { [string]$cur.logPath } else { '' }
                $stdoutPath = if ($cur.PSObject.Properties['stdoutLogPath']) { [string]$cur.stdoutLogPath } else { '' }
                $msg = if ($cur.PSObject.Properties['message']) { [string]$cur.message } else { '' }
                $stateHt['activeEditorTaskId'] = $null
                $stateHt['mcpUrl'] = $null
                Write-CoordinatorState -StateData $stateHt

                $tailText = Get-LogTailText -PrimaryLogPath $logPath -FallbackStdoutPath $stdoutPath -TailLines 40
                Write-Error ("[Invoke-CoordinatorTask] 托管 Editor 未能进入 ready 状态: taskId={0}, state={1}, exitCode={2}, message={3}`n{4}" -f $editorTaskId, $curState, $cur.exitCode, $msg, $tailText)
                exit 1
            }

            Start-Sleep -Seconds $PollIntervalSeconds
        }

        Write-Error ("[Invoke-CoordinatorTask] 等待托管 Editor 就绪超时 ({0}s): taskId={1}" -f $TimeoutSeconds, $editorTaskId)
        exit 1
    }

    'Status' {
        $stateObj = Read-CoordinatorState
        $targetId = if (-not [string]::IsNullOrWhiteSpace($TaskId)) { $TaskId } else { [string]$stateObj.activeEditorTaskId }

        if (-not [string]::IsNullOrWhiteSpace($targetId)) {
            $stResp = Invoke-CoordinatorTool -ToolName 'ue_task_status' -Arguments @{
                agentId      = [string]$stateObj.agentId
                sessionToken = [string]$stateObj.sessionToken
                taskId       = $targetId
            }
            Write-Output ($stResp | ConvertTo-Json -Depth 8)
        }
        else {
            $listResp = Invoke-CoordinatorTool -ToolName 'list_ue_tasks' -Arguments @{
                agentId      = [string]$stateObj.agentId
                sessionToken = [string]$stateObj.sessionToken
            }
            Write-Output ($listResp | ConvertTo-Json -Depth 8)
        }
        exit 0
    }

    'CloseTask' {
        $stateObj = Read-CoordinatorState
        $stateHt = ConvertTo-StateHashtable -InputObject $stateObj
        $targetId = if (-not [string]::IsNullOrWhiteSpace($TaskId)) { $TaskId } else { [string]$stateHt['activeEditorTaskId'] }

        if ([string]::IsNullOrWhiteSpace($targetId)) {
            Write-Error "[Invoke-CoordinatorTask] 未指定 -TaskId，且状态文件中无活跃的 activeEditorTaskId。"
            exit 1
        }

        $cancelResp = Invoke-CoordinatorTool -ToolName 'cancel_ue_task' -Arguments @{
            agentId      = [string]$stateHt['agentId']
            sessionToken = [string]$stateHt['sessionToken']
            taskId       = $targetId
            force        = [bool]$Force
        }
        Write-Output ("[CloseTask] 已请求关闭任务 taskId={0} (force={1})，等待进程树退出并释放工作区..." -f $targetId, [bool]$Force)

        # 阻塞等待直到协调器确认任务进入终态，防止紧接着调用 submit_build 时触发 WORKTREE_BUSY
        $deadline = (Get-Date).AddSeconds([Math]::Min($TimeoutSeconds, 180))
        while ((Get-Date) -lt $deadline) {
            $stResp = Invoke-CoordinatorTool -ToolName 'ue_task_status' -Arguments @{
                agentId      = [string]$stateHt['agentId']
                sessionToken = [string]$stateHt['sessionToken']
                taskId       = $targetId
            }
            $cur = Get-TaskRecord -ResponsePayload $stResp
            $curState = [string]$cur.state
            if ($curState -in @('cancelled', 'succeeded', 'failed')) {
                if ([string]$stateHt['activeEditorTaskId'] -eq $targetId) {
                    $stateHt['activeEditorTaskId'] = $null
                    $stateHt['mcpUrl'] = $null
                    Write-CoordinatorState -StateData $stateHt
                }
                Write-Output ("[CloseTask] 任务已结束并释放占用: taskId={0}, state={1}, exitCode={2}" -f $targetId, $curState, $cur.exitCode)
                exit 0
            }
            Start-Sleep -Seconds 2
        }

        Write-Error ("[Invoke-CoordinatorTask] 等待任务关闭超时: taskId={0}" -f $targetId)
        exit 1
    }

    'Disconnect' {
        $stateObj = Read-CoordinatorState
        $stateHt = ConvertTo-StateHashtable -InputObject $stateObj

        if (-not [string]::IsNullOrWhiteSpace([string]$stateHt['activeEditorTaskId'])) {
            Write-Error ("[Invoke-CoordinatorTask] 当前状态文件仍记录有未关闭的活跃 Editor 任务 ({0})，请先执行 -Action CloseTask。" -f $stateHt['activeEditorTaskId'])
            exit 1
        }

        $null = Invoke-CoordinatorTool -ToolName 'disconnect_agent' -Arguments @{
            agentId      = [string]$stateHt['agentId']
            sessionToken = [string]$stateHt['sessionToken']
        }

        $stateHt['sessionToken'] = $null
        Write-CoordinatorState -StateData $stateHt
        Write-Output ("[Disconnect] 已断开协调器会话: agentId={0}" -f $stateHt['agentId'])
        exit 0
    }
}
