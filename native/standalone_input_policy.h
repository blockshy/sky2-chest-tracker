// 独立交互窗口的纯输入规则：保留关闭窗口时的普通开关，窗口内只允许关闭组合。
// 此文件不访问 Win32、ImGui 或游戏对象，失焦、松键尾部和危险组合可独立回归。
#pragma once
#include "controller_logic.h"

namespace tracker {
inline bool StandaloneNeutral(const PadSample& pad) noexcept {
    return pad.buttons == 0 && pad.leftTrigger <= 15 && pad.rightTrigger <= 15 &&
        std::abs(static_cast<int>(pad.lx)) <= 7849 && std::abs(static_cast<int>(pad.ly)) <= 7849 &&
        std::abs(static_cast<int>(pad.rx)) <= 8689 && std::abs(static_cast<int>(pad.ry)) <= 8689;
}
struct StandalonePadResult {
    uint32_t actions = 0;
    bool capture = false, replayView = false, activity = false, navigate = false;
};
class StandalonePadPolicy {
    PadFilter shortcuts_;
    bool previousOpen_ = false, tail_ = false, navigationArmed_ = false;
    bool shortcutArmed_ = false;
    bool discardViewReplay_ = false;
public:
    void Reset() noexcept { *this = StandalonePadPolicy{}; }
    StandalonePadResult Update(const PadSample& raw, bool open, bool foreground, bool otherWindow = false,
                               uint32_t requestedActions = 0) noexcept {
        if (!foreground) { Reset(); return {}; }
        const auto shortcut = shortcuts_.Update(raw, true);
        // View 按下期间只要任一窗口拥有输入，就把整次手势视为窗口输入。
        // 即使窗口在松开 View 前关闭，也不能重新补发一次原生地图按键。
        if ((raw.buttons & kView) && (open || otherWindow)) discardViewReplay_ = true;
        StandalonePadResult result;
        result.activity = shortcut.activity;
        const bool neutral = StandaloneNeutral(raw);
        // 动作由公共动态绑定表匹配，此层不再解释固定业务键。旧 PadFilter
        // 仅用于 View 手势、补发和按钮尾部；它计算的历史动作全部忽略。
        if (!shortcutArmed_) { shortcutArmed_ = neutral; requestedActions = 0; }
        result.actions = open ? (requestedActions & TogglePanel) : requestedActions;
        if (open && !previousOpen_) navigationArmed_ = false;
        if (!open) navigationArmed_ = false;
        else if (neutral) navigationArmed_ = true;
        // 关闭时必须吞完本窗口接收的 A/B/摇杆尾部；失焦则立即放行，不能
        // 把用户在别的窗口中的按压变成游戏的永久输入锁。
        if (open) tail_ = true;
        else if (neutral) tail_ = false;
        const auto& filtered = shortcut.game;
        result.replayView = !open && !discardViewReplay_ && !(raw.buttons & kView) && (filtered.buttons & kView);
        const bool shortcutCapture = !result.replayView &&
            (raw.buttons != filtered.buttons || raw.leftTrigger != filtered.leftTrigger ||
             raw.rightTrigger != filtered.rightTrigger || raw.lx != filtered.lx ||
             raw.ly != filtered.ly || raw.rx != filtered.rx || raw.ry != filtered.ry);
        result.capture = open || tail_ || shortcutCapture;
        result.navigate = open && navigationArmed_ && !(raw.buttons & kView);
        if (!(raw.buttons & kView)) discardViewReplay_ = false;
        previousOpen_ = open;
        return result;
    }
};
}
