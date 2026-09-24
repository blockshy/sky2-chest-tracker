<#
.SYNOPSIS
卸载已知发行的宝箱 Mod；保留运行数据及公共 Loader。
.DESCRIPTION
手动安装不需要收据。只有实际内容匹配当前已校验载荷或历史发行 SHA-256 的
固定产品文件才允许删除；不会递归清空数据目录，也不操作游戏存档。
#>
[CmdletBinding(SupportsShouldProcess=$true)]
param([Parameter(Mandatory=$true)][string]$GamePath)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'installer/Common.ps1')
$metadata = Read-Sky2Json (Join-Path $PSScriptRoot 'installer/manifest.json')
if ($metadata.type -notin @('standalone-proxy','asi-plugin')) { throw '请使用与此发行包匹配的卸载入口。' }
$package = Get-Sky2Package $PSScriptRoot $metadata.type
$gameRoot = Get-Sky2GameRoot $GamePath
$plan = @(Get-Sky2UninstallPlan $package $gameRoot)
if (-not $PSCmdlet.ShouldProcess($gameRoot, '仅卸载已核验的宝箱插件及其许可，保留运行数据和公共 Loader')) { return }
Invoke-Sky2Uninstall $package $gameRoot $plan
Write-Output '宝箱 Mod 已卸载；运行数据、公共 Loader 及其许可、其他插件均保留。'
