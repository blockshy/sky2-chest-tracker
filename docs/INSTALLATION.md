# 安装、更新与卸载

[项目首页](../README.md) · [操作说明](USAGE.md) · [全传送与精确返程](TRAVEL.md)

本文对应 **0.6.0**。安装包与 SHA-256 校验文件见 [0.6.0 Release](https://github.com/blockshy/sky2-chest-tracker/releases/tag/v0.6.0)。可以使用 PowerShell 脚本，也可以手动复制，无需 Python 或开发工具。

## 目录

- [选择分发方式](#选择分发方式)
- [兼容版本](#兼容版本)
- [开始前](#开始前)
- [脚本安装、更新与卸载](#脚本安装更新与卸载)
- [手动安装、更新与卸载](#手动安装更新与卸载)
- [手动与脚本混用](#手动与脚本混用)
- [与其他 Mod 共存](#与其他-mod-共存)
- [保留哪些文件](#保留哪些文件)

## 选择分发方式

| 分发 | 安装入口 | 数据目录 |
| --- | --- | --- |
| Standalone 独立版 | 根目录 `xinput1_4.dll`，无需额外 Loader | `Sky2ChestTracker/` |
| ASI 插件版 | 公共 Loader 加载 `plugins/Sky2ChestTracker.asi` | `plugins/Sky2ChestTracker/` |

两种宝箱分发功能一致，**二选一安装**。ASI 所需的 Ultimate ASI Loader 9.7.4 x64 单独打包；宝箱卸载不移除公共 Loader。ASI 安装与旧数据迁移见 [ASI 与 Loader 指南](ASI_LOADER.md)，以下以独立版为例。

两版日志和返程记录互不导入。切换分发前先用原版本完成返程并保存，或准备正常流程地点的存档。

## 兼容版本

| 项目 | 当前支持 |
| --- | --- |
| 游戏 | Trails in the Sky 2nd Chapter / 空之轨迹 the 2nd |
| 平台 | Steam、Windows x64 |
| Steam build | 25386012 |
| 渲染路径 | 原生 Direct3D 11 |
| 面板语言 | 简体中文、繁体中文、日文、英文、德文、法文、西班牙文、韩文，自动跟随游戏文字语言 |
| 手柄提示 | Xbox ABXY，支持已实测的 Steam Input 环境 |

支持的 `sora_2nd.exe` SHA-256：

```text
d8b2911d1576216bdc22d070550e4f531e105de7ed2981885849669f4acf8aaf
```

八种语言的文本已包含在独立版 DLL 与 ASI 插件中，不需下载语言包或往游戏目录增加翻译文件。只跟随游戏的**文字语言**，不跟随语音、Steam 客户端或 Windows 语言；八种有效文字语言分别使用各自的界面与原生地点名称。

显示中日韩文字需要本机具备相应 Windows 字体。若出现方框、缺字或字体缺失提示，请安装 Windows 对应的简体中文／繁体中文／日文／韩文补充字体后重启游戏；本项目不捆绑游戏或系统字体。完整说明见 [语言与字体](USAGE.md#语言与字体)。

安装器和运行时都会检查游戏版本；不匹配时不启用游戏挂钩。手动安装前可使用以下只读命令核对，比较哈希时忽略字母大小写：

```powershell
Get-FileHash -LiteralPath '你的游戏安装目录\sora_2nd.exe' -Algorithm SHA256
```

## 开始前

1. 完整解压 `Sky2ChestTracker-0.6.0-Standalone.zip`。GitHub 的 **Source code** 包只有源码，不能直接安装。
2. 保存所需进度并正常退出游戏，找到包含 `sora_2nd.exe` 的游戏根目录。
3. 已有 `xinput1_4.dll` 时先核对来源；未知同名文件不覆盖、不删除。

**`dist/` 中全部内容就是需要复制的游戏文件。**文档、脚本和校验资料位于包内其他位置，不复制到游戏，也不生成安装收据 JSON。

本次八语构建重新发布仍使用版本 **0.6.0**，不带修订号。请重新下载当前 Release 附件及对应 `.sha256`，以包内 `installer/manifest.json` 的文件哈希核对构建；同为 0.6.0 的旧附件不能仅凭文件名或面板版本区分。当前包的 `dist/` 不含 `manifest.json` 或 `install.json`，八语支持不改变精简安装结构。

## 脚本安装、更新与卸载

### 脚本安装

在完整解压的独立版包根目录打开 PowerShell：

```powershell
.\Install-Mod.ps1 -GamePath '你的游戏安装目录'
```

脚本检查游戏版本、载荷哈希和目标文件归属；从 Steam 启动后核对宝箱面板。首次安装仅写入 **DLL 与 `Sky2ChestTracker/LICENSES.txt` 两个文件**。

### 脚本更新

退出游戏，在新版完整安装包内运行同一安装命令。脚本使用包内当前清单和历史 SHA-256 白名单识别旧文件，备份被替换的已知文件至解压包的 `backups/` 后更新，保留日志及返程数据。

旧版收据、拆分许可证和说明文件仅在路径、产品与已知内容均可核对时清理；修改过或未知文件保留，目录仅在为空时删除。不凭旧收据中的哈希认领未知 DLL，不递归删除 Mod 目录。只替换脚本不会更新 DLL。

### 脚本卸载

```powershell
.\Uninstall-Mod.ps1 -GamePath '你的游戏安装目录'
```

只删除确认属于本产品的 DLL 和对应许可；保留存档、日志、返程数据及未知文件。DLL 已被其他 Mod 替换时停止。

### 先预演，不改文件

安装与卸载均支持 `-WhatIf`：

```powershell
.\Install-Mod.ps1 -GamePath '你的游戏安装目录' -WhatIf
.\Uninstall-Mod.ps1 -GamePath '你的游戏安装目录' -WhatIf
```

预演检查通过不等于游戏内兼容测试。不要同时运行多个安装器或文件替换工具。脚本被执行策略阻止时，可按设备管理规则处理或选择手动安装；本项目不自动修改系统执行策略。

## 手动安装、更新与卸载

### 手动安装

核对游戏版本并退出游戏后，将 **`dist/` 内的全部内容**复制到游戏根目录，不要额外套一层 `dist`：

```text
游戏目录/
├─ xinput1_4.dll                  独立版宝箱 Mod
└─ Sky2ChestTracker/
   └─ LICENSES.txt                项目与第三方完整许可
```

从 Steam 启动游戏。日志和返程数据运行时按需生成，不需要提前创建，不需要复制安装记录。

### 更新或卸载前：核对 DLL 归属

```powershell
Get-FileHash -LiteralPath '你的游戏安装目录\xinput1_4.dll' -Algorithm SHA256
```

将结果与对应版本安装包的 `installer/manifest.json` 中 `sha256`，或当前包 `installer/known-files.json` 中类型为 `standalone-proxy` 的已知条目比较。旧安装包的清单可能位于 `dist/manifest.json`。只使用可信发行包提供的哈希，不凭文件名、版本文本或游戏中的旧收据认领文件。

### 手动更新

退出游戏并核对归属后，可将旧 DLL 另存到游戏目录外，再合并复制新版 `dist/` 的全部内容，更新 DLL 和 `LICENSES.txt`。保留日志、返程记录及其他文件，不先删除整个数据目录。需要回退时使用保留的旧发行包；恢复旧目录规则前确认其运行数据路径。

如需同时清理旧说明、旧许可证或收据，推荐先用新版脚本的 `-WhatIf` 查看；手动方式仅删除能逐项确认属于本 Mod 的旧文件，未知内容保留。

### 手动卸载

退出游戏，核对 DLL 归属后移走 `xinput1_4.dll`；对应的 `Sky2ChestTracker/LICENSES.txt` 可一并移走。保留运行数据及未知文件，不删除整个 `Sky2ChestTracker` 目录。

## 手动与脚本混用

| 操作顺序或情况 | 结果 |
| --- | --- |
| 手动复制当前 `dist/` → 脚本卸载或更新 | 支持；按实际文件 SHA-256 识别，不依赖收据 |
| 脚本安装 → 手动更新 → 脚本卸载 | 使用认识该构建的新版脚本即可 |
| 只有已知 DLL，缺少许可或旧收据 | DLL 身份仍可识别；安装可补齐缺失的配套许可 |
| DLL 被替换为未知文件，即使留有本 Mod 收据 | 停止覆盖或删除 |
| 使用不认识新构建的旧脚本 | 可能拒绝；改用与当前构建配套的新包 |
| 目录内包含用户文件 | 保留；不因目录名相同而全部清理 |

`installer/manifest.json` 和 `known-files.json` 只留在解压包中，不能通过手改这些文件强行认领其他 Mod。

## 与其他 Mod 共存

**两个需要根目录 `xinput1_4.dll` 的 Mod 不能直接共存。**独立版只转发 Windows 系统 XInput，没有第二份 Mod 的串联功能；改名不会自动建立加载关系。需要多个 ASI 时使用插件版与公共 Loader，详见 [ASI 指南](ASI_LOADER.md)。

文件及父目录为链接、重解析点，或实际文件哈希未知时，脚本停止处理。即使文件名不同，多个 Mod 仍可能竞争游戏挂钩、界面或按键，需分别验证。文件检查不能替代任意并发修改下的事务保证，不要并行操作安装目录。

## 保留哪些文件

| 文件 | 用途 |
| --- | --- |
| `Sky2ChestTracker/LICENSES.txt` | 合并后的完整项目及第三方许可，安装时提供 |
| `Sky2ChestTracker/tracker.log` | 单个诊断日志，运行时生成并限制大小 |
| `Sky2ChestTracker/revisit-return.dat` | 最近的出发点，使用传送后生成 |
| `Sky2ChestTracker/revisit-history/` | 历次返程记录，更新、卸载和转移游戏时保留 |

ASI 的对应数据位于 `plugins/Sky2ChestTracker/`。返程文件不绑定存档栏位，也不保存整份进度，详见 [传送指南](TRAVEL.md)。卸载 Mod 不会撤销已经保存的剧情、道具或开箱变化。
