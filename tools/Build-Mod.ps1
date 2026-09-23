<#
.SYNOPSIS
从用户自己的游戏资源生成目录，完整编译 Mod 并运行测试。
.DESCRIPTION
请在已加载 x64 MSVC 环境的 Developer PowerShell 中执行。不会安装 DLL 或修改存档。
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$GamePath,
    [string]$PythonExecutable = 'python',
    [string]$BuildDirectory,
    [string]$DependencyDirectory,
    # 正式默认构建显式写入 ON，不继承旧构建目录的 OFF 缓存；开发对照时才指定
    # 此参数构建剧情限制策略。两种策略均运行各自独立的自动回归目标。
    [switch]$StoryRestrictedTravel
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
# 脚本目录在进入脚本正文后才用于补全默认路径，确保 Windows PowerShell 5.1 也可运行。
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $projectRoot 'build-release' }
if (-not $DependencyDirectory) { $DependencyDirectory = Join-Path $projectRoot '.deps' }
foreach ($command in @('cl', 'cmake', 'ctest', 'nmake')) { Get-Command $command -ErrorAction Stop | Out-Null }
# 向 CMake 提供绝对路径，兼容工具链位于含空格目录时 NMake 自动查找失败的环境。
$makeExecutable = (Get-Command nmake -ErrorAction Stop).Source
$pythonCommand = (Get-Command $PythonExecutable -ErrorAction Stop).Source
$gameRoot = (Resolve-Path -LiteralPath $GamePath).Path
$expected = 'd8b2911d1576216bdc22d070550e4f531e105de7ed2981885849669f4acf8aaf'
if ((Get-FileHash -LiteralPath (Join-Path $gameRoot 'sora_2nd.exe')).Hash -ne $expected) {
    throw '游戏版本不匹配，停止目录生成和构建。'
}
& (Join-Path $PSScriptRoot 'Setup-Dependencies.ps1') -Destination $DependencyDirectory
$dependencyRoot = (Resolve-Path -LiteralPath $DependencyDirectory).Path
$catalog = Join-Path $projectRoot 'data/generated'
& $pythonCommand (Join-Path $PSScriptRoot 'extract_catalog.py') --game $gameRoot --out $catalog
if ($LASTEXITCODE -ne 0) { throw '宝箱目录生成失败。' }
& $pythonCommand (Join-Path $PSScriptRoot 'extract_map_catalog.py') --game $gameRoot --catalog (Join-Path $catalog 'chests.json') --out $catalog
if ($LASTEXITCODE -ne 0) { throw '地图目录生成失败。' }
& $pythonCommand (Join-Path $PSScriptRoot 'extract_travel_catalog.py') --game $gameRoot --out $catalog
if ($LASTEXITCODE -ne 0) { throw '传送目录生成失败。' }
$travelPolicy = if ($StoryRestrictedTravel) { 'OFF' } else { 'ON' }
& cmake -S $projectRoot -B $BuildDirectory -G 'NMake Makefiles' '-DCMAKE_BUILD_TYPE=Release' '-DSKY2_BUILD_MOD=ON' '-DBUILD_TESTING=ON' "-DSKY2_UNRESTRICTED_TRAVEL=$travelPolicy" "-DSKY2_DEPS_DIR=$dependencyRoot" "-DCMAKE_MAKE_PROGRAM=$makeExecutable"
if ($LASTEXITCODE -ne 0) { throw 'CMake 配置失败。' }
# 发布始终清理旧对象，防止头文件依赖扫描或开发缓存导致源码与二进制不一致。
& cmake --build $BuildDirectory --clean-first
if ($LASTEXITCODE -ne 0) { throw '编译失败。' }
& ctest --test-dir $BuildDirectory --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'C++ 测试失败，禁止发布。' }
& $pythonCommand -m unittest discover -s (Join-Path $projectRoot 'tests') -p 'test_public_*.py' -v
if ($LASTEXITCODE -ne 0) { throw 'Python 测试失败，禁止发布。' }
Write-Output "构建完成：$(Join-Path $BuildDirectory 'xinput1_4.dll')"
