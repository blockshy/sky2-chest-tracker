# 构建指南

## 环境

完整 DLL 构建需要 Windows x64、Visual Studio 的“使用 C++ 的桌面开发”工作负载与 Windows SDK、CMake 3.20+、Git、Python 3.10+。
推荐使用 **PowerShell 7**，并加载 Visual Studio 的 x64 编译环境，确认 `cl`、`cmake`、`ctest`、`nmake` 可用。
也可在 **x64 Native Tools Command Prompt** 中运行 `pwsh`，继承已经配置好的编译环境。
生成器和公开 Python 测试只用标准库；玩家安装现成 DLL 不需要这些工具。

## 完整构建

```powershell
git clone https://github.com/blockshy/sky2-chest-tracker.git
cd sky2-chest-tracker
py -3 -m venv .venv
.\tools\Build-Mod.ps1 -GamePath '你的游戏安装目录' -PythonExecutable '.\.venv\Scripts\python.exe'
```

构建脚本会依次：

1. 校验游戏 EXE 的 SHA-256。
2. 按 `dependencies.json` 下载并核对 Dear ImGui 和 MinHook 的固定提交，放入被 Git 忽略的 `.deps/`。
3. 从本机游戏生成 `data/generated/` 下的宝箱和地图目录。
4. 使用 NMake 与 MSVC 完整清理重编译，生成 `build-release/xinput1_4.dll`。
5. 运行 CTest 与公开 Python 测试，任何步骤失败都会停止。

首次构建需要网络下载开源依赖。已经存在的依赖目录必须处于锁定提交且没有修改；脚本不会覆盖你的本地修改。
可通过 `-DependencyDirectory` 指定已准备好的依赖父目录，通过 `-BuildDirectory` 指定构建目录。
两种目录不要与其他项目的构建目录混用。

不经过脚本时，可分别运行：

```powershell
.\tools\Setup-Dependencies.ps1
.\.venv\Scripts\python.exe tools\extract_catalog.py --game '你的游戏安装目录' --out data/generated
.\.venv\Scripts\python.exe tools\extract_map_catalog.py --game '你的游戏安装目录' --out data/generated
cmake -S . -B build-release -G 'NMake Makefiles' -DCMAKE_BUILD_TYPE=Release "-DCMAKE_MAKE_PROGRAM=$((Get-Command nmake).Source)"
cmake --build build-release --clean-first
ctest --test-dir build-release --output-on-failure
.\.venv\Scripts\python.exe -m unittest discover -s tests -p 'test_public_*.py' -v
```

请先自行核对 EXE 哈希，再使用手动生成方式。不要将新游戏版本生成的哈希当作已适配的证明；运行时地址也必须重新核对。
目录生成后共有 566 个正式宝箱、59 个统计地图；原始行号参与标志计算，不得重新编号。

## 不持有游戏时运行测试

```powershell
cmake -S . -B build-tests -DSKY2_BUILD_MOD=OFF
cmake --build build-tests --config Release
ctest --test-dir build-tests -C Release --output-on-failure
python -m unittest discover -s tests -p 'test_public_*.py' -v
```

此模式不下载 ImGui、MinHook，也不需要 Windows 游戏环境。C++ 核心状态、手柄状态机和 Python 解析边界测试可以运行；
依赖真实目录的用例明确跳过。GitHub Actions 使用此模式，不上传或下载游戏资源。

## 打包

完整构建、测试和实机验证后运行：

```powershell
.\tools\Package-Mod.ps1 -DllPath '.\build-release\xinput1_4.dll'
```

输出 `release/Sky2ChestTracker-<版本>.zip` 和同名 `.sha256` 文件。脚本只打包明确列出的 DLL、安装脚本、正式文档和许可证。
暂存目录保留在被忽略的 `release/` 中；不要将整个工作目录压缩发布。
安装需要解压整个 ZIP，再使用 `Install-Mod.ps1 -GamePath ...`。

## 构建一致性

项目显式声明本地头文件依赖，发布构建仍使用 `--clean-first`。
更改手柄状态机等头文件后，核对对应 DLL 源文件确实重新编译，不能只凭独立测试程序通过就认定发布 DLL 已更新。
生成目录、编译时间和工具链差异可能改变 DLL 哈希；每个安装包的 manifest 记录该包的实际 DLL 哈希。
