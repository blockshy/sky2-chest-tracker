# Sky2 Mod Hub 模块版

模块版是新增的第三种分发，对应 [Sky2 Mod Hub 0.5.0](https://github.com/blockshy/sky2-mod-hub/releases/tag/v0.5.0)，模块标识为 `chest`，模块版本为 `0.6.0-hub.1`。**原 ASI 和 Standalone 永久保留**，仍能独立构建、打包和安装，不要求安装 Hub。三个版本共用宝箱、探索和传送业务，界面与输入入口各自独立。

## 选择和安装

- Standalone：原 `xinput1_4.dll`，原快捷键与只读面板不变。
- ASI：由 UAL 加载 `plugins/Sky2ChestTracker.asi`，原快捷键与只读面板不变。
- HubModule：由 Sky2 Mod Hub 加载统一模块，使用控制中心页面、常用动作和宿主快捷键设置。

同一进程中宝箱三种入口只能启用一种。切换前退出游戏并保存原文件；不要同时放置旧宝箱 ASI 和启用的模块清单，不要把 Standalone 改名放进模块目录。Hub 与 UAL 由宿主安装工具管理，宝箱模块包不覆盖它们。

玩家可从 [Hub 0.5.0 Release](https://github.com/blockshy/sky2-mod-hub/releases/tag/v0.5.0) 下载 `Sky2ModHub-0.5.0-Windows-x64.zip` 及对应 `.sha256`。整合包包含宿主、宝箱、高亮与队伍模块，不含公共 Loader、原 ASI 或 Standalone；按 [宿主部署与恢复指南](https://github.com/blockshy/sky2-mod-hub/blob/main/docs/DEPLOYMENT.md) 校验、安装或从旧入口迁移。本仓库的原 0.6.0 Release 继续提供 ASI / Standalone，不新增同名模块 Release。

模块包 `dist/` 只含三项载荷：

```text
plugins/Sky2ModHub/modules/Sky2ChestTracker.module.dll
plugins/Sky2ModHub/modules/Sky2ChestTracker.module.ini
plugins/Sky2ModHub/modules/Sky2ChestTracker.LICENSES.txt
```

清单使用 `Id=chest`、`Binary=Sky2ChestTracker.module.dll`、`Abi=1`、`Enabled=1`。宿主按显式清单发现模块，并保存用户的启用偏好。启动时禁用但已被发现的模块可在“模块与诊断”中首次启用，无需重启；已加载模块也可实时启停业务。新增、移除或替换模块文件须退出游戏；DLL 和必要挂钩始终驻留，不支持运行中替换或卸载 DLL。

## 使用方式

在 Hub 控制中心打开“宝箱追踪”页面：

- **概览**：用卡片与进度条分别显示本周目、继承记录和当前地区统计，提供清单与设置入口。
- **清单**：选择统计口径、筛选遗漏、搜索地点并分页查看紧凑的地点计数。地图统计规则放在可展开的使用说明中。
- **设置**：地图标记、精简 HUD、显示口径与探索开关分组展示；等待原生菜单刷新时明确提示“等待开启/关闭”。
- **传送**：目的地目录与行程确认分卡展示，保留搜索、分页、可用条件和提交状态。先“准备行程”，再“再次确认”，仍是两次独立激活。
- **返程**：独立查看出发地点、场景与创建时间；非活动行程时可以切换已有历史候选。仍须两次确认，记录不绑定存档栏位。
- **宝箱简报**：可选左下角精简 HUD，控制中心打开时隐藏；显示开关不改变地图标记或探索功能。

鼠标、键盘和手柄导航均由 Hub 处理：

| 操作 | 键盘 | Xbox 手柄 |
| --- | --- | --- |
| 打开／关闭中心 | F11 | 先按住 View，再按下 LS |
| 切换左侧页面 | PageUp / PageDown | LB / RB |
| 切换顶部页签 | Ctrl + PageUp / PageDown | LT / RT |
| 移动黄色焦点 | 方向键 | 十字键／左摇杆 |
| 确认／编辑 | Enter | A |
| 退出编辑；未编辑时关闭中心 | Esc | B |
| 滚动当前内容 | 鼠标滚轮／滚动条 | 倾斜右摇杆，无需按下 |

黄色焦点只进入功能内容及可滚动的只读框。列表选中项需确认，方向移动本身不会提交操作；只读框到边缘后可继续移到下一个功能。模块不硬编码原 F6–F10 或 View 组合；宝箱动作的默认键盘组合是 Ctrl+F1～F8，可在宿主中调整或清空。默认手柄组合、冲突规则及恢复默认方式见 [公共快捷键说明](https://github.com/blockshy/sky2-mod-hub/blob/main/docs/DEFAULT_BINDINGS.md)。全局传送动作只打开页面，不能直接换图。

Hub 0.5.0 将页面标题、简介、顶部五项页签和底部提示固定，只有 Main 内容区滚动。宝箱清单、传送目录依据 Main 的可视高度分配空间，宽度不足时卡片按单列排列；单列传送目录限制高度，为下方的行程条件与确认留出空间。长说明可以折叠，操作条件和最终确认不放在折叠内容中。

运行于缺少固定页头扩展的 ABI v1 宿主时，页签回退到内容区，列表保留旧高度；缺少卡片等布局扩展时回退为线性控件。旧宿主不能提供本身未实现的固定页头或实时启停界面，完整体验以 Hub 0.5.0 为准。

切出游戏时可以继续看到已打开的页面，控件变为只读；待确认操作仍会撤销。返回游戏后需要重新完成两次确认。

**传送仍要求游戏原生区域地图已经打开并处于稳定浏览状态。** 控制中心不会替代原生地图、暂停游戏或放宽剧情/场景校验。先选目的地，再两次确认；失焦、离开模块页、切换内部页面或目的地会取消待确认。原有请求队列仍由游戏线程复核并执行。完整传送适用范围见 [传送说明](TRAVEL.md)。

所附原操作指南中的固定快捷键适用于 ASI/Standalone；Hub 模块通过控制中心同名控件和宿主绑定操作，传送条件与返程记录规则共用。

## 状态与实时启停

首页开关状态实时读取地图标记、HUD、地图揭示与未访问地点传送辅助。传送辅助在等待原生安全刷新时显示“待生效”，开启、关闭请求均以实际刷新结果为准；清单与传送页面入口不显示开关状态。

实时停用先撤销待确认与尚未派发的传送，阻止新动作，再等待原生交接结果清理和传送菜单恢复。若正在使用未访问地点辅助，请保持游戏区域地图打开并稳定浏览。等待最多 15 秒；无法完成时恢复启用并保留设置，页面说明原因。等待期间重新启用可以取消停用。

已经派发的换图不能撤回；有活动返程行程或当前场景要求恢复返程时，会拒绝停用并保留返程入口。先完成返程，再停用模块。停用完成后，额外图标、揭示与保护效果走原生路径，HUD 隐藏；用户的功能偏好保留，重新启用时恢复。恢复传送辅助仍由游戏线程安全刷新，不复用旧菜单对象。

Hub 保存模块启用偏好、收藏与绑定，重启后保持；宝箱的标记、HUD、显示口径和探索开关在本次运行的停用／恢复间保留，退出游戏后仍按宝箱原有默认状态初始化。这些开关没有新增持久化配置。

此能力只影响 Hub 模块。原 ASI 和 Standalone 的安装、快捷键及运行方式保持不变。旧 ABI v1 宿主仍可读取原查询前缀和使用页面，只是没有实时生命周期控件。首次初始化失败时保留诊断信息，退出并修正问题后再启动，不在同一进程重复安装业务挂钩。

## 数据保留

Hub 模块复用 `plugins/Sky2ChestTracker/` 的原 ASI 日志、`revisit-return.dat` 与 `revisit-history/`。安装、切换和卸载不删除这些数据，也不把 Standalone 的 `Sky2ChestTracker/` 记录自动导入。切换入口前应先完成原版本返程；记录格式、章节核对与历史恢复规则保持原样。

## 构建和打包

需要 Hub **0.5.0 SDK**，默认路径为相邻仓库 `../sky2-mod-hub/sdk`。存在 SDK 时 CMake 首次配置默认额外构建模块；没有 SDK 时默认仍只构建原两版。已有构建目录会保留缓存选项，可显式设置。先按 [宝箱构建指南](https://github.com/blockshy/sky2-chest-tracker/blob/main/docs/BUILDING.md) 准备自己的受支持游戏目录、生成数据、固定依赖和 x64 MSVC 环境，再运行：

```powershell
# 从宝箱仓库根目录检出相邻 SDK。它是开源接口，不包含游戏资源。
git clone https://github.com/blockshy/sky2-mod-hub.git ../sky2-mod-hub

# 原两版可以明确关闭模块目标，无需 Hub SDK。
cmake -S . -B build-original -G 'NMake Makefiles' -DCMAKE_BUILD_TYPE=Release -DSKY2_BUILD_HUB_MODULE=OFF "-DCMAKE_MAKE_PROGRAM=$((Get-Command nmake).Source)"

# 构建三版。其它游戏资源、依赖与传送策略选项和原构建流程一致。
cmake -S . -B build-hub -G 'NMake Makefiles' -DCMAKE_BUILD_TYPE=Release -DSKY2_BUILD_HUB_MODULE=ON -DSKY2_HUB_SDK_DIR=../sky2-mod-hub/sdk -DSKY2_UNRESTRICTED_TRAVEL=ON "-DCMAKE_MAKE_PROGRAM=$((Get-Command nmake).Source)"
cmake --build build-hub --clean-first
ctest --test-dir build-hub --output-on-failure

# 仅生成离线模块包；不执行游戏安装。
./tools/Package-HubModule.ps1 -DllPath ./build-hub/Sky2ChestTracker.module.dll
```

模块目标名为 `Sky2ChestTrackerModule`。原 `Sky2ChestTracker`、`Sky2ChestTrackerAsi` 及 `Package-Mod.ps1`、`Package-Asi.ps1` 保留。模块不链接自己的 ImGui/MinHook，不安装 Present、WndProc 或 XInput 钩子；游戏业务钩子通过宿主登记。

`Package-HubModule.ps1` 生成 `Sky2ChestTracker-0.6.0-HubModule.zip` 与 `.sha256`，适用于独立更新模块的离线载荷；宿主整合包使用自己的白名单打包和安装流程。模块包不带游戏安装脚本，不直接覆盖宿主或 Loader，不能将原版 `Install-Mod.ps1` 用于它。`dist/` 之外的指南和清单留在下载目录。构建和打包均不执行 Git 提交、推送或 Release 发布。

## 接口边界

- `Sky2Module_Query` 仅返回静态元信息；按调用者提供的结构容量复制，旧 ABI 前缀后不写入数据。
- 宿主工作线程同步完成业务初始化。动作注册先于挂钩；页面、状态查询和动作回调在宿主 UI 线程执行，游戏修改仍通过原安全队列处理。
- `draw_header` 只绘制五项页签并在切页时撤销确认，不重复推进 Tick。`draw_page` 根据可选 `header_drawn` 字段跳过重复页签；短 Frame 安全回退。
- 可选 `content_size` 读取 Main 固定可视宽高，避免从自动增高卡片推算导致布局反馈。字段不存在或尺寸无效时保留原列表高度。
- 业务挂钩通过宿主所有权服务登记；软停用等待必要事务完成后旁路附加效果，始终保留 DLL、虚表回调与跳板。

接口定义和后续模块接入规则见 [Hub 模块开发指南](https://github.com/blockshy/sky2-mod-hub/blob/main/docs/MODULE_DEVELOPMENT.md)。

## 验证边界

自动回归覆盖既有游戏业务、汇编 ABI、Hub 二次确认撤销／语言映射、短结构边界、首页状态和安全实时启停。真实宿主的离屏夹具使用生产页面与 20px 字体，验证固定页头、Main 高度稳定、内外滚动、禁用分页和底部说明可达；业务数据均为测试快照，不注入游戏。详见 [测试说明](https://github.com/blockshy/sky2-chest-tracker/blob/main/docs/TESTING.md#hub-模块验证)。

实际游戏仍需核对三个 Mod 同时启用、手柄／键鼠切换、失焦松键、原生区域地图上下文、传送与精确返程。自动测试通过不能替代这些实机验收，也不代表全目录和所有剧情组合均已验证。
