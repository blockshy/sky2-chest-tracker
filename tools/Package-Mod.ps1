<#
.SYNOPSIS
使用指定的已验证 DLL 生成玩家安装包及 SHA-256 校验文件。
.DESCRIPTION
通过文件白名单组装独立暂存目录，不打包源代码树、存档、日志或研究资料。
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$DllPath,
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $projectRoot 'release' }
$dll = (Resolve-Path -LiteralPath $DllPath).Path
$cmake = Get-Content -Raw (Join-Path $projectRoot 'CMakeLists.txt')
if ($cmake -notmatch 'project\(Sky2ChestTracker VERSION ([0-9]+\.[0-9]+\.[0-9]+)') { throw '找不到项目版本。' }
$version = $Matches[1]
$dependencies = Get-Content -Raw (Join-Path $projectRoot 'dependencies.json') | ConvertFrom-Json
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$outputRoot = (Resolve-Path -LiteralPath $OutputDirectory).Path
# 每次使用独立暂存目录，既不混入上次产物，也无需递归删除任何已有内容。
$stage = Join-Path $outputRoot ('.stage-' + [Guid]::NewGuid().ToString('N'))
$stageDist = Join-Path $stage 'dist'
New-Item -ItemType Directory -Path $stageDist -Force | Out-Null
Copy-Item -LiteralPath $dll -Destination (Join-Path $stageDist 'xinput1_4.dll')
@{ version = $version; dll_sha256 = (Get-FileHash -LiteralPath $dll).Hash;
   exe_sha256 = 'd8b2911d1576216bdc22d070550e4f531e105de7ed2981885849669f4acf8aaf';
   imgui_commit = $dependencies.imgui.commit; minhook_commit = $dependencies.minhook.commit } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stageDist 'manifest.json') -Encoding utf8
foreach ($file in @('README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'CHANGELOG.md', 'CONTRIBUTING.md', 'Install-Mod.ps1', 'Uninstall-Mod.ps1')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $file) -Destination (Join-Path $stage $file)
}
New-Item -ItemType Directory -Path (Join-Path $stage 'docs'),(Join-Path $stage 'licenses'),(Join-Path $stageDist 'licenses') -Force | Out-Null
foreach ($file in @('BUILDING.md', 'ARCHITECTURE.md', 'TESTING.md')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot "docs/$file") -Destination (Join-Path $stage "docs/$file")
}
foreach ($file in @('Dear-ImGui.txt', 'MinHook.txt', 'ED9ModManager.txt')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot "licenses/$file") -Destination (Join-Path $stage "licenses/$file")
    Copy-Item -LiteralPath (Join-Path $projectRoot "licenses/$file") -Destination (Join-Path $stageDist "licenses/$file")
}
$archive = Join-Path $outputRoot "Sky2ChestTracker-$version.zip"
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive -Force
$checksum = (Get-FileHash -LiteralPath $archive).Hash.ToLowerInvariant() + '  ' + [IO.Path]::GetFileName($archive)
$checksum | Set-Content -LiteralPath (Join-Path $outputRoot "Sky2ChestTracker-$version.sha256") -Encoding ascii
Write-Output $archive
Write-Output $checksum
