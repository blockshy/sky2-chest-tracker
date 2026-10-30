# 手动安装、更新与卸载

[项目首页](../README.md) · [操作指南](USAGE.md) · [传送与返程](TRAVEL.md) · [ASI 与 Loader](ASI_LOADER.md)

本文对应 **[0.6.2 Release](https://github.com/blockshy/sky2-chest-tracker/releases/tag/v0.6.2)**，目标游戏为 **Ver.1.05**（EXE **1.5.0.0**，Steam build **26290169**）。0.6.1 附件仅支持 Ver.1.04，不能用于本次补丁。请下载 Release 中的预编译安装包，GitHub 的 **Source code** 包不能直接安装。详细验证状态见 [兼容性记录](https://github.com/blockshy/sky2-chest-tracker/blob/main/docs/GAME_1_5_0_COMPATIBILITY.md)。

## 开始前

1. 保存进度并完全退出游戏。使用全传送时，先完成旧版本的返程，或准备正常流程地点的存档。
2. 在 Steam 中打开游戏的本地文件，确认该目录包含 `sora_2nd.exe`。支持 Steam Windows x64 与原生 Direct3D 11。
3. 核对游戏标题界面的版本及 EXE 属性中的文件版本，选择相匹配的 Mod 安装包。安装现成文件无需开发工具或额外语言包。
4. 完整解压安装包。确认压缩包校验值与对应 `.sha256` 一致；可以在支持 SHA-256 的文件校验工具中比较。保留原安装包用于确认旧文件归属。
5. 若游戏目录已有 `xinput1_4.dll`，先确认属于本 Mod 还是公共 Loader／其他 Mod，备份到游戏目录外。未知来源文件不要覆盖。

两种分发功能和快捷键相同，**二选一**。Standalone 适合只安装本 Mod；ASI 依赖公共 Loader，为其他 ASI 插件提供共同入口，但不保证所有插件组合兼容。

## 安装独立版

将 Standalone 包内 **`dist/` 的全部内容**复制到游戏根目录，不要再套一层 `dist`。初次安装只有两个文件：

```text
游戏目录/
├─ sora_2nd.exe
├─ xinput1_4.dll                  宝箱 Mod 独立版
└─ Sky2ChestTracker/
   └─ LICENSES.txt               项目与第三方许可
```

从 Steam 启动游戏，左上角应显示宝箱追踪面板和 `0.6.2`；按 `F7` 可切换显隐。不要同时保留宝箱 `.asi`。

## 安装 ASI 版

1. 先按 [Loader 核对说明](ASI_LOADER.md#已有-loader-如何处理) 处理已有入口。无 Loader 时，将公共 Loader 包内 `dist/` 的内容复制到游戏根目录。
2. 将宝箱 ASI 包内 `dist/` 的内容合并复制到同一游戏根目录，保留其他文件。
3. 确认结构如下，再从 Steam 启动游戏。初次安装宝箱和 Loader 共四个文件：

```text
游戏目录/
├─ sora_2nd.exe
├─ xinput1_4.dll                       Ultimate ASI Loader
└─ plugins/
   ├─ Sky2ChestTracker.asi             宝箱 Mod
   └─ Sky2ChestTracker/
      ├─ LICENSES.txt                  宝箱及第三方许可
      └─ UltimateASILoader.LICENSE.txt  Loader 许可
```

Loader 只需安装一次，不随每次宝箱更新重复替换。游戏目录无需复制包内说明和校验清单；日志及返程记录由 Mod 按需生成。

## 手动更新

1. 退出游戏，核对旧 DLL／ASI 确实属于本 Mod。需要比对时，使用 SHA-256 校验工具与可信旧包或当前包的 `installer/manifest.json`、`installer/known-files.json` 对应条目比较。
2. 将旧二进制和对应数据目录备份到游戏目录外。
3. 合并复制新版包 `dist/` 内的文件，替换已确认属于本 Mod 的二进制和 `LICENSES.txt`。**不要先删除整个数据目录。**
4. 启动游戏核对面板版本，并确认日志没有 `Unsupported executable`、签名失败或重复加载提示。

更新宝箱 ASI 时保留公共 Loader、Loader 许可及其他插件。游戏补丁改变了 EXE 时，应使用专门适配的构建；仅复制旧 Mod 不会恢复兼容性。

## 手动卸载

| 对象 | 退出游戏后移走 | 保留 |
| --- | --- | --- |
| 独立版宝箱 | 已确认归属的根目录 `xinput1_4.dll`、`Sky2ChestTracker/LICENSES.txt` | 日志、返程记录及未知文件 |
| 宝箱 ASI | `plugins/Sky2ChestTracker.asi`、其数据目录中的 `LICENSES.txt` | 公共 Loader、Loader 许可、其他插件及返程记录 |
| 公共 Loader | 所有 ASI 均不再依赖它后，移走对应 DLL 与 `UltimateASILoader.LICENSE.txt` | 宝箱历史及其他产品的数据 |

不要删除整个 `plugins/` 或 `Sky2ChestTracker/`。卸载不会撤销已经写入游戏存档的剧情、道具或开箱变化。

## 切换分发

先完成返程并保存，退出游戏后移走旧宝箱入口，再安装另一分发。两个需要根目录 `xinput1_4.dll` 的入口不能并存，也不能通过随意改名建立加载关系。

从 ASI 切回独立版时，必须先确认其他 ASI 不再依赖公共 Loader；仍需这些插件时继续使用宝箱 ASI。两种分发的数据各自保留，不互相导入。

## 数据与旧版本

| 分发 | 日志与返程数据目录 |
| --- | --- |
| 独立版 | `Sky2ChestTracker/` |
| ASI | `plugins/Sky2ChestTracker/` |

Ver.1.05 使用 `revisit-return-26290169.dat` 和 `revisit-history-26290169/`。Ver.1.04 的 `25721473` 文件及更早无构建后缀的记录应原样保留；新版本不读取旧构建的返程记录。不要改名或修改记录指纹。

旧 ASI 数据若位于 `Sky2Mods/Sky2ChestTracker/`，按 [旧数据整理](ASI_LOADER.md#旧数据整理) 备份处理。返程记录不绑定存档栏位，也不是完整游戏存档，详见 [传送指南](TRAVEL.md#返程记录与重启)。
