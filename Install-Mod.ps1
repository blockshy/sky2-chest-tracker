<#
.SYNOPSIS
安装或更新本地宝箱插件，先核对游戏版本和已有 DLL 的归属。
.DESCRIPTION
只写入游戏根目录的 xinput1_4.dll 与 Sky2ChestTracker 安装记录；不写入存档。
第一次安装遇到同名 DLL 会停止；更新时保留旧版本至工作区 backups 文件夹。
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param([Parameter(Mandatory = $true)][string]$GamePath)
$ErrorActionPreference = 'Stop'
$gameRoot = (Resolve-Path -LiteralPath $GamePath).Path
$exe = Join-Path $gameRoot 'sora_2nd.exe'
$expectedExe = 'd8b2911d1576216bdc22d070550e4f531e105de7ed2981885849669f4acf8aaf'
if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne $expectedExe) {
    throw '游戏 EXE 与适配版本不符，停止安装。'
}
if (Get-Process -Name sora_2nd -ErrorAction SilentlyContinue) { throw '请先正常退出游戏。' }
$source = Join-Path $PSScriptRoot 'dist\xinput1_4.dll'
$manifest = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'dist\manifest.json') | ConvertFrom-Json
$sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
if ($sourceHash -ne $manifest.dll_sha256) { throw '安装包 DLL 哈希不一致，停止安装。' }
$target = Join-Path $gameRoot 'xinput1_4.dll'
$modDirectory = Join-Path $gameRoot 'Sky2ChestTracker'
$receiptPath = Join-Path $modDirectory 'install.json'
# 文件归属必须由安装记录与实际内容共同确认。拒绝链接形式的入口和记录，
# 防止复制或读取时沿链接操作本 Mod 目录之外的文件；不把文件名视作归属证据。
foreach ($path in @($target, $modDirectory, $receiptPath)) {
    $item = Get-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
    if ($item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "发现链接或重解析点，未安装：$path。请先确认实际文件位置。"
    }
}
$oldHash = $null
if (Test-Path -LiteralPath $target) {
    if (-not (Test-Path -LiteralPath $receiptPath -PathType Leaf)) {
        throw '已有 xinput1_4.dll，但缺少本 Mod 的安装记录，未覆盖。它可能属于其他 Mod；请通过原 Mod 的卸载方式处理，不要直接删除或改名尝试共存。'
    }
    try { $receipt = Get-Content -Raw -LiteralPath $receiptPath | ConvertFrom-Json }
    catch { throw '本 Mod 的安装记录无法读取，未覆盖。请保留现有 DLL 和 install.json，先核对来源。' }
    $oldHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    if ($receipt.product -ne 'Sky2ChestTracker' -or $receipt.dll_sha256 -ne $oldHash) {
        throw '已有 DLL 与本 Mod 的安装记录不一致，未覆盖。文件可能被其他 Mod 替换；请保留现有文件并核对来源，不要修改安装记录来绕过检查。'
    }
    if ($sourceHash -eq $oldHash) { Write-Output '同一版本已经安装。'; exit 0 }
}
# 预演也完成版本、安装包及文件归属检查，但在任何建目录或备份之前停止。
if (-not $PSCmdlet.ShouldProcess($gameRoot, '安装宝箱追踪器（更新时先备份已核对的旧 DLL）')) { return }
if ($oldHash) {
    # 按内容哈希保存之前的 DLL，更新失败时可以恢复，不覆盖已有备份。
    $backupRoot = Join-Path $PSScriptRoot 'backups'
    New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null
    $backupPath = Join-Path $backupRoot ($oldHash + '.dll')
    if (-not (Test-Path -LiteralPath $backupPath)) { Copy-Item -LiteralPath $target -Destination $backupPath }
    if ((Get-FileHash -LiteralPath $backupPath -Algorithm SHA256).Hash -ne $oldHash) {
        throw '旧 DLL 备份校验失败，未更新。请保留现有文件并检查备份目录。'
    }
}
New-Item -ItemType Directory -Path $modDirectory -Force | Out-Null
if ($oldHash) {
    # 在实际覆盖前再次核对，防止预演确认或备份期间已有 DLL 被其他程序替换。
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $oldHash) {
        throw '已有 DLL 在检查后发生变化，未更新。请勿同时运行其他 Mod 安装器。'
    }
    Copy-Item -LiteralPath $source -Destination $target
} else {
    # 首次安装使用不覆盖语义：即使检查后出现了同名文件，也只报错而不替换它。
    [IO.File]::Copy($source, $target, $false)
}
if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $sourceHash) { throw '安装后的校验失败。' }
@{ product = 'Sky2ChestTracker'; version = $manifest.version; dll_sha256 = $sourceHash;
   exe_sha256 = $expectedExe; installed_at = (Get-Date).ToString('o') } |
    ConvertTo-Json | Set-Content -LiteralPath $receiptPath -Encoding utf8
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.md') -Destination (Join-Path $modDirectory 'README.md')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'THIRD_PARTY_NOTICES.md') -Destination (Join-Path $modDirectory 'THIRD_PARTY_NOTICES.md')
# 正式文档与项目许可证一同安装，保证 README 的本地说明链接可打开。
foreach ($document in @('LICENSE', 'CHANGELOG.md', 'CONTRIBUTING.md')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $document) -Destination (Join-Path $modDirectory $document)
}
$docsDirectory = Join-Path $modDirectory 'docs'
New-Item -ItemType Directory -Path $docsDirectory -Force | Out-Null
foreach ($document in @('BUILDING.md', 'ARCHITECTURE.md', 'TESTING.md')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "docs/$document") -Destination (Join-Path $docsDirectory $document)
}
$licenseDirectory = Join-Path $modDirectory 'licenses'
New-Item -ItemType Directory -Path $licenseDirectory -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'dist\licenses') -File |
    ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $licenseDirectory $_.Name) }
Write-Output "已安装并校验：$target"
