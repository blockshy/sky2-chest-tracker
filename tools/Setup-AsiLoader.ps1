<#
.SYNOPSIS
准备固定版本的官方 UAL x64，仅写入构建依赖目录，不安装到游戏。
.DESCRIPTION
同时核验发布 ZIP 与 DLL 的 SHA-256；已有未知文件不覆盖。
可提供本机 ArchivePath 进行离线准备。下载内容只解压，不在此脚本中执行。
#>
[CmdletBinding()]
param([string]$Destination, [string]$ArchivePath)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$lock = Get-Content -Raw -LiteralPath (Join-Path $projectRoot 'loader-dependency.json') | ConvertFrom-Json
if (-not $Destination) { $Destination = Join-Path $projectRoot ('.deps/ultimate-asi-loader-' + $lock.version) }
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
$destinationRoot = (Resolve-Path -LiteralPath $Destination).Path
if (-not $ArchivePath) {
    $ArchivePath = Join-Path $destinationRoot 'Ultimate-ASI-Loader-NoPDB_x64.zip'
    if (-not (Test-Path -LiteralPath $ArchivePath)) {
        Invoke-WebRequest -Uri $lock.url -OutFile $ArchivePath -UseBasicParsing
    }
}
if ((Get-FileHash -LiteralPath $ArchivePath).Hash -ne $lock.archive_sha256) { throw 'UAL 压缩包与锁定版本不一致，未解压。' }
$target = Join-Path $destinationRoot 'xinput1_4.dll'
if (Test-Path -LiteralPath $target) {
    if ((Get-FileHash -LiteralPath $target).Hash -ne $lock.dll_sha256) { throw '已有 Loader 与锁定版本不一致，未覆盖。' }
} else {
    # 全新暂存目录避免混入历史文件。只复制核验后的官方 DLL；不递归删除已有目录。
    $stage = Join-Path $destinationRoot ('.extract-' + [Guid]::NewGuid().ToString('N'))
    Expand-Archive -LiteralPath $ArchivePath -DestinationPath $stage
    $source = Join-Path $stage 'dinput8.dll'
    if ((Get-FileHash -LiteralPath $source).Hash -ne $lock.dll_sha256) { throw 'UAL DLL 校验失败。' }
    [IO.File]::Copy($source, $target, $false)
}
Write-Output "已核对 UAL $($lock.version) x64：$target"
