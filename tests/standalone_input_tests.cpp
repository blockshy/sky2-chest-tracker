// 覆盖真正影响游戏输入的序列：打开组合不得同时激活内容，关闭后不得泄漏按键。
#include "standalone_input_policy.h"
#include <cstdio>
#include <initializer_list>

int main() {
    using namespace tracker;
    unsigned failures = 0;
    const auto check = [&](bool value, const char* name) { if (!value) { ++failures; std::printf("FAIL: %s\n", name); } };
    StandalonePadPolicy policy;
    policy.Update({}, false, true);
    auto result = policy.Update({static_cast<uint16_t>(kView | 0x0001)}, false, true, false, TogglePanel);
    check(result.actions == TogglePanel && result.capture && !result.navigate, "打开窗口仅切换可见性并吞组合");
    result = policy.Update({0x0001}, true, true);
    check(!result.actions && result.capture && !result.navigate, "打开后残留十字键上不能立刻导航");
    check(policy.Update({}, true, true).navigate, "全部释放后内容导航可用");
    result = policy.Update({0x1000}, true, true);
    check(result.navigate && result.capture && !result.actions, "内容A只作为确认而不执行旧清单动作");
    result = policy.Update({0x1000}, false, true);
    check(result.capture && !result.navigate, "窗口关闭后吞完A尾部");
    check(!policy.Update({}, false, true).capture, "释放后正常游戏输入恢复");
    result = policy.Update({static_cast<uint16_t>(kView | 0x1000)}, false, true);
    check(!result.actions && result.capture, "View加A不再直达宝箱清单");
    policy.Update({}, false, true);
    policy.Update({kView}, false, true);
    result = policy.Update({}, false, true);
    check(result.replayView && !result.capture, "单按View仍向外层请求一次原生地图键");
    policy.Update({}, false, true);
    result = policy.Update({static_cast<uint16_t>(kView | 0x0004)}, false, true);
    check(!result.actions && result.capture, "队伍窗口View左不打开宝箱传送");
    policy.Update({}, false, true);
    result = policy.Update({static_cast<uint16_t>(kView | 0x0008)}, true, true);
    check(!result.actions && result.capture, "旧全局确认组合不能提交传送");
    result = policy.Update({0x1000}, true, false);
    check(!result.capture && !result.navigate && !result.actions, "切出游戏立即释放输入所有权");
    result = policy.Update({static_cast<uint16_t>(kView | 0x0001)}, true, true, false, TogglePanel);
    check(!result.actions && result.capture && !result.navigate, "带着组合返回前台不关闭或确认");
    policy.Update({}, true, true);
    result = policy.Update({static_cast<uint16_t>(kView | 0x0001)}, true, true, false, TogglePanel);
    check(result.actions == TogglePanel && !result.navigate, "重新释放后的窗口关闭组合有效");
    // RS 模拟量也要在关闭后回到死区；只检查按钮会让视角突然转动。
    policy.Update({0, 0, 0, 0, 0, 0, 24000}, true, true);
    result = policy.Update({0, 0, 0, 0, 0, 0, 24000}, false, true);
    check(result.capture, "关闭后仍按住右摇杆不转动游戏视角");
    check(!policy.Update({}, false, true).capture, "摇杆回中后释放尾部");
    policy.Reset(); policy.Update({}, false, true);
    policy.Update({kView}, false, true, true);
    result = policy.Update({}, false, true, false);
    check(!result.replayView, "别的窗口占有期间按住View，关闭后释放不得补发地图");
    policy.Update({kView}, true, true);
    result = policy.Update({}, false, true);
    check(!result.replayView, "自己的窗口在View释放前关闭也不得补发地图");
    policy.Reset(); policy.Update({}, false, true);
    // 本层只接受已经由公共配置匹配的动作，不根据旧固定按钮自行生成功能。
    result = policy.Update({static_cast<uint16_t>(kView | 0x8000)}, false, true, false, ToggleRevisit);
    check(result.actions == ToggleRevisit, "动态绑定允许原本未用于传送的新按钮");
    policy.Update({}, true, true);
    result = policy.Update({static_cast<uint16_t>(kView | 0x8000)}, true, true, false, ToggleRevisit);
    check(!result.actions, "窗口内动态业务动作也必须关闭");
    return failures ? 1 : 0;
}
