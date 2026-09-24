<#
.SYNOPSIS
安装或更新独立版/ASI 宝箱 Mod，按包内清单选择唯一发行模式。
.DESCRIPTION
只复制二进制及合并许可，不落盘收据或使用说明。默认在解压包 backups 备份已
核验的旧载荷；-NoBackup 仅省略备份，不放宽任何内容归属和路径检查。
#>
[CmdletBinding(SupportsShouldProcess=$true)]
param([Parameter(Mandatory=$true)][string]$GamePath, [switch]$NoBackup)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'installer/Common.ps1')
$metadata = Read-Sky2Json (Join-Path $PSScriptRoot 'installer/manifest.json')
if ($metadata.type -notin @('standalone-proxy','asi-plugin')) { throw '请使用与此发行包匹配的安装入口。' }
$package = Get-Sky2Package $PSScriptRoot $metadata.type
$gameRoot = Get-Sky2GameRoot $GamePath -CheckVersion
$plan = @(Get-Sky2InstallPlan $package $gameRoot)
$cleanup = @(Get-Sky2CleanupPlan $package $gameRoot)
$migration = @(Get-Sky2MigrationPlan $package $gameRoot)
if (-not $PSCmdlet.ShouldProcess($gameRoot, ('安装 ' + $metadata.type + '；迁移已确认数据并清理已知旧文件'))) { return }
Invoke-Sky2Install $package $gameRoot $plan $cleanup $migration -NoBackup:$NoBackup
Write-Output '宝箱 Mod 已安装并校验；说明文档及安装元数据仅保留在解压包。'
