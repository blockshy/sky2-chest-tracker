<#
.SYNOPSIS
用真实 UAL、无游戏资源宿主和两份最小 ASI 验证装载；可追加生产宝箱 ASI 安全拒绝测试。
.DESCRIPTION
仅在新建的输出目录复制测试文件，不启动游戏、不安装 Mod、不访问存档。
所有子进程都以独立、隐藏窗口运行；移除插件通过另一个新目录/新进程模拟，不热卸载。
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$LoaderPath,
    [Parameter(Mandatory = $true)][string]$BinaryDirectory,
    [string]$ChestPluginPath,
    [string]$StandaloneDllPath,
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$loader = (Resolve-Path -LiteralPath $LoaderPath).Path
$binaries = (Resolve-Path -LiteralPath $BinaryDirectory).Path
$chest = if ($ChestPluginPath) { (Resolve-Path -LiteralPath $ChestPluginPath).Path } else { $null }
$standaloneDll = if ($StandaloneDllPath) { (Resolve-Path -LiteralPath $StandaloneDllPath).Path } else { $null }
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $projectRoot ('research/private/asi-loader-validation-' + [Guid]::NewGuid().ToString('N'))
}
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $outputRoot) { throw '输出目录已存在；为避免混入旧证据，请使用新目录。' }
foreach ($name in @('sky2_asi_loader_host.exe', 'Sky2AsiFixtureA.asi', 'Sky2AsiFixtureB.asi', 'Sky2CoexistProbe.asi')) {
    if (-not (Test-Path -LiteralPath (Join-Path $binaries $name) -PathType Leaf)) { throw "缺少构建产物：$name" }
}
New-Item -ItemType Directory -Path $outputRoot | Out-Null

function Invoke-Scenario {
    param([string]$Name, [hashtable]$Plugins, [string[]]$HostArguments,
          [string]$ExpectedOrder, [int]$ExpectedChestRejects = 0,
          [int]$ExpectedDuplicates = 0, [string]$ExpectedChestAtProbeEntry, [switch]$Standalone)
    $folder = Join-Path $outputRoot $Name
    New-Item -ItemType Directory -Path $folder | Out-Null
    $entryDll = if ($Standalone) { $standaloneDll } else { $loader }
    Copy-Item -LiteralPath $entryDll -Destination (Join-Path $folder 'xinput1_4.dll')
    Copy-Item -LiteralPath (Join-Path $binaries 'sky2_asi_loader_host.exe') -Destination $folder
    foreach ($relative in $Plugins.Keys) {
        $destination = Join-Path $folder $relative
        $parent = Split-Path $destination -Parent
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
        Copy-Item -LiteralPath $Plugins[$relative] -Destination $destination
    }
    # 插件分发必须与独立版数据隔离，旧 ASI 目录则只能通过显式迁移接管。
    # 两套目录放入非真实日志/返程哨兵，验证运行时不会自行复制、合并或删除旧数据。
    $legacySentinels = @{}
    if ($ExpectedChestRejects -gt 0 -and -not $Standalone) {
        foreach ($legacyRelative in @('Sky2ChestTracker', 'Sky2Mods/Sky2ChestTracker')) {
            $legacyFolder = Join-Path $folder $legacyRelative
            New-Item -ItemType Directory -Path $legacyFolder -Force | Out-Null
            foreach ($sentinelName in @('tracker.log', 'revisit-return.dat')) {
                $sentinelPath = Join-Path $legacyFolder $sentinelName
                [IO.File]::WriteAllBytes($sentinelPath, [Text.Encoding]::UTF8.GetBytes("legacy sentinel $legacyRelative/$sentinelName; keep exact bytes"))
                $legacySentinels[$sentinelPath] = (Get-FileHash -LiteralPath $sentinelPath -Algorithm SHA256).Hash
            }
        }
    }
    # 保留 UAL 官方默认配置。先扫描根目录，再 scripts/plugins 的顺序来自固定版本源码。
    # 用目录位置而非文件名排序切换 A/B 和 Chest/Probe，避免依赖文件系统枚举排序。
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = Join-Path $folder 'sky2_asi_loader_host.exe'
    $start.WorkingDirectory = $folder
    $start.Arguments = $HostArguments -join ' '
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($start)
    # 宿主最长等待少于 25 秒；这里留出额外裕量。只结束自己启动的诊断进程。
    if (-not $process.WaitForExit(30000)) { $process.Kill(); throw "$Name 宿主超时。" }
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    [IO.File]::WriteAllText((Join-Path $folder 'host.stdout.log'), $stdout)
    [IO.File]::WriteAllText((Join-Path $folder 'host.stderr.log'), $stderr)
    if ($process.ExitCode -ne 0) { throw "$Name 失败：`n$stdout`n$stderr" }
    if ($ExpectedOrder) {
        $order = (Get-Content -LiteralPath (Join-Path $folder 'fixture-order.txt')) -join ''
        if ($order -ne $ExpectedOrder) { throw "$Name 实際初始化顺序为 $order，预期 $ExpectedOrder。" }
    }
    $trackerDataRoot = if ($Standalone) { 'Sky2ChestTracker' } else { 'plugins/Sky2ChestTracker' }
    $trackerLogPath = Join-Path $folder "$trackerDataRoot/tracker.log"
    if ($ExpectedChestRejects -gt 0) {
        $trackerLog = Get-Content -LiteralPath $trackerLogPath -Raw
        $rejectCount = [regex]::Matches($trackerLog, [regex]::Escape('Unsupported executable; all hooks skipped.')).Count
        $duplicateCount = [regex]::Matches($trackerLog, [regex]::Escape('Duplicate tracker module or unavailable process guard; all hooks skipped.')).Count
        if ($rejectCount -ne $ExpectedChestRejects -or $duplicateCount -ne $ExpectedDuplicates) {
            throw "$Name EXE 拒绝/副本拒绝次数异常：$rejectCount / $duplicateCount。"
        }
        if ($trackerLog -match 'hook installed| active:|Overlay initialization failed') {
            throw "$Name 假宿主不应该进入游戏挂钩或图形初始化。"
        }
        if ($Standalone -and (Test-Path -LiteralPath (Join-Path $folder 'plugins/Sky2ChestTracker'))) {
            throw "$Name 独立版不应该创建 ASI 数据目录。"
        }
        foreach ($sentinelPath in $legacySentinels.Keys) {
            if ((Get-FileHash -LiteralPath $sentinelPath -Algorithm SHA256).Hash -ne $legacySentinels[$sentinelPath]) {
                throw "$Name 意外修改了旧安装的数据哨兵。"
            }
        }
        if (-not $Standalone -and (Test-Path -LiteralPath (Join-Path $folder 'plugins/Sky2ChestTracker/revisit-return.dat'))) {
            throw "$Name 意外复制或创建了新返程记录。"
        }
    } elseif (Test-Path -LiteralPath $trackerLogPath) { throw "$Name 未安装宝箱插件却产生了宝箱日志。" }
    if ($ExpectedChestAtProbeEntry) {
        $probeLog = Get-Content -LiteralPath (Join-Path $folder 'Sky2CoexistProbe/probe.log') -Raw
        if ($probeLog -notmatch ('chest-at-first-entry=' + [regex]::Escape($ExpectedChestAtProbeEntry))) {
            throw "$Name 未观察到预期 Chest/Probe 装载先后。"
        }
        if ($probeLog -notmatch 'heartbeat=2' -or $probeLog -notmatch 'xinput-module=') {
            throw "$Name 缺少第二次心跳或 Loader 身份日志。"
        }
        if ([regex]::Matches($probeLog, 'InitializeASI worker started;').Count -ne 1) {
            throw "$Name 重复启动了探针线程。"
        }
    }
    Write-Output "PASS $Name"
    [PSCustomObject]@{ Scenario = $Name; ExitCode = $process.ExitCode; Folder = $folder }
}

$fixtureA = Join-Path $binaries 'Sky2AsiFixtureA.asi'
$fixtureB = Join-Path $binaries 'Sky2AsiFixtureB.asi'
$probe = Join-Path $binaries 'Sky2CoexistProbe.asi'
$results = @()
$results += Invoke-Scenario 'fixture-a-before-b' @{
    'Sky2AsiFixtureA.asi' = $fixtureA; 'plugins/Sky2AsiFixtureB.asi' = $fixtureB
} @('--fixture=Sky2AsiFixtureA.asi,A', '--fixture=Sky2AsiFixtureB.asi,B') -ExpectedOrder 'AB'
$results += Invoke-Scenario 'fixture-b-before-a' @{
    'Sky2AsiFixtureB.asi' = $fixtureB; 'plugins/Sky2AsiFixtureA.asi' = $fixtureA
} @('--fixture=Sky2AsiFixtureA.asi,A', '--fixture=Sky2AsiFixtureB.asi,B') -ExpectedOrder 'BA'
$results += Invoke-Scenario 'fixture-a-removed' @{
    'plugins/Sky2AsiFixtureB.asi' = $fixtureB
} @('--fixture=Sky2AsiFixtureB.asi,B') -ExpectedOrder 'B'
$results += Invoke-Scenario 'fixture-b-removed' @{
    'plugins/Sky2AsiFixtureA.asi' = $fixtureA
} @('--fixture=Sky2AsiFixtureA.asi,A') -ExpectedOrder 'A'
$results += Invoke-Scenario 'probe-without-chest' @{
    'plugins/Sky2CoexistProbe.asi' = $probe
} @('--probe') -ExpectedChestAtProbeEntry 'absent'
if ($chest) {
    $results += Invoke-Scenario 'chest-before-probe' @{
        'Sky2ChestTracker.asi' = $chest; 'plugins/Sky2CoexistProbe.asi' = $probe
    } @('--chest', '--probe') -ExpectedChestRejects 1 -ExpectedChestAtProbeEntry 'present'
    $results += Invoke-Scenario 'probe-before-chest' @{
        'Sky2CoexistProbe.asi' = $probe; 'plugins/Sky2ChestTracker.asi' = $chest
    } @('--chest', '--probe') -ExpectedChestRejects 1 -ExpectedChestAtProbeEntry 'absent'
    $results += Invoke-Scenario 'chest-without-probe' @{
        'plugins/Sky2ChestTracker.asi' = $chest
    } @('--chest') -ExpectedChestRejects 1
    $results += Invoke-Scenario 'duplicate-chest-copy' @{
        'Sky2ChestTrackerDuplicate.asi' = $chest; 'plugins/Sky2ChestTracker.asi' = $chest
    } @('--chest') -ExpectedChestRejects 1 -ExpectedDuplicates 1
}
if ($standaloneDll) {
    $results += Invoke-Scenario 'standalone-forwarding' @{} @('--standalone') -ExpectedChestRejects 1 -Standalone
}
$report = [ordered]@{
    LoaderSha256 = (Get-FileHash -LiteralPath $loader -Algorithm SHA256).Hash
    ChestSha256 = if ($chest) { (Get-FileHash -LiteralPath $chest -Algorithm SHA256).Hash } else { $null }
    ProbeSha256 = (Get-FileHash -LiteralPath $probe -Algorithm SHA256).Hash
    StandaloneSha256 = if ($standaloneDll) { (Get-FileHash -LiteralPath $standaloneDll -Algorithm SHA256).Hash } else { $null }
    Scope = 'Actual UAL load/InitializeASI/idempotence/order/XInput forwarding and fake-host rejection only; no second UI/input-hook compatibility claim.'
    Results = @($results | Where-Object { $_ -isnot [string] })
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputRoot 'report.json') -Encoding UTF8
$results | Where-Object { $_ -is [string] } | Write-Output
Write-Output "隔离验证证据：$outputRoot"
