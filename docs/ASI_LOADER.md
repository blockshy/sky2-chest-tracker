# ASI 与公共 Loader

[项目首页](../README.md) · [手动安装、更新与卸载](INSTALLATION.md) · [操作指南](USAGE.md)

宝箱 ASI 与独立版功能相同，区别是由公共 Loader 加载 `plugins/Sky2ChestTracker.asi`。具体文件复制与卸载步骤统一见安装指南；本文只说明 Loader 来源、已有入口和数据处理。

## Loader 来源

使用 ThirteenAG 的 [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)。本项目核验的版本为 **9.7.4 x64**，以根目录 `xinput1_4.dll` 加载插件，公共包为 `Sky2ModLoader-UAL-9.7.4.zip`。

已核验 Loader DLL 的 SHA-256：

```text
031a3e5576d91dce1e438d36b9a3d462c7334ab4791990a8ff1e3ddc0e132daf
```

Loader 是独立的 MIT 开源项目；本 Mod 的非商业许可不改变它的许可。安装后保留对应 `UltimateASILoader.LICENSE.txt`。已有兼容 Loader 时不必随宝箱更新替换它。

## 已有 Loader 如何处理

| 当前情况 | 处理 |
| --- | --- |
| 根目录没有 `xinput1_4.dll` | 安装公共 Loader 后，再安装宝箱 ASI |
| 已有本项目核验的 UAL 9.7.4 x64 | 保留 Loader，只添加或更新宝箱 ASI |
| 已有宝箱独立版 DLL | 先移走独立版，再安装 Loader 和宝箱 ASI |
| 已有其他来源的 Loader 或同名 DLL | 先核对来源、版本与依赖；备份后按其自身说明处理，不直接覆盖 |

只保留一个宝箱入口，也检查 `plugins/`、`scripts/`、`update/` 中是否有旧副本。公共 Loader 解决入口加载问题，不能协调不同 Mod 的绘制、输入或相同函数挂钩。

## 旧数据整理

当前 ASI 数据目录为 `plugins/Sky2ChestTracker/`，独立版为根目录 `Sky2ChestTracker/`，两者不共享数据。

若此前 ASI 使用 `Sky2Mods/Sky2ChestTracker/`：

1. 退出游戏，将整个旧数据目录备份到游戏目录外。
2. 将需要保存的日志与返程历史复制到新 ASI 数据目录；遇到同名文件先比较，保留两份备份，不覆盖来源不明的数据。
3. 确认备份完整后，只清理能够确认属于旧 ASI 的文件，保留其他 Mod 内容。

移动文件不会改变兼容范围。Ver.1.05 只读取 `26290169` 构建的返程记录，旧 `25721473` 及无后缀记录继续保留；不要通过改名或改指纹让旧记录参与新游戏的返程。

## 故障排查与兼容范围

| 现象 | 检查 |
| --- | --- |
| 有 Loader 但没有面板 | 是否安装 `.asi`，是否按 `F7` 隐藏了面板，插件日志是否生成 |
| 游戏补丁后失效 | 游戏 build 是否与 Mod 相符，日志是否提示不支持的 EXE |
| 重复加载提示 | 独立版与 ASI 是否同时存在，其他加载目录是否还有宝箱副本 |
| 返程历史为空 | 当前游戏构建及分发的数据目录是否一致；旧构建记录不自动导入 |
| 加入其他 Mod 后异常 | 退出游戏，备份并移走其他 Mod，仅保留本插件复测 |

日志位置为 `plugins/Sky2ChestTracker/tracker.log`。当前构建的实测状态见 [Ver.1.05 兼容性记录](https://github.com/blockshy/sky2-chest-tracker/blob/main/docs/GAME_1_5_0_COMPATIBILITY.md)，隔离加载测试的能力边界见 [插件验证说明](https://github.com/blockshy/sky2-chest-tracker/blob/main/docs/PLUGIN_TESTING.md)。
