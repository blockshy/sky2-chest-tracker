# 实现结构

## 模块

| 文件 | 职责 |
| --- | --- |
| `native/xinput_proxy.cpp` | 转发游戏导入的 XInput ordinal 2／3，启动后台初始化 |
| `native/tracker.cpp` | 校验 EXE、读取状态快照、接入原生地图图标选择函数 |
| `native/core.h` | 纯位标志读取和两种口径的图标语义 |
| `native/progress.h` | 同一快照内计算地图进度与遗漏筛选 |
| `native/overlay.cpp` | D3D11 Present 绘制、清单布局及动作分发 |
| `native/controller_logic.h` | 手柄组合、松键保护、前台恢复和死区活动判断 |
| `native/input_bridge.cpp` | 接入游戏自己的 XInput 调用链，观察键鼠来源 |
| `tools/extract_catalog.py` | 从本机资源关联宝箱行号、标志和位置 |
| `tools/extract_map_catalog.py` | 生成中文地图归属与统计分组 |

## 状态语义

当前周目使用当前开箱位；继承口径使用“当前开箱位或继承位”。新开的箱子因此立即进入两组计数。
每次读取存档都以游戏当前状态为准，不在 Mod 中维护独立的累计历史。
两组全局和地区统计来自同一份快照，面板每 250 毫秒刷新，原生地图图标在调用时读取状态。

目录生成器保留宝箱表原始行号，应用游戏定义的覆盖标志规则，排除开发地图记录。
每个正式目标只属于一个统计地图。相邻道路拆开统计，迷宫按场景汇总各楼层；
大地图前缀负责定位，统计口径不改变地图归属。

## 版本与地图挂钩

挂钩前检查 EXE 完整哈希和内存中的预期指令或虚表目标，冲突时不覆盖未知挂钩。
原生区域地图采用表驱动的图标函数，对象图标路径另行兼容。位置、缩放和区域布局由游戏负责。
面板初始化失败时不启用地图修改，防止玩家无法辨认当前口径。

所有 RVA 仅针对 README 指定的 EXE。适配新版不能只修改哈希；需要重新验证函数、对象布局和标志语义。
DLL 不写宝箱状态或存档，也不替玩家触发成就。

## 手柄和设备切换

输入接入点在游戏自身的 XInputGetState 导入槽，继续调用原目标以保留 Steam Input 映射。
每个手柄槽独立维护状态。View 作为修饰键时，组合不传给游戏；先松开 View 后仍按住的功能键继续被屏蔽到真正释放。
单独 View 在松开时补发一次，并维护输出状态的数据包序号。

实际按键活动及死区外的有效模拟输入切换到手柄提示；键盘和真实鼠标消息切回键鼠提示。
后台不执行 Mod 动作，重连／回到前台需松开控制后重新进入可用状态。
Mod 不拦截系统全局快捷键，因此避开会与 Xbox 窗口冲突的 View + Menu，使用 View + RS 暂停。

## 参考

- [Microsoft：XINPUT_GAMEPAD](https://learn.microsoft.com/en-us/windows/win32/api/xinput/ns-xinput-xinput_gamepad)
- [Microsoft：XINPUT_STATE](https://learn.microsoft.com/en-us/windows/win32/api/xinput/ns-xinput-xinput_state)
- [Valve：Steam Input 手柄模拟](https://partner.steamgames.com/doc/features/steam_controller/steam_input_gamepad_emulation_bestpractices)
- [第三方来源与许可](../THIRD_PARTY_NOTICES.md)
