<#
.SYNOPSIS
按 dependencies.json 准备固定版本的 Dear ImGui 和 MinHook。
.DESCRIPTION
仅创建缺失目录；已有目录必须是对应提交且工作区干净，避免覆盖开发者自己的修改。
#>
[CmdletBinding()]
param([string]$Destination)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
# 在参数绑定完成后解析脚本目录，兼容 Windows PowerShell 5.1 的默认参数求值时机。
if (-not $Destination) { $Destination = Join-Path $projectRoot '.deps' }
$lock = Get-Content -Raw (Join-Path $projectRoot 'dependencies.json') | ConvertFrom-Json
Get-Command git -ErrorAction Stop | Out-Null
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
$dependencyRoot = (Resolve-Path -LiteralPath $Destination).Path
foreach ($property in $lock.PSObject.Properties) {
    $name = $property.Name
    $entry = $property.Value
    $directory = Join-Path $dependencyRoot $name
    if (-not (Test-Path -LiteralPath $directory)) {
        # 不跟随浮动分支；只取所需提交，降低下载量并保证版本可追溯。
        & git init $directory
        if ($LASTEXITCODE -ne 0) { throw "无法初始化依赖：$name" }
        & git -C $directory remote add origin $entry.repository
        if ($LASTEXITCODE -ne 0) { throw "无法设置依赖来源：$name" }
        & git -C $directory fetch --depth 1 origin $entry.commit
        if ($LASTEXITCODE -ne 0) { throw "依赖下载失败：$name；请检查网络后手动处理该未完成目录。" }
        & git -C $directory checkout --detach $entry.commit
        if ($LASTEXITCODE -ne 0) { throw "无法检出锁定提交：$name" }
    }
    $headCommit = & git -C $directory rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $headCommit -ne $entry.commit) { throw "依赖提交不匹配：$directory" }
    $changes = & git -C $directory status --porcelain
    if ($LASTEXITCODE -ne 0 -or $changes) { throw "依赖目录包含修改，未覆盖：$directory" }
    Write-Output "已核对 $name $headCommit"
}
