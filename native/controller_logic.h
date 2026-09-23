// 输入组合键的纯状态机；不调用系统 API，也不依赖游戏对象，便于验证松键与切后台行为。
#pragma once
#include <cstdint>
#include <cstdlib>

namespace tracker {
enum InputAction : uint32_t {
    ToggleMode = 1u << 0, TogglePanel = 1u << 1, ToggleEnabled = 1u << 2,
    ToggleList = 1u << 3, ToggleFilter = 1u << 4, PreviousPage = 1u << 5, NextPage = 1u << 6,
    ToggleMapReveal = 1u << 7, ToggleTravelUnlock = 1u << 8,
    ToggleRevisit = 1u << 9, ConfirmRevisit = 1u << 10,
    // 两个清单共用PreviousPage/NextPage翻页；传送清单的逐项选择独立编码。
    // 保持原动作位数值，避免改动输入队列与消费边界。
    PreviousTravelItem = 1u << 11, NextTravelItem = 1u << 12
};

// bits 0～6 分别对应 F6、F7、F9、F8、F10、PgUp、PgDn，与既有动作位保持一致。
// 只把物理功能键的新按下沿解释为操作；先按住 F6 再按 Ctrl 不应产生第二次切换。
// 失焦或初始化时等待所有快捷键和 Ctrl 松开，避免 Alt+Tab 回游戏时把旧组合当成新指令。
class KeyboardFilter {
    uint32_t previous_ = 0;
    bool rearm_ = true;
public:
    uint32_t Update(uint32_t down, bool control, bool foreground) noexcept {
        constexpr uint32_t supported = (1u << 7) - 1;
        down &= supported;
        const uint32_t pressed = down & ~previous_;
        previous_ = down;
        if (!foreground) { rearm_ = true; return 0; }
        if (rearm_) {
            rearm_ = down != 0 || control;
            return 0;
        }
        if (!control) return pressed;
        // Ctrl 的探索组合与原功能互斥；回访确认只在回访窗口打开时才由界面接收。
        // F9 仍保持暂停功能，避免改变玩家已经熟悉的暂停／恢复操作。
        // PgUp/PgDn在两个清单中统一翻页；加Ctrl才转换为传送清单逐项选择。
        uint32_t actions = pressed & ~(ToggleMode | ToggleList | ToggleFilter | TogglePanel | PreviousPage | NextPage);
        if (pressed & ToggleMode) actions |= ToggleMapReveal;
        if (pressed & ToggleList) actions |= ToggleTravelUnlock;
        if (pressed & ToggleFilter) actions |= ToggleRevisit;
        if (pressed & TogglePanel) actions |= ConfirmRevisit;
        if (pressed & PreviousPage) actions |= PreviousTravelItem;
        if (pressed & NextPage) actions |= NextTravelItem;
        return actions;
    }
};
inline constexpr uint16_t kView = 0x0020;
struct PadSample {
    uint16_t buttons = 0;
    uint8_t leftTrigger = 0, rightTrigger = 0;
    int16_t lx = 0, ly = 0, rx = 0, ry = 0;
};
struct PadResult { PadSample game; uint32_t actions = 0; bool activity = false; bool modifier = false; };

inline bool AnalogActive(const PadSample& p) noexcept {
    return p.leftTrigger > 30 || p.rightTrigger > 30 ||
        std::abs(static_cast<int>(p.lx)) > 7849 || std::abs(static_cast<int>(p.ly)) > 7849 ||
        std::abs(static_cast<int>(p.rx)) > 8689 || std::abs(static_cast<int>(p.ry)) > 8689;
}

class PadFilter {
    PadSample previous_{};
    PadSample activityAnchor_{};
    bool rearm_ = true, viewPending_ = false, used_ = false;
    uint16_t blocked_ = 0;
    bool blockedLeft_ = false, blockedRight_ = false;
    bool leftTriggerDown_ = false, rightTriggerDown_ = false;

    // 扳机是模拟量：按下后必须真正回到释放区才允许下一次触发。上下阈值分离，
    // 避免轻压时在30附近来回波动被当作多次按压。此状态也在View未按下时更新。
    static bool UpdateTrigger(uint8_t value,bool& down) noexcept {
        const bool wasDown=down;
        if (value>30) down=true;
        else if (value<=15) down=false;
        return down && !wasDown;
    }
public:
    void Reset() noexcept { *this = PadFilter{}; }
    PadResult Update(const PadSample& raw, bool foreground) noexcept {
        PadResult result{raw};
        if (!foreground) {
            Reset();
            previous_ = raw;
            return result;
        }
        const bool leftPressed=UpdateTrigger(raw.leftTrigger,leftTriggerDown_);
        const bool rightPressed=UpdateTrigger(raw.rightTrigger,rightTriggerDown_);
        // 接入或切回游戏时先等按键松开，防止插线／Alt+Tab 时仍按住的组合误触。
        if (rearm_) {
            rearm_ = raw.buttons != 0 || AnalogActive(raw);
            previous_ = raw;
            activityAnchor_ = raw;
            return result;
        }
        const auto pressed = static_cast<uint16_t>(raw.buttons & ~previous_.buttons);
        // 与上次显著活动的位置比较，缓慢推杆也能累积到阈值；持续握住摇杆不反复抢回提示。
        // 从死区跨入有效区同样视作新活动，死区内漂移则只更新基准，不切换输入来源。
        const bool analogChanged = AnalogActive(raw) &&
            (!AnalogActive(previous_) ||
             std::abs(static_cast<int>(raw.lx) - activityAnchor_.lx) > 1500 ||
             std::abs(static_cast<int>(raw.ly) - activityAnchor_.ly) > 1500 ||
             std::abs(static_cast<int>(raw.rx) - activityAnchor_.rx) > 1500 ||
             std::abs(static_cast<int>(raw.ry) - activityAnchor_.ry) > 1500 ||
             std::abs(static_cast<int>(raw.leftTrigger) - activityAnchor_.leftTrigger) > 8 ||
             std::abs(static_cast<int>(raw.rightTrigger) - activityAnchor_.rightTrigger) > 8);
        if (analogChanged || !AnalogActive(raw)) activityAnchor_ = raw;
        result.activity = pressed != 0 || analogChanged;
        blocked_ &= raw.buttons;
        if (raw.leftTrigger <= 15) blockedLeft_ = false;
        if (raw.rightTrigger <= 15) blockedRight_ = false;
        if (raw.buttons & kView) {
            if (!viewPending_) { viewPending_ = true; used_ = false; }
            result.modifier = true;
            used_ |= (raw.buttons & ~kView) != 0 || AnalogActive(raw) || leftTriggerDown_ || rightTriggerDown_;
            // 必须先按住 View 再按功能键；同一帧一起按下也可，预先按住的功能键不算新指令。
            // 暂停使用 RS（按下右摇杆，XINPUT_GAMEPAD_RIGHT_THUMB），避开用户实测会弹出
            // Xbox 窗口的 View + Menu。此层只过滤游戏收到的状态，不能阻止系统独立监听组合键。
            const struct { uint16_t button; uint32_t action; } bindings[] = {
                {0x1000, ToggleList}, {0x2000, TogglePanel}, {0x4000, ToggleMode}, {0x8000, ToggleFilter},
                {0x0100, PreviousPage}, {0x0200, NextPage}, {0x0080, ToggleEnabled},
                // 十字键上／下没有占用既有 Mod 组合；单独按方向键仍交给游戏。
                {0x0001, ToggleMapReveal}, {0x0002, ToggleTravelUnlock},
                // 左键打开独立回访窗口，右键在窗口内二次确认；避开系统 View + Menu。
                {0x0004, ToggleRevisit}, {0x0008, ConfirmRevisit}
            };
            for (const auto& binding : bindings) if (pressed & binding.button) result.actions |= binding.action;
            // 两个清单均用View+LB/RB翻页；View+LT/RT独立提交传送清单逐项选择。
            // 每次只认经过释放区后的新按下沿，不因长按或阈值附近抖动跳过多个项目。
            // 组合期间仍向游戏发送中立状态，避免同时触发游戏里的扳机动作。
            if (leftPressed) result.actions|=PreviousTravelItem;
            if (rightPressed) result.actions|=NextTravelItem;
            blocked_ |= static_cast<uint16_t>(raw.buttons & ~kView);
            blockedLeft_ |= leftTriggerDown_;
            blockedRight_ |= rightTriggerDown_;
            // View 作为修饰键期间把手柄状态置为中立，避免开箱、攻击、切人或菜单同时响应。
            result.game = {};
        } else {
            // 单独点击 View 延后到松开时向游戏发送一次；组合成功则不再补发地图键。
            if (viewPending_ && !used_) result.game.buttons |= kView;
            viewPending_ = false;
            result.game.buttons &= static_cast<uint16_t>(~blocked_);
            // 先松开 View、仍按住 A 等键时继续吞掉这些键，直到它们真正松开。
            if (blockedLeft_) result.game.leftTrigger = 0;
            if (blockedRight_) result.game.rightTrigger = 0;
        }
        previous_ = raw;
        return result;
    }
};
}
