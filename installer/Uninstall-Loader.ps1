<#
.SYNOPSIS
独立卸载公共 Loader；发现任何 ASI 插件时拒绝卸载。
.DESCRIPTION
只删除匹配已发布 SHA-256 的 Loader、对应许可及已确认旧元数据。不删除其他
插件、日志或返程记录，含未知文件的目录始终保留。
#>
[CmdletBinding(SupportsShouldProcess=$true)]
param([Parameter(Mandatory=$true)][string]$GamePath,
    [string]$PackageRoot=(Split-Path $PSScriptRoot -Parent))
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')
$package=Get-Sky2Package $PackageRoot 'asi-loader'
$gameRoot=Get-Sky2GameRoot $GamePath
$plan=@(Get-Sky2UninstallPlan $package $gameRoot)
if (-not $PSCmdlet.ShouldProcess($gameRoot, '卸载已核验且没有 ASI 插件依赖的公共 Loader 及许可')) { return }
Invoke-Sky2Uninstall $package $gameRoot $plan
Write-Output '公共 Loader 及其许可已卸载；其他文件和运行数据保留。'
