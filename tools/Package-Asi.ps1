<#
.SYNOPSIS
保留 ASI／Loader 专用打包入口，委托统一白名单实现生成精简发行包。
.DESCRIPTION
所有分发共用 Package-Mod.ps1 的清单及许可逻辑，避免三个版本的目录规则分叉。
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$DllPath,
    [Parameter(Mandatory = $true)][ValidateSet('Plugin', 'Loader')][string]$Distribution,
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'Package-Mod.ps1') @PSBoundParameters
