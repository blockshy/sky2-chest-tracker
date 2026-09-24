<#
.SYNOPSIS
ASI 专用卸载入口；禁止用其他发行包卸载，公共 Loader 始终独立管理。
#>
[CmdletBinding(SupportsShouldProcess=$true)]
param([Parameter(Mandatory=$true)][string]$GamePath,
    [string]$PackageRoot=(Split-Path $PSScriptRoot -Parent))
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')
$null=Get-Sky2Package $PackageRoot 'asi-plugin'
& (Join-Path $PackageRoot 'Uninstall-Mod.ps1') -GamePath $GamePath -WhatIf:$WhatIfPreference
