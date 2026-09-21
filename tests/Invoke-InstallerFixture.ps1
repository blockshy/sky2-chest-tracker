<#
.SYNOPSIS
仅在合成目录内运行真实安装脚本；不加载游戏、不读写真实安装目录。
.DESCRIPTION
测试只替代游戏 EXE 版本识别与进程查询，所有 DLL、JSON、复制、删除、哈希和
WhatIf 行为仍由正式脚本与文件系统执行。这个入口不进入玩家安装包。
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ScriptPath,
    [Parameter(Mandatory = $true)][string]$GamePath,
    [switch]$Preview,
    [switch]$Running,
    [switch]$UseRealExeHash
)
$ErrorActionPreference = 'Stop'
$global:Sky2FixtureExe = Join-Path $GamePath 'sora_2nd.exe'
$global:Sky2FixtureRunning = [bool]$Running
$global:Sky2FixtureRealExeHash = [bool]$UseRealExeHash
# 即使误把真实游戏路径传给测试入口，也必须在任何替代行为生效前停止。
if ((Get-Content -Raw -LiteralPath $global:Sky2FixtureExe) -cne 'SKY2 INSTALLER TEST FIXTURE') {
    throw '测试入口只接受合成 EXE 占位文件。'
}
function Get-FileHash {
    [CmdletBinding()]
    param([string]$LiteralPath, [string]$Algorithm = 'SHA256')
    # 被测脚本有自己的 script 作用域，因此桩状态使用本测试子进程专用全局名。
    if ($LiteralPath -eq $global:Sky2FixtureExe -and -not $global:Sky2FixtureRealExeHash) {
        return [PSCustomObject]@{ Hash = 'd8b2911d1576216bdc22d070550e4f531e105de7ed2981885849669f4acf8aaf' }
    }
    # DLL 与备份的哈希必须真实计算，不能用桩函数掩盖文件覆盖或校验缺陷。
    Microsoft.PowerShell.Utility\Get-FileHash -LiteralPath $LiteralPath -Algorithm $Algorithm
}
function Get-Process {
    [CmdletBinding()]
    param([string]$Name)
    if ($Name -ne 'sora_2nd') { throw '测试脚本查询了未预期的进程。' }
    if ($global:Sky2FixtureRunning) { return [PSCustomObject]@{ ProcessName = 'sora_2nd' } }
}
& $ScriptPath -GamePath $GamePath -WhatIf:$Preview
