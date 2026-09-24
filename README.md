# Sky2 Chest Tracker

**《空之轨迹 the 2nd》宝箱与探索辅助 Mod**。在游戏地图中追踪遗漏宝箱，分别查看本周目与继承记录的收集进度，并通过全传送清单前往其他地点、返回原来的位置。

当前版本为 **0.6.0**，提供功能相同的独立版与 ASI 插件版。安装包与 SHA-256 校验文件见 [0.6.0 Release](https://github.com/blockshy/sky2-chest-tracker/releases/tag/v0.6.0)，请按下方说明选择所需分发。

## 目录

- [功能概览](#功能概览)
- [选择版本与 Loader](#选择版本与-loader)
- [快速开始](#快速开始)
- [常用操作](#常用操作)
- [更新与卸载](#更新与卸载)
- [兼容性与使用范围](#兼容性与使用范围)
- [文档导航](#文档导航)
- [反馈与贡献](#反馈与贡献)
- [许可](#许可)

## 功能概览

| 功能 | 说明 |
| --- | --- |
| 宝箱地图标记 | 显示对应区域的宝箱，包括尚未发现的宝箱；按当前显示模式区分已开／未开 |
| 两组收集记录 | 分别显示本周目与继承记录的全局、当前地区进度 |
| 各地图收集清单 | 统计 **59 个地图分组、566 个宝箱**，支持遗漏筛选、分页和完整地区名称 |
| 探索辅助 | 地图全显，以及按原生菜单规则补显当前地区的未到访传送点 |
| 全传送与精确返程 | 独立清单提供 **155 个原生地点、迷途之森入口与 1 项返程**；连续传送保留首次出发位置与朝向 |
| 键盘与手柄 | 支持 Xbox 组合键、输入提示热切换、重连、分辨率缩放和清单位置记忆 |
| 八种语言 | 界面与游戏地点名称自动跟随游戏全部八种文字语言；独立版与 ASI 版均支持，无需额外语言包 |

“继承记录”来自**当前读取的存档**，包含本周目新增记录，不会合并其他存档栏位。图标与计数的完整解释见 [操作指南](docs/USAGE.md)。

支持简体中文、繁體中文、日本語、English、Deutsch、Français、Español、한국어；切换游戏的**文字语言**即可，不跟随语音、Steam 客户端或 Windows 语言。地图、地区和传送地点名称取自对应语言的原生资源，详见 [语言与字体](docs/USAGE.md#语言与字体)。

English: The interface follows all eight game text languages: Simplified Chinese, Traditional Chinese, Japanese, English, German, French, Spanish and Korean. Location names come from the corresponding game resources.

日本語：ゲームのテキスト言語に合わせて、日本語・英語・簡体字中国語・繁体字中国語・ドイツ語・フランス語・スペイン語・韓国語に自動で切り替わります。地名は各言語のゲーム内データを使用します。

## 选择版本与 Loader

| 选择 | 安装包 | 适用情况 |
| --- | --- | --- |
| 独立版 | `Sky2ChestTracker-0.6.0-Standalone.zip` | 只用本 Mod，沿用根目录 `xinput1_4.dll` 的安装方式 |
| ASI 插件版 | `Sky2ChestTracker-0.6.0-ASI.zip` + `Sky2ModLoader-UAL-9.7.4.zip` | 由公共 Loader 加载 `plugins/Sky2ChestTracker.asi`，为多个 ASI 插件共存提供入口 |

**宝箱独立版与 ASI 版二选一，功能和快捷键相同。**公共 Loader 安装一次即可，更新或卸载宝箱 ASI 不需要同时替换或移除 Loader。

ASI 版使用 **ThirteenAG 的 [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)**，本项目核验并单独打包官方 **9.7.4 x64**，以 `xinput1_4.dll` 作为游戏入口。它是独立的 MIT 开源项目；本 Mod 的非商业许可不改变 Loader 的许可。来源、校验值、已有 Loader 的处理及兼容范围见 [ASI 与 Loader 指南](docs/ASI_LOADER.md)。

两版日志和返程记录独立：独立版使用 `Sky2ChestTracker/`，ASI 版使用 `plugins/Sky2ChestTracker/`。首次切换分发不导入旧记录；切换前先完成原版本的返程，或准备正常流程地点的存档。

## 快速开始

需要 Steam Windows 版游戏，安装预编译 Mod 无需 Python、Visual Studio 等开发工具。保存所需进度并正常退出游戏，找到包含 `sora_2nd.exe` 的游戏根目录。

1. 完整解压所选安装包。GitHub 的 **Source code** 压缩包只有源码，不能直接安装。
2. **独立版：**在独立版包根目录运行下方命令；或按 [手动安装](docs/INSTALLATION.md#手动安装) 将 `dist/` 内全部内容复制到游戏根目录。
3. **ASI 版：**先安装公共 Loader，再安装宝箱插件。分别在两个包内操作，完整脚本与手动步骤见 [ASI 安装指南](docs/ASI_LOADER.md)。已经装有其他来源 Loader 时先按该指南核对归属。
4. 从 Steam 启动游戏，左上角应显示对应语言的宝箱追踪面板和版本号。

独立版初次安装 **2 个文件**，ASI＋Loader 初次安装 **4 个文件**，两种方式都只在游戏根目录增加两项。游戏目录仅保留二进制、必要许可和运行时生成的数据；说明、脚本及校验资料留在下载包。

独立版安装命令（将路径替换为实际游戏目录）：

```powershell
.\Install-Mod.ps1 -GamePath '你的游戏安装目录'
```

**发现来源不明的同名 `xinput1_4.dll` 时不要覆盖。**已安装本 Mod 时按更新步骤操作；两个占用同名 DLL 的 Mod 不能直接共存。

## 常用操作

| 操作 | 键盘 | Xbox 手柄 |
| --- | --- | --- |
| 切换本周目／继承记录 | F6 | View + X |
| 显示／隐藏面板 | F7 | View + B |
| 打开／收起宝箱清单 | F8 | View + A |
| 暂停／恢复宝箱标记 | F9 | View + RS |
| 清单上一页／下一页 | PgUp / PgDn | View + LB / View + RB |
| 地图全显开／关 | Ctrl + F6 | View + 十字键上 |
| 未到访传送点开／关 | Ctrl + F8 | View + 十字键下 |
| 打开／收起全传送清单 | Ctrl + F10 | View + 十字键左 |

**View 是双窗口键，RS 是按下右摇杆。**先按住 View，再按功能键。隐藏面板不关闭地图标记；暂停宝箱标记不影响探索辅助。

传送时先打开游戏的区域地图，在清单中选择目的地，再用 `Ctrl + F7` 或 `View + 十字键右` **按下两次确认**。末页可返回首次出发点。逐项选择、历史记录与四塔位置见 [传送指南](docs/TRAVEL.md)；完整键位见 [操作指南](docs/USAGE.md)。

## 更新与卸载

所有操作均应先退出游戏，并使用对应分发的安装包。保留各自数据目录中的日志与返程记录，不要删除整个目录。旧 ASI 的 `Sky2Mods/Sky2ChestTracker/` 数据按 [迁移说明](docs/ASI_LOADER.md#安装前与旧版迁移) 移至新路径。

0.6.0 已加入八语支持并按精简结构重新打包，版本号不变；请重新下载当前附件和配套 `.sha256`，通过文件哈希区分旧包。

| 对象 | 更新 | 卸载 |
| --- | --- | --- |
| 宝箱独立版 | 运行独立版包的 `Install-Mod.ps1`；手动方式合并复制新版 `dist/` | 使用该包的 `Uninstall-Mod.ps1`，或核对归属后仅移走本 Mod 的 DLL |
| 宝箱 ASI | 运行 ASI 包的 `Install-Mod.ps1`；手动方式合并复制新版 `dist/` | 使用 ASI 包的 `Uninstall-Mod.ps1`，或核对归属后仅移走宝箱 `.asi`；保留 Loader 和其他插件 |
| 公共 Loader | 独立维护；不用随宝箱每次更新 | 所有 ASI 均移走后，使用 Loader 包自己的卸载脚本；其他来源的 Loader 由原安装方式维护 |

脚本按可信发行包的 SHA-256 识别文件，未知同名文件不覆盖、不删除；不依赖游戏目录中的安装收据。手动安装后仍可使用新版脚本更新或卸载。完整步骤及混用规则见 [独立版安装指南](docs/INSTALLATION.md) 和 [ASI 与 Loader 指南](docs/ASI_LOADER.md)。

## 兼容性与使用范围

| 项目 | 当前支持 |
| --- | --- |
| 游戏平台 | Steam、Windows x64 |
| 游戏构建 | Steam build **25386012**；[EXE 校验值](docs/INSTALLATION.md#兼容版本) |
| 渲染 | 原生 Direct3D 11 |
| 界面语言 | 简体中文、繁体中文、日文、英文、德文、法文、西班牙文、韩文，自动跟随游戏文字语言 |
| 手柄 | Xbox ABXY，已验证的 Steam Input 环境 |

游戏版本不匹配时不会启用挂钩。已验证宝箱 ASI 与只读诊断插件同时加载；公共 Loader 不负责协调不同 Mod 的界面、输入或同一函数挂钩。其他手柄、DXVK 和第三方 Mod 组合尚未全面验证。

全传送只在自由行动、游戏区域地图稳定浏览时使用。它允许越过剧情门槛，但原生剧情与自动保存仍会执行，前往不同进度的地点前请保留独立存档。

返程记录**不绑定存档栏位**。切档前先返程，或正常退出重启后再读档；核对记录的地点与时间后再返回。请保留 `revisit-return.dat` 和 `revisit-history/`。详细范围见 [传送指南](docs/TRAVEL.md#使用边界)。

## 文档导航

| 面向 | 文档 | 内容 |
| --- | --- | --- |
| 玩家 | [安装指南](docs/INSTALLATION.md) | 脚本／手动安装、更新、卸载、混用与 Mod 冲突 |
| 玩家 | [操作指南](docs/USAGE.md) | 宝箱记录、完整键位、探索辅助、手柄与常见问题 |
| 玩家 | [传送指南](docs/TRAVEL.md) | 前往与返程、历史记录、清单记忆、特殊地点 |
| 玩家 | [ASI 与 Loader 指南](docs/ASI_LOADER.md) | Loader 来源、双版本选择、迁移、手动安装、卸载与故障排查 |
| 开发者 | [构建指南](https://github.com/blockshy/sky2-chest-tracker/blob/main/docs/BUILDING.md) | 工具链、目录生成、编译、测试与打包 |
| 开发者 | [实现结构](https://github.com/blockshy/sky2-chest-tracker/blob/main/docs/ARCHITECTURE.md) | 数据语义、版本校验、挂钩、输入与返程 |
| 开发者 | [语言与原生名称](https://github.com/blockshy/sky2-chest-tracker/blob/main/docs/LOCALIZATION.md) | 文本语言检测、八语资源来源、名称身份与异常处理 |
| 开发者 | [测试说明](https://github.com/blockshy/sky2-chest-tracker/blob/main/docs/TESTING.md) | 自动验证、界面检查与实机回归范围 |
| 开发者 | [插件共存验证](https://github.com/blockshy/sky2-chest-tracker/blob/main/docs/PLUGIN_TESTING.md) | 隔离宿主、诊断探针、实机记录与验证边界 |

安装包内附带四份玩家指南，支持离线阅读；开发文档和 [更新记录](https://github.com/blockshy/sky2-chest-tracker/blob/main/CHANGELOG.md) 保留在仓库。

## 反馈与贡献

遇到问题请先查阅对应指南，在 [Issues](https://github.com/blockshy/sky2-chest-tracker/issues) 提供 Mod 版本、独立版／ASI 类型、游戏版本、复现步骤和完整提示。ASI 问题请补充 Loader 版本及其他插件名称；传送问题请补充目的地及是否切档或重启；语言或名称问题请补充游戏文字语言及对应原生界面截图。日志为独立版的 `Sky2ChestTracker/tracker.log` 或 ASI 版的 `plugins/Sky2ChestTracker/tracker.log`，不要公开完整存档、内存转储或个人信息。

提交代码前请阅读 [贡献指南](https://github.com/blockshy/sky2-chest-tracker/blob/main/CONTRIBUTING.md)。

## 许可

项目采用 [PolyForm Noncommercial 1.0.0](https://github.com/blockshy/sky2-chest-tracker/blob/main/LICENSE)，仅限非商业使用；第三方组件保留各自许可，见 [第三方声明](https://github.com/blockshy/sky2-chest-tracker/blob/main/THIRD_PARTY_NOTICES.md)。

这是非官方社区 Mod，与游戏开发商、发行商及 Valve 无隶属关系。游戏资源和用户存档不随项目分发。
