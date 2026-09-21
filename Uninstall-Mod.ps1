<#
.SYNOPSIS
卸载经安装记录核对的宝箱插件，保留日志和用户存档。
.DESCRIPTION
仅删除确定属于本 Mod 的一个 DLL 文件；不递归删除目录，不改动其他 Mod。
#>
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$GamePath)
$ErrorActionPreference = 'Stop'
if (Get-Process -Name sora_2nd -ErrorAction SilentlyContinue) { throw '请先正常退出游戏。' }
$gameRoot = (Resolve-Path -LiteralPath $GamePath).Path
$target = Join-Path $gameRoot 'xinput1_4.dll'
$receiptPath = Join-Path $gameRoot 'Sky2ChestTracker\install.json'
if (-not (Test-Path -LiteralPath $target)) { Write-Output '插件 DLL 已不存在。'; exit 0 }
$receipt = Get-Content -Raw -LiteralPath $receiptPath | ConvertFrom-Json
if ($receipt.product -ne 'Sky2ChestTracker' -or
    $receipt.dll_sha256 -ne (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash) {
    throw '文件不符合本 Mod 的安装记录，未删除。'
}
Remove-Item -LiteralPath $target
Write-Output '已卸载宝箱插件；存档、其他 Mod 和诊断日志均保留。'
