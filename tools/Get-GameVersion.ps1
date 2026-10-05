<#
.SYNOPSIS
严格读取运行时共用的游戏版本头，供构建、打包及离线安装器使用。
.DESCRIPTION
只接受约定的单行 constexpr 字符串声明，缺失、重复、非法格式均立即失败。
打包时连同原始头文件复制到 installer；不执行头文件内容或提供默认旧指纹。
#>
[CmdletBinding()]
param([string]$HeaderPath = (Join-Path (Split-Path $PSScriptRoot -Parent) 'native/game_version.h'))
$ErrorActionPreference = 'Stop'
$source = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $HeaderPath).Path)
$fields = [ordered]@{
    ExeSha256 = @('kSupportedExeSha256', '[0-9a-f]{64}')
    FileVersion = @('kSupportedFileVersion', '[0-9]+(?:\.[0-9]+){3}')
    SteamBuildId = @('kSteamBuildId', '[0-9]+')
    PreviousExeSha256 = @('kPreviousExeSha256', '[0-9a-f]{64}')
}
$version = [ordered]@{}
foreach ($key in $fields.Keys) {
    # 使用命名捕获、逐字段验证，避免无意匹配注释中的示例或多份声明。
    $constant = $fields[$key][0]
    $pattern = '(?m)^inline constexpr char ' + $constant + '\[\] = "(?<value>[^"]*)";\r?$'
    $entries = [regex]::Matches($source, $pattern)
    if ($entries.Count -ne 1 -or $entries[0].Groups['value'].Value -cnotmatch ('^' + $fields[$key][1] + '$')) {
        throw "游戏版本配置无效：$constant"
    }
    $version[$key] = $entries[0].Groups['value'].Value
}
[PSCustomObject]$version
