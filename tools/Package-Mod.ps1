<#
.SYNOPSIS
使用指定的已验证 DLL 生成玩家安装包及 SHA-256 校验文件。
.DESCRIPTION
通过文件白名单组装独立暂存目录，不打包源代码树、存档、日志或研究资料。
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$DllPath,
    [string]$OutputDirectory,
    # 仅修订安装器或文档时保留已验证 DLL 的版本，通过独立后缀区分安装包。
    [ValidatePattern('^r[1-9][0-9]*$')][string]$PackageRevision
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $projectRoot 'release' }
$dll = (Resolve-Path -LiteralPath $DllPath).Path
$cmake = Get-Content -Raw (Join-Path $projectRoot 'CMakeLists.txt')
if ($cmake -notmatch 'project\(Sky2ChestTracker VERSION ([0-9]+\.[0-9]+\.[0-9]+)') { throw '找不到项目版本。' }
$version = $Matches[1]
$packageVersion = $version
if ($PackageRevision) { $packageVersion += '-' + $PackageRevision }
$dependencies = Get-Content -Raw (Join-Path $projectRoot 'dependencies.json') | ConvertFrom-Json
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$outputRoot = (Resolve-Path -LiteralPath $OutputDirectory).Path
# 每次使用独立暂存目录，既不混入上次产物，也无需递归删除任何已有内容。
$stage = Join-Path $outputRoot ('.stage-' + [Guid]::NewGuid().ToString('N'))
$stageDist = Join-Path $stage 'dist'
New-Item -ItemType Directory -Path $stageDist -Force | Out-Null
Copy-Item -LiteralPath $dll -Destination (Join-Path $stageDist 'xinput1_4.dll')
@{ version = $version; package_revision = $PackageRevision; dll_sha256 = (Get-FileHash -LiteralPath $dll).Hash;
   exe_sha256 = 'd8b2911d1576216bdc22d070550e4f531e105de7ed2981885849669f4acf8aaf';
   imgui_commit = $dependencies.imgui.commit; minhook_commit = $dependencies.minhook.commit } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stageDist 'manifest.json') -Encoding utf8
foreach ($file in @('README.md', 'Install-Mod.ps1', 'Uninstall-Mod.ps1')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $file) -Destination (Join-Path $stage $file)
}
# 玩家可在解压包内离线查看安装、操作和传送用法；只收白名单中的三份玩家指南。
# 不附带开发过程、构建说明或验证记录，防止安装包再次混入与玩家操作无关的文档。
# 指南位于 dist 之外，安装器不会把它复制进游戏目录，手动安装仍只需复制 dist 内容。
$guideDirectory = Join-Path $stage 'docs'
New-Item -ItemType Directory -Path $guideDirectory -Force | Out-Null
foreach ($guide in @('INSTALLATION.md', 'USAGE.md', 'TRAVEL.md')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot ('docs/' + $guide)) -Destination (Join-Path $guideDirectory $guide)
}
# 手动安装只需复制 dist 中的 DLL 和同名 Mod 文件夹。模板记录与该 DLL 绑定，
# 以后改用脚本仍可核对归属；不预填虚假的安装时间，也不包含本机路径。
$stageMod = Join-Path $stageDist 'Sky2ChestTracker'
New-Item -ItemType Directory -Path (Join-Path $stageMod 'licenses') -Force | Out-Null
@{ product = 'Sky2ChestTracker'; version = $version;
   dll_sha256 = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash;
   exe_sha256 = 'd8b2911d1576216bdc22d070550e4f531e105de7ed2981885849669f4acf8aaf';
   installed_at = $null; installation_method = 'manual-package-template' } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stageMod 'install.json') -Encoding utf8
foreach ($file in @('LICENSE', 'THIRD_PARTY_NOTICES.md')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $file) -Destination (Join-Path $stageMod $file)
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'installer/legacy-documents.json') -Destination (Join-Path $stageDist 'legacy-documents.json')
foreach ($file in @('Dear-ImGui.txt', 'MinHook.txt', 'ED9ModManager.txt')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot "licenses/$file") -Destination (Join-Path $stageMod "licenses/$file")
}
$archive = Join-Path $outputRoot "Sky2ChestTracker-$packageVersion.zip"
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive -Force
$checksum = (Get-FileHash -LiteralPath $archive).Hash.ToLowerInvariant() + '  ' + [IO.Path]::GetFileName($archive)
$checksum | Set-Content -LiteralPath (Join-Path $outputRoot "Sky2ChestTracker-$packageVersion.sha256") -Encoding ascii
Write-Output $archive
Write-Output $checksum
