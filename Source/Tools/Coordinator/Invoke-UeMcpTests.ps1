<#
.SYNOPSIS
    UE 5.8 官方 MCP 自动化测试阻塞式执行与精简汇总脚本。

.DESCRIPTION
    连接受管 Unreal Editor 的官方 ModelContextProtocol 服务端（优先使用 -McpUrl，未传时从
    <ProjectDir>/Saved/Coordinator/coord_state.json 读取 mcpUrl），一次性完成：
    1. MCP initialize 会话握手与 Mcp-Session-Id 维护；
    2. AutomationTestToolset.DiscoverTests 与 ListTests 发现真实已注册测试名；
    3. 严格校验 -TestNames（逐项核对是否存在）或 -TestRegex（至少匹配 1 项），不静默跳过；
    4. 执行 RunTests 并确认 GetTestStatus 进入完成态；
    5. 读取 GetTestResults 将完整原始 JSON 落盘至 Saved/ 目录，同时在控制台仅打印精简通过/失败/警告统计与失败详情，
       避免数千行原始测试 JSON 撑爆模型上下文。

    严格遵守项目规则：无任何静默兜底或吞错逻辑；未匹配到测试、HTTP/协议异常或存在任意失败用例时，均明确报错并以退出码 1 结束。
#>
[CmdletBinding()]
param(
    # 当前工作区的 .uproject 绝对路径
    [Parameter(Mandatory = $true)]
    [string]$ProjectFile,

    # 可选：显式指定受管 Editor 的 MCP 地址（例如 http://127.0.0.1:4968/mcp）；未传时从 coord_state.json 读取
    [Parameter(Mandatory = $false)]
    [string]$McpUrl,

    # 按正则表达式筛选完整测试路径（与 -TestNames / -ListOnly 配合或二选一）
    [Parameter(Mandatory = $false)]
    [string]$TestRegex,

    # 按精确完整测试名列表运行（每一项都必须在已注册测试列表中存在，否则报错退出）
    [Parameter(Mandatory = $false)]
    [string[]]$TestNames,

    # ListTests 预筛选前缀（默认只拉取本项目 MovingCastle 测试树）
    [Parameter(Mandatory = $false)]
    [string]$NameFilter = 'MovingCastle',

    # 仅列出匹配的已注册测试名而不执行测试
    [Parameter(Mandatory = $false)]
    [switch]$ListOnly,

    # 是否强制重新扫描测试（热重载 Python 测试后使用）
    [Parameter(Mandatory = $false)]
    [switch]$ForceRediscover,

    # RunTests 请求及等待完成的最大超时时间（秒）
    [Parameter(Mandatory = $false)]
    [ValidateRange(15, 7200)]
    [int]$TimeoutSeconds = 900,

    # GetTestStatus 补充轮询间隔（秒）
    [Parameter(Mandatory = $false)]
    [ValidateRange(1, 60)]
    [int]$PollIntervalSeconds = 4,

    # 完整测试结果 JSON 保存路径（默认存于 <ProjectDir>/Saved/Coordinator/last_test_results.json）
    [Parameter(Mandatory = $false)]
    [string]$OutputJsonPath,

    # 可选的协调器状态文件路径（默认 <ProjectDir>/Saved/Coordinator/coord_state.json）
    [Parameter(Mandatory = $false)]
    [string]$StateFile
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)

# 校验 .uproject 文件存在
if (-not (Test-Path -LiteralPath $ProjectFile -PathType Leaf)) {
    Write-Error "[Invoke-UeMcpTests] 项目文件不存在: '$ProjectFile'"
    exit 1
}
$resolvedProjectFile = (Resolve-Path -LiteralPath $ProjectFile).Path
$projectDir = Split-Path -Parent $resolvedProjectFile

if ([string]::IsNullOrWhiteSpace($StateFile)) {
    $StateFile = Join-Path $projectDir 'Saved\Coordinator\coord_state.json'
}
if ([string]::IsNullOrWhiteSpace($OutputJsonPath)) {
    $OutputJsonPath = Join-Path $projectDir 'Saved\Coordinator\last_test_results.json'
}

# 解析目标 Editor MCP 地址
if ([string]::IsNullOrWhiteSpace($McpUrl)) {
    if (-not (Test-Path -LiteralPath $StateFile -PathType Leaf)) {
        Write-Error "[Invoke-UeMcpTests] 未传入 -McpUrl 且协调器状态文件不存在: '$StateFile'。请先通过 Invoke-CoordinatorTask.ps1 -Action OpenEditor 启动受管 Editor。"
        exit 1
    }
    $coordState = (Get-Content -LiteralPath $StateFile -Raw -Encoding UTF8) | ConvertFrom-Json
    if (-not $coordState.PSObject.Properties['mcpUrl'] -or [string]::IsNullOrWhiteSpace([string]$coordState.mcpUrl)) {
        Write-Error "[Invoke-UeMcpTests] 状态文件 '$StateFile' 中无有效的 mcpUrl，请确认受管 Editor 当前处于 ready 状态。"
        exit 1
    }
    $McpUrl = [string]$coordState.mcpUrl
}

# 非 ListOnly 模式下必须明确指定 -TestRegex 或 -TestNames，禁止无意触发全量测试
if (-not $ListOnly -and [string]::IsNullOrWhiteSpace($TestRegex) -and ($null -eq $TestNames -or $TestNames.Count -eq 0)) {
    Write-Error "[Invoke-UeMcpTests] 执行测试时必须显式提供 -TestRegex 或 -TestNames（若仅查询测试列表请传 -ListOnly）。"
    exit 1
}

$script:RpcId = 0
$script:SessionId = $null

<#
.SYNOPSIS
    解析 UE Streamable HTTP MCP 返回的 SSE 数据帧或 JSON 文本。
#>
function ConvertFrom-UeMcpBody {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RawContent
    )

    if ([string]::IsNullOrWhiteSpace($RawContent)) {
        return $null
    }

    $lines = $RawContent -split "`r?`n"
    $dataLines = @(
        $lines | Where-Object { $_ -match '^data:\s*(.+)$' } | ForEach-Object { $Matches[1] }
    )
    if ($dataLines.Count -gt 0) {
        return (($dataLines | Select-Object -Last 1) | ConvertFrom-Json)
    }
    return ($RawContent | ConvertFrom-Json)
}

<#
.SYNOPSIS
    发送 JSON-RPC 请求至 UE Editor MCP 服务端。
#>
function Invoke-UeMcpRpc {
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
    if (-not [string]::IsNullOrWhiteSpace($script:SessionId)) {
        $headers['Mcp-Session-Id'] = $script:SessionId
    }

    if ($IsNotification) {
        $bodyObj = @{
            jsonrpc = '2.0'
            method  = $Method
            params  = $Params
        }
    }
    else {
        $script:RpcId++
        $bodyObj = @{
            jsonrpc = '2.0'
            id      = $script:RpcId
            method  = $Method
            params  = $Params
        }
    }

    $bodyJson = $bodyObj | ConvertTo-Json -Depth 12 -Compress
    $resp = Invoke-WebRequest -Uri $McpUrl -Method Post -Headers $headers -Body $bodyJson -UseBasicParsing -TimeoutSec $HttpTimeoutSec

    $respSid = $resp.Headers['Mcp-Session-Id']
    if ($respSid -is [array]) {
        $respSid = $respSid[0]
    }
    if (-not [string]::IsNullOrWhiteSpace($respSid)) {
        $script:SessionId = [string]$respSid
    }

    if ($IsNotification) {
        return $null
    }

    $parsed = ConvertFrom-UeMcpBody -RawContent $resp.Content
    if ($null -eq $parsed) {
        throw "UE Editor MCP 返回空响应: url=$McpUrl, method=$Method"
    }
    if ($parsed.PSObject.Properties['error'] -and $null -ne $parsed.error) {
        $errJson = $parsed.error | ConvertTo-Json -Depth 8 -Compress
        throw "UE Editor MCP JSON-RPC 错误 ($Method): $errJson"
    }
    return $parsed
}

<#
.SYNOPSIS
    调用 UE Toolset 工具并解包内层 returnValue JSON。
#>
function Invoke-UeToolsetCall {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ToolsetName,

        [Parameter(Mandatory = $true)]
        [string]$ToolName,

        [Parameter(Mandatory = $true)]
        [hashtable]$ToolArguments,

        [Parameter(Mandatory = $false)]
        [int]$HttpTimeoutSec = 30,

        [Parameter(Mandatory = $false)]
        [switch]$AllowTestErrorFlag
    )

    $rpcResp = Invoke-UeMcpRpc -Method 'tools/call' -Params @{
        name      = 'call_tool'
        arguments = @{
            toolset_name = $ToolsetName
            tool_name    = $ToolName
            arguments    = $ToolArguments
        }
    } -HttpTimeoutSec $HttpTimeoutSec

    $res = $rpcResp.result
    if ($null -eq $res) {
        throw "UE 工具 '$ToolsetName.$ToolName' 未返回 result。"
    }

    $textPayload = ''
    if ($res.PSObject.Properties['content'] -and $res.content.Count -gt 0) {
        $textPayload = [string]$res.content[0].text
    }

    $isErr = ($res.PSObject.Properties['isError'] -and [bool]$res.isError)
    if ($isErr -and (-not $AllowTestErrorFlag)) {
        throw "UE 工具 '$ToolsetName.$ToolName' 执行报错: $textPayload"
    }

    return @{
        IsError = $isErr
        RawText = $textPayload
    }
}

<#
.SYNOPSIS
    将 UE Toolset 返回的字符串 JSON（含嵌套 returnValue 字符串）解包为对象。
#>
function ConvertFrom-UeReturnValue {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RawText,

        [Parameter(Mandatory = $true)]
        [string]$ContextName
    )

    if ([string]::IsNullOrWhiteSpace($RawText)) {
        throw "UE 工具 '$ContextName' 返回空文本，无法解析 JSON。"
    }
    $outer = $RawText | ConvertFrom-Json
    if ($outer.PSObject.Properties['returnValue'] -and $null -ne $outer.returnValue) {
        if ($outer.returnValue -is [string]) {
            return ($outer.returnValue | ConvertFrom-Json)
        }
        return $outer.returnValue
    }
    return $outer
}

# 1. 初始化 UE MCP 会话
$null = Invoke-UeMcpRpc -Method 'initialize' -Params @{
    protocolVersion = '2025-03-26'
    capabilities    = @{}
    clientInfo      = @{
        name    = 'moving-castle-test-runner'
        version = '1.0.0'
    }
} -HttpTimeoutSec 25
$null = Invoke-UeMcpRpc -Method 'notifications/initialized' -Params @{} -HttpTimeoutSec 15 -IsNotification

$toolset = 'AutomationTestToolset.AutomationTestToolset'

# 2. 发现并枚举测试列表
$null = Invoke-UeToolsetCall -ToolsetName $toolset -ToolName 'DiscoverTests' -ToolArguments @{
    bForceRediscover = [bool]$ForceRediscover
} -HttpTimeoutSec 60

$listCall = Invoke-UeToolsetCall -ToolsetName $toolset -ToolName 'ListTests' -ToolArguments @{
    nameFilter = $NameFilter
    tagFilter  = ''
    limit      = 0
} -HttpTimeoutSec 60

$listObj = ConvertFrom-UeReturnValue -RawText $listCall.RawText -ContextName 'ListTests'
$rawList = @()
if ($listObj -is [array]) {
    $rawList = $listObj
}
elseif ($listObj.PSObject.Properties['tests']) {
    $rawList = @($listObj.tests)
}
elseif ($listObj.PSObject.Properties['Tests']) {
    $rawList = @($listObj.Tests)
}

$discoveredNames = @(
    $rawList | ForEach-Object {
        if ($_ -is [string]) { $_ }
        elseif ($_.PSObject.Properties['fullTestPath']) { [string]$_.fullTestPath }
        elseif ($_.PSObject.Properties['FullTestPath']) { [string]$_.FullTestPath }
        elseif ($_.PSObject.Properties['fullName']) { [string]$_.fullName }
        elseif ($_.PSObject.Properties['testName']) { [string]$_.testName }
        elseif ($_.PSObject.Properties['name']) { [string]$_.name }
        else { throw "无法识别 ListTests 条目结构: $($_ | ConvertTo-Json -Compress)" }
    } | Sort-Object -Unique
)

if ($discoveredNames.Count -eq 0) {
    Write-Error "[Invoke-UeMcpTests] ListTests(nameFilter='$NameFilter') 未发现任何自动化测试，请检查模块是否已加载。"
    exit 1
}

# 3. 根据参数筛选并严格校验目标测试集
$selectedTests = @()
if ($null -ne $TestNames -and $TestNames.Count -gt 0) {
    $discoveredSet = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
    foreach ($dn in $discoveredNames) { $null = $discoveredSet.Add($dn) }

    $missingNames = @($TestNames | Where-Object { -not $discoveredSet.Contains($_) })
    if ($missingNames.Count -gt 0) {
        Write-Error ("[Invoke-UeMcpTests] 以下指定的测试名称在 Editor 注册表中不存在（禁止静默跳过）: {0}" -f ($missingNames -join ', '))
        exit 1
    }
    $selectedTests = @($TestNames)
}
if (-not [string]::IsNullOrWhiteSpace($TestRegex)) {
    $regexMatched = @($discoveredNames | Where-Object { $_ -match $TestRegex })
    $selectedTests = @(@($selectedTests) + @($regexMatched) | Sort-Object -Unique)
}
elseif ($ListOnly -and ($null -eq $TestNames -or $TestNames.Count -eq 0)) {
    $selectedTests = $discoveredNames
}

if ($selectedTests.Count -eq 0) {
    Write-Error ("[Invoke-UeMcpTests] 筛选条件未匹配到任何已注册测试: TestRegex='{0}', NameFilter='{1}'（总已发现 {2} 项）。" -f $TestRegex, $NameFilter, $discoveredNames.Count)
    exit 1
}

if ($ListOnly) {
    Write-Output ("[ListTests] 共匹配 {0} 项测试（总已发现 {1} 项）：" -f $selectedTests.Count, $discoveredNames.Count)
    foreach ($tn in $selectedTests) {
        Write-Output "  $tn"
    }
    exit 0
}

Write-Output ("[RunTests] 目标端点 {0}，共选中 {1} 项测试，开始执行（超时上限 {2}s）..." -f $McpUrl, $selectedTests.Count, $TimeoutSeconds)

# 4. 发起 RunTests（注意：当用例内部产生日志 Error 时，RunTests 可能带 isError=true 返回错误文本，最终明细以 GetTestResults 为准）
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$runCall = Invoke-UeToolsetCall -ToolsetName $toolset -ToolName 'RunTests' -ToolArguments @{
    testNames = [string[]]$selectedTests
} -HttpTimeoutSec $TimeoutSeconds -AllowTestErrorFlag

# 5. 确认测试控制器状态已回到非运行态（兼容同步返回与异步运行两种模式）
while ((Get-Date) -lt $deadline) {
    $statusCall = Invoke-UeToolsetCall -ToolsetName $toolset -ToolName 'GetTestStatus' -ToolArguments @{} -HttpTimeoutSec 30
    $stObj = ConvertFrom-UeReturnValue -RawText $statusCall.RawText -ContextName 'GetTestStatus'

    $isRunning = $false
    if ($stObj.PSObject.Properties['isRunning'] -and $null -ne $stObj.isRunning) {
        $isRunning = [bool]$stObj.isRunning
    }
    elseif ($stObj.PSObject.Properties['state'] -and ([string]$stObj.state -in @('Running', 'InProgress'))) {
        $isRunning = $true
    }
    elseif ($stObj.PSObject.Properties['status'] -and ([string]$stObj.status -in @('running', 'in_progress', 'active'))) {
        $isRunning = $true
    }

    if (-not $isRunning) {
        break
    }

    $doneCnt = if ($stObj.PSObject.Properties['numComplete']) { $stObj.numComplete } else { '?' }
    $totalCnt = if ($stObj.PSObject.Properties['numEnabled']) { $stObj.numEnabled } else { $selectedTests.Count }
    Write-Output ("[{0}] 测试运行中: {1}/{2}" -f (Get-Date -Format 'HH:mm:ss'), $doneCnt, $totalCnt)
    Start-Sleep -Seconds $PollIntervalSeconds
}

if ((Get-Date) -ge $deadline) {
    Write-Error ("[Invoke-UeMcpTests] 等待自动化测试完成超时 ({0}s)。" -f $TimeoutSeconds)
    exit 1
}

# 6. 拉取完整测试结果并写入本地 JSON 文件
$resultsCall = Invoke-UeToolsetCall -ToolsetName $toolset -ToolName 'GetTestResults' -ToolArguments @{} -HttpTimeoutSec 60
$outDir = Split-Path -Parent $OutputJsonPath
if (-not (Test-Path -LiteralPath $outDir -PathType Container)) {
    $null = New-Item -ItemType Directory -Path $outDir -Force
}
$resObj = ConvertFrom-UeReturnValue -RawText $resultsCall.RawText -ContextName 'GetTestResults'
$formattedJson = $resObj | ConvertTo-Json -Depth 12
[System.IO.File]::WriteAllText($OutputJsonPath, $formattedJson, [System.Text.UTF8Encoding]::new($false))

$testEntries = @()
if ($resObj -is [array]) {
    $testEntries = $resObj
}
elseif ($resObj.PSObject.Properties['tests']) {
    $testEntries = @($resObj.tests)
}
elseif ($resObj.PSObject.Properties['results']) {
    $testEntries = @($resObj.results)
}
elseif ($resObj.PSObject.Properties['Results']) {
    $testEntries = @($resObj.Results)
}

if ($testEntries.Count -eq 0) {
    Write-Error ("[Invoke-UeMcpTests] GetTestResults 返回了 0 条结果记录，原始文件已保存至: '{0}'，RunTests 响应: {1}" -f $OutputJsonPath, $runCall.RawText)
    exit 1
}

$passedEntries = @()
$failedEntries = @()
$totalWarnings = 0

foreach ($entry in $testEntries) {
    $st = if ($entry.PSObject.Properties['state']) { [string]$entry.state }
          elseif ($entry.PSObject.Properties['State']) { [string]$entry.State }
          elseif ($entry.PSObject.Properties['status']) { [string]$entry.status }
          else { '' }

    $warns = if ($entry.PSObject.Properties['warnings'] -and $null -ne $entry.warnings) { @($entry.warnings) } else { @() }
    $totalWarnings += $warns.Count

    if ($st -in @('Success', 'passed', 'Passed')) {
        $passedEntries += $entry
    }
    else {
        $failedEntries += $entry
    }
}

Write-Output ("=== 测试汇总: 总计={0}, 通过={1}, 失败={2}, 警告总条数={3} | 完整结果={4} ===" -f $testEntries.Count, $passedEntries.Count, $failedEntries.Count, $totalWarnings, $OutputJsonPath)

if ($failedEntries.Count -gt 0) {
    foreach ($fe in $failedEntries) {
        $tName = if ($fe.PSObject.Properties['name']) { $fe.name } elseif ($fe.PSObject.Properties['fullTestPath']) { $fe.fullTestPath } else { '<unknown>' }
        $errs = if ($fe.PSObject.Properties['errors'] -and $null -ne $fe.errors) { @($fe.errors) -join ' | ' } else { '（无详细 errors 字段）' }
        Write-Output ("[FAIL] {0} -> {1}" -f $tName, $errs)
    }
    Write-Error ("[Invoke-UeMcpTests] 自动化测试存在 {0} 项失败，详见上方 [FAIL] 列表及 '{1}'。" -f $failedEntries.Count, $OutputJsonPath)
    exit 1
}

exit 0
