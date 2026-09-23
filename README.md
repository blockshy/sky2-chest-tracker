# Sky2 Chest Tracker

**《空之轨迹 the 2nd》宝箱与探索辅助 Mod**。在游戏地图中追踪遗漏宝箱，分别查看本周目与继承记录的收集进度，并通过全传送清单前往其他地点、返回原来的位置。

当前正式版为 **0.5.0**。下载安装包请前往 [GitHub Releases](https://github.com/blockshy/sky2-chest-tracker/releases/latest)；旧版安装包不包含本文全部新增功能。

## 目录

- [功能概览](#功能概览)
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

“继承记录”来自**当前读取的存档**，包含本周目新增记录，不会合并其他存档栏位。图标与计数的完整解释见 [操作指南](docs/USAGE.md)。

## 快速开始

需要 Steam Windows 版游戏，安装预编译 Mod 无需 Python、Visual Studio 等开发工具。详细版本校验、手动安装和冲突处理见 [安装指南](docs/INSTALLATION.md)。

1. 获取并完整解压 `Sky2ChestTracker-<版本号>.zip`。GitHub 的 **Source code** 压缩包是源码，不能直接安装。
2. 保存进度并正常退出游戏，找到包含 `sora_2nd.exe` 的游戏根目录。
3. 选择下列一种安装方式，然后从 Steam 启动游戏；左上角应显示“宝箱追踪”和对应版本号。

**脚本安装：**在解压目录打开 PowerShell，替换路径后运行：

```powershell
.\Install-Mod.ps1 -GamePath '你的游戏安装目录'
```

**手动安装：**将安装包 `dist` 内的 **`xinput1_4.dll` 与 `Sky2ChestTracker` 文件夹一起**复制到游戏根目录，保留配套的 `install.json`。具体结构见 [手动安装](docs/INSTALLATION.md#手动安装)。

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

所有操作均应先退出游戏。更新时保留 `Sky2ChestTracker` 中的日志与返程记录，不要删除整个目录。

| 方式 | 更新 | 卸载 |
| --- | --- | --- |
| 脚本 | 解压新版安装包，运行新版 `Install-Mod.ps1` | 运行 `Uninstall-Mod.ps1`，使用相同的 `-GamePath` 参数 |
| 手动 | 核对归属后，同时更新 DLL 与配套 `install.json` | 核对归属后，只移走本 Mod 的 DLL |

脚本会检查产品记录与实际 DLL 哈希；缺少记录、哈希不一致或文件被其他 Mod 替换时停止操作。手动操作需要自行核对。完整步骤及混用规则见 [安装指南](docs/INSTALLATION.md)。

## 兼容性与使用范围

| 项目 | 当前支持 |
| --- | --- |
| 游戏平台 | Steam、Windows x64 |
| 游戏构建 | Steam build **25386012**；[EXE 校验值](docs/INSTALLATION.md#兼容版本) |
| 渲染与语言 | 原生 Direct3D 11、简体中文面板 |
| 手柄 | Xbox ABXY，已验证的 Steam Input 环境 |

游戏版本不匹配时不会启用挂钩。其他手柄、DXVK 和第三方 Mod 组合尚未全面验证；同名 DLL 冲突处理见安装指南。

全传送只在自由行动、游戏区域地图稳定浏览时使用。它允许越过剧情门槛，但原生剧情与自动保存仍会执行，前往不同进度的地点前请保留独立存档。

返程记录**不绑定存档栏位**。切档前先返程，或正常退出重启后再读档；核对记录的地点与时间后再返回。请保留 `revisit-return.dat` 和 `revisit-history/`。详细范围见 [传送指南](docs/TRAVEL.md#使用边界)。

## 文档导航

| 面向 | 文档 | 内容 |
| --- | --- | --- |
| 玩家 | [安装指南](docs/INSTALLATION.md) | 脚本／手动安装、更新、卸载、混用与 Mod 冲突 |
| 玩家 | [操作指南](docs/USAGE.md) | 宝箱记录、完整键位、探索辅助、手柄与常见问题 |
| 玩家 | [传送指南](docs/TRAVEL.md) | 前往与返程、历史记录、清单记忆、特殊地点 |
| 开发者 | [构建指南](https://github.com/blockshy/sky2-chest-tracker/blob/main/docs/BUILDING.md) | 工具链、目录生成、编译、测试与打包 |
| 开发者 | [实现结构](https://github.com/blockshy/sky2-chest-tracker/blob/main/docs/ARCHITECTURE.md) | 数据语义、版本校验、挂钩、输入与返程 |
| 开发者 | [测试说明](https://github.com/blockshy/sky2-chest-tracker/blob/main/docs/TESTING.md) | 自动验证、界面检查与实机回归范围 |

安装包内附带三份玩家指南，支持离线阅读；开发文档和 [更新记录](https://github.com/blockshy/sky2-chest-tracker/blob/main/CHANGELOG.md) 保留在仓库。

## 反馈与贡献

遇到问题请先查阅对应指南，在 [Issues](https://github.com/blockshy/sky2-chest-tracker/issues) 提供 Mod 版本、游戏版本、复现步骤和完整提示。传送问题请补充目的地及是否切档或重启。日志位于游戏目录的 `Sky2ChestTracker/tracker.log`，不要公开完整存档、内存转储或个人信息。

提交代码前请阅读 [贡献指南](https://github.com/blockshy/sky2-chest-tracker/blob/main/CONTRIBUTING.md)。

## 许可

项目采用 [PolyForm Noncommercial 1.0.0](https://github.com/blockshy/sky2-chest-tracker/blob/main/LICENSE)，仅限非商业使用；第三方组件保留各自许可，见 [第三方声明](https://github.com/blockshy/sky2-chest-tracker/blob/main/THIRD_PARTY_NOTICES.md)。

这是非官方社区 Mod，与游戏开发商、发行商及 Valve 无隶属关系。游戏资源和用户存档不随项目分发。
