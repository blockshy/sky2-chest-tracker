# 构建指南

当前版本为 **0.4.0**，预编译安装包见 [Releases](https://github.com/blockshy/sky2-chest-tracker/releases)。
以下步骤用于从源码构建；本地构建或打包不会自动发布新版本。

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

Windows x64 + MSVC 下另编译正式 MASM 跳板的 ABI 测试，验证额外寄存器保留、浮点参数、栈对齐及返回标志。
其他平台运行同一套纯逻辑边界测试，不尝试编译 Windows 汇编。探索回归需覆盖动态枚举、当前地区限制、
最终登记和禁用状态、area 禁用与临时分组显示，以及原生内部／特殊点过滤。
还需验证状态脚本完成后才统一评估，不依赖登记顺序，不写真实到访旗标；这些测试不读取玩家存档或启动游戏。

面板验证重点见 [测试说明](TESTING.md)：检查清单页码与顶部统计同行并靠右对齐，底部第一行为模式、筛选、收起，第二行为上一页、下一页；
主面板没有快捷键标题，宽度随实际内容及输入方式调整，地区计数、完整组合键和探索状态仍可见；清单按路径、计数、统计与快捷键内容测量宽度，长路径在窄窗口下仍可完整换行。每个手柄组合均完整带有 View 前缀，按键高亮颜色与主面板一致。同时回归“显示模式／切换模式”用语、探索名称与状态紧邻、
探索组合键列对齐，以及键鼠／手柄提示切换和普通、等待、不可用状态下的提示区。
检查不同分辨率、空清单与分页边界没有裁切或重叠。

探索功能的游戏内回归步骤见 [探索辅助测试指南](EXPLORATION_TESTING.md)。涉及探索逻辑修改时重点检查统一范围、原生菜单禁用负例、
设施回归样例，以及区域地图、
全地点列表、城镇子列表的原地开关刷新，临时选中项移除后的有效选择，确认／转场期间延迟应用，
以及镜头和缩放的保留。面板名称为“未到访传送点”，快捷键仍是 `Ctrl + F8` / `View + 十字键↓`；
仍需同时检查地图全显、键盘、手柄和原有宝箱功能。
还需覆盖普通地图初始显示数组为空、以及切换后当前子列表或整个传送清单合法为空的情况：空清单应正常关图，功能保持可用，重新开图后仍可切换；结构校验失败才标为不可用。
自动测试不能代替地图显露、真实传送落点与具体剧情阶段的实机验证。

## 打包

完整构建、测试和实机验证后运行：

```powershell
.\tools\Package-Mod.ps1 -DllPath '.\build-release\xinput1_4.dll'
```

输出 `release/Sky2ChestTracker-<版本>.zip` 和同名 `.sha256` 文件。脚本只打包明确列出的 DLL、安装脚本、玩家 README、安装数据和许可证；开发文档不进入玩家包。
暂存目录保留在被忽略的 `release/` 中；不要将整个工作目录压缩发布。
安装需要解压整个 ZIP，再使用 `Install-Mod.ps1 -GamePath ...`。
手动安装时仅复制 `dist/xinput1_4.dll` 与 `dist/Sky2ChestTracker/`；后者包含与 DLL 配套的归属记录和许可证。
`installer/legacy-documents.json` 记录首次公开版六份文档的 SHA-256，计算前按 UTF-8 文本读取、去掉 BOM 并统一为 LF；它只用于识别可归档的旧文档，不用于 DLL 身份判断。

只更新安装器或文档、继续使用原有已验证 DLL 时，可添加 `-PackageRevision r1`，生成
`Sky2ChestTracker-<版本>-r1.zip` 及对应校验文件。修订号不改变游戏内版本，也不能用来省略 DLL 修改后的构建与验证。

## 构建一致性

项目显式声明本地头文件依赖，发布构建仍使用 `--clean-first`。
更改手柄状态机等头文件后，核对对应 DLL 源文件确实重新编译，不能只凭独立测试程序通过就认定发布 DLL 已更新。
生成目录、编译时间和工具链差异可能改变 DLL 哈希；每个安装包的 manifest 记录该包的实际 DLL 哈希。
