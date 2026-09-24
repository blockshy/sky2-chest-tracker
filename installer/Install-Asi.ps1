<#
.SYNOPSIS
ASI 专用入口；实际安装复用根脚本，确保手动与脚本安装遵循同一保护规则。
#>
[CmdletBinding(SupportsShouldProcess=$true)]
param([Parameter(Mandatory=$true)][string]$GamePath,
    [string]$PackageRoot=(Split-Path $PSScriptRoot -Parent), [switch]$NoBackup)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')
$null=Get-Sky2Package $PackageRoot 'asi-plugin'
& (Join-Path $PackageRoot 'Install-Mod.ps1') -GamePath $GamePath -NoBackup:$NoBackup -WhatIf:$WhatIfPreference
