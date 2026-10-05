// 使用真实业务页面和假的绘制控件/业务端点，验证点击路由不能绕过二次确认。
// 游戏上下文全部是测试值对象；QueueRevisitTravel 只计数，不读写游戏或玩家记录。
#include "tracker.h"
#include "exploration.h"
#include "revisit.h"
#include "panel_pages.h"
#include "controller_logic.h"
#include <cstring>
#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
namespace fixture {
std::string click;
std::vector<std::string> controls;
std::vector<std::string> cards;
tracker::Counts counts;
tracker::ExplorationStatus exploration;
tracker::RevisitReturnStatus returned;
tracker::RevisitNativeContext context;
tracker::RevisitNativeStatus status;
tracker::RevisitMapPreparationPhase preparation = tracker::RevisitMapPreparationPhase::Idle;
bool targetAllowed = true;
int submitted = 0, cancelled = 0;
int preparationRequests = 0;
int contextReads = 0;
uint32_t lastTarget = 0;
float listHeight = 0;
float mainWidth = 800, mainHeight = 600;
int cardDepth = 0, columnDepth = 0, layoutColumns = 2, progresses = 0;
int tabBarCalls = 0, requestedPage = -1, observedPage = -1;
bool Consume(const char* id) { controls.emplace_back(id); if (click == id) { click.clear(); return true; } return false; }
void Text(const char*) {}
void Void() {}
void Disabled(int32_t) {}
int32_t Button(const char* id, const char*) { return Consume(id); }
int32_t Checkbox(const char* id, const char*, int32_t* value) {
    if (!Consume(id)) return 0;
    *value = !*value; return 1;
}
int32_t Selectable(const char* id, const char*, int32_t) { return Consume(id); }
int32_t Begin(const char*, float height) { listHeight = height; return 1; }
void ContentSize(float* width, float* height) { if (width) *width = mainWidth; if (height) *height = mainHeight; }
int32_t Input(const char*, const char*, char*, uint32_t) { return 0; }
// 布局替身不模拟 ImGui，只检查实际页面是否正确结束卡片/列，以及折叠说明后
// 必需操作仍然存在。尺寸和绘制效果由共享 WARP 截图夹具检验。
void Section(const char*, const char*) {}
void BeginCard(const char* id) { cards.emplace_back(id); ++cardDepth; }
void EndCard() { assert(cardDepth > 0); --cardDepth; }
int32_t Columns(const char*, float) { ++columnDepth; return layoutColumns; }
void EndColumns() { assert(columnDepth > 0); --columnDepth; }
void Status(const char*, int32_t) {}
void Progress(float, const char*) { ++progresses; }
int32_t Disclosure(const char*, const char*, int32_t) { return 0; }
int32_t TabBar(const char* id, const char* const* labels, int32_t count, int32_t selected) {
    assert(std::strcmp(id, "page") == 0 && (count == 1 || count == 2) && labels);
    ++tabBarCalls; observedPage = selected;
    // 覆盖值模拟独立窗口 LT/RT 的原子结果；原按钮 ID 继续供已有鼠标/截图测试使用。
    if (requestedPage >= 0) { const int value = requestedPage; requestedPage = -1; return value; }
    constexpr const char* ids[]{"page.overview", "page.chests", "page.settings", "page.travel", "page.return"};
    for (int index = 0; index < count; ++index) if (Consume(ids[index])) return index;
    return selected;
}

}
namespace tracker {
HMODULE g_module = nullptr;
std::atomic<bool> g_enabled{true}, g_panel{true};
std::atomic<Mode> g_mode{Mode::Current};
Counts ReadCounts() { return fixture::counts; }
void Log(const char*) noexcept {}
void ToggleExploration(ExplorationFeature feature) noexcept {
    if (feature == ExplorationFeature::MapReveal && fixture::exploration.mapAvailable)
        fixture::exploration.mapEnabled = !fixture::exploration.mapEnabled;
    if (feature == ExplorationFeature::TravelUnlock && fixture::exploration.travelAvailable) {
        fixture::exploration.travelRequested = !fixture::exploration.travelRequested;
        fixture::exploration.travelPending = true;
    }
}
ExplorationStatus ReadExplorationStatus() noexcept { return fixture::exploration; }
bool RevisitReady() noexcept { return true; }
bool RevisitContextAllowed(const RevisitNativeContext& context) noexcept { return context.valid; }
bool RevisitRecoveryRequired(const RevisitNativeContext&) noexcept { return false; }
RevisitNativeContext ReadRevisitNativeContext() noexcept { ++fixture::contextReads; return fixture::context; }
RevisitNativeStatus ReadRevisitNativeStatus() noexcept { return fixture::status; }
RevisitMapPreparationPhase ReadRevisitMapPreparation() noexcept { return fixture::preparation; }
bool QueueRevisitNativeMapPreparation(const RevisitNativeContext&) noexcept {
    ++fixture::preparationRequests; fixture::preparation = RevisitMapPreparationPhase::Queued; return true;
}
void CancelRevisitNativeTravel() noexcept { ++fixture::cancelled; fixture::preparation = RevisitMapPreparationPhase::Idle; }
bool RevisitTargetAllowed(uint32_t, const RevisitNativeContext&) noexcept { return fixture::targetAllowed; }
RevisitReturnStatus ReadRevisitReturnStatus(const RevisitNativeContext&) noexcept {
    auto result = fixture::returned; result.storageReady = true; return result;
}
void CycleRevisitReturnRecord(const RevisitNativeContext&) noexcept {}
bool QueueRevisitTravel(uint32_t target, uint64_t, const RevisitNativeContext&) noexcept { ++fixture::submitted; fixture::lastTarget = target; return true; }
const char* RevisitNativeTargetReason(uint32_t, const RevisitNativeContext&) noexcept { return "fixture unavailable"; }
}

// 离屏截图复用同一组值对象替身；普通回归直接调用正式页面和输入动作分发。
#ifndef SKY2_STANDALONE_VISUAL_FIXTURE
int main() {
    using namespace tracker;
    Sky2UiApi ui{}; ui.size = sizeof(ui);
    ui.text = &fixture::Text; ui.text_wrapped = &fixture::Text;
    ui.separator = &fixture::Void; ui.same_line = &fixture::Void; ui.spacing = &fixture::Void;
    ui.button = &fixture::Button; ui.checkbox = &fixture::Checkbox; ui.selectable = &fixture::Selectable;
    ui.begin_disabled = &fixture::Disabled; ui.end_disabled = &fixture::Void;
    ui.begin_child = &fixture::Begin; ui.end_child = &fixture::Void; ui.input_text = &fixture::Input;
    ui.tab_bar = &fixture::TabBar;
    ui.section = &fixture::Section; ui.begin_card = &fixture::BeginCard; ui.end_card = &fixture::EndCard;
    ui.columns = &fixture::Columns; ui.next_column = &fixture::Void; ui.end_columns = &fixture::EndColumns;
    ui.tab = &fixture::Selectable; ui.status = &fixture::Status; ui.progress = &fixture::Progress;
    ui.disclosure = &fixture::Disclosure; ui.content_size = &fixture::ContentSize;
    SetPanelUi(&ui); assert(PanelUiReady() && &PanelUi() == &ui);
    fixture::context.available = fixture::context.valid = fixture::context.browsing = true;
    fixture::context.busy = false; fixture::context.returnPointReady = true;
    fixture::context.browseIdentity = 12; fixture::context.chapter = 4;
    std::memcpy(fixture::context.scene, "mp0001", sizeof("mp0001"));
    fixture::counts.valid = true;
    Sky2Frame frame{sizeof(Sky2Frame), 1920, 1080, 1, 1000, 1, 1, 1, 0};
    const auto draw = [&](int tab, const char* control = "") {
        ++frame.time_ms; fixture::requestedPage = tab; fixture::click = control; fixture::controls.clear(); fixture::cards.clear();
        TickPanel(&frame); const int before = fixture::contextReads;
        fixture::tabBarCalls = 0;
        frame.header_drawn = 0; DrawPanelHeader(&frame);
        assert(before == fixture::contextReads);
        frame.header_drawn = 1; DrawPanelPage(&frame);
        assert(fixture::tabBarCalls == 1 && fixture::cardDepth == 0 && fixture::columnDepth == 0);
    };
    // 直达动作只能切换业务开关或打开页面，不能提交原生传送。
    ApplyPanelActions(ToggleEnabled); assert(!g_enabled.load());
    ApplyPanelActions(ToggleEnabled); assert(g_enabled.load());
    ApplyPanelActions(ToggleMode); assert(g_mode.load() == Mode::Inherited);
    ApplyPanelActions(ToggleMode); assert(g_mode.load() == Mode::Current);
    fixture::exploration.mapAvailable = fixture::exploration.travelAvailable = true;
    ApplyPanelActions(ToggleMapReveal | ToggleTravelUnlock);
    assert(fixture::exploration.mapEnabled && fixture::exploration.travelRequested && fixture::exploration.travelPending);
    SetStandaloneHudVisible(false); assert(!StandaloneHudVisible());
    SetStandaloneHudVisible(true); assert(StandaloneHudVisible());
    g_panel.store(false); ApplyPanelActions(ToggleRevisit);
    assert(g_panel.load() && StandalonePageIndex() == 3 && fixture::submitted == 0);
    StandaloneChoosePage(0);
    draw(1); assert(StandalonePageIndex() == 1);
    draw(2); assert(StandalonePageIndex() == 1); // 组内非法 Tab 不可越界到设置。
    StandaloneChoosePage(2); draw(0); assert(StandalonePageIndex() == 2);
    draw(1); assert(StandalonePageIndex() == 2);
    // 地图准备是独立的显式动作，不能在进入页面、被禁用或后台时自动提交；
    // 准备排队期间重复点击也不能重复调用游戏入口，完成后仍要原样确认两次。
    StandaloneChoosePage(3); fixture::context.browsing = false;
    fixture::context.canPrepareMap = true; draw(-1); assert(fixture::preparationRequests == 0);
    draw(-1, "travel.prepare_map"); assert(fixture::preparationRequests == 1 && fixture::submitted == 0);
    draw(-1, "travel.prepare_map"); assert(fixture::preparationRequests == 1);
    draw(-1, "travel.confirm"); assert(fixture::submitted == 0);
    StandaloneChoosePage(4); assert(fixture::preparation == RevisitMapPreparationPhase::Idle);
    frame.foreground = 0; draw(-1, "travel.prepare_map"); assert(fixture::preparationRequests == 1);
    frame.foreground = 1; fixture::context.canPrepareMap = false;
    draw(-1, "travel.prepare_map"); assert(fixture::preparationRequests == 1);
    fixture::context.canPrepareMap = true; fixture::context.busy = true;
    draw(-1, "travel.prepare_map"); assert(fixture::preparationRequests == 1);
    fixture::context.busy = false; draw(-1, "travel.prepare_map"); assert(fixture::preparationRequests == 2);
    PanelVisibilityChanged(0); assert(fixture::preparation == RevisitMapPreparationPhase::Idle);
    StandaloneChoosePage(2); draw(-1, "exploration.prepare_map");
    assert(fixture::preparationRequests == 3 && fixture::submitted == 0);
    draw(-1, "exploration.prepare_map"); assert(fixture::preparationRequests == 3);
    fixture::context.browsing = true; fixture::context.canPrepareMap = false;
    // 两次确认、切页与后台均走同一正式页面；替身即使忽略 disabled 也不能提交。
    StandaloneChoosePage(3); draw(0, "travel.confirm"); assert(fixture::submitted == 0);
    draw(1, "travel.confirm"); assert(StandalonePageIndex() == 4 && fixture::submitted == 0);
    draw(-1, "travel.confirm"); assert(fixture::submitted == 1 && fixture::lastTarget == kRevisitReturnTarget);
    draw(-1, "travel.confirm"); StandaloneChoosePage(0); StandaloneChoosePage(4);
    draw(-1, "travel.confirm"); assert(fixture::submitted == 1);
    frame.foreground = 0; draw(0, "travel.confirm");
    assert(StandalonePageIndex() == 4 && fixture::submitted == 1);
    assert(std::find(fixture::controls.begin(), fixture::controls.end(), "travel.confirm") != fixture::controls.end());
    frame.foreground = 1; draw(-1, "travel.confirm"); assert(fixture::submitted == 1);
    draw(-1, "travel.confirm"); assert(fixture::submitted == 2);
    draw(-1, "travel.confirm"); PanelVisibilityChanged(0);
    draw(-1, "travel.confirm"); assert(fixture::submitted == 2);
    ++fixture::context.browseIdentity; draw(-1, "travel.confirm"); assert(fixture::submitted == 2);
    draw(-1, "travel.confirm"); assert(fixture::submitted == 3);
    fixture::targetAllowed = false; draw(-1, "travel.confirm"); draw(-1, "travel.confirm");
    assert(fixture::submitted == 3); fixture::targetAllowed = true;
    fixture::context.browsing = false; draw(-1, "travel.confirm"); draw(-1, "travel.confirm");
    assert(fixture::submitted == 3); fixture::context.browsing = true;
    draw(-1, "travel.confirm"); assert(fixture::submitted == 3);
    draw(-1, "travel.confirm"); assert(fixture::submitted == 4);
    StandaloneChoosePage(-1); StandaloneChoosePage(5); assert(StandalonePageIndex() == 4);
    // 概览、清单、设置仍分离；单列与双列都结束全部卡片，隐藏说明不隐藏必需操作。
    for (const int columns : {1, 2}) {
        fixture::layoutColumns = columns;
        for (int page = 0; page < 5; ++page) {
            StandaloneChoosePage(page); draw(-1);
            assert(std::find(fixture::controls.begin(), fixture::controls.end(), "chests.mode.current") == fixture::controls.end());
            assert(std::find(fixture::controls.begin(), fixture::controls.end(), "chests.mode.inherited") == fixture::controls.end());
        }
    }
    StandaloneChoosePage(0); draw(-1);
    assert(std::find(fixture::controls.begin(), fixture::controls.end(), "chests.markers") == fixture::controls.end());
    assert(std::find(fixture::controls.begin(), fixture::controls.end(), "overview.list") == fixture::controls.end());
    assert(std::find(fixture::controls.begin(), fixture::controls.end(), "overview.settings") == fixture::controls.end());
    assert(std::find(fixture::cards.begin(), fixture::cards.end(), "overview.state") == fixture::cards.end());
    StandaloneChoosePage(2); draw(-1, "chests.markers"); assert(!g_enabled.load());
    draw(-1, "chests.markers"); assert(g_enabled.load());
    StandaloneChoosePage(1); SetStandaloneDisplayMode(Mode::Inherited); draw(-1); assert(g_mode.load() == Mode::Inherited);
    SetStandaloneDisplayMode(Mode::Current); draw(-1); assert(g_mode.load() == Mode::Current);
    SetStandaloneDisplayMode(static_cast<Mode>(7)); assert(g_mode.load() == Mode::Current);
    // 自适应高度来自固定 Main；无效尺寸仍有有限回退，不随内部滚动条伸缩。
    frame.scale = 1.5f; fixture::mainHeight = 400; draw(-1); const float shortList = fixture::listHeight;
    assert(shortList == 120.0f * frame.scale);
    fixture::mainHeight = 900; draw(-1); assert(fixture::listHeight > shortList && fixture::listHeight <= 520.0f * frame.scale);
    fixture::mainHeight = 0; draw(-1); assert(fixture::listHeight == 250.0f * frame.scale);
    fixture::mainHeight = 600; fixture::layoutColumns = 1; StandaloneChoosePage(3); draw(-1);
    const float singleColumnList = fixture::listHeight;
    fixture::layoutColumns = 2; draw(-1); assert(fixture::listHeight > singleColumnList);
    // 精简绘制表仍可在 Main 内补绘当前组页签，不越界访问可选函数表尾部。
    ui.tab_bar = nullptr; frame.header_drawn = 0; StandaloneChoosePage(0);
    fixture::click = "page.chests"; DrawPanelPage(&frame); assert(StandalonePageIndex() == 1);
    SetPanelUi(nullptr); assert(!PanelUiReady()); TickPanel(&frame); DrawPanelPage(&frame);
    std::cout << "Standalone page actions, layout and confirmation checks passed.\n";
}
#endif
