// 验证玩家真实操作序列，尤其是组合键松开后不得残留一次开箱、攻击或菜单输入。
#include "controller_logic.h"
#include <cstdio>
#include <initializer_list>

using namespace tracker;

int main() {
    unsigned failures = 0;
    const auto check = [&](bool condition, const char* description) {
        if (!condition) { std::printf("FAIL: %s\n", description); ++failures; }
    };
    const auto same = [](const PadSample& a, const PadSample& b) {
        return a.buttons == b.buttons && a.leftTrigger == b.leftTrigger && a.rightTrigger == b.rightTrigger &&
            a.lx == b.lx && a.ly == b.ly && a.rx == b.rx && a.ry == b.ry;
    };
    PadFilter pad;
    pad.Update({}, true);
    auto r = pad.Update({kView}, true);
    check(r.game.buttons == 0 && r.actions == 0 && r.activity && r.modifier, "按住 View 时不提前弹出游戏地图");
    for (int frame = 0; frame < 180; ++frame) {
        r = pad.Update({kView}, true);
        check(r.actions == 0 && r.game.buttons == 0 && !r.activity, "长按修饰键不重复触发或抢占提示");
    }
    r = pad.Update({}, true);
    check(r.game.buttons == kView && !r.modifier && r.actions == 0, "单按 View 松开时补发原生动作一次");
    check(pad.Update({}, true).game.buttons == 0, "补发后下一次读取恢复松开状态");

    const struct { uint16_t button; uint32_t action; } bindings[] = {
        {0x1000, ToggleList}, {0x2000, TogglePanel}, {0x4000, ToggleMode}, {0x8000, ToggleFilter},
        {0x0100, PreviousPage}, {0x0200, NextPage}, {0x0080, ToggleEnabled}
    };
    for (const auto& binding : bindings) {
        for (bool viewReleasedFirst : {false, true}) {
            PadFilter filter;
            filter.Update({}, true);
            filter.Update({kView}, true);
            const PadSample chord{static_cast<uint16_t>(kView | binding.button)};
            r = filter.Update(chord, true);
            check(r.actions == binding.action && same(r.game, {}), "每个组合只触发对应动作，游戏收到中立状态");
            r = filter.Update(chord, true);
            check(r.actions == 0 && same(r.game, {}), "持续按住组合不重复触发");
            // 两种松键顺序都不能把 A 或右摇杆按压残留给游戏，最终也不能再补发 View。
            r = filter.Update({viewReleasedFirst ? binding.button : kView}, true);
            check(r.actions == 0 && same(r.game, {}), "先松开任意一键均不泄漏组合输入");
            r = filter.Update({}, true);
            check(r.actions == 0 && same(r.game, {}), "组合完成后不额外触发 View");
            r = filter.Update({binding.button}, true);
            check(r.game.buttons == binding.button && !r.actions, "全部松开后功能键恢复原生用途");
        }
    }
    // 旧组合已从 Mod 解绑；此测试只验证游戏内动作，不模拟或声称拦截系统 Xbox 窗口。
    pad.Reset(); pad.Update({}, true); pad.Update({kView}, true);
    check(pad.Update({static_cast<uint16_t>(kView | 0x0010)}, true).actions == 0,
          "View + Menu 不再切换 Mod 暂停状态");
    pad.Reset(); pad.Update({}, true);
    r = pad.Update({static_cast<uint16_t>(kView | 0x1000)}, true);
    check(r.actions == ToggleList && !r.game.buttons, "同帧按下 View 和 A 可以打开清单");
    pad.Update({}, true);
    pad.Update({0x1000}, true);
    r = pad.Update({static_cast<uint16_t>(kView | 0x1000)}, true);
    check(!r.actions && !r.game.buttons, "先按住 A 再按 View 不重复解释旧按键");
    pad.Update({}, true);
    pad.Update({kView}, true);
    pad.Update({static_cast<uint16_t>(kView | 0x0200)}, true);
    pad.Update({kView}, true);
    check(pad.Update({static_cast<uint16_t>(kView | 0x0200)}, true).actions == NextPage,
          "可以一直按住 View 并逐次点按 RB 翻页");

    // 组合期间吞掉扳机和方向键；先松 View 后仍握住扳机也不能突然攻击。
    pad.Reset(); pad.Update({}, true);
    const PadSample movingChord{static_cast<uint16_t>(kView | 0x0001), 255, 220, 16000, -16000, 18000, -18000};
    check(same(pad.Update(movingChord, true).game, {}), "修饰期间按方向键、扳机或摇杆均不给游戏输入");
    r = pad.Update({0x0001, 255, 220}, true);
    check(same(r.game, {}), "先松 View 时继续屏蔽未释放的方向键和扳机");
    pad.Update({}, true);
    const PadSample normal{0x0001, 255, 220, 16000, -16000, 18000, -18000};
    check(same(pad.Update(normal, true).game, normal), "不按修饰键时所有普通状态原样转发");

    // 切出、重连时放弃未完成的组合；重新松开所有控制后才接收下一条指令。
    pad.Reset(); pad.Update({}, true); pad.Update({kView}, true);
    const PadSample chord{static_cast<uint16_t>(kView | 0x1000)};
    r = pad.Update(chord, false);
    check(!r.actions && !r.modifier && same(r.game, chord), "后台不消费手柄或排入 Mod 指令");
    check(pad.Update(chord, true).actions == 0, "切回前台时尚未释放的组合不触发");
    pad.Update({}, true);
    check(pad.Update(chord, true).actions == ToggleList, "松开后再次组合恢复正常");
    pad.Reset();
    check(pad.Update(chord, true).actions == 0, "带着按键重连不误触");
    pad.Update({}, true);
    check(pad.Update(chord, true).actions == ToggleList, "重连后松键可以重新操作");

    // 死区抑制漂移，缓慢跨过死区或累积移动仍能切回手柄提示。
    pad.Reset(); pad.Update({}, true);
    check(!pad.Update({0, 20, 0, 7000}, true).activity, "摇杆与扳机死区内不切换提示");
    pad.Update({0, 20, 0, 7800}, true);
    check(pad.Update({0, 20, 0, 7900}, true).activity, "缓慢跨越死区也属于手柄活动");
    check(!pad.Update({0, 20, 0, 7900}, true).activity, "持续握住摇杆不反复抢回手柄提示");
    bool slowActivity = false;
    for (int value = 8000; value < 10100; value += 100)
        slowActivity |= pad.Update({0, 20, 0, static_cast<int16_t>(value)}, true).activity;
    check(slowActivity, "每帧小幅移动可累积成有效活动");
    pad.Update({}, true);
    pad.Update({0, 29}, true);
    check(pad.Update({0, 31}, true).activity, "缓慢扣动扳机越过阈值可切换提示");
    std::printf("%u controller failure(s)\n", failures);
    return failures ? 1 : 0;
}
