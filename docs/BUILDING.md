# 构建指南

本页对应 **0.6.0**。完整构建同时生成独立版与 ASI 插件版，两者默认提供全传送；保留剧情限制的构建仅用于开发对照。已发布的预编译包见 [Releases](https://github.com/blockshy/sky2-chest-tracker/releases)。本地构建和打包不会自动发布版本，也不会安装 DLL 或修改游戏存档。

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
3. 从本机游戏生成 `data/generated/` 下的宝箱、地图及传送目录，同时生成游戏全部八种语言的原生名称表。
4. 显式配置全传送策略，使用 NMake 与 MSVC 清理重编译，共用 `tracker_runtime` 对象生成 `build-release/xinput1_4.dll` 与 `build-release/Sky2ChestTracker.asi`。
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

## 八语资源与界面

现有 `extract_map_catalog.py` 与 `extract_travel_catalog.py` 已接入 `localized_game_names.py`；无需额外生成命令。它们读取玩家本机八种语言表包中的 `t_place.tbl`、`t_mapjump.tbl`，必要时从 `t_notemenu.tbl` 取得面向玩家的地区名称。日语使用 `table.pac`，其他语言分别使用 `table_sc.pac`、`table_en.pac`、`table_tc.pac`、`table_de.pac`、`table_fr.pac`、`table_es.pac`、`table_ko.pac`。

新增生成产物为 `data/generated/map_localization.h`、`travel_localization.h` 和便于核对来源的 `travel_localization.json`；地图八语名称与来源记录在 `maps.json`。这些名称表按固定 `sc/jp/en/tc/de/fr/es/ko` 顺序编入 DLL／ASI，不作为额外文件安装到玩家游戏目录。原始表、生成目录与私人研究资料均不提交。

游戏专名必须通过稳定的地点／传送 ID 和场景关联，不可手写译名、靠外文模糊匹配、按译名排序后重编号，或将内部入口的外文名称误判成新增地点。`native/ui_text.h` 只维护 Mod 自有文案；`native/localized_names.h` 使用生成的原生名称。语言检测在完整 EXE 哈希通过后只读原生文本语言，与语音设置无关，详见 [语言与原生名称](LOCALIZATION.md)。

中日韩字形使用本机 Windows 字体，项目和安装包不携带游戏或 Windows 字体。界面检查机器也需要具备对应字体，否则不能将缺字的截图作为对应语言通过证据。

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

此模式不下载 ImGui、MinHook，也不需要 Windows 游戏环境。C++ 核心状态、输入状态机、语言映射和 Python 解析边界测试可以运行；依赖真实目录的用例明确跳过。GitHub Actions 使用此模式，不上传或下载游戏资源。
语言检测的 Windows 用例使用自行分配的内存验证空指针、不可读页面和非法枚举，不启动游戏；其他平台只运行纯语言映射及文本选择测试。
Windows x64 + MSVC 下另运行记录文件 I/O、双策略会话和生产 MASM 跳板的 ABI 测试；其他平台不编译 Windows 汇编。
生产 helper 的合成内存测试随完整 DLL 构建启用，使用测试自行分配的对象，不连接游戏、不读取真实存档、不安装挂钩。

自动验证范围、传送策略负例、两列页脚和导航位置记忆回归见 [测试说明](TESTING.md)。
实际游戏检查分别见 [探索辅助测试指南](EXPLORATION_TESTING.md)、[全传送指南](TRAVEL.md) 和 [插件共存测试](PLUGIN_TESTING.md)。编译、合成测试及离屏绘制通过不代表所有地点、章节和剧情组合均已实测。

## 准备 Loader 与隔离加载验证

公共 Loader 使用 ThirteenAG 的官方 [Ultimate ASI Loader 9.7.4 x64](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/tag/v9.7.4)。`loader-dependency.json` 固定上游提交、下载地址、发布 ZIP 与 DLL 的 SHA-256；准备脚本核验两层哈希，仅写入 `.deps/`，不安装或运行 Loader。重命名官方 DLL 不改变其二进制内容；独立 Loader 包保留 MIT 许可。玩家用法见 [ASI 与 Loader 指南](ASI_LOADER.md)。

```powershell
# 在线准备固定版本；已有匹配文件可直接复用。
.\tools\Setup-AsiLoader.ps1
# 离线时指定已下载的官方压缩包，仍执行相同哈希校验。
.\tools\Setup-AsiLoader.ps1 -ArchivePath '你的下载目录\Ultimate-ASI-Loader-NoPDB_x64.zip'
```

完整 Windows 构建会生成 `build-release/tests/asi/` 中的隔离宿主、两个最小插件和诊断探针。构建完成后运行：

```powershell
# 输出目录必须是全新路径；省略该参数时脚本自动创建独立研究目录。
.\tests\asi\Invoke-LoaderIntegration.ps1 -LoaderPath '.\.deps\ultimate-asi-loader-9.7.4\xinput1_4.dll' -BinaryDirectory '.\build-release\tests\asi' -ChestPluginPath '.\build-release\Sky2ChestTracker.asi' -StandaloneDllPath '.\build-release\xinput1_4.dll'
```

精简安装结构的前一构建已通过 26 组 CTest 与 10 个真实 UAL 隔离场景，覆盖加载顺序互换、分别移除插件、`InitializeASI`、重复副本保护、XInput 名称／序号转发、生产插件拒绝不受支持的假宿主，以及旧独立版日志和返程哨兵不变。此前插件化测试构建另已在本机 Steam 启动中验证宝箱与探针同时加载、面板、键鼠／Xbox 手柄热切换与重连、一次传送和精确返程，详见 [实机记录](PLUGIN_TESTING.md#本轮验证记录)。这些既有结果不替代本次八语构建的测试与游戏内语言切换检查；本次验证范围以 [测试说明](TESTING.md) 为准。探针没有第二套界面或输入挂钩，不能据此宣称任意两个 Mod 均兼容。

## 打包

公开发布前应完成完整构建、自动测试和适用范围的实机验证。当前构建版本为 0.6.0；开发对照 DLL 不应混入正式包。以下三种包分别打包，不能把公共 Loader 当成独立版 DLL。

```powershell
# 独立版：根目录代理；Distribution 的默认值也是 Standalone。
.\tools\Package-Mod.ps1 -Distribution Standalone -DllPath '.\build-release\xinput1_4.dll'
# 插件版：只携带宝箱 ASI，不包含公共 Loader。
.\tools\Package-Mod.ps1 -Distribution Plugin -DllPath '.\build-release\Sky2ChestTracker.asi'
# 公共加载器：独立包、独立文件归属，使用前一步核验过的官方文件。
.\tools\Package-Mod.ps1 -Distribution Loader -DllPath '.\.deps\ultimate-asi-loader-9.7.4\xinput1_4.dll'
```

分别输出 `Sky2ChestTracker-0.6.0-Standalone.zip`、`Sky2ChestTracker-0.6.0-ASI.zip`、`Sky2ModLoader-UAL-9.7.4.zip`，默认位于 `release/`，各有同名 `.sha256` 文件。脚本按白名单打包二进制、安装脚本、玩家 README、四份指南（`INSTALLATION.md`、`USAGE.md`、`TRAVEL.md`、`ASI_LOADER.md`）、包内校验清单和许可证；诊断探针、`PLUGIN_TESTING.md` 与开发资料不进入玩家包。
四份指南位于包内 `docs/`，与 README 互相链接，解压后可离线阅读；它们不在 `dist/` 内，不会复制到游戏目录。
暂存目录保留在被忽略的 `release/` 中，不要压缩整个工作目录。
玩家自己的 `revisit-return.dat`、`revisit-history/`、存档、日志和私有研究资料不进入安装包。
安装更新和卸载均保留用户返程记录，不应在打包或清理脚本中递归清空 Mod 目录。

安装需要解压整个 ZIP。宝箱包根目录 `Install-Mod.ps1` 根据包内类型安装独立版或 ASI；公共 Loader 使用 `installer/Install-Loader.ps1`。手动复制 `dist/` 的全部内容即可；安装脚本、文档及校验清单不在 `dist/` 中，不生成游戏目录收据。手动路径与数据迁移见 [ASI 指南](ASI_LOADER.md)。

每个分发的 `dist/` 恰好两个文件：独立版 DLL＋`Sky2ChestTracker/LICENSES.txt`，插件 ASI＋`plugins/Sky2ChestTracker/LICENSES.txt`，Loader DLL＋`plugins/Sky2ChestTracker/UltimateASILoader.LICENSE.txt`。合并许可逐字保留项目、第三方来源、Dear ImGui、MinHook/HDE 和 ED9ModManager 文本；Loader 的 MIT 许可仍单独管理。

包内 `installer/manifest.json` 使用 schema 2，包含 `type`、`product`、`version`、`path`、`sha256`、`exe_sha256` 以及两项 `files`；`installer/known-files.json` 使用 schema 1，提供历史路径、哈希、产品和类型供识别及旧资料清理。它们只属于安装包，不能复制进游戏作为新的归属凭证。

0.6.0 八语构建重新发布不增加修订号；应重建并核对 ZIP 与 `.sha256`，通过实际文件哈希区分旧的同名安装包。语言文本和原生名称已编入二进制，不新增玩家安装文件。正式包不携带诊断探针或运行时数据。

## 构建一致性

项目显式声明本地头文件依赖，发布构建仍使用 `--clean-first`。
更改输入状态机或传送策略等头文件后，核对对应 DLL 源文件确实重新编译，不能只凭独立测试程序通过就认定发布 DLL 已更新。
生成目录、编译时间和工具链差异可能改变 DLL 哈希；每个安装包的 manifest 记录该包的实际 DLL 哈希。
