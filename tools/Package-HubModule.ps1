<#
.SYNOPSIS
按明确文件白名单生成新增的 Hub 模块包，不改变既有 ASI 或 Standalone 打包流程。
.DESCRIPTION
只加入模块 DLL、模块清单、许可和离线指南。不会安装游戏、复制玩家返程记录，
不会把本机资源目录、调试符号或宿主/UAL 混入此包；宿主由自己的安装工具管理。
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$DllPath,
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$source = (Resolve-Path -LiteralPath $DllPath).Path
if ([IO.Path]::GetFileName($source) -ne 'Sky2ChestTracker.module.dll') {
    throw '只接受 Sky2ChestTracker.module.dll；不能把原 ASI 或 Standalone 改名作为模块发布。'
}
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $projectRoot 'release' }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$outputRoot = (Resolve-Path -LiteralPath $OutputDirectory).Path
# 每次使用新暂存目录，避免包含上次产物或误删用户选择的输出目录。
$stage = Join-Path $outputRoot ('.hub-stage-' + [Guid]::NewGuid().ToString('N'))
$moduleRelative = 'plugins/Sky2ModHub/modules/Sky2ChestTracker.module.dll'
$manifestRelative = 'plugins/Sky2ModHub/modules/Sky2ChestTracker.module.ini'
$licenseRelative = 'plugins/Sky2ModHub/modules/Sky2ChestTracker.LICENSES.txt'
$dist = Join-Path $stage 'dist'
$payload = Join-Path $dist $moduleRelative
New-Item -ItemType Directory -Path (Split-Path $payload -Parent) -Force | Out-Null
Copy-Item -LiteralPath $source -Destination $payload
$utf8 = [Text.UTF8Encoding]::new($false)
# 显式清单是宿主唯一的模块枚举入口；安装文件名、ABI 和稳定模块 ID 必须一致。
[IO.File]::WriteAllText((Join-Path $dist $manifestRelative),
    "[Module]`r`nId=chest`r`nBinary=Sky2ChestTracker.module.dll`r`nAbi=1`r`nEnabled=1`r`n", $utf8)
$sections = @('Sky2 Chest Tracker Hub module - Licenses and notices')
foreach ($relative in @('LICENSE', 'THIRD_PARTY_NOTICES.md', 'licenses/Dear-ImGui.txt', 'licenses/MinHook.txt', 'licenses/ED9ModManager.txt')) {
    $sections += ("`n======== " + $relative + " ========`n" + [IO.File]::ReadAllText((Join-Path $projectRoot $relative)))
}
[IO.File]::WriteAllText((Join-Path $dist $licenseRelative), ($sections -join "`n"), $utf8)
# 模块指南放在包根目录时同步调整相对链接，并按白名单保留它引用的业务指南。
$guide = [IO.File]::ReadAllText((Join-Path $projectRoot 'docs/HUB_MODULE.md')).Replace('](TRAVEL.md)', '](docs/TRAVEL.md)')
[IO.File]::WriteAllText((Join-Path $stage 'README.md'), $guide, $utf8)
$guides = Join-Path $stage 'docs'
New-Item -ItemType Directory -Path $guides -Force | Out-Null
foreach ($name in @('TRAVEL.md', 'USAGE.md', 'INSTALLATION.md', 'ASI_LOADER.md')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot ('docs/' + $name)) -Destination (Join-Path $guides $name)
}
$files = foreach ($relative in @($moduleRelative, $manifestRelative, $licenseRelative)) {
    [ordered]@{ path = $relative; sha256 = (Get-FileHash -LiteralPath (Join-Path $dist $relative) -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$metadata = [ordered]@{
    schema = 1; type = 'hub-module'; product = 'Sky2ChestTracker'; module_id = 'chest'
    version = '0.6.0-hub.1'; abi = 1
    exe_sha256 = 'd8b2911d1576216bdc22d070550e4f531e105de7ed2981885849669f4acf8aaf'
    files = @($files)
}
[IO.File]::WriteAllText((Join-Path $stage 'manifest.json'), ($metadata | ConvertTo-Json -Depth 5), $utf8)
$archive = Join-Path $outputRoot 'Sky2ChestTracker-0.6.0-HubModule.zip'
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive -Force
$checksum = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + [IO.Path]::GetFileName($archive)
[IO.File]::WriteAllText(($archive + '.sha256'), $checksum + "`n", [Text.Encoding]::ASCII)
Write-Output $archive
Write-Output $checksum
