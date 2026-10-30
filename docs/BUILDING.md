# 手动构建与项目结构

[项目首页](../README.md) · [实现说明](ARCHITECTURE.md) · [测试说明](TESTING.md)

当前版本为 **0.6.2**，目标为游戏 **Ver.1.05 / EXE 1.5.0.0 / Steam build 26290169**。现成 Mod 的文件复制见 [安装指南](INSTALLATION.md)，此页面向修改源码的开发者。

## 准备环境

- Windows x64、Visual Studio 的“使用 C++ 的桌面开发”工作负载、Windows SDK 与 MASM。
- CMake 3.20+、Git；资源处理及公开 Python 测试需要 Python 3.10+。
- 自己合法安装的受支持游戏。游戏 EXE、资源包、存档和派生目录不纳入仓库。

依赖源码放在同一目录下的 `imgui/` 与 `minhook/`，默认根目录为 `.deps/`。固定版本如下，不直接使用上游最新分支：

| 依赖 | 提交 |
| --- | --- |
| Dear ImGui | `420f1793417e39560ca39a4209c55a9f204fde13` |
| MinHook | `8af6b4acae5a9388fd742b56fa79ece89d96f823` |

## 本机资源目录

完整构建要求 `data/generated/` 中已有与本机 EXE 指纹一致的宝箱、地图、传送及八语名称目录。生成逻辑位于 `tools/`；目录内容只来自本机资源，不能使用他人版本的目录或手改哈希。

| 文件 | 用途 |
| --- | --- |
| `chest_catalog.h` | 566 个宝箱身份、旗标及 EXE 指纹 |
| `map_catalog.h` | 59 个地图分组与宝箱归属 |
| `travel_catalog.h` | 原生传送目标与地区归属 |
| `map_localization.h`、`travel_localization.h` | 八种语言的官方地点名称 |

`native/game_version.h` 是当前 EXE、文件版本与 build 的权威来源。运行时、构建和目录指纹必须一致。仅更新这个头文件不能完成新游戏适配；地址、对象字段及脚本资源核验要求见 [兼容性记录](GAME_1_5_0_COMPATIBILITY.md)。

## 手动配置与编译

1. 打开 CMake GUI，源目录选择仓库，构建目录选择独立的空目录。
2. 选择已安装的 Visual Studio 生成器和 **x64** 平台，设置 `SKY2_DEPS_DIR` 为依赖根目录。
3. 完整 Mod 构建保持 `SKY2_BUILD_MOD=ON`、`BUILD_TESTING=ON`；正式功能使用 `SKY2_UNRESTRICTED_TRAVEL=ON`，关闭该项仅用于剧情限制策略的开发对照。
4. 依次执行 Configure、Generate、Open Project。在 Visual Studio 中选择 **Release / x64**，构建 `ALL_BUILD`。
5. 执行 CTest，确认测试全部通过。两种传送策略有各自测试，不以当前 DLL 的默认策略替代另一种验证。

也可在构建完成后手动执行测试命令：

```text
ctest --test-dir <构建目录> -C Release --output-on-failure
```

输出包含 `xinput1_4.dll`（独立版）和 `Sky2ChestTracker.asi`（插件版），两者共用功能对象，仅加载入口不同。二进制位于生成器对应的输出目录，Visual Studio 通常为 `Release/`。

不持有游戏资源时，可在新的构建目录将 `SKY2_BUILD_MOD=OFF`，运行能够构建的公开逻辑测试；这不能证明实际游戏地址、图形界面或手柄兼容。

## 交付与本机安装

手动安装前按 [安装指南](INSTALLATION.md) 备份现有文件，关闭游戏并只保留一个宝箱入口。ASI 需要单独的公共 Loader，依赖版本与许可见 [ASI 与 Loader](ASI_LOADER.md)。不要把诊断探针或游戏派生数据放入玩家安装目录。

发行包只保留对应二进制、许可、玩家说明及校验资料。更新 `LICENSES.txt` 时保留项目和全部第三方许可；公共 Loader 继续保留自身 MIT 许可。构建和整理本机文件不会自动提交、推送或发布 GitHub Release。
