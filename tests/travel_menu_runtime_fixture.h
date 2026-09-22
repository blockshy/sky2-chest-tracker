// 即时刷新集成测试：只在 exploration_runtime_tests.cpp 中包含。
// 使用真实生产刷新函数和自行分配的对象；所有“原生 API”均替换为本进程内的确定性桩。
// 桩模拟原生登记／销毁／重建和选择回调，不调用游戏、挂钩安装、文件或保存接口。
#pragma once
#include <new>

namespace {

struct TravelMenuFixture;
inline TravelMenuFixture* g_menuFixture = nullptr;

struct TravelMenuFixture {
    using Bytes64 = std::array<unsigned char, 0x40>;
    std::array<unsigned char, 0x400> menu{};
    std::array<unsigned char, 0x340> manager{};
    std::array<unsigned char, 0x1100> flags{};
    std::array<unsigned char, 0x200> root{};
    std::array<std::array<unsigned char, 0x200>, 3> icons{};
    // 交替使用两个显示数组，确保测试能发现没有移除的旧数组指针。
    std::array<std::array<Bytes64, 3>, 2> displays{};
    std::array<std::array<unsigned char, 0x150>, 2> lists{};
    std::array<std::array<unsigned char, 0x20>, 2> callbacks{};
    std::array<std::array<Bytes64, 3>, 2> items{};
    std::array<std::array<uintptr_t, 3>, 2> itemPointers{};
    std::array<tracker::TravelSpotState, 2> spots{};
    std::array<tracker::TravelAreaState, 1> areas{};
    // 仅包含本样本两个目的地的合成原生表；供生产列表“至少一项”前提检查使用。
    // 行布局与真实已适配构建一致，但名称、资源和玩家数据均未复制进测试。
    std::array<unsigned char, 0x100> tableRoot{};
    std::array<unsigned char, 0x20> tableHolder{};
    std::array<unsigned char, 0x38> tableFile{};
    std::array<unsigned char, 0xA0> tableHeaders{};
    std::array<unsigned char, 0x300> tableBuffer{};
    std::array<unsigned char, 0x650> sceneRoot{};
    TestResult& result;
    uintptr_t image = 0;
    unsigned bank = 0, scriptCalls = 0, displayCalls = 0, clearCalls = 0;
    unsigned spotBuilds = 0, areaBuilds = 0, selectionCalls = 0;
    bool failScript = false, emptyDisplay = false, corruptDisplayCapacity = false, emptyActiveList = false;
    bool omitAreaCallback = false, requestDuringScript = false, reenterScript = false;
    bool reentrySuppressed = false, scriptSnapshotEnabled = false;
    bool scriptCompleted = false;
    bool finalAnchorBlocked = false, finalTargetBlocked = false;
    std::array<unsigned char, 0x80> originalCamera{};

    TravelMenuFixture(TestResult& checks, uintptr_t syntheticImage) : result(checks), image(syntheticImage) {}
    uintptr_t Menu() const { return reinterpret_cast<uintptr_t>(menu.data()); }
    uintptr_t Manager() const { return reinterpret_cast<uintptr_t>(manager.data()); }
    uintptr_t List(unsigned index) const { return reinterpret_cast<uintptr_t>(lists[index].data()); }
    uintptr_t Display(unsigned index) const { return reinterpret_cast<uintptr_t>(displays[bank][index].data()); }

    template<class T> static T Load(uintptr_t address) {
        T value{};
        std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
        return value;
    }
    template<class T> static void Store(uintptr_t address, T value) {
        std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
    }
    void SetState(int32_t state, bool nested = true) {
        // 与原生状态机布局一致；确认状态使用额外顶帧，使生产守卫实际拒绝它。
        std::memset(menu.data() + 0xB8, 0, 0x30);
        const int32_t depth = state == 3 ? 0 : (state == 5 && nested ? 2 : 1);
        Put(menu, 0xB8, int32_t{3}); Put(menu, 0xBC, int32_t{3}); Put(menu, 0xC0, int32_t{20});
        if (depth == 2) {
            Put(menu, 0xC4, int32_t{4}); Put(menu, 0xC8, int32_t{4}); Put(menu, 0xCC, int32_t{20});
        }
        const auto frame = 0xB8 + static_cast<size_t>(depth) * 12;
        Put(menu, frame, state); Put(menu, frame + 4, state); Put(menu, frame + 8, int32_t{20});
        Put(menu, 0xE8, depth); Put(menu, 0xEC, depth);
    }
    void Reset(int32_t state = 3, bool enabled = false, bool nested = true) {
        menu.fill(0); manager.fill(0); flags.fill(0); root.fill(0xC5);
        tableRoot.fill(0); tableHolder.fill(0); tableFile.fill(0); tableHeaders.fill(0);
        tableBuffer.fill(0); sceneRoot.fill(0);
        for (auto& icon : icons) icon.fill(0xB6);
        for (auto& list : lists) list.fill(0);
        for (auto& callback : callbacks) callback.fill(0);
        scriptCalls = displayCalls = clearCalls = spotBuilds = areaBuilds = selectionCalls = 0;
        failScript = emptyDisplay = corruptDisplayCapacity = emptyActiveList = omitAreaCallback = requestDuringScript = reenterScript = false;
        reentrySuppressed = scriptSnapshotEnabled = scriptCompleted = false;
        finalAnchorBlocked = finalTargetBlocked = false;
        bank = 0;
        // 原子状态类不可赋值；仅在独立测试进程无并发读写时重建，生产代码没有此入口。
        tracker::g_travelRefresh.~TravelRefreshState();
        new (&tracker::g_travelRefresh) tracker::TravelRefreshState();
        tracker::g_explorationBase = image;
        tracker::g_travelAvailable.store(true);
        tracker::g_travelUnlock.store(enabled);
        tracker::g_travelClosePending = false;
        if (enabled) {
            tracker::g_travelRefresh.ToggleRequest();
            const auto ticket = tracker::g_travelRefresh.TryBegin(true);
            tracker::g_travelRefresh.Complete(ticket);
        }
        g_menuFixture = this;
        tracker::g_travelApi = {ExecuteScript, BuildDisplay, ClearList, BuildSpotList, BuildAreaList,
                               SelectList, OnSpotSelection, OnAreaSelection};
        spots = {{{37, 8, 3, 1, 0, 1, 0x91}, {42, 8, 3, static_cast<uint8_t>(enabled), 0, 1, 0x92}}};
        areas = {{{8, 3, 1, 0, {0x81, 0x82}}}};
        flags[0x100 + 6037 / 8] = static_cast<unsigned char>(1u << (6037 % 8));
        Store(image + kFlagsGlobalOffset, reinterpret_cast<uintptr_t>(flags.data()));
        Store(image + 0xC5D778, reinterpret_cast<uintptr_t>(tableRoot.data()));
        Put(tableRoot, 0xF0, reinterpret_cast<uintptr_t>(tableHolder.data()));
        Put(tableHolder, 8, reinterpret_cast<uintptr_t>(tableFile.data()));
        Put(tableFile, 0x10, reinterpret_cast<uintptr_t>(tableBuffer.data()));
        Put(tableFile, 0x20, reinterpret_cast<uintptr_t>(tableHeaders.data()));
        Put(tableFile, 0x28, uint32_t{0}); Put(tableFile, 0x2C, uint32_t{1});
        Put(tableHeaders, 0x44, uint32_t{0x10}); Put(tableHeaders, 0x48, uint32_t{56});
        Put(tableHeaders, 0x4C, uint32_t{1});
        Put(tableHeaders, 0x50 + 0x44, uint32_t{0x100}); Put(tableHeaders, 0x50 + 0x48, uint32_t{152});
        Put(tableHeaders, 0x50 + 0x4C, uint32_t{2});
        Put(tableBuffer, 0x10, uint32_t{8});
        Put(tableBuffer, 0x14, uint32_t{3});
        for (unsigned i = 0; i < 2; ++i) {
            Put(tableBuffer, 0x100 + i * 152, uint32_t{i == 0 ? 37u : 42u});
            Put(tableBuffer, 0x104 + i * 152, uint32_t{3});
            Put(tableBuffer, 0x108 + i * 152, uint32_t{8});
        }
        Store(image + 0xC60E08, reinterpret_cast<uintptr_t>(sceneRoot.data()));
        Put(menu, 8, Manager()); Put(menu, 0x18, Menu()); Put(manager, 0x28, Menu());
        Put(menu, 0x279, uint8_t{1});
        Put(manager, 0x70, reinterpret_cast<uintptr_t>(root.data()));
        Put(manager, 0xC8, reinterpret_cast<uintptr_t>(areas.data())); Put(manager, 0xD0, uint64_t{1});
        Put(manager, 0xE0, reinterpret_cast<uintptr_t>(spots.data())); Put(manager, 0xE8, uint64_t{2});
        Put(manager, 0xF8, uint32_t{3}); Put(manager, 0x104, 0.4f);
        // 已知镜头／缩放邻域填充可辨认数据；原生替身也不触碰该范围。
        for (size_t i = 0; i < originalCamera.size(); ++i) manager[0x100 + i] = static_cast<unsigned char>(i + 7);
        Put(manager, 0x104, 0.4f);
        std::memcpy(originalCamera.data(), manager.data() + 0x100, originalCamera.size());
        for (unsigned i = 0; i < 2; ++i) {
            const auto callback = reinterpret_cast<uintptr_t>(callbacks[i].data());
            Put(lists[i], 0, image + (i ? 0xB0DC28 : 0xB0DC88));
            Put(lists[i], 0xF8, Menu()); Put(lists[i], 0x138, callback);
            Put(callbacks[i], 0, image + 0xB0DB48);
            Put(callbacks[i], 8, image + (i ? 0x3EB210 : 0x3EB2D0));
            Put(callbacks[i], 0x10, Menu());
        }
        Put(menu, 0x1C8, List(0)); Put(menu, 0x1C0, List(1));
        CreateDisplay(enabled);
        PopulateList(0, false); PopulateList(1, true);
        SetState(state, nested);
        Choose(enabled ? 42 : 37);
    }
    void CreateDisplay(bool enabled) {
        auto& rows = displays[bank];
        for (auto& row : rows) row.fill(0);
        // 启用时存在“城镇聚合项 + 入口 + 酒店”；关闭后只剩真实入口且聚合项消失。
        const unsigned count = enabled ? 3 : 1;
        for (unsigned i = 0; i < count; ++i) {
            Put(rows[i], 0, reinterpret_cast<uintptr_t>(icons[i].data()));
            Put(rows[i], 8, uint32_t{enabled && i == 0 ? 0u : 1u});
            // 普通场景中，具有area的独立spot须具备原生列表位0x4才进入总列表。
            Put(rows[i], 0x0C, uint32_t{4});
            Put(rows[i], 0x10, uint32_t{enabled ? (i == 0 ? 8u : (i == 1 ? 37u : 42u)) : 37u});
            Put(rows[i], 0x20, uint8_t{1}); Put(rows[i], 0x21, uint8_t{1});
            Put(icons[i], 0xA0, uint32_t{0xA5});
        }
        Put(root, 0xA0, uint32_t{0xB5});
        Put(manager, 0xB0, Display(0)); Put(manager, 0xB8, uint64_t{count}); Put(manager, 0xC0, uint64_t{3});
    }
    uintptr_t Find(uint32_t id) const {
        const auto count = Load<uint64_t>(Manager() + 0xB8);
        for (uint64_t i = 0; i < count; ++i)
            if (Load<uint32_t>(Display(static_cast<unsigned>(i)) + 0x10) == id) return Display(static_cast<unsigned>(i));
        return 0;
    }
    void PopulateList(unsigned which, bool areaOnly) {
        unsigned count = 0;
        const auto displayCount = Load<uint64_t>(Manager() + 0xB8);
        for (unsigned i = 0; i < displayCount; ++i) {
            if (Load<uint32_t>(Display(i) + 8) != 1) continue;
            const auto id = Load<uint32_t>(Display(i) + 0x10);
            // 子列表严格按静态分组筛选；允许测试把已到访入口移到area=0，验证整个
            // 未到访分组关闭后清单为空的正常情形，而不是让桩总有一个虚构入口。
            if (areaOnly) {
                bool belongs = false;
                for (unsigned row = 0; row < spots.size(); ++row)
                    if (Load<uint32_t>(reinterpret_cast<uintptr_t>(tableBuffer.data()) + 0x100 + row * 152) == id &&
                        Load<uint32_t>(reinterpret_cast<uintptr_t>(tableBuffer.data()) + 0x108 + row * 152) == 8)
                        belongs = true;
                if (!belongs) continue;
            }
            auto& item = items[which][count]; item.fill(0);
            Put(item, 0, image + 0xB0DCF8); Put(item, 0x28, Display(i));
            itemPointers[which][count] = reinterpret_cast<uintptr_t>(item.data()); ++count;
        }
        Put(lists[which], 0x38, reinterpret_cast<uintptr_t>(itemPointers[which].data()));
        Put(lists[which], 0x40, uint64_t{count}); Put(lists[which], 0x70, int32_t{0});
    }
    void Choose(uint32_t id) {
        const auto display = Find(id);
        Put(menu, 0x190, display); Put(menu, 0x198, display);
        for (unsigned which = 0; which < 2; ++which) {
            const auto count = Load<uint64_t>(List(which) + 0x40);
            for (unsigned i = 0; i < count; ++i)
                if (Load<uintptr_t>(itemPointers[which][i] + 0x28) == display)
                    Put(lists[which], 0x70, static_cast<int32_t>(i));
        }
    }
    bool IsCurrentSelection() const {
        const auto address = Load<uintptr_t>(Menu() + 0x190);
        tracker::TravelDisplayRange range{};
        return tracker::DisplayRange(Manager(), range) && tracker::DisplayAddress(range, address);
    }
    bool PreservedCamera() const { return std::memcmp(originalCamera.data(), manager.data() + 0x100, originalCamera.size()) == 0; }

    static uint32_t* ExecuteScript(uintptr_t savedata, uint32_t* output, const char* name,
                                   uintptr_t actor, uintptr_t arguments, uint32_t count) {
        auto& f = *g_menuFixture; ++f.scriptCalls;
        f.result.Check(savedata == reinterpret_cast<uintptr_t>(f.flags.data()) &&
                       std::strcmp(name, "system.MapJumpState") == 0 && !actor && !arguments && !count,
                       "刷新只调用原生 MapJumpState 无参状态重算");
        f.scriptSnapshotEnabled = tracker::g_travelUnlock.load();
        if (f.reenterScript) {
            f.reenterScript = false;
            f.reentrySuppressed = !tracker::Sky2BeforeMapRefresh(f.Menu());
        }
        if (f.requestDuringScript) {
            f.requestDuringScript = false;
            tracker::g_travelRefresh.ToggleRequest();
        }
        // 模拟真实原生重算：此阶段只按保存记录登记/恢复可见状态，再计算最终灰态。
        // 不调用Mod单点helper，否则测试仍会模拟已经移除的旧逐点挂钩而掩盖接线错误。
        for (auto& spot : f.spots) {
            // 保存位读取只存在于这个原生脚本替身；生产统一候选不读取到访位。
            const auto flag = 6000u + spot.id;
            spot.visible = (f.flags[0x100 + flag / 8] & (1u << (flag % 8))) ? 1 : 0;
            spot.registered = 1;
        }
        for (auto& area : f.areas) {
            area.visible = 0;
            for (const auto& spot : f.spots)
                if (spot.area == area.id && spot.visible) area.visible = 1;
        }
        f.spots[0].blocked = f.finalAnchorBlocked ? 1 : 0;
        f.spots[1].blocked = f.finalTargetBlocked ? 1 : 0;
        f.scriptCompleted = true;
        *output = 0;
        return f.failScript ? nullptr : output;
    }
    static void ClearList(uintptr_t object) {
        auto& f = *g_menuFixture; ++f.clearCalls;
        f.result.Check(f.scriptCompleted, "销毁旧列表之前已完成原生剧情状态重算");
        f.result.Check(!Load<uintptr_t>(f.Menu() + 0x190) && !Load<uintptr_t>(f.Menu() + 0x198),
                       "销毁旧引用之前已清空菜单选择和详情指针");
        Store(object + 0x40, uint64_t{0}); Store(object + 0x70, int32_t{-1});
    }
    static void BuildDisplay(uintptr_t object) {
        auto& f = *g_menuFixture; ++f.displayCalls;
        f.result.Check(object == f.Manager() && f.clearCalls == f.displayCalls * 2,
                       "显示数组重建前总列表和子列表均已清理");
        // 显式即时刷新从Mod调用Build，不属于三个原生返回地址；只有线程局部许可已
        // 在完整脚本执行后开启才允许补显。故意传入未核准来源，验证许可确实必需。
        f.result.Check(f.scriptCompleted && tracker::g_travelExplicitBuild &&
                       !tracker::Sky2BeforeBuildTravel(object, f.image + 0x29EE97),
                       "即时刷新仅在剧情重算完成后的线程局部许可范围内调用构建前辅助");
        f.bank ^= 1u;
        f.CreateDisplay(f.spots[1].visible == 1);
        if (f.emptyDisplay) Put(f.manager, 0xB8, uint64_t{0});
        if (f.corruptDisplayCapacity) Put(f.manager, 0xC0, uint64_t{0});
    }
    static void BuildSpotList(uintptr_t object) {
        auto& f = *g_menuFixture; ++f.spotBuilds;
        f.result.Check(object == f.List(0), "总列表重建使用原生总列表对象");
        f.PopulateList(0, false);
        if (f.emptyActiveList) { Put(f.lists[0], 0x40, uint64_t{0}); return; }
        OnSpotSelection(f.Menu());
    }
    static void BuildAreaList(uintptr_t object) {
        auto& f = *g_menuFixture; ++f.areaBuilds;
        const auto temporary = Load<uintptr_t>(f.Menu() + 0x190);
        f.result.Check(object == f.List(1) && temporary && Load<uint32_t>(temporary + 0x10) == 8,
                       "子列表重建从已验证上下文取得正确城镇编号");
        f.PopulateList(1, true);
        if (!f.omitAreaCallback) OnAreaSelection(f.Menu());
    }
    static void SelectList(uintptr_t object, int32_t selected, int32_t previous, bool animate) {
        auto& f = *g_menuFixture; ++f.selectionCalls;
        f.result.Check(selected >= 0 && static_cast<uint64_t>(selected) < Load<uint64_t>(object + 0x40) &&
                       previous == -1 && !animate, "恢复的列表选中索引必须有效且不触发确认动作");
        Store(object + 0x70, selected);
    }
    static void Selection(unsigned which, uintptr_t menuAddress) {
        auto& f = *g_menuFixture;
        f.result.Check(menuAddress == f.Menu(), "选择回调仅更新当前合成菜单");
        const auto selected = Load<int32_t>(f.List(which) + 0x70);
        if (selected < 0 || static_cast<uint64_t>(selected) >= Load<uint64_t>(f.List(which) + 0x40)) return;
        const auto display = Load<uintptr_t>(f.itemPointers[which][selected] + 0x28);
        Put(f.menu, 0x190, display); Put(f.menu, 0x198, display);
    }
    static void OnSpotSelection(uintptr_t menuAddress) { Selection(0, menuAddress); }
    static void OnAreaSelection(uintptr_t menuAddress) { Selection(1, menuAddress); }
};

void TestTravelMenu(TestResult& result, uintptr_t image, uintptr_t inaccessible) {
    TravelMenuFixture fixture(result, image);
    const auto execute = [&] { return tracker::Sky2BeforeMapRefresh(fixture.Menu()); };
    // 两个显示银行地址不同，开启后再选新增酒店并关闭，可发现旧 selection/item 指针残留。
    for (int32_t state : {3, 4, 5}) {
        fixture.Reset(state);
        const auto beforeFlags = fixture.flags;
        tracker::g_travelRefresh.ToggleRequest();
        result.Check(execute(), "三个浏览层均能消费开启请求并跳过本帧原生输入");
        result.Check(fixture.Find(42) && tracker::g_travelRefresh.ReadStatus().appliedEnabled &&
                     !tracker::g_travelRefresh.ReadStatus().pending, "新增设施立即出现在新显示数组且状态已应用");
        result.Check(!tracker::g_travelExplicitBuild, "显式构建返回后立即清除许可，不能泄漏到后续原生初始化");
        result.Check(fixture.PreservedCamera() && Same(beforeFlags, fixture.flags), "即时刷新保留镜头缩放且不写真实旗标");
        result.Check(TravelMenuFixture::Load<uint32_t>(reinterpret_cast<uintptr_t>(fixture.root.data()) + 0xA0) == 0xB4 &&
                     TravelMenuFixture::Load<uint32_t>(reinterpret_cast<uintptr_t>(fixture.icons[0].data()) + 0xA0) == 0xA4 &&
                     TravelMenuFixture::Load<float>(reinterpret_cast<uintptr_t>(fixture.icons[0].data()) + 0x148) == 15.0f,
                     "新图标恢复原生显示标志和比例，不只是生成不可见数组");
        const auto oldSelection = fixture.Find(42);
        fixture.Choose(42);
        tracker::g_travelRefresh.ToggleRequest();
        result.Check(execute(), "关闭辅助可在相同浏览层原地重建");
        result.Check(!fixture.Find(42) && !tracker::g_travelRefresh.ReadStatus().appliedEnabled &&
                     !tracker::g_travelRefresh.ReadStatus().pending, "关闭时未到访酒店立即移除");
        const auto selection = TravelMenuFixture::Load<uintptr_t>(fixture.Menu() + 0x190);
        result.Check(selection != oldSelection && (state == 3 ? !selection : fixture.IsCurrentSelection()),
                     "消失的旧选择不保留；列表恢复有效入口，区域图交回原生选择");
        result.Check(!TravelMenuFixture::Load<uintptr_t>(fixture.Menu() + 0x198), "刷新后详情缓存不保留旧显示项");
        result.Check(fixture.PreservedCamera() && Same(beforeFlags, fixture.flags), "关闭刷新同样保留镜头和真实旗标");
        result.Check(state != 5 || fixture.areaBuilds == 2, "聚合城镇图标消失时活动子列表仍完成关闭重建");
    }
    fixture.Reset(5, false, false);
    tracker::g_travelRefresh.ToggleRequest();
    result.Check(execute() && fixture.IsCurrentSelection(), "区域图直接进入子列表的另一种合法栈同样刷新");

    // 端到端覆盖最终灰态的时序：脚本完成后才补显，不能把早期注册时的临时可用态
    // 当成最终许可；被拒绝的候选仍保持隐藏，但开关请求本身可以安全完成。
    for (bool blockAnchor : {false, true}) {
        fixture.Reset(4);
        fixture.finalAnchorBlocked = blockAnchor;
        fixture.finalTargetBlocked = !blockAnchor;
        const auto beforeFlags = fixture.flags;
        tracker::g_travelRefresh.ToggleRequest();
        result.Check(execute() && fixture.scriptCalls == 1 && fixture.displayCalls == 1 &&
                     (fixture.Find(42) != 0) == blockAnchor && !tracker::g_travelRefresh.ReadStatus().pending,
                     "统一规则保留目标最终灰态，但不再依赖另一个入口的锚点状态");
        result.Check(Same(beforeFlags, fixture.flags), "最终灰态拒绝不更改真实到访及剧情位");
    }

    fixture.Reset(3);
    tracker::g_travelRefresh.ToggleRequest(); tracker::g_travelRefresh.ToggleRequest();
    result.Check(!execute() && fixture.scriptCalls == 0 && !tracker::g_travelRefresh.ReadStatus().pending,
                 "消费前双切换合并，不销毁重建未改变的原生界面");
    for (int32_t unsafeState : {6, 7, 8, 9, 10}) {
        fixture.Reset(3); fixture.SetState(unsafeState);
        tracker::g_travelRefresh.ToggleRequest();
        result.Check(!execute() && fixture.scriptCalls == 0 && tracker::g_travelRefresh.ReadStatus().pending,
                     "确认或转场阶段不执行任何原生重建，保留请求");
        fixture.SetState(3);
        result.Check(execute() && !tracker::g_travelRefresh.ReadStatus().pending,
                     "回到安全浏览阶段自动应用等待请求");
    }
    fixture.Reset(3); fixture.reenterScript = true;
    tracker::g_travelRefresh.ToggleRequest();
    result.Check(execute() && fixture.reentrySuppressed && fixture.scriptCalls == 1 && fixture.displayCalls == 1,
                 "原生脚本同步重入不会开始第二次刷新");
    fixture.Reset(3); fixture.requestDuringScript = true;
    tracker::g_travelRefresh.ToggleRequest();
    result.Check(execute() && fixture.scriptSnapshotEnabled && fixture.Find(42) &&
                 tracker::g_travelRefresh.ReadStatus().pending, "重建中的新请求不改变当前事务固定目标");
    result.Check(execute() && !fixture.Find(42) && !tracker::g_travelRefresh.ReadStatus().pending,
                 "下一安全帧应用重建期间收到的新请求");

    const auto rejected = [&](const char* label, auto alter) {
        fixture.Reset(4); alter(); tracker::g_travelRefresh.ToggleRequest();
        const auto beforeMenu = fixture.menu;
        result.Check(!execute() && fixture.scriptCalls == 0 && Same(beforeMenu, fixture.menu) &&
                     tracker::g_travelRefresh.ReadStatus().pending, label);
    };
    rejected("未知列表虚表拒绝刷新且不改菜单", [&] { Put(fixture.lists[0], 0, uintptr_t{0}); });
    rejected("错误回调所有者拒绝刷新", [&] { Put(fixture.callbacks[0], 0x10, uintptr_t{0}); });
    rejected("不可读菜单管理器由受保护读取拒绝", [&] { Put(fixture.menu, 8, inaccessible); });
    rejected("当前活动列表为空时保持请求，不调用危险原生重建", [&] { Put(fixture.lists[0], 0x40, uint64_t{0}); });
    // 统一规则不要求真实到访锚点；地图上下文改由原生对象互指和静态表校验。
    fixture.Reset(4); fixture.flags.fill(0);
    tracker::g_travelRefresh.ToggleRequest();
    result.Check(execute() && fixture.Find(42) && !tracker::g_travelRefresh.ReadStatus().pending,
                 "没有任何真实到访锚点时仍可补显已登记非灰目的地");

    fixture.Reset(3); fixture.flags.fill(0);
    Put(fixture.manager, 0xB8, uint64_t{0});
    Put(fixture.menu, 0x190, uintptr_t{0}); Put(fixture.menu, 0x198, uintptr_t{0});
    for (auto& list : fixture.lists) Put(list, 0x40, uint64_t{0});
    tracker::g_travelRefresh.ToggleRequest();
    result.Check(execute() && fixture.Find(42) && !tracker::g_travelRefresh.ReadStatus().pending &&
                 tracker::g_travelAvailable.load(),
                 "纯地图浏览初始零个到访和显示项时仍可开启，活动列表非空限制不被放宽");

    // 模拟原生主列表中已有area=0入口，而整个area=8都是功能补显出来的未到访点。
    // 开启要同时补分组；在这个新子列表关闭功能时必须正常关图，不能调用空列表回调。
    fixture.Reset(4);
    fixture.spots[0].area = 0; fixture.areas[0].visible = 0;
    Put(fixture.tableBuffer, 0x108, uint32_t{0});
    const auto unvisitedAreaFlags = fixture.flags;
    tracker::g_travelRefresh.ToggleRequest();
    result.Check(execute() && fixture.Find(42) && fixture.areas[0].visible == 1,
                 "此前未发现的整个分组随原生许可目的地一起临时显示");
    fixture.SetState(5); fixture.PopulateList(1, true); fixture.Choose(42);
    tracker::g_travelRefresh.ToggleRequest();
    result.Check(execute() && fixture.areaBuilds == 0 && fixture.spotBuilds == 1 &&
                 TravelMenuFixture::Load<int32_t>(fixture.Menu() + 0xD4) == 17 &&
                 tracker::g_travelAvailable.load() && !tracker::g_travelRefresh.ReadStatus().appliedEnabled &&
                 !tracker::g_travelRefresh.ReadStatus().pending,
                 "关闭后活动未到访分组为空则正常退出地图，功能仍可使用且不调用空列表构建");
    result.Check(!TravelMenuFixture::Load<uintptr_t>(fixture.Menu() + 0x190) &&
                 !TravelMenuFixture::Load<uintptr_t>(fixture.Menu() + 0x198) &&
                 Same(unvisitedAreaFlags, fixture.flags) && fixture.PreservedCamera(),
                 "整个分组消失后不残留选择指针、不修改真实到访及镜头");

    fixture.Reset(4); fixture.failScript = true;
    tracker::g_travelRefresh.ToggleRequest();
    result.Check(execute() && fixture.scriptCalls == 1 && fixture.displayCalls == 0 && fixture.clearCalls == 0 &&
                 !tracker::g_travelAvailable.load() && !tracker::g_travelUnlock.load(),
                 "脚本返回验证失败时停用辅助，不销毁原GUI也不继续接受传送输入");
    result.Check(TravelMenuFixture::Load<int32_t>(fixture.Menu() + 0xC8) == 17,
                 "脚本返回失败同样请求原生关闭状态，避免缓存与旧显示不一致");
    const auto closeAfterFailure = [&](const char* label, auto alter) {
        fixture.Reset(5, true); alter(); tracker::g_travelRefresh.ToggleRequest();
        const auto beforeFlags = fixture.flags;
        result.Check(execute() && !tracker::g_travelAvailable.load(), label);
        const auto depth = TravelMenuFixture::Load<int32_t>(fixture.Menu() + 0xE8);
        result.Check(TravelMenuFixture::Load<int32_t>(fixture.Menu() + 0xBC + depth * 12) == 17 &&
                     !TravelMenuFixture::Load<uintptr_t>(fixture.Menu() + 0x190) &&
                     !TravelMenuFixture::Load<uintptr_t>(fixture.Menu() + 0x198),
                     "后置失败清除选择并请求原生关闭状态，不在更新调用中直接销毁菜单");
        result.Check(Same(beforeFlags, fixture.flags), "失败路径同样不修改真实到访旗标");
    };
    // 真正结构损坏仍故障停用；“关闭后合法空显示数组”则属于正常状态，不混为一谈。
    closeAfterFailure("新显示数组容量矛盾时安全关闭并停用刷新", [&] { fixture.corruptDisplayCapacity = true; });
    closeAfterFailure("重建的活动列表为空时安全关闭并停用刷新", [&] { fixture.emptyActiveList = true; });
    closeAfterFailure("子列表回调未移走栈上下文时安全关闭", [&] { fixture.omitAreaCallback = true; });

    for (bool initiallyEnabled : {false, true}) {
        fixture.Reset(4, initiallyEnabled); fixture.emptyDisplay = true;
        tracker::g_travelRefresh.ToggleRequest();
        result.Check(execute() && tracker::g_travelAvailable.load() && fixture.spotBuilds == 0 &&
                     !tracker::g_travelRefresh.ReadStatus().pending &&
                     tracker::g_travelRefresh.ReadStatus().appliedEnabled == !initiallyEnabled &&
                     TravelMenuFixture::Load<int32_t>(fixture.Menu() + 0xC8) == 17,
                     "原生重算得到合法空显示数组时正常退出，不构建空总列表且功能仍可用");
    }

    // 这些反例发生在调用原生列表构建函数之前，区别于上面的“构建后才返回空”。
    // 原生构建末尾可能直接读取第0项，所以必须先证明其自身过滤规则至少产生一项。
    fixture.Reset(4, true);
    Put(fixture.tableBuffer, 0x100 + 0x58, uint32_t{8});
    tracker::g_travelRefresh.ToggleRequest();
    result.Check(execute() && tracker::g_travelAvailable.load() && fixture.spotBuilds == 0 &&
                 fixture.Find(37) && !fixture.Find(8),
                 "关闭后入口被原生表过滤且聚合项消失时，空总列表正常关闭且不调用其构建函数");

    fixture.Reset(4, true);
    Put(fixture.tableBuffer, 0x100 + 152, uint32_t{999});
    tracker::g_travelRefresh.ToggleRequest();
    result.Check(!execute() && fixture.scriptCalls == 0 && fixture.spotBuilds == 0 && fixture.areaBuilds == 0 &&
                 tracker::g_travelRefresh.ReadStatus().pending,
                 "显示中的spot在静态表缺失时，前置检查保留请求且不调用任何原生重建");

    fixture.Reset(5, true);
    Put(fixture.tableBuffer, 0x108, uint32_t{9});
    tracker::g_travelRefresh.ToggleRequest();
    result.Check(execute() && tracker::g_travelAvailable.load() && fixture.areaBuilds == 0,
                 "关闭后仅剩其他分组入口时，活动分组零项正常关图且不调用子列表构建");
    tracker::g_travelApi = {};
    g_menuFixture = nullptr;
}
}
