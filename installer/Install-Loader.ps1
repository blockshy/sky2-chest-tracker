<#
.SYNOPSIS
显式安装公共 Ultimate ASI Loader；只允许迁移已发布的独立宝箱代理。
.DESCRIPTION
不由宝箱安装器隐式调用。可识别历史白名单中的 0.5.0/0.6.0 独立代理，不读取
或迁移独立版返程记录；只有 ASI 安装器负责迁移旧 ASI 数据目录。
#>
[CmdletBinding(SupportsShouldProcess=$true)]
param([Parameter(Mandatory=$true)][string]$GamePath,
    [string]$PackageRoot=(Split-Path $PSScriptRoot -Parent), [switch]$NoBackup)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')
$package=Get-Sky2Package $PackageRoot 'asi-loader'
$gameRoot=Get-Sky2GameRoot $GamePath -CheckVersion
$plan=@(Get-Sky2InstallPlan $package $gameRoot)
$cleanup=@(Get-Sky2CleanupPlan $package $gameRoot)
if (-not $PSCmdlet.ShouldProcess($gameRoot, '安装公共 Loader 并迁移其许可；独立版数据保持原位')) { return }
Invoke-Sky2Install $package $gameRoot $plan $cleanup @() -NoBackup:$NoBackup
Write-Output '公共 Loader 已安装并校验；其许可位于 plugins/Sky2ChestTracker。'
