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
# 在写入 DLL 前确认精简安装包完整，避免只复制脚本和 DLL 后出现半途失败。
foreach ($relativePath in @('dist/legacy-documents.json', 'dist/Sky2ChestTracker/LICENSE',
    'dist/Sky2ChestTracker/THIRD_PARTY_NOTICES.md', 'dist/Sky2ChestTracker/licenses/Dear-ImGui.txt',
    'dist/Sky2ChestTracker/licenses/MinHook.txt', 'dist/Sky2ChestTracker/licenses/ED9ModManager.txt')) {
    if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot $relativePath) -PathType Leaf)) {
        throw '安装包文件不完整，未安装。请完整解压安装包，不要只复制脚本或 DLL。'
    }
}
$legacyHashes = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'dist\legacy-documents.json') | ConvertFrom-Json
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
$alreadyInstalled = $false
# 手动卸载可能只移走 DLL、留下记录；允许这种恢复流程，但不接管来源不明的同名目录。
if (-not (Test-Path -LiteralPath $target) -and (Test-Path -LiteralPath $modDirectory) -and
    @(Get-ChildItem -LiteralPath $modDirectory -Force).Count -gt 0) {
    if (-not (Test-Path -LiteralPath $receiptPath -PathType Leaf)) {
        throw '已有非空 Sky2ChestTracker 目录但缺少安装记录，未安装。请先核对目录来源，脚本不会接管未知文件。'
    }
    try { $remainingReceipt = Get-Content -Raw -LiteralPath $receiptPath | ConvertFrom-Json }
    catch { throw '残留安装记录无法读取，未安装。请保留目录并先核对来源。' }
    if ($remainingReceipt.product -ne 'Sky2ChestTracker' -or $remainingReceipt.dll_sha256 -notmatch '^[0-9a-fA-F]{64}$') {
        throw '残留安装记录不属于本 Mod 或内容无效，未安装。'
    }
}
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
    $alreadyInstalled = ($sourceHash -eq $oldHash)
}
# 预演也完成版本、安装包及文件归属检查，但在任何建目录或备份之前停止。
if (-not $PSCmdlet.ShouldProcess($gameRoot, '安装宝箱追踪器（更新时先备份已核对的旧 DLL）')) { return }
if ($oldHash -and -not $alreadyInstalled) {
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
if ($alreadyInstalled) {
    Write-Output '同一 DLL 已安装，检查旧版文档是否可以归档。'
} elseif ($oldHash) {
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
if (-not $alreadyInstalled) {
    @{ product = 'Sky2ChestTracker'; version = $manifest.version; dll_sha256 = $sourceHash;
   exe_sha256 = $expectedExe; installed_at = (Get-Date).ToString('o') } |
        ConvertTo-Json | Set-Content -LiteralPath $receiptPath -Encoding utf8
    # 游戏目录只保留运行记录与许可证；玩家说明留在安装包，开发文档留在仓库。
    $packagedModDirectory = Join-Path $PSScriptRoot 'dist\Sky2ChestTracker'
    foreach ($document in @('LICENSE', 'THIRD_PARTY_NOTICES.md')) {
        Copy-Item -LiteralPath (Join-Path $packagedModDirectory $document) -Destination (Join-Path $modDirectory $document)
    }
    $licenseDirectory = Join-Path $modDirectory 'licenses'
    New-Item -ItemType Directory -Path $licenseDirectory -Force | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $packagedModDirectory 'licenses') -File |
        ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $licenseDirectory $_.Name) }
}

# 只迁移 0.3.2 正式安装包曾附带且内容未改的六份说明。白名单哈希来自已发布包，
# 计算时统一换行并去掉 BOM；归档仍保留文件的原始字节。未知或修改过的文件不动。
$archiveRoot = $null
foreach ($relativePath in @('README.md', 'CHANGELOG.md', 'CONTRIBUTING.md', 'docs/BUILDING.md', 'docs/ARCHITECTURE.md', 'docs/TESTING.md')) {
    $legacyPath = Join-Path $modDirectory $relativePath
    $parent = Get-Item -LiteralPath (Split-Path $legacyPath -Parent) -Force -ErrorAction SilentlyContinue
    $item = Get-Item -LiteralPath $legacyPath -Force -ErrorAction SilentlyContinue
    if (-not $item -or $item.PSIsContainer -or
        ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
        ($parent.Attributes -band [IO.FileAttributes]::ReparsePoint)) { continue }
    $normalized = [IO.File]::ReadAllText($legacyPath).Replace("`r`n", "`n")
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $hash = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($normalized))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
    if ($hash -ne $legacyHashes.$relativePath) { continue }
    if (-not $archiveRoot) {
        $archiveRoot = Join-Path $PSScriptRoot ('backups\legacy-docs-' + [Guid]::NewGuid().ToString('N'))
    }
    $destination = Join-Path $archiveRoot $relativePath
    New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
    # 逐文件移动到全新备份路径，不覆盖、不递归删除，便于随时恢复原始文档。
    Move-Item -LiteralPath $legacyPath -Destination $destination
}
if ($archiveRoot) { Write-Output "旧版未修改文档已归档：$archiveRoot" }
$docsDirectory = Join-Path $modDirectory 'docs'
$docsItem = Get-Item -LiteralPath $docsDirectory -Force -ErrorAction SilentlyContinue
if ($docsItem -and $docsItem.PSIsContainer -and
    -not ($docsItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -and
    @(Get-ChildItem -LiteralPath $docsDirectory -Force).Count -eq 0) {
    # 只删除空目录；若有其他程序同时放入文件，非递归删除会失败并保留目录。
    try { [IO.Directory]::Delete($docsDirectory, $false) }
    catch { Write-Warning 'docs 目录未清空，已保留。' }
}
Write-Output "已安装并校验：$target"
