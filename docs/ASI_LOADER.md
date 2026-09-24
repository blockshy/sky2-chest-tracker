# ASI 插件与公共 Loader 使用指南

[项目首页](../README.md) · [独立版安装](INSTALLATION.md) · [操作说明](USAGE.md) · [全传送与精确返程](TRAVEL.md)

本文对应 **0.6.0**。ASI 版由公共 Loader 加载，安装后根目录只增加 `xinput1_4.dll` 与 `plugins/` 两项。下载见 [0.6.0 Release](https://github.com/blockshy/sky2-chest-tracker/releases/tag/v0.6.0)。

## 目录

- [选择安装包](#选择安装包)
- [Loader 来源与校验](#loader-来源与校验)
- [安装前与旧版迁移](#安装前与旧版迁移)
- [脚本安装更新与卸载](#脚本安装更新与卸载)
- [手动安装更新与卸载](#手动安装更新与卸载)
- [文件与返程记录](#文件与返程记录)
- [切回独立版](#切回独立版)
- [故障排查与兼容范围](#故障排查与兼容范围)

## 选择安装包

| 安装包 | 用途 | 需要搭配 |
| --- | --- | --- |
| `Sky2ChestTracker-0.6.0-Standalone.zip` | 独立版宝箱入口 | 无需 Loader，见 [独立版安装](INSTALLATION.md) |
| `Sky2ChestTracker-0.6.0-ASI.zip` | 宝箱 ASI 插件 | 公共 Loader |
| `Sky2ModLoader-UAL-9.7.4.zip` | 公共 Loader，本包没有宝箱功能 | ASI 插件 |

**宝箱独立版与 ASI 版二选一，功能和快捷键一致。**通常安装“ASI 包＋Loader 包”，不要同时放置两个宝箱入口，不要把独立版 DLL 改名成 ASI，也不要放置多个宝箱 ASI 副本。

公共 Loader 可以加载多个 ASI，但不负责协调插件的游戏挂钩、界面和输入，不保证任意组合兼容。

## Loader 来源与校验

**Ultimate ASI Loader（UAL）由 ThirteenAG 开发维护，是独立开源项目。**本项目原样分发官方 **v9.7.4、Windows x64、NoPDB** 二进制，只将上游 `dinput8.dll` 改名为本游戏采用的 `xinput1_4.dll`。[官方仓库](https://github.com/ThirteenAG/Ultimate-ASI-Loader) · [官方 v9.7.4 发布页](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/tag/v9.7.4)

UAL 采用 [MIT 许可证](https://github.com/ThirteenAG/Ultimate-ASI-Loader/blob/v9.7.4/license)，安装后原文位于 `plugins/Sky2ChestTracker/UltimateASILoader.LICENSE.txt`。这份文件由 **Loader** 管理，即使卸载宝箱 ASI，只要 Loader 还在就应保留；宝箱的非商业许可不改变 UAL 许可。

固定 SHA-256 如下。第一项是**上游官方 ZIP**；本项目重新打包的 ZIP 另附 `.sha256`：

```text
官方 Ultimate-ASI-Loader-NoPDB_x64.zip
e5860e7d9a1805267535b65749575b5e406cc6ea3325c7392189c578815045d1

官方 DLL（本游戏使用名 xinput1_4.dll）
031a3e5576d91dce1e438d36b9a3d462c7334ab4791990a8ff1e3ddc0e132daf
```

```powershell
Get-FileHash -LiteralPath '你的游戏安装目录\xinput1_4.dll' -Algorithm SHA256
```

下载地址、上游提交和校验值记录在 [loader-dependency.json](https://github.com/blockshy/sky2-chest-tracker/blob/main/loader-dependency.json)。未知版本或位数须另行验证，不以文件名作为身份依据。

## 安装前与旧版迁移

先核对 [支持的游戏构建](INSTALLATION.md#兼容版本)，保存进度并退出游戏。首次从独立版切换时，先完成原版本的返程，或准备正常流程地点的存档；ASI 不读取独立版的出发点和历史。

| 现有安装 | 处理方式 |
| --- | --- |
| 没有根目录代理 | 先安装 Loader，再安装宝箱 ASI |
| 已知发行版的独立版 DLL | 由 Loader 安装脚本核对已知 SHA 后切换（支持 0.5.0 与旧 0.6.0）；原独立版运行数据保留 |
| 已有固定官方 9.7.4 x64 UAL | 文件哈希匹配即可识别，无需本项目旧收据；补齐 Loader 许可后安装 ASI |
| 来源不明的同名 DLL | 停止，先核对其他 Mod 的安装方式 |
| 旧 0.6.0 ASI 数据在 `Sky2Mods/Sky2ChestTracker/` | 按下文迁移现有 ASI 数据后继续使用 |

**同为 0.6.0 的旧包与当前重新发布的包通过 SHA-256 区分。**重新下载当前附件和校验文件；新包的 `dist/` 只有实际安装文件，清单位于 `installer/manifest.json`。

旧 ASI 目录迁移规则：

- 更新时保留现有已知宝箱 ASI，直接运行新版安装脚本，不必先卸载。脚本识别到现有插件后，将旧 `Sky2Mods/Sky2ChestTracker/` 中的 `tracker.log`、`revisit-return.dat`、`revisit-history/*.dat` 迁移至 `plugins/Sky2ChestTracker/`，不导入独立版 `Sky2ChestTracker/`。
- 新旧同名数据一致时可以去重；内容不一致则停止，保留两份供核对，不覆盖返程记录。
- 手动更新时，在启动新版之前自行移动上述 ASI 数据，保持历史文件名和相对目录。新目录已有不同内容时先核对，不选择覆盖。
- 旧收据、拆分许可和文档仅按已知内容清理；未知或修改过的文件保留，空目录才删除。运行时不会自行扫描或合并旧目录。

## 脚本安装更新与卸载

### 安装

先在 **Loader 包根目录**打开 PowerShell：

```powershell
.\installer\Install-Loader.ps1 -GamePath '你的游戏安装目录'
```

再在 **ASI 包根目录**运行：

```powershell
.\Install-Mod.ps1 -GamePath '你的游戏安装目录'
```

安装共写入四个文件：Loader、宝箱 ASI、宝箱合并许可、Loader 许可。脚本和指南留在解压包内，不生成游戏目录收据。启动游戏后应只有一份宝箱面板。

### 更新

在新版完整 **ASI 包**中运行同一安装命令。脚本按实际 SHA-256 识别旧二进制，备份被替换的已知文件至解压包 `backups/` 后更新，保留插件数据和公共 Loader。只有更新脚本而不更新 `.asi` 不会更新功能。

Loader 单独维护，只接受已核验的官方二进制，不用随宝箱每次更新。来源相同且字节一致的手动安装不需要补造收据。

### 卸载

在 **ASI 包根目录**运行：

```powershell
.\Uninstall-Mod.ps1 -GamePath '你的游戏安装目录'
```

仅删除可核对属于宝箱的 ASI 与合并许可，保留公共 Loader、Loader 许可、其他插件、日志和返程数据。即使宝箱已卸载，`plugins/Sky2ChestTracker/` 也可能因 Loader 许可或数据而保留。

确认全部 ASI 已移出且不再需要 Loader 后，在 **Loader 包根目录**单独运行：

```powershell
.\installer\Uninstall-Loader.ps1 -GamePath '你的游戏安装目录'
```

脚本核对依赖、二进制与许可归属，仍有 ASI 或无法安全检查目录时停止。所有命令均支持追加 `-WhatIf` 预演，不要并行运行多个安装器。

## 手动安装更新与卸载

手动方式与脚本方式安装后的结构一致，无需维护任何 JSON 收据。

| 操作 | 手动步骤 |
| --- | --- |
| 安装 Loader | 核对无冲突后，将 Loader 包 `dist/` 的全部内容复制到游戏根目录 |
| 安装宝箱 ASI | 将 ASI 包 `dist/` 的全部内容合并复制到游戏根目录，保留其他插件 |
| 更新 ASI | 核对旧文件 SHA，按前述规则先迁移旧 ASI 数据，再合并复制新版 `dist/` |
| 卸载 ASI | 核对归属后移走 `plugins/Sky2ChestTracker.asi` 与宝箱 `LICENSES.txt`；保留 Loader 许可和数据 |
| 卸载 Loader | 所有插件不再依赖它后，核对并移走根目录 Loader DLL 及对应 `UltimateASILoader.LICENSE.txt` |

可用 `Get-FileHash -Algorithm SHA256` 将实际二进制与可信包内 `installer/manifest.json` 或 `installer/known-files.json` 的相应产品、类型、路径条目比较。旧包的清单可能在 `dist/manifest.json`。未知同名文件不覆盖、不删除。

完整手动安装后可使用脚本更新或卸载；使用认识当前构建的新包即可。旧收据不能认领未知文件。不要把 `dist` 本身复制成额外目录，也不要删除整个 `plugins` 或插件数据目录。

## 文件与返程记录

安装后结构如下，日志和返程数据按需生成：

```text
游戏目录/
├─ xinput1_4.dll                      公共 Loader
└─ plugins/
   ├─ Sky2ChestTracker.asi            宝箱插件
   └─ Sky2ChestTracker/
      ├─ LICENSES.txt                宝箱完整合并许可
      ├─ UltimateASILoader.LICENSE.txt
      │                             Loader 完整许可
      ├─ tracker.log                 运行日志
      ├─ revisit-return.dat          当前返程记录
      └─ revisit-history/            返程历史
         └─ <记录编号>.dat
```

初次安装四个文件；运行日志使用单文件并限制大小，返程历史按出发记录生成。文档、脚本和安装校验资料均不进入游戏目录。独立版仍使用根目录 `Sky2ChestTracker/`，两版不共享数据；旧 ASI 数据只是同一分发的路径迁移。

返程记录不绑定存档栏位，也不能回滚游戏进度。切档、重启后须核对地点和时间，详见 [传送指南](TRAVEL.md#返程记录与重启)。

## 切回独立版

1. 在 ASI 版完成当前返程并保存，或准备正常流程地点的存档，然后退出游戏。
2. 卸载宝箱 ASI，保留两版运行数据。
3. 先移走其他 ASI，确认其他 Mod 不再依赖 Loader 后将其卸载。仍需其他插件时，继续使用宝箱 ASI。
4. 按 [独立版安装](INSTALLATION.md) 安装 Standalone 包，保留唯一宝箱入口。

独立版重新使用原 `Sky2ChestTracker/`，不读取插件历史；保留旧历史不等于与当前存档相符，返程前仍要核对。

## 故障排查与兼容范围

| 情况 | 先检查 |
| --- | --- |
| 只有 Loader，没有面板 | 是否安装 `.asi`，再按 F7／View + B 检查显隐 |
| 脚本提示未知哈希 | 核对文件来源，并使用对应新版安装包；不要篡改校验清单 |
| 更新后看不到返程历史 | 检查是否按迁移步骤移动旧 ASI 数据；独立版历史不会导入 |
| 数据迁移提示冲突 | 保留两边数据，核对各自地点和时间，不覆盖 |
| 重复加载提示 | 检查 `plugins`、`scripts`、`update` 等目录，只保留一个宝箱入口 |
| Loader 无法卸载 | 仍有 ASI、链接目录或无法确认内容，先核对依赖 |
| 加入其他插件后异常 | 分别单独运行定位，反馈版本及相关日志 |

本机 Steam Input 与 Xbox ABXY 环境已验证宝箱与只读诊断探针共存、输入热切换和重连、一次传送返程；不代表任意两个界面或输入 Mod 兼容。探针不进入玩家包，具体范围见 [插件共存验证](https://github.com/blockshy/sky2-chest-tracker/blob/main/docs/PLUGIN_TESTING.md)。

反馈时附分发、版本、游戏构建、步骤及 `plugins/Sky2ChestTracker/tracker.log` 的相关片段，无需公开整份存档或返程文件。
