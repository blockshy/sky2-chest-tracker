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
        {0x0100, PreviousPage}, {0x0200, NextPage}, {0x0080, ToggleEnabled},
        {0x0001, ToggleMapReveal}, {0x0002, ToggleTravelUnlock},
        {0x0004, ToggleRevisit}, {0x0008, ConfirmRevisit}
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
            // 两种松键顺序都不能把 A、右摇杆或十字键残留给游戏，最终也不能再补发 View。
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
    // 新组合要保留普通十字键用途，也不能把预先按住的方向键误认为解锁指令。
    for (uint16_t direction : {uint16_t{0x0001}, uint16_t{0x0002}}) {
        pad.Reset(); pad.Update({}, true);
        check(pad.Update({direction}, true).actions == 0, "单独十字键不切换探索辅助");
        r = pad.Update({static_cast<uint16_t>(kView | direction)}, true);
        check(!r.actions && !r.game.buttons, "先按方向键再按 View 不误触探索开关");
        pad.Update({}, true);
    }
    pad.Reset(); pad.Update({}, true);
    r = pad.Update({static_cast<uint16_t>(kView | 0x0001)}, true);
    check(r.actions == ToggleMapReveal, "同帧按下 View 与上键只切换地图全显");
    pad.Update({kView}, true);
    r = pad.Update({static_cast<uint16_t>(kView | 0x0002)}, true);
    check(r.actions == ToggleTravelUnlock, "保持 View 后改按下键只切换传送点");
    pad.Update({}, true);
    r = pad.Update({static_cast<uint16_t>(kView | 0x0080)}, true);
    check(r.actions == ToggleEnabled, "探索组合后原 View + RS 仍仅暂停宝箱标记");

    // 传送逐项选择只响应扳机的新按下沿；统一翻页由View+LB/RB承担。
    // 改变动作名称不能放松组合过滤或释放区，避免游戏误响应及一次跳过多项。
    for (bool right : {false,true}) {
        pad.Reset();pad.Update({},true);pad.Update({kView},true);
        PadSample trigger{kView};
        (right ? trigger.rightTrigger : trigger.leftTrigger)=200;
        r=pad.Update(trigger,true);
        check(r.actions==(right ? NextTravelItem : PreviousTravelItem) && same(r.game,{}),
              "View加扳机只提交对应逐项选择动作，游戏收到中立输入");
        check(!pad.Update(trigger,true).actions,"长按逐项选择组合不连续跳转");
        trigger.buttons=0;
        check(same(pad.Update(trigger,true).game,{}),"先松View不泄漏仍按住的扳机");
        pad.Update({},true);
        pad.Update(trigger,true);
        trigger.buttons=kView;
        check(!pad.Update(trigger,true).actions,"先按扳机再按View不追加逐项选择");

        // 同一次轻压跨过旧阈值多次，只能有一次按下；回到释放区后才能再次选择一项。
        pad.Reset();pad.Update({},true);trigger={kView};
        auto& value=right ? trigger.rightTrigger : trigger.leftTrigger;
        value=31;
        check(pad.Update(trigger,true).actions==(right ? NextTravelItem : PreviousTravelItem),"扳机首次按下仅选择一项");
        for (uint8_t sample : {uint8_t{29},uint8_t{31},uint8_t{16},uint8_t{32},uint8_t{200}}) {
            value=sample;
            check(!pad.Update(trigger,true).actions,"扳机未回释放区时阈值抖动不重复选择");
        }
        trigger.buttons=0;value=29;
        check(same(pad.Update(trigger,true).game,{}),"先松View后轻压扳机仍不会泄漏组合尾部");
        trigger.buttons=kView;value=15;pad.Update(trigger,true);value=31;
        check(pad.Update(trigger,true).actions==(right ? NextTravelItem : PreviousTravelItem),"完全释放后重新按下可再次选择一项");
        pad.Reset();pad.Update({},true);trigger.buttons=0;value=31;pad.Update(trigger,true);
        trigger.buttons=kView;value=29;pad.Update(trigger,true);value=31;
        check(!pad.Update(trigger,true).actions,"预持扳机在阈值附近抖动后加View仍不误触");
        // 与数字按键一致，带着扳机组合失焦或重连后必须先释放再接受新选择。
        pad.Reset();pad.Update({},true);trigger.buttons=kView;value=200;
        check(!pad.Update(trigger,false).actions,"后台扳机组合不排入逐项选择");
        check(!pad.Update(trigger,true).actions,"按住扳机组合切回前台不误选下一项");
        pad.Update({},true);
        check(pad.Update(trigger,true).actions==(right ? NextTravelItem : PreviousTravelItem),
              "失焦后全部释放再组合可以恢复逐项选择");
        pad.Reset();
        check(!pad.Update(trigger,true).actions,"重连时已按住的逐项选择组合不触发");
        pad.Update({},true);
        check(pad.Update(trigger,true).actions==(right ? NextTravelItem : PreviousTravelItem),
              "重连后全部释放再组合可以恢复逐项选择");
    }
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

    // 键盘与手柄使用同一组动作位，但 Ctrl 组合必须互斥于原 F6/F8 功能。
    KeyboardFilter keyboard;
    keyboard.Update(0, false, true);
    check(keyboard.Update(ToggleMode, true, true) == ToggleMapReveal,
          "Ctrl + F6 仅切换全显，不切换宝箱统计口径");
    check(keyboard.Update(ToggleMode, true, true) == 0, "长按 Ctrl + F6 不重复触发");
    check(keyboard.Update(ToggleMode, false, true) == 0, "先松 Ctrl 不泄漏一次原 F6 动作");
    keyboard.Update(0, false, true);
    check(keyboard.Update(ToggleMode, false, true) == ToggleMode, "松键后原 F6 功能恢复");
    check(keyboard.Update(ToggleMode, true, true) == 0, "先按 F6 再按 Ctrl 不追加全显指令");
    keyboard.Update(0, true, true);
    check(keyboard.Update(ToggleList, true, true) == ToggleTravelUnlock,
          "持续按住 Ctrl 再按 F8 仅切换传送点，不打开清单");
    check(keyboard.Update(0, true, true) == 0, "先松 F8 不触发其他功能");
    check(keyboard.Update(ToggleList, true, true) == ToggleTravelUnlock,
          "持续按住 Ctrl 时再次点按 F8 可以关闭传送点辅助");
    keyboard.Update(0, false, true);
    check(keyboard.Update(ToggleList, false, true) == ToggleList, "单独 F8 保留原清单功能");
    keyboard.Update(0, false, true);
    check(keyboard.Update(ToggleEnabled, true, true) == ToggleEnabled,
          "Ctrl 不改变 F9 的宝箱暂停功能，也不影响探索开关");
    keyboard.Update(0, false, true);
    check(keyboard.Update(ToggleFilter, true, true) == ToggleRevisit,
          "Ctrl + F10 打开回访，不同时切换宝箱清单筛选");
    keyboard.Update(0, false, true);
    check(keyboard.Update(TogglePanel, true, true) == ConfirmRevisit,
          "Ctrl + F7 只提交回访确认，不同时隐藏面板");
    keyboard.Update(0,false,true);
    check(keyboard.Update(PreviousPage,true,true)==PreviousTravelItem,
          "Ctrl加PgUp只选择传送清单上一项");
    keyboard.Update(0,false,true);
    check(keyboard.Update(NextPage,true,true)==NextTravelItem,
          "Ctrl加PgDn只选择传送清单下一项");
    keyboard.Update(0,false,true);
    check(keyboard.Update(NextPage,false,true)==NextPage,"单独PgDn提交两个清单共用的下一页动作");
    keyboard.Update(0,false,true);
    check(keyboard.Update(PreviousPage,false,true)==PreviousPage,"单独PgUp提交两个清单共用的上一页动作");
    check(!keyboard.Update(PreviousPage,true,true),"先按PgUp再按Ctrl不追加逐项选择");
    keyboard.Update(0,true,true);
    check(keyboard.Update(PreviousPage,true,true)==PreviousTravelItem,"按住Ctrl重新点按PgUp仅选择上一项");
    check(!keyboard.Update(PreviousPage,true,true),"长按Ctrl加PgUp不连续选择");
    check(!keyboard.Update(PreviousPage,false,true),"先松Ctrl不泄漏额外翻页动作");
    keyboard.Update(0,false,true);
    check(!keyboard.Update(NextPage,true,false),"后台逐项选择组合不排入指令");
    check(!keyboard.Update(NextPage,true,true),"带着逐项选择组合切回前台不触发");
    keyboard.Update(0,false,true);
    check(keyboard.Update(NextPage,true,true)==NextTravelItem,"全部释放后逐项选择组合恢复");
    // 后台采样必须丢弃动作；按住组合切回来也不会触发，直到全部松开再重新按下。
    check(keyboard.Update(ToggleList, true, false) == 0, "后台 Ctrl + F8 不触发传送辅助");
    check(keyboard.Update(ToggleList, true, true) == 0, "按住组合切回前台不误触");
    keyboard.Update(0, true, true);
    check(keyboard.Update(ToggleMode, true, true) == 0, "切回后未松 Ctrl 仍等待重新就绪");
    keyboard.Update(0, false, true);
    check(keyboard.Update(ToggleMode, true, true) == ToggleMapReveal, "全部松开后探索快捷键恢复");
    KeyboardFilter initializing;
    check(initializing.Update(ToggleMode, true, true) == 0, "加载 Mod 时已按住的组合不会误触");
    initializing.Update(0, false, true);
    check(initializing.Update(ToggleMode | ToggleList, true, true) == (ToggleMapReveal | ToggleTravelUnlock),
          "同时点按两个探索快捷键时只产生对应探索动作");
    std::printf("%u controller failure(s)\n", failures);
    return failures ? 1 : 0;
}
