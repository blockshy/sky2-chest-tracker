# 构建指南

本页对应 **0.5.0**。正式 DLL 默认提供全传送清单；保留剧情限制的构建仅用于开发对照。预编译安装包见 [Releases](https://github.com/blockshy/sky2-chest-tracker/releases)。本地构建和打包不会自动发布版本，也不会安装 DLL 或修改游戏存档。

## 环境

完整 DLL 构建需要 Windows x64、Visual Studio 的“使用 C++ 的桌面开发”工作负载与 Windows SDK、CMake 3.20+、Git、Python 3.10+。
推荐使用 **PowerShell 7**，并加载 Visual Studio 的 x64 编译环境，确认 `cl`、`ml64`、`cmake`、`ctest`、`nmake` 可用。
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
3. 从本机游戏生成 `data/generated/` 下的宝箱、地图及传送目录。
4. 显式配置全传送策略，使用 NMake 与 MSVC 清理重编译，生成 `build-release/xinput1_4.dll`。
5. 运行两种传送策略的 CTest 及公开 Python 测试；任何步骤失败都会停止。

首次构建需要网络下载开源依赖。已经存在的依赖目录必须处于锁定提交且没有修改；脚本不会覆盖你的本地修改。
可通过 `-DependencyDirectory` 指定已准备好的依赖父目录，通过 `-BuildDirectory` 指定构建目录；不要与其他项目混用。

不经过脚本时，可分别运行：

```powershell
.\tools\Setup-Dependencies.ps1
.\.venv\Scripts\python.exe tools\extract_catalog.py --game '你的游戏安装目录' --out data/generated
.\.venv\Scripts\python.exe tools\extract_map_catalog.py --game '你的游戏安装目录' --out data/generated
.\.venv\Scripts\python.exe tools\extract_travel_catalog.py --game '你的游戏安装目录' --out data/generated
cmake -S . -B build-release -G 'NMake Makefiles' -DCMAKE_BUILD_TYPE=Release -DSKY2_BUILD_MOD=ON -DBUILD_TESTING=ON -DSKY2_UNRESTRICTED_TRAVEL=ON "-DCMAKE_MAKE_PROGRAM=$((Get-Command nmake).Source)"
cmake --build build-release --clean-first
ctest --test-dir build-release --output-on-failure
.\.venv\Scripts\python.exe -m unittest discover -s tests -p 'test_public_*.py' -v
```

请先自行核对 EXE 哈希，再使用手动生成方式。不能将新游戏版本生成的哈希当作已适配的证明，运行时地址也必须重新核对。
目录生成后共有 566 个正式宝箱、59 个统计地图，以及 180 条传送表记录、166 个唯一传送 ID。展示层保留 155 个原生地点，另加迷途之森入口与精确返程，共 157 项。11 个纯内部 ID 不展示。
原始宝箱行号参与标志计算，不得重新编号；传送表的同 ID 备用入口保留至运行时核对，不应去重为任意一个坐标。生成目录只提供静态数据，不能替代运行时身份检查，也不应提交到仓库。

## 全传送与剧情限制策略

`SKY2_UNRESTRICTED_TRAVEL` 的 CMake 默认值为 `ON`，头文件宏默认值为 `1`。正式构建不以目的地登记、禁用、所属分组或前置剧情作为全传送清单的准入条件，仍要求真实场景、坐标、稳定菜单、二次确认、出发点持久化及必要保护模块通过检查。
该选项不改变“未到访传送点”探索开关的原生菜单补显规则。功能范围见 [全传送指南](TRAVEL.md)。

需要开发对照时，使用独立目录显式构建剧情限制策略：

```powershell
.\tools\Build-Mod.ps1 -GamePath '你的游戏安装目录' -PythonExecutable '.\.venv\Scripts\python.exe' -BuildDirectory '.\build-story-restricted' -StoryRestrictedTravel
```

手动使用 CMake 时，对照构建设置 `-DSKY2_UNRESTRICTED_TRAVEL=OFF`，正式构建设置 `ON`。CMake 缓存会保留既有值，修改选项默认值或省略参数都不会覆盖旧缓存；构建脚本每次显式写入所选策略。

所有测试均显式指定策略，与 DLL 配置独立：原有保守规则、会话和运行时测试使用宏 `0`，名称带 `_unrestricted_tests` 的会话及运行时测试使用宏 `1`。无论 DLL 选择哪个策略，两套现有回归都应通过，不能用正式默认值掩盖保守规则的失败。

## 不持有游戏时运行测试

```powershell
cmake -S . -B build-tests -DSKY2_BUILD_MOD=OFF
cmake --build build-tests --config Release
ctest --test-dir build-tests -C Release --output-on-failure
python -m unittest discover -s tests -p 'test_public_*.py' -v
```

此模式不下载 ImGui、MinHook，也不需要 Windows 游戏环境。C++ 核心状态、输入状态机和 Python 解析边界测试可以运行；依赖真实目录的用例明确跳过。GitHub Actions 使用此模式，不上传或下载游戏资源。
Windows x64 + MSVC 下另运行记录文件 I/O、双策略会话和生产 MASM 跳板的 ABI 测试；其他平台不编译 Windows 汇编。
生产 helper 的合成内存测试随完整 DLL 构建启用，使用测试自行分配的对象，不连接游戏、不读取真实存档、不安装挂钩。

自动验证范围、传送策略负例、两列页脚和导航位置记忆回归见 [测试说明](TESTING.md)。
实际游戏检查分别见 [探索辅助测试指南](EXPLORATION_TESTING.md) 和 [全传送指南](TRAVEL.md)。编译、合成测试及离屏绘制通过不代表所有地点、章节和剧情组合均已实测。

## 打包

公开发布前应完成完整构建、自动测试和适用范围的实机验证。发布 DLL 使用全传送策略，并确认版本信息为 0.5.0；开发对照 DLL 不应混入正式包。

```powershell
.\tools\Package-Mod.ps1 -DllPath '.\build-release\xinput1_4.dll'
```

输出 `release/Sky2ChestTracker-<版本>.zip` 和同名 `.sha256` 文件。脚本只打包明确列出的 DLL、安装脚本、玩家 README、三份玩家指南（`INSTALLATION.md`、`USAGE.md`、`TRAVEL.md`）、安装数据和许可证；开发文档不进入玩家包。
三份指南位于包内 `docs/`，与 README 互相链接，解压后可离线阅读；它们不在 `dist/` 内，不会复制到游戏目录。
暂存目录保留在被忽略的 `release/` 中，不要压缩整个工作目录。
玩家自己的 `revisit-return.dat`、`revisit-history/`、存档、日志和私有研究资料不进入安装包。
安装更新和卸载均保留用户返程记录，不应在打包或清理脚本中递归清空 Mod 目录。

安装需要解压整个 ZIP，再使用 `Install-Mod.ps1 -GamePath ...`。
手动安装时仅复制 `dist/xinput1_4.dll` 与 `dist/Sky2ChestTracker/`；后者包含与 DLL 配套的归属记录和许可证。
`installer/legacy-documents.json` 记录首次公开版六份文档的 SHA-256，计算前按 UTF-8 文本读取、去掉 BOM 并统一为 LF；它只用于识别可归档的旧文档，不用于 DLL 身份判断。

只更新安装器或文档、继续使用原有已验证 DLL 时，可添加 `-PackageRevision r1`，生成 `Sky2ChestTracker-<版本>-r1.zip` 及对应校验文件。
修订号不改变游戏内版本，也不能省略 DLL 修改后的构建与验证。

## 构建一致性

项目显式声明本地头文件依赖，发布构建仍使用 `--clean-first`。
更改输入状态机或传送策略等头文件后，核对对应 DLL 源文件确实重新编译，不能只凭独立测试程序通过就认定发布 DLL 已更新。
生成目录、编译时间和工具链差异可能改变 DLL 哈希；每个安装包的 manifest 记录该包的实际 DLL 哈希。
