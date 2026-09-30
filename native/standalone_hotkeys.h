// 独立窗口只维护稳定的业务动作表；具体键位、持久化和跨 Mod 冲突检查由
// 随仓库分发的公共组件完成，六项动作由独立窗口直接调用已有业务接口。
#pragma once
#include "standalone_ui/hotkeys.h"
#include "controller_logic.h"
#include <string>

namespace tracker {
inline bool InitializeStandaloneHotkeys(const std::wstring& folder) noexcept {
    static constexpr sky2solo::HotkeyDefinition definitions[]{
        {"chest.open", "宝箱窗口", {VK_F7, 0, XINPUT_GAMEPAD_DPAD_UP}, true},
        {"chest.cycle_mode", "统计口径", {VK_F6, 0, XINPUT_GAMEPAD_X}},
        {"chest.toggle_markers", "宝箱标记", {0, 0, XINPUT_GAMEPAD_RIGHT_THUMB}},
        {"chest.toggle_map_reveal", "地图全显", {VK_F6, sky2solo::HotkeyCtrl, 0}},
        {"chest.toggle_unvisited", "未到访传送点", {VK_F8, sky2solo::HotkeyCtrl, 0}},
        {"chest.open_travel", "全传送页面", {VK_F10, sky2solo::HotkeyCtrl, 0}}
    };
    return sky2solo::InitializeHotkeys(1, "宝箱追踪", folder.c_str(), definitions, std::size(definitions));
}
inline uint32_t StandaloneInputActions(uint32_t registeredMask) noexcept {
    // 位序只在此处转为既有 UI 消费协议，页面无需知道用户当前选的是哪个键。
    constexpr uint32_t actions[]{TogglePanel, ToggleMode, ToggleEnabled, ToggleMapReveal, ToggleTravelUnlock, ToggleRevisit};
    uint32_t result = 0;
    for (unsigned index = 0; index < std::size(actions); ++index)
        if (registeredMask & (1u << index)) result |= actions[index];
    return result;
}
}
