// 验证模块 UI 的高影响边界：第二次确认、离页/失焦撤销、原生快照变化和语言映射。
// 测试只使用值对象，不读取游戏内存，不触发任何真实传送或存档写入。
#include "hub_ui_state.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
using namespace tracker;

int main() {
    assert(HubLanguage(0) == Language::Chinese);
    assert(HubLanguage(1) == Language::TraditionalChinese);
    assert(HubLanguage(2) == Language::Japanese);
    assert(HubLanguage(3) == Language::English);
    assert(HubLanguage(7) == Language::Korean);
    assert(HubLanguage(-1) == Language::Chinese);
    RevisitNativeContext context{};
    context.chapter = 4; context.browseIdentity = 17; context.progressSignature = 23;
    std::memcpy(context.scene, "mp0001", sizeof("mp0001"));
    HubTravelConfirmation state;
    assert(!state.Press(10, 100, true, true, context));
    assert(state.Armed(10, 101));
    assert(state.Press(10, 200, true, true, context));
    assert(!state.Armed(10, 201));
    // 当前页面不活动或原生菜单不可用时，即使重复调用也不能完成确认。
    assert(!state.Press(10, 300, true, true, context));
    state.Observe(false, true, context);
    assert(!state.Press(10, 301, true, true, context));
    assert(!state.Press(10, 302, true, false, context));
    assert(!state.Armed(10, 303));
    // 每种上下文变化都会令下一次点击重新成为第一次确认。
    for (int dimension = 0; dimension < 4; ++dimension) {
        state.Cancel();
        assert(!state.Press(10, 400, true, true, context));
        auto changed = context;
        if (dimension == 0) ++changed.chapter;
        if (dimension == 1) ++changed.browseIdentity;
        if (dimension == 2) ++changed.progressSignature;
        if (dimension == 3) std::memcpy(changed.scene, "mp0002", sizeof("mp0002"));
        assert(!state.Press(10, 401, true, true, changed));
        assert(state.Press(10, 402, true, true, changed));
    }
    state.Cancel();
    assert(!state.Press(10, 1000, true, true, context));
    assert(!state.Press(11, 1100, true, true, context));
    assert(!state.Press(11, 9100, true, true, context));
    assert(state.Press(11, 9101, true, true, context));
    std::cout << "Hub confirmation and language checks passed.\n";
}
