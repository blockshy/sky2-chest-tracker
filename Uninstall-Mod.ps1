<#
.SYNOPSIS
卸载经安装记录核对的宝箱插件，保留日志和用户存档。
.DESCRIPTION
仅删除确定属于本 Mod 的一个 DLL 文件；不递归删除目录，不改动其他 Mod。
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param([Parameter(Mandatory = $true)][string]$GamePath)
$ErrorActionPreference = 'Stop'
if (Get-Process -Name sora_2nd -ErrorAction SilentlyContinue) { throw '请先正常退出游戏。' }
$gameRoot = (Resolve-Path -LiteralPath $GamePath).Path
$target = Join-Path $gameRoot 'xinput1_4.dll'
$modDirectory = Join-Path $gameRoot 'Sky2ChestTracker'
$receiptPath = Join-Path $modDirectory 'install.json'
if (-not (Test-Path -LiteralPath $target)) { Write-Output '插件 DLL 已不存在。'; exit 0 }
# 记录只描述普通文件的归属，不能授权沿链接删除文件或使用外部的伪装记录。
foreach ($path in @($target, $modDirectory, $receiptPath)) {
    $item = Get-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
    if ($item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "发现链接或重解析点，未卸载：$path。请先确认实际文件位置。"
    }
}
if (-not (Test-Path -LiteralPath $receiptPath -PathType Leaf)) {
    throw '缺少本 Mod 的安装记录，未删除 xinput1_4.dll。它可能属于其他 Mod，请先核对来源。'
}
try { $receipt = Get-Content -Raw -LiteralPath $receiptPath | ConvertFrom-Json }
catch { throw '本 Mod 的安装记录无法读取，未删除。请保留现有 DLL 和 install.json，先核对来源。' }
if ($receipt.product -ne 'Sky2ChestTracker' -or
    $receipt.dll_sha256 -ne (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash) {
    throw '文件不符合本 Mod 的安装记录，未删除。DLL 可能被其他 Mod 替换；不要修改安装记录或手动删除未知文件。'
}
if (-not $PSCmdlet.ShouldProcess($target, '卸载已核对归属的宝箱追踪器 DLL')) { return }
# 再次检查用户确认期间的变化；卸载不清理整个目录，避免影响其他文件。
if ($receipt.dll_sha256 -ne (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash) {
    throw 'DLL 在检查后发生变化，未删除。请勿同时运行其他 Mod 安装器。'
}
Remove-Item -LiteralPath $target
Write-Output '已卸载宝箱插件；存档、其他 Mod 和诊断日志均保留。'
