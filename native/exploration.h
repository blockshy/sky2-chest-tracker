// 探索辅助的最小跨模块接口：输入层只提交开关，游戏数据处理留在原生调用线程。
// 两项功能独立于宝箱标记，启动时均关闭；不得通过此接口批量写入剧情或存档标志。
#pragma once
#include <cstdint>

namespace tracker {
enum class ExplorationFeature { MapReveal, TravelUnlock };

struct ExplorationStatus {
    bool mapAvailable = false;
    bool travelAvailable = false;
    bool mapEnabled = false;
    bool travelEnabled = false;
    // 区分输入意图与实际菜单状态；确认窗口、切图等阶段等待安全刷新时不可虚报已生效。
    bool travelRequested = false;
    bool travelPending = false;
};

// 必须在完整 EXE 哈希验证和 MinHook 初始化成功后调用。
// 每个功能单独核对原生入口，冲突时仅停用受影响的探索辅助。
void InstallExploration(uintptr_t gameBase) noexcept;

// UI 只切换 Mod 自己的原子开关，不直接执行地图跳转或变更游戏保存数据。
void ToggleExploration(ExplorationFeature feature) noexcept;
ExplorationStatus ReadExplorationStatus() noexcept;
}
