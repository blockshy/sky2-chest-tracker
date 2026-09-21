// 手柄组合键的纯状态机；不调用系统 API，也不依赖游戏对象，便于验证每种松键顺序。
#pragma once
#include <cstdint>
#include <cstdlib>

namespace tracker {
enum InputAction : uint32_t {
    ToggleMode = 1u << 0, TogglePanel = 1u << 1, ToggleEnabled = 1u << 2,
    ToggleList = 1u << 3, ToggleFilter = 1u << 4, PreviousPage = 1u << 5, NextPage = 1u << 6
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
public:
    void Reset() noexcept { *this = PadFilter{}; }
    PadResult Update(const PadSample& raw, bool foreground) noexcept {
        PadResult result{raw};
        if (!foreground) {
            Reset();
            previous_ = raw;
            return result;
        }
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
        if (raw.leftTrigger <= 30) blockedLeft_ = false;
        if (raw.rightTrigger <= 30) blockedRight_ = false;
        if (raw.buttons & kView) {
            if (!viewPending_) { viewPending_ = true; used_ = false; }
            result.modifier = true;
            used_ |= (raw.buttons & ~kView) != 0 || AnalogActive(raw);
            // 必须先按住 View 再按功能键；同一帧一起按下也可，预先按住的功能键不算新指令。
            // 暂停使用 RS（按下右摇杆，XINPUT_GAMEPAD_RIGHT_THUMB），避开用户实测会弹出
            // Xbox 窗口的 View + Menu。此层只过滤游戏收到的状态，不能阻止系统独立监听组合键。
            const struct { uint16_t button; uint32_t action; } bindings[] = {
                {0x1000, ToggleList}, {0x2000, TogglePanel}, {0x4000, ToggleMode}, {0x8000, ToggleFilter},
                {0x0100, PreviousPage}, {0x0200, NextPage}, {0x0080, ToggleEnabled}
            };
            for (const auto& binding : bindings) if (pressed & binding.button) result.actions |= binding.action;
            blocked_ |= static_cast<uint16_t>(raw.buttons & ~kView);
            blockedLeft_ |= raw.leftTrigger > 30;
            blockedRight_ |= raw.rightTrigger > 30;
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
