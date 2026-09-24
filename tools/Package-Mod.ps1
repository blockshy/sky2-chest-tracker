<#
.SYNOPSIS
按分发类型生成精简玩家安装包及 SHA-256 校验文件。
.DESCRIPTION
严格区分游戏载荷与离线安装资料：dist 仅有二进制及许可，安装脚本、历史
文件指纹、玩家指南均留在解压包中。三个包的归属独立，不写游戏安装收据。
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$DllPath,
    [ValidateSet('Standalone', 'Plugin', 'Loader')][string]$Distribution = 'Standalone',
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$source = (Resolve-Path -LiteralPath $DllPath).Path
$hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$cmake = Get-Content -Raw -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt')
if ($cmake -notmatch 'project\(Sky2ChestTracker VERSION ([0-9]+\.[0-9]+\.[0-9]+)') { throw '找不到项目版本。' }
$version = $Matches[1]
$product = 'Sky2ChestTracker'
if ($Distribution -eq 'Loader') {
    $loader = Get-Content -Raw -LiteralPath (Join-Path $projectRoot 'loader-dependency.json') | ConvertFrom-Json
    if ($hash -ne $loader.dll_sha256) { throw '只打包锁定版本的官方 UAL x64，未知 DLL 未打包。' }
    $product = 'UltimateASILoader'; $version = $loader.version
    $type = 'asi-loader'; $path = 'xinput1_4.dll'
    $licensePath = 'plugins/Sky2ChestTracker/UltimateASILoader.LICENSE.txt'
    $name = 'Sky2ModLoader-UAL-' + $version
} elseif ($Distribution -eq 'Plugin') {
    $type = 'asi-plugin'; $path = 'plugins/Sky2ChestTracker.asi'
    $licensePath = 'plugins/Sky2ChestTracker/LICENSES.txt'
    $name = 'Sky2ChestTracker-' + $version + '-ASI'
} else {
    $type = 'standalone-proxy'; $path = 'xinput1_4.dll'
    $licensePath = 'Sky2ChestTracker/LICENSES.txt'
    $name = 'Sky2ChestTracker-' + $version + '-Standalone'
}
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $projectRoot 'release' }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$outputRoot = (Resolve-Path -LiteralPath $OutputDirectory).Path
# 每次建立独立暂存目录，避免把前次日志、开发产物或游戏资料混入发行包。
$stage = Join-Path $outputRoot ('.stage-' + [Guid]::NewGuid().ToString('N'))
$dist = Join-Path $stage 'dist'
$payload = Join-Path $dist $path
$license = Join-Path $dist $licensePath
New-Item -ItemType Directory -Path (Split-Path $payload -Parent) -Force | Out-Null
New-Item -ItemType Directory -Path (Split-Path $license -Parent) -Force | Out-Null
Copy-Item -LiteralPath $source -Destination $payload
if ($Distribution -eq 'Loader') {
    # Loader 保留上游完整 MIT 文本；即使放在插件目录，也由 Loader 独立管理。
    Copy-Item -LiteralPath (Join-Path $projectRoot 'licenses/Ultimate-ASI-Loader.txt') -Destination $license
} else {
    # 合并时不摘录、不删段落：项目 Required Notices、第三方来源和 MinHook/HDE
    # 的完整条款均原文保留。统一 LF 换行，避免同一提交在不同 Git 换行配置下
    # 生成不同许可指纹；只增加分隔标题，不将 Loader 许可归入宝箱所有权。
    $sections = @('Sky2 Chest Tracker - Licenses and notices')
    foreach ($file in @('LICENSE', 'THIRD_PARTY_NOTICES.md', 'licenses/Dear-ImGui.txt', 'licenses/MinHook.txt', 'licenses/ED9ModManager.txt')) {
        $body = [IO.File]::ReadAllText((Join-Path $projectRoot $file)).Replace("`r`n", "`n").Replace("`r", "`n")
        $sections += ("`n================================================================================`n" + $file + "`n================================================================================`n" + $body)
    }
    [IO.File]::WriteAllText($license, ($sections -join "`n"), (New-Object Text.UTF8Encoding($false)))
}
$scriptDirectory = Join-Path $stage 'installer'
New-Item -ItemType Directory -Path $scriptDirectory -Force | Out-Null
# 清单仅在安装包内用于校验，不复制到游戏；每个分发的载荷必须恰好两项。
$metadata = [ordered]@{
    schema = 2; type = $type; product = $product; version = $version; path = $path; sha256 = $hash
    exe_sha256 = 'd8b2911d1576216bdc22d070550e4f531e105de7ed2981885849669f4acf8aaf'
    files = @(
        [ordered]@{ path = $path; sha256 = $hash },
        [ordered]@{ path = $licensePath; sha256 = (Get-FileHash -LiteralPath $license -Algorithm SHA256).Hash }
    )
}
$metadata | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $scriptDirectory 'manifest.json') -Encoding utf8
# 各包只包含对应入口所需脚本，避免把其他产品的入口混入当前分发。
# 写成 UTF-8 BOM，确保 Windows PowerShell 5.1 正确解析中文提示。
$scripts = if ($Distribution -eq 'Loader') {
    @('Common.ps1', 'Install-Loader.ps1', 'Uninstall-Loader.ps1')
} else {
    @('Common.ps1')
}
foreach ($script in $scripts) {
    $body = [IO.File]::ReadAllText((Join-Path $projectRoot ('installer/' + $script)))
    [IO.File]::WriteAllText((Join-Path $scriptDirectory $script), $body, (New-Object Text.UTF8Encoding($true)))
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'installer/known-files.json') -Destination (Join-Path $scriptDirectory 'known-files.json')
if ($Distribution -ne 'Loader') {
    foreach ($file in @('Install-Mod.ps1', 'Uninstall-Mod.ps1')) {
        $body = [IO.File]::ReadAllText((Join-Path $projectRoot $file))
        [IO.File]::WriteAllText((Join-Path $stage $file), $body, (New-Object Text.UTF8Encoding($true)))
    }
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') -Destination (Join-Path $stage 'README.md')
$guides = Join-Path $stage 'docs'
New-Item -ItemType Directory -Path $guides -Force | Out-Null
foreach ($guide in @('INSTALLATION.md', 'USAGE.md', 'TRAVEL.md', 'ASI_LOADER.md')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot ('docs/' + $guide)) -Destination (Join-Path $guides $guide)
}
$archive = Join-Path $outputRoot ($name + '.zip')
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive -Force
$checksum = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + [IO.Path]::GetFileName($archive)
$checksum | Set-Content -LiteralPath (Join-Path $outputRoot ($name + '.sha256')) -Encoding ascii
Write-Output $archive
Write-Output $checksum
