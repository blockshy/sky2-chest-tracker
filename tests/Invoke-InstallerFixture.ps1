<#
.SYNOPSIS
在临时合成游戏目录运行真实安装器，不读取或修改实际游戏安装。
.DESCRIPTION
仅替代游戏 EXE 指纹与运行中检查；复制、原子替换、文件哈希、迁移格式、互斥和
WhatIf 均执行正式逻辑。-NoBackup 用于验证用户明确选择不备份时不产生备份文件。
#>
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ScriptPath,
    [Parameter(Mandatory=$true)][string]$GamePath,
    [switch]$Preview, [switch]$Running, [switch]$UseRealExeHash, [switch]$NoBackup,
    [string]$LockRelativePath)
$ErrorActionPreference='Stop'
$global:Sky2FixtureExe=Join-Path $GamePath 'sora_2nd.exe'
$global:Sky2FixtureRunning=[bool]$Running
$global:Sky2FixtureRealExeHash=[bool]$UseRealExeHash
if ((Get-Content -Raw -LiteralPath $global:Sky2FixtureExe) -cne 'SKY2 INSTALLER TEST FIXTURE') { throw '测试入口只接受合成 EXE 占位文件。' }
Import-Module Microsoft.PowerShell.Utility -ErrorAction Stop
function Get-FileHash {
    [CmdletBinding()]
    param([string]$LiteralPath, [string]$Algorithm='SHA256')
    if ($LiteralPath -eq $global:Sky2FixtureExe -and -not $global:Sky2FixtureRealExeHash) {
        return [PSCustomObject]@{ Hash='d8b2911d1576216bdc22d070550e4f531e105de7ed2981885849669f4acf8aaf' }
    }
    if ($Algorithm -ne 'SHA256') { throw '测试入口只允许 SHA256。' }
    $hasher=[Security.Cryptography.SHA256]::Create()
    $stream=[IO.File]::OpenRead($LiteralPath)
    try { $hash=[BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-','') }
    finally { $stream.Dispose(); $hasher.Dispose() }
    return [PSCustomObject]@{ Hash=$hash }
}
function Get-Process {
    [CmdletBinding()]
    param([string]$Name)
    if ($Name -ne 'sora_2nd') { throw '查询了未预期的进程。' }
    if ($global:Sky2FixtureRunning) { return [PSCustomObject]@{ ProcessName='sora_2nd' } }
}
$arguments=@{ GamePath=$GamePath; WhatIf=[bool]$Preview }
if ($NoBackup) { $arguments.NoBackup=$true }
$fixtureLock=$null
if ($LockRelativePath) {
    # 锁只允许指向合成游戏目录内已经存在的普通文件，模拟外部程序持有读取句柄。
    # FileShare.Read 允许真实哈希校验和复制读取，但阻止原子替换及删除。
    $lockPath=[IO.Path]::GetFullPath((Join-Path $GamePath $LockRelativePath))
    if (-not $lockPath.StartsWith([IO.Path]::GetFullPath($GamePath).TrimEnd('\')+'\', [StringComparison]::OrdinalIgnoreCase)) { throw '测试锁越界。' }
    $fixtureLock=[IO.File]::Open($lockPath,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
}
try { & $ScriptPath @arguments } finally { if ($fixtureLock) { $fixtureLock.Dispose() } }
