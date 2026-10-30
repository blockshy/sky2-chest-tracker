# Sky2 Chest Tracker

《空之轨迹 the 2nd》宝箱与探索辅助 Mod：追踪遗漏宝箱，查看本周目与继承记录，并通过全传送清单前往其他地点、返回出发位置。

当前版本为 **[0.6.2](https://github.com/blockshy/sky2-chest-tracker/releases/tag/v0.6.2)**，适配 **游戏 Ver.1.05**（EXE 文件版本 **1.5.0.0**，Steam build **26290169**）。验证结果见 [Ver.1.05 兼容性记录](https://github.com/blockshy/sky2-chest-tracker/blob/main/docs/GAME_1_5_0_COMPATIBILITY.md)。

**快速入口：[安装、更新与卸载指南](docs/INSTALLATION.md)**

| Mod 版本 | 对应游戏 | 状态 |
| --- | --- | --- |
| [0.6.2](https://github.com/blockshy/sky2-chest-tracker/releases/tag/v0.6.2) | Ver.1.05 · EXE 1.5.0.0 · build 26290169 | 当前版本 |
| [0.6.1](https://github.com/blockshy/sky2-chest-tracker/releases/tag/v0.6.1) | Ver.1.04 · EXE 1.4.0.0 · build 25721473 | 已发布，仅供该旧游戏构建 |
| [0.6.0](https://github.com/blockshy/sky2-chest-tracker/releases/tag/v0.6.0) | Ver.1.03.2 · EXE 1.3.2.0 · build 25386012 | 历史版本 |

游戏界面的 `Ver.1.05` 与 Windows 文件属性中的 `1.5.0.0` 是两种编号，指向这里核验的同一构建。Mod 按完整 EXE 指纹校验兼容性，旧安装包不能用于新补丁。

## 功能

- **宝箱追踪：**覆盖 59 个地图分组、566 个宝箱，显示标记、进度和遗漏清单。
- **两组记录：**分别查看当前存档的本周目与继承记录，不合并其他存档栏位。
- **探索辅助：**地图全显，按原生菜单规则补显当前地区的未到访传送点。
- **全传送与返程：**155 个原生地点、迷途之森入口及精确返程，连续传送保留首次出发位置与朝向。
- **输入与语言：**键盘、Xbox 手柄；界面和地点名称跟随游戏八种文字语言。

## 常用操作

| 操作 | 键盘 | Xbox 手柄 |
| --- | --- | --- |
| 本周目／继承记录 | F6 | View + X |
| 显示／隐藏面板 | F7 | View + B |
| 宝箱清单 | F8 | View + A |
| 暂停／恢复宝箱标记 | F9 | View + RS |
| 地图全显 | Ctrl + F6 | View + 十字键上 |
| 未到访传送点 | Ctrl + F8 | View + 十字键下 |
| 全传送清单 | Ctrl + F10 | View + 十字键左 |

View 是双窗口键，RS 是按下右摇杆。传送前先打开游戏区域地图，选中目的地后，用 `Ctrl + F7` 或 `View + 十字键右` 按下两次确认。完整键位见 [操作指南](docs/USAGE.md)，返程与历史记录见 [传送指南](docs/TRAVEL.md)。

全传送仍会执行游戏的剧情与自动保存，使用前保留独立存档。返程记录不绑定存档栏位，切档前先返程；升级游戏后保留旧记录，但不能将旧记录改名或修改指纹后用于新构建。

## 文档与反馈

- 玩家：[安装、更新与卸载](docs/INSTALLATION.md) · [操作](docs/USAGE.md) · [传送与返程](docs/TRAVEL.md) · [ASI 与 Loader](docs/ASI_LOADER.md)
- 开发：[构建与项目结构](https://github.com/blockshy/sky2-chest-tracker/blob/main/docs/BUILDING.md) · [实现说明](https://github.com/blockshy/sky2-chest-tracker/blob/main/docs/ARCHITECTURE.md) · [测试](https://github.com/blockshy/sky2-chest-tracker/blob/main/docs/TESTING.md)
- [更新记录](https://github.com/blockshy/sky2-chest-tracker/blob/main/CHANGELOG.md) · [贡献指南](https://github.com/blockshy/sky2-chest-tracker/blob/main/CONTRIBUTING.md) · [问题反馈](https://github.com/blockshy/sky2-chest-tracker/issues)

反馈请提供 Mod／游戏版本、分发类型、复现步骤及日志。独立版日志为 `Sky2ChestTracker/tracker.log`，ASI 版为 `plugins/Sky2ChestTracker/tracker.log`；有其他插件时请列出其名称。

项目采用 [PolyForm Noncommercial 1.0.0](https://github.com/blockshy/sky2-chest-tracker/blob/main/LICENSE)，仅限非商业使用；[第三方组件](https://github.com/blockshy/sky2-chest-tracker/blob/main/THIRD_PARTY_NOTICES.md)保留各自许可。公共 Ultimate ASI Loader 使用 MIT 许可。本项目为非官方社区 Mod，不分发游戏资源或用户存档。
