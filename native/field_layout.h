#pragma once
#include <cstdint>

// Ver.1.05（EXE 1.5.0.0 / Steam build 26290169）的 fieldmap::Manager 布局。
// 这里只描述场景管理器；菜单、玩家 Actor、地点表和存档对象不是同一类型，
// 不能把本次部分字段前移 8 字节的规律应用到那些对象。调用者必须先通过完整
// EXE 指纹及自身挂钩签名验证，再从实时全局关系取得管理器，禁止缓存对象地址。
namespace tracker::field_layout {
inline constexpr uintptr_t kSceneRoot = 0x100;       // 当前场景根节点；原生地图更新读取此指针。
inline constexpr uintptr_t kSceneName = 0x168;       // 当前场景名，加载器提交后才成为当前身份。
inline constexpr uintptr_t kSceneNameLength = 0x188; // 当前场景名长度，不包含结尾 NUL。
inline constexpr uintptr_t kPendingScene = 0x18C;    // 原生完整加载入口保存的目标场景名。
inline constexpr uintptr_t kEventObjects = 0x258;    // 当前场景事件盒指针数组。
inline constexpr uintptr_t kEventObjectCount = 0x260;// 数组元素数；重载时先清空，再构建新对象。
inline constexpr uintptr_t kMapData = 0x640;         // 当前站位对应的地点数据，可能不同于场景根节点。
inline constexpr uintptr_t kPlayer = 0x658;          // Player 对象；其 +0x60 Actor 指针保持不变。
inline constexpr uintptr_t kMinimap = 0x728;         // 地图管理器，最终原生结果消费者也读取此字段。
inline constexpr uintptr_t kLoadPosition = 0x1BA0;   // 加载目标 XYZ；由原生 API 写入，Mod 仅观察。
inline constexpr uintptr_t kLoadYaw = 0x1BB0;        // 加载目标朝向；不能替代 Actor 四元数的实时采样。
inline constexpr uintptr_t kLoadBusy = 0x1BC0;       // 原生加载繁忙位；非零时不能提交新的请求。
inline constexpr uintptr_t kTargetSpot = 0x1BCC;     // 原生目的地 ID；精确返程交给加载 API 前置为 -1。
}
