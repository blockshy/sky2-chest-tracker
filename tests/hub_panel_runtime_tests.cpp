// 使用真实 Hub 页面和假的宿主控件/业务端点，验证点击路由不能绕过二次确认。
// 游戏上下文全部是测试值对象；QueueRevisitTravel 只计数，不读写游戏或玩家记录。
#include "tracker.h"
#include "exploration.h"
#include "revisit.h"
#include "hub_panel.h"
#include "hosted_activity.h"
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
#ifdef SKY2_MODULE_VISUAL_FIXTURE
// 截图仅链接真实公共绘制层和本文件的纯值替身，不链接游戏挂钩或文件写入实现。
#include "../../sky2-mod-hub/tests/visual_capture.h"
#include <imgui_internal.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <cmath>
#endif

namespace fixture {
std::string click;
std::vector<Sky2Action> actions;
std::vector<std::string> controls;
tracker::Counts counts;
tracker::ExplorationStatus exploration;
tracker::RevisitReturnStatus returned;
tracker::RevisitNativeContext context;
tracker::RevisitNativeStatus status;
bool targetAllowed = true;
bool pauseAllowed = true, tripActive = false, nativePending = false, nativeAccepting = true;
int explorationPause = 0;
int submitted = 0, cancelled = 0, opened = 0;
int contextReads = 0;
uint32_t lastTarget = 0;
float listHeight = 0;
float mainWidth = 800, mainHeight = 600;
int cardDepth = 0, columnDepth = 0, layoutColumns = 2, progresses = 0;
int displayLanguage = 0;
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
void BeginCard(const char*) { ++cardDepth; }
void EndCard() { assert(cardDepth > 0); --cardDepth; }
int32_t Columns(const char*, float) { ++columnDepth; return layoutColumns; }
void EndColumns() { assert(columnDepth > 0); --columnDepth; }
void Status(const char*, int32_t) {}
void Progress(float, const char*) { ++progresses; }
int32_t Disclosure(const char*, const char*, int32_t) { return 0; }
int32_t TabBar(const char* id, const char* const* labels, int32_t count, int32_t selected) {
    assert(std::strcmp(id, "page") == 0 && count == 5 && labels);
    ++tabBarCalls; observedPage = selected;
    // 覆盖值模拟宿主 LT/RT 的原子结果；原按钮 ID 继续供已有鼠标/截图测试使用。
    if (requestedPage >= 0) { const int value = requestedPage; requestedPage = -1; return value; }
    constexpr const char* ids[]{"page.overview", "page.chests", "page.settings", "page.travel", "page.return"};
    for (int index = 0; index < count; ++index) if (Consume(ids[index])) return index;
    return selected;
}
int32_t Register(void*, const Sky2Action* action) { actions.push_back(*action); return 1; }
void Open(void*) { ++opened; }
int32_t Language() { return displayLanguage; }
void Invoke(const char* id) {
    for (const auto& action : actions) if (std::strcmp(action.id, id) == 0) { action.invoke(action.user); return; }
    assert(false);
}
int32_t State(const char* id) {
    for (const auto& action : actions) if (std::strcmp(action.id, id) == 0) {
        assert(action.size >= offsetof(Sky2Action, state_now) + sizeof(action.state_now));
        assert(action.state_now);
        return action.state_now(action.user);
    }
    assert(false); return -1;
}
}
const Sky2HostApi* Sky2Hub_Host = nullptr;
namespace tracker {
HMODULE g_module = nullptr;
std::atomic<bool> g_enabled{true}, g_panel{true};
std::atomic<Mode> g_mode{Mode::Current};
Counts ReadCounts() { return fixture::counts; }
void Log(const char*) noexcept {}
void ToggleExploration(ExplorationFeature) noexcept {}
ExplorationStatus ReadExplorationStatus() noexcept { return fixture::exploration; }
bool RequestHostedTravelEnabled(bool enabled) noexcept {
    fixture::exploration.travelRequested = enabled;
    fixture::exploration.travelPending = enabled != fixture::exploration.travelEnabled;
    return true;
}
int HostedExplorationPauseStatus() noexcept { return fixture::explorationPause; }
bool TryPauseHostedNativeTravel() noexcept {
    if (!fixture::pauseAllowed) return false;
    fixture::nativeAccepting = false; return true;
}
void ResumeHostedNativeTravel() noexcept { fixture::nativeAccepting = true; }
bool HostedNativeTravelPausePending() noexcept { return fixture::nativePending; }
bool HostedRevisitTripActive() noexcept { return fixture::tripActive; }
bool RevisitReady() noexcept { return true; }
bool RevisitContextAllowed(const RevisitNativeContext& context) noexcept { return context.valid; }
bool RevisitRecoveryRequired(const RevisitNativeContext&) noexcept { return false; }
RevisitNativeContext ReadRevisitNativeContext() noexcept { ++fixture::contextReads; return fixture::context; }
RevisitNativeStatus ReadRevisitNativeStatus() noexcept { return fixture::status; }
void CancelRevisitNativeTravel() noexcept { ++fixture::cancelled; }
bool RevisitTargetAllowed(uint32_t, const RevisitNativeContext&) noexcept { return fixture::targetAllowed; }
RevisitReturnStatus ReadRevisitReturnStatus(const RevisitNativeContext&) noexcept {
    auto result = fixture::returned; result.storageReady = true; return result;
}
void CycleRevisitReturnRecord(const RevisitNativeContext&) noexcept {}
bool QueueRevisitTravel(uint32_t target, uint64_t, const RevisitNativeContext&) noexcept { ++fixture::submitted; fixture::lastTarget = target; return true; }
const char* RevisitNativeTargetReason(uint32_t, const RevisitNativeContext&) noexcept { return "fixture unavailable"; }
}

// 截图目标可复用真实页面和本文件的业务替身；普通回归仍由以下 main 独立运行。
#ifndef SKY2_MODULE_VISUAL_FIXTURE
int main() {
    Sky2UiApi ui{}; ui.size = sizeof(ui);
    ui.text = &fixture::Text; ui.text_wrapped = &fixture::Text;
    ui.separator = &fixture::Void; ui.same_line = &fixture::Void; ui.spacing = &fixture::Void;
    ui.button = &fixture::Button; ui.checkbox = &fixture::Checkbox; ui.selectable = &fixture::Selectable;
    ui.begin_disabled = &fixture::Disabled; ui.end_disabled = &fixture::Void;
    ui.begin_child = &fixture::Begin; ui.end_child = &fixture::Void; ui.input_text = &fixture::Input;
    Sky2HostApi host{}; host.size = sizeof(host); host.abi = SKY2_HUB_ABI; host.ui = &ui;
    host.register_action = &fixture::Register; host.open_page = &fixture::Open; host.language = &fixture::Language;
    Sky2Hub_Host = &host;
    assert(tracker::RegisterHubActions());
    assert(fixture::actions.size() == 8);
    for (const auto& action : fixture::actions)
        assert(std::strstr(action.id, "confirm") == nullptr);
    // 首页查询必须实时反映同一个业务状态，不能缓存上次点击或将查询变成切换。
    assert(fixture::State("chest.toggle_markers") == 1);
    fixture::Invoke("chest.toggle_markers");
    assert(fixture::State("chest.toggle_markers") == 0);
    assert(fixture::State("chest.toggle_markers") == 0 && !tracker::g_enabled.load());
    fixture::Invoke("chest.toggle_markers");
    assert(fixture::State("chest.toggle_hud") == 1);
    fixture::Invoke("chest.toggle_hud"); assert(fixture::State("chest.toggle_hud") == 0);
    fixture::Invoke("chest.toggle_hud"); assert(fixture::State("chest.toggle_hud") == 1);
    fixture::exploration.mapEnabled = true;
    assert(fixture::State("chest.toggle_map_reveal") == 1);
    fixture::exploration.mapEnabled = false;
    assert(fixture::State("chest.toggle_map_reveal") == 0);
    // 安全刷新尚未消费时，无论开启还是关闭，都显示待生效；消费后读取实际值。
    fixture::exploration.travelRequested = true; fixture::exploration.travelPending = true;
    assert(fixture::State("chest.toggle_unvisited") == 2);
    fixture::exploration.travelEnabled = true; fixture::exploration.travelPending = false;
    assert(fixture::State("chest.toggle_unvisited") == 1);
    fixture::exploration.travelRequested = false; fixture::exploration.travelPending = true;
    assert(fixture::State("chest.toggle_unvisited") == 2);
    fixture::exploration.travelEnabled = false; fixture::exploration.travelPending = false;
    assert(fixture::State("chest.toggle_unvisited") == 0);
    for (const auto* id : {"chest.cycle_mode", "chest.open_list", "chest.open_travel", "chest.open_return"})
        assert(fixture::State(id) == -1);
    assert(fixture::opened == 0 && fixture::submitted == 0 && fixture::cancelled == 0);
    fixture::context.available = true; fixture::context.valid = true; fixture::context.browsing = true;
    fixture::context.busy = false; fixture::context.returnPointReady = true;
    fixture::context.browseIdentity = 12; fixture::context.chapter = 4;
    std::memcpy(fixture::context.scene, "mp0001", sizeof("mp0001"));
    fixture::Invoke("chest.open_travel");
    assert(fixture::opened == 1 && fixture::submitted == 0);
    Sky2Frame frame{sizeof(Sky2Frame), 1920, 1080, 1, 1000, 1, 1, 1, 0};
    const auto click = [&](const char* id) {
        ++frame.time_ms; fixture::click = id; fixture::controls.clear();
        tracker::HubTick(&frame); tracker::HubDrawPage(&frame);
    };
    click("travel.confirm"); assert(fixture::submitted == 0);
    click("travel.confirm"); assert(fixture::submitted == 1);
    // 离页后必须重新两次点击；即使测试宿主忽略 begin_disabled，也不能绕过业务守卫。
    click("travel.confirm");
    tracker::HubVisibilityChanged(0);
    click("travel.confirm"); assert(fixture::submitted == 1);
    fixture::targetAllowed = false;
    click("travel.confirm"); assert(fixture::submitted == 1);
    fixture::context.browsing = false;
    fixture::targetAllowed = true;
    click("travel.confirm"); click("travel.confirm"); assert(fixture::submitted == 1);
    fixture::context.browsing = true;
    click("travel.confirm"); assert(fixture::submitted == 1);
    // 原生浏览身份变化后，旧确认不能应用于新菜单。
    ++fixture::context.browseIdentity;
    click("travel.confirm"); assert(fixture::submitted == 1);
    click("travel.confirm"); assert(fixture::submitted == 2);
    click("travel.confirm");
    click("page.chests"); click("page.travel");
    click("travel.confirm"); assert(fixture::submitted == 2);
    click("travel.confirm"); assert(fixture::submitted == 3);
    // 新的返程页与目的地页共享安全队列，但切换页必须撤销已建立的确认。
    click("travel.confirm"); click("page.return");
    click("travel.confirm"); assert(fixture::submitted == 3);
    click("travel.confirm"); assert(fixture::submitted == 4 && fixture::lastTarget == tracker::kRevisitReturnTarget);
    click("page.travel"); click("travel.confirm"); assert(fixture::submitted == 4);
    click("travel.confirm"); assert(fixture::submitted == 5 && fixture::lastTarget != tracker::kRevisitReturnTarget);
    // 概览不再夹带设置开关；设置页保留真实开关，清单页保留统计口径切换。
    click("page.overview");
    assert(std::find(fixture::controls.begin(), fixture::controls.end(), "chests.markers") == fixture::controls.end());
    click("page.settings"); click("chests.markers"); assert(!tracker::g_enabled.load());
    click("chests.markers"); assert(tracker::g_enabled.load());
    click("chests.mode.inherited"); assert(tracker::g_mode.load() == tracker::Mode::Inherited);
    fixture::counts.valid = true; frame.time_ms += 300; frame.scale = 1.5f;
    click("page.chests"); assert(fixture::listHeight == 375.0f);
    click("chests.mode.current"); assert(tracker::g_mode.load() == tracker::Mode::Current);
    // 旧 ABI v1 宿主没有布局尾部时仍可导航和操作，不能把功能藏在缺失的折叠控件后。
    ui.size = static_cast<uint32_t>(offsetof(Sky2UiApi, is_any_item_active) + sizeof(ui.is_any_item_active));
    click("page.settings"); click("chests.markers"); assert(!tracker::g_enabled.load());
    click("page.travel"); click("travel.confirm"); assert(fixture::submitted == 5);
    click("travel.confirm"); assert(fixture::submitted == 6);
    click("travel.confirm");
    frame.foreground = 0; click("travel.confirm");
    assert(fixture::submitted == 6);
    assert(std::find(fixture::controls.begin(), fixture::controls.end(), "travel.confirm") != fixture::controls.end());
    frame.foreground = 1; click("travel.confirm"); assert(fixture::submitted == 6);
    click("travel.confirm"); assert(fixture::submitted == 7);
    // 新布局和旧布局共用业务路径；说明全部折叠时，确认仍留在可操作区域。
    ui.size = sizeof(ui); ui.section = &fixture::Section;
    ui.begin_card = &fixture::BeginCard; ui.end_card = &fixture::EndCard;
    ui.columns = &fixture::Columns; ui.next_column = &fixture::Void; ui.end_columns = &fixture::EndColumns;
    ui.tab = &fixture::Selectable; ui.status = &fixture::Status; ui.progress = &fixture::Progress;
    ui.disclosure = &fixture::Disclosure;
    for (const int columns : {1, 2}) {
        fixture::layoutColumns = columns;
        for (const auto* page : {"page.overview", "page.chests", "page.settings", "page.travel", "page.return"}) {
            click(page); assert(fixture::cardDepth == 0 && fixture::columnDepth == 0);
        }
    }
    assert(fixture::progresses >= 4);
    click("travel.confirm"); assert(fixture::submitted == 7);
    click("travel.confirm"); assert(fixture::submitted == 8 && fixture::lastTarget == tracker::kRevisitReturnTarget);
    // 新页栏一次处理整个页面选择；改变页索引必须作废已有危险操作确认。
    ui.tab_bar = &fixture::TabBar;
    fixture::tabBarCalls = 0; click("page.travel"); assert(fixture::tabBarCalls == 1);
    click("travel.confirm");
    fixture::requestedPage = 4; click("");
    click("travel.confirm"); assert(fixture::submitted == 8);
    click("travel.confirm"); assert(fixture::submitted == 9 && fixture::lastTarget == tracker::kRevisitReturnTarget);
    // 即便替身在禁用状态返回另一个索引，后台绘制也不能改变当前页面或提交请求。
    frame.foreground = 0; fixture::requestedPage = 2; click("");
    frame.foreground = 1; click(""); assert(fixture::observedPage == 4);
    fixture::requestedPage = 99; click(""); click(""); assert(fixture::observedPage == 4);
    click("travel.confirm"); assert(fixture::submitted == 9);
    click("travel.confirm"); assert(fixture::submitted == 10);
    // 原布局尾部仍能通过旧 ID 切页；不可因为结构里留有指针而越过 size 读取。
    ui.size = static_cast<uint32_t>(offsetof(Sky2UiApi, tab_bar));
    fixture::tabBarCalls = 0; click("page.chests"); assert(fixture::tabBarCalls == 0);
    ui.size = sizeof(ui); click("page.chests");
    click("chests.mode.inherited"); assert(tracker::g_mode.load() == tracker::Mode::Inherited);
    assert(fixture::cancelled > 0);
    // 新宿主把页头与 Main 分开调用：页签只画一次，切页仍通过同一条业务路径
    // 取消旧确认。只提供原 Frame 前缀的旧宿主则忽略尾部，并在 Main 内回退。
    const auto fixedPage = [&](int page, const char* id) {
        ++frame.time_ms; fixture::requestedPage = page; fixture::click = id;
        // 按生产宿主先 tick_ui 再 Header 的次序执行；Header 自身不能再次
        // 读取游戏上下文或推进业务，切页取消确认仍由 ChooseSection 完成。
        tracker::HubTick(&frame); const auto readsBeforeHeader = fixture::contextReads;
        frame.header_drawn = 0; tracker::HubDrawHeader(&frame);
        assert(fixture::contextReads == readsBeforeHeader);
        frame.header_drawn = 1; tracker::HubDrawPage(&frame); frame.header_drawn = 0;
    };
    fixture::tabBarCalls = 0; fixedPage(3, "travel.confirm");
    assert(fixture::tabBarCalls == 1 && fixture::submitted == 10);
    fixedPage(4, "travel.confirm"); assert(fixture::submitted == 10);
    fixedPage(-1, "travel.confirm"); assert(fixture::submitted == 11 && fixture::lastTarget == tracker::kRevisitReturnTarget);
    frame.header_drawn = 1; frame.size = static_cast<uint32_t>(offsetof(Sky2Frame, header_drawn));
    fixture::tabBarCalls = 0; click("page.chests"); assert(fixture::tabBarCalls == 1);
    frame.size = sizeof(frame); frame.header_drawn = 0;
    // 尺寸来自固定 Main，增大窗口会扩大列表；窄窗保留最少行和后续说明。
    // 缺少可选 API、短函数表或非法尺寸时均保持旧高度，不越界读取尾部。
    ui.content_size = &fixture::ContentSize;
    fixture::mainHeight = 400; click("page.chests"); const float shortList = fixture::listHeight;
    assert(shortList == 120.0f * frame.scale);
    fixture::mainHeight = 900; click(""); assert(fixture::listHeight > shortList && fixture::listHeight <= 520.0f * frame.scale);
    fixture::mainHeight = 0; click(""); assert(fixture::listHeight == 250.0f * frame.scale);
    fixture::mainHeight = 600; ui.size = static_cast<uint32_t>(offsetof(Sky2UiApi, content_size));
    click(""); assert(fixture::listHeight == 250.0f * frame.scale);
    ui.size = sizeof(ui); fixture::layoutColumns = 1; click("page.travel");
    const float singleColumnList = fixture::listHeight;
    assert(singleColumnList <= 280.0f * frame.scale);
    fixture::layoutColumns = 2; click(""); assert(fixture::listHeight > singleColumnList);
    ui.content_size = nullptr;
    // 实际生命周期代码等待两个独立安全边界，不能仅因取消回调返回就宣布已停用。
    tracker::g_enabled.store(true);
    fixture::exploration.mapEnabled = true;
    fixture::exploration.travelAvailable = true;
    fixture::exploration.travelRequested = fixture::exploration.travelEnabled = true;
    fixture::nativePending = true; fixture::explorationPause = 1;
    assert(tracker::HubRequestEnabled(0) && tracker::HubActivityState() == 3);
    assert(!fixture::nativeAccepting && !fixture::exploration.travelRequested);
    const auto beforePause = fixture::submitted;
    fixture::Invoke("chest.toggle_markers"); fixture::Invoke("chest.open_travel");
    assert(tracker::g_enabled.load() && fixture::submitted == beforePause);
    click("travel.confirm"); assert(tracker::HubActivityState() == 3);
    fixture::explorationPause = 0; click(""); assert(tracker::HubActivityState() == 3);
    fixture::nativePending = false; fixture::exploration.travelEnabled = false;
    click(""); assert(tracker::HubActivityState() == 0 && !tracker::HostedEffectsEnabled());
    assert(tracker::g_enabled.load() && fixture::exploration.mapEnabled);
    assert(fixture::State("chest.toggle_markers") == 0 && fixture::State("chest.toggle_hud") == 0);
    assert(fixture::State("chest.toggle_map_reveal") == 0 && fixture::State("chest.toggle_unvisited") == 0);
    assert(tracker::HubRequestEnabled(1) && tracker::HubActivityState() == 1 && fixture::nativeAccepting);
    assert(tracker::HostedEffectsEnabled() && fixture::exploration.travelRequested);
    assert(fixture::State("chest.toggle_markers") == 1 && fixture::State("chest.toggle_hud") == 1);
    // 返程行程与不可撤回的派发均拒绝停用；保留入口，不使玩家困在回访地图。
    fixture::tripActive = true;
    assert(!tracker::HubRequestEnabled(0) && tracker::HubActivityState() == 1 && fixture::nativeAccepting);
    assert(tracker::HubActivityMessage()); fixture::tripActive = false;
    fixture::pauseAllowed = false;
    assert(!tracker::HubRequestEnabled(0) && tracker::HubActivityState() == 1);
    fixture::pauseAllowed = true; fixture::explorationPause = 1;
    // 等待过程中重新启用可取消停止；超时或恢复失败同样回到稳定启用态并给出原因。
    assert(tracker::HubRequestEnabled(0)); click("");
    assert(tracker::HubRequestEnabled(1) && fixture::exploration.travelRequested);
    assert(tracker::HubRequestEnabled(0)); click(""); frame.time_ms += 15001; click("");
    assert(tracker::HubActivityState() == 1 && fixture::nativeAccepting && fixture::exploration.travelRequested);
    assert(tracker::HubActivityMessage());
    assert(tracker::HubRequestEnabled(0)); fixture::explorationPause = -1; click("");
    assert(tracker::HubActivityState() == 1 && tracker::HubActivityMessage());
    assert(!tracker::HubRequestEnabled(2) && tracker::HubActivityState() == 1);
    std::cout << "Real Hub page routing and confirmation checks passed.\n";
}
#endif

#ifdef SKY2_MODULE_VISUAL_FIXTURE
namespace {
Sky2HostApi* visualHost = nullptr;
const Sky2UiApi* visualFallback = nullptr;
bool prepareVisualConfirmation = false;
bool scrollVisualBottom = false;
// 真实页面输入回归只代理 UI 函数的观测结果。控件仍由生产 UiApi 创建，
// 不插入测试按钮、不改变卡片几何，也不直接设置黄色焦点或滚动位置。
struct VisualControl {
    ImGuiID id = 0;
    ImGuiWindow* window = nullptr;
    ImRect rect;
    bool open = false;
};
struct VisualStep {
    std::string name;
    int frames = 1;
    std::function<void(int)> before;
    std::function<void()> after;
};
bool navigationProbe = false;
const Sky2UiApi* observedUi = nullptr;
Sky2UiApi observingUi{};
std::map<std::string, VisualControl> visualControls;
std::vector<VisualStep> visualSteps;
size_t visualStepIndex = 0;
int visualStepFrame = 0, visualFailures = 0;
float visualFont = 0, visualScale = 0, outerBefore = 0;
ImGuiWindow* visualOuter = nullptr;
ImGuiWindow* visualHeader = nullptr;
ImVec2 visualHeaderPosition{}, initialHeaderPosition{}, initialHeaderWindowPosition{};
float initialMainHeight = 0, observedMainHeight = 0;
bool observedHeaderDrawn = false;
std::ofstream visualReport;

void RememberControl(const char* id, bool open = false) {
    const auto& item = GImGui->LastItemData;
    visualControls[id] = {item.ID, GImGui->CurrentWindow, item.NavRect, open};
}
int32_t SKY2_CALL ObserveButton(const char* id, const char* label) {
    const auto result = observedUi->button(id, label); RememberControl(id); return result;
}
int32_t SKY2_CALL ObserveCheckbox(const char* id, const char* label, int32_t* value) {
    const auto result = observedUi->checkbox(id, label, value); RememberControl(id); return result;
}
int32_t SKY2_CALL ObserveInput(const char* id, const char* label, char* value, uint32_t capacity) {
    const auto result = observedUi->input_text(id, label, value, capacity); RememberControl(id); return result;
}
int32_t SKY2_CALL ObserveTab(const char* id, const char* label, int32_t selected) {
    const auto result = observedUi->tab(id, label, selected); RememberControl(id); return result;
}
int32_t SKY2_CALL ObserveDisclosure(const char* id, const char* label, int32_t defaultOpen) {
    const auto result = observedUi->disclosure(id, label, defaultOpen); RememberControl(id, result != 0); return result;
}
int32_t SKY2_CALL ObserveChild(const char* id, float height) {
    const auto result = observedUi->begin_child(id, height);
    auto* window = GImGui->CurrentWindow;
    visualControls[id] = {window->GetID("##read-only-scroll"), window, window->InnerRect, false};
    return result;
}
bool Focused(const char* id) {
    const auto found = visualControls.find(id);
    return found != visualControls.end() && found->second.id && GImGui->NavId == found->second.id;
}
bool StatisticsScrollable() {
    const auto found = visualControls.find("chests.rows");
    return found != visualControls.end() && found->second.window &&
        std::floor(found->second.window->ScrollMax.y) > 0;
}
void WriteWindow(const char* label, const ImGuiWindow* window) {
    if (!window) { visualReport << label << "=null\n"; return; }
    visualReport << label << " name=" << window->Name << " y=" << window->Scroll.y
        << " max=" << window->ScrollMax.y << " target=" << window->ScrollTarget.y
        << " flags=" << window->Flags << '\n';
}
void WriteGeometry(const char* reason) {
    visualReport << reason << " NavId=" << GImGui->NavId << " font=" << visualFont
        << " scale=" << visualScale << '\n';
    WriteWindow("outer", visualOuter);
    WriteWindow("fixed header", visualHeader);
    visualReport << "header_position=" << visualHeaderPosition.x << ',' << visualHeaderPosition.y
        << " main_height=" << observedMainHeight << '\n';
    for (const auto& [name, control] : visualControls) {
        visualReport << "control=" << name << " id=" << control.id << " rect="
            << control.rect.Min.x << ',' << control.rect.Min.y << ','
            << control.rect.Max.x << ',' << control.rect.Max.y << " open=" << control.open << '\n';
        WriteWindow("window", control.window);
    }
    visualReport.flush();
}
void VisualCheck(bool condition, const std::string& message) {
    visualReport << (condition ? "PASS " : "FAIL ") << message << '\n';
    if (!condition) { ++visualFailures; WriteGeometry(message.c_str()); }
}
void AddVisualStep(const char* name, int frames, std::function<void(int)> before = {},
                   std::function<void()> after = {}) {
    visualSteps.push_back({name, frames, std::move(before), std::move(after)});
}
void AddKeyStep(ImGuiKey key, std::function<void()> after = {}) {
    AddVisualStep("press/release", 3, [key](int frame) {
        if (frame < 2) ImGui::GetIO().AddKeyEvent(key, frame == 0);
    }, std::move(after));
}
void AddSeekStep(const char* target) {
    const std::string id(target);
    AddVisualStep("seek by d-pad", 60, [id](int frame) {
        // 每次按下后留出释放帧；到达目标就停止，避免按住重复越过统计框。
        const bool noInnerScroll = id == "chests.rows" && !StatisticsScrollable();
        ImGui::GetIO().AddKeyEvent(ImGuiKey_GamepadDpadDown, frame % 2 == 0 && !noInnerScroll && !Focused(id.c_str()));
    }, [id] {
        if (id == "chests.rows" && !StatisticsScrollable()) {
            visualReport << "PASS adaptive list already shows all statistics; no inner scroll focus required\n";
        } else VisualCheck(Focused(id.c_str()), "d-pad reaches " + id);
    });
}
void AddStickStep(ImGuiKey direction, int frames, std::function<void()> after = {}) {
    AddVisualStep("right stick", frames + 2, [direction, frames](int frame) {
        const bool held = frame < frames;
        ImGui::GetIO().AddKeyAnalogEvent(direction, held, held ? 1.0f : 0.0f);
    }, std::move(after));
}
void StartNavigationProbe(bool listPage) {
    navigationProbe = true; visualControls.clear(); visualSteps.clear();
    visualStepIndex = 0; visualStepFrame = 0; visualOuter = visualHeader = nullptr;
    AddVisualStep("settle production layout", 8, [](int) {
        sky2hub::usingController.store(true);
        ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_HasGamepad;
    }, [] {
        // LoadFonts 使用实际 20px 字体；缩放由 Capture 与生产 Present 相同公式
        // 给出。ImGui 的 GetRoundedFontSize 按 IM_ROUND 将缩放字号取整，
        // 因此 1000p 的 20 * (1000 / 1080) 应实际使用 19px，而不是 18.5185px。
        VisualCheck(std::abs(visualFont - std::round(20.0f * visualScale)) < .05f, "production 20px font and framebuffer scale");
        VisualCheck(visualHeader && visualOuter && visualHeader != visualOuter, "fixed header and Main use separate windows");
        VisualCheck(observedHeaderDrawn, "production host reports fixed header before Main");
        initialHeaderPosition = visualHeaderPosition;
        initialHeaderWindowPosition = visualHeader ? visualHeader->Pos : ImVec2{};
        initialMainHeight = observedMainHeight;
        VisualCheck(initialMainHeight > 0, "production Main reports its allocated viewport height");
        WriteGeometry("initial geometry");
    });
    if (listPage) {
        for (const char* target : {"chests.mode.current", "chests.missing", "chests.search", "chests.next"}) {
            const std::string id(target);
            AddSeekStep(target);
            // 从同一外层起点分别检查模式、筛选、搜索和分页按钮。回顶也使用
            // 真实右摇杆事件，不能用 SetScrollY 掩盖焦点窗口选择错误。
            AddStickStep(ImGuiKey_GamepadRStickUp, 90);
            AddVisualStep("record outer start", 1, {}, [id] {
                outerBefore = visualOuter ? visualOuter->Scroll.y : 0;
                VisualCheck(visualOuter && visualOuter->ScrollMax.y > 5, "list really overflows outside " + id);
                WriteGeometry(("before outer scroll from " + id).c_str());
            });
            AddStickStep(ImGuiKey_GamepadRStickDown, 30, [id] {
                VisualCheck(visualOuter && visualOuter->Scroll.y > outerBefore + 5,
                    "right stick scrolls outer content from " + id);
                VisualCheck(Focused(id.c_str()), "right stick preserves function focus " + id);
                VisualCheck(visualHeader && visualHeaderPosition.x == initialHeaderPosition.x &&
                    visualHeaderPosition.y == initialHeaderPosition.y && visualHeader->Pos.x == initialHeaderWindowPosition.x &&
                    visualHeader->Pos.y == initialHeaderWindowPosition.y && visualHeader->Scroll.y == 0,
                    "page title, introduction and tabs remain fixed while Main scrolls from " + id);
                VisualCheck(observedMainHeight == initialMainHeight, "Main viewport height stays stable during scrolling");
                WriteGeometry(("after outer scroll from " + id).c_str());
            });
        }
        AddSeekStep("chests.rows");
        AddVisualStep("read-only list scroll to bottom", 120, [](int frame) {
            const auto found = visualControls.find("chests.rows");
            const auto* window = found == visualControls.end() ? nullptr : found->second.window;
            // ImGui 将最终 Scroll.y 落到整数像素；分数像素的 ScrollMax 余量
            // 并不代表还有可见内容，测试也必须按实际可到达的下边界判断。
            const bool held = frame < 118 && window && window->Scroll.y + .1f < std::floor(window->ScrollMax.y);
            ImGui::GetIO().AddKeyAnalogEvent(ImGuiKey_GamepadRStickDown, held, held ? 1.0f : 0.0f);
        }, [] {
            const auto* window = visualControls.at("chests.rows").window;
            if (StatisticsScrollable())
                VisualCheck(window && window->Scroll.y + .1f >= std::floor(window->ScrollMax.y), "right stick reaches statistics bottom");
            WriteGeometry("statistics bottom before d-pad exit");
        });
        AddKeyStep(ImGuiKey_GamepadDpadDown, [] {
            VisualCheck(Focused("chests.help"), "down at statistics bottom reaches help disclosure");
        });
        AddKeyStep(ImGuiKey_GamepadFaceDown, [] {
            VisualCheck(visualControls.at("chests.help").open, "A opens actual list help disclosure");
        });
    } else {
        AddSeekStep("tracking.help");
        AddKeyStep(ImGuiKey_GamepadFaceDown, [] {
            VisualCheck(visualControls.at("tracking.help").open, "A opens actual tracking help disclosure");
        });
        AddKeyStep(ImGuiKey_GamepadFaceDown);
        // 两列时向右进入探索卡片；窄窗单列时该按键保持原位，随后向下继续。
        AddKeyStep(ImGuiKey_GamepadDpadRight);
        AddSeekStep("exploration.help");
        AddKeyStep(ImGuiKey_GamepadFaceDown, [] {
            VisualCheck(visualControls.at("exploration.help").open, "A opens actual exploration help disclosure");
        });
    }
    AddVisualStep("final evidence", 3, {}, [] { WriteGeometry("final geometry"); });
}
void ProbeFrame(int, bool afterDraw) {
    if (visualStepIndex >= visualSteps.size()) return;
    auto& step = visualSteps[visualStepIndex];
    if (!afterDraw) { if (step.before) step.before(visualStepFrame); return; }
    if (++visualStepFrame >= step.frames) {
        if (step.after) step.after();
        ++visualStepIndex; visualStepFrame = 0;
    }
}
int ProbeFrameCount() {
    int result = 0; for (const auto& step : visualSteps) result += step.frames; return result;
}
void SKY2_CALL DrawVisualPage(const Sky2Frame* frame) {
    if (prepareVisualConfirmation && frame && frame->foreground && frame->panel_open && frame->page_active) {
        // 截图壳会先绘制关闭帧，正常失活逻辑会取消确认。因此在第一个实际页面
        // 帧中通过真实按钮路由准备一次，再交给真实公共 UI 绘制待确认状态。
        prepareVisualConfirmation = false;
        const auto* actual = visualHost->ui; visualHost->ui = visualFallback;
        fixture::click = "travel.confirm"; tracker::HubDrawPage(frame);
        visualHost->ui = actual;
    }
    const auto* actual = visualHost->ui;
    if (navigationProbe) {
        observedUi = actual; observingUi = *actual;
        observingUi.button = &ObserveButton; observingUi.checkbox = &ObserveCheckbox;
        observingUi.input_text = &ObserveInput; observingUi.tab = &ObserveTab;
        observingUi.disclosure = &ObserveDisclosure; observingUi.begin_child = &ObserveChild;
        visualHost->ui = &observingUi;
        visualOuter = GImGui->CurrentWindow; visualFont = ImGui::GetFontSize(); visualScale = frame->scale;
        float width = 0;
        if (actual->content_size) actual->content_size(&width, &observedMainHeight);
        observedHeaderDrawn = frame->header_drawn != 0;
    }
    tracker::HubDrawPage(frame);
    visualHost->ui = actual;
    // 单列长页面另拍滚动后的确认区；仅测试夹具操作 ImGui 滚动，模块本体不
    // 持有私有 ImGui 状态，也不替玩家滚动或触发游戏操作。
    if (scrollVisualBottom) ImGui::SetScrollHereY(1.0f);
}
void SKY2_CALL DrawVisualHeader(const Sky2Frame* frame) {
    // 页头回调仍是模块实际实现。仅记录真实绘制窗口和页签起点，不能用测试
    // 固定坐标伪造 Header，也不改宿主提供的 Main 尺寸。
    if (navigationProbe) {
        visualHeader = GImGui->CurrentWindow;
        visualHeaderPosition = ImGui::GetCursorScreenPos();
    }
    tracker::HubDrawHeader(frame);
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path outputDirectory(argv[1]);
    std::filesystem::create_directories(outputDirectory);
    Sky2UiApi fallback{}; fallback.size = sizeof(fallback);
    fallback.text = &fixture::Text; fallback.text_wrapped = &fixture::Text;
    fallback.separator = &fixture::Void; fallback.same_line = &fixture::Void; fallback.spacing = &fixture::Void;
    fallback.button = &fixture::Button; fallback.checkbox = &fixture::Checkbox; fallback.selectable = &fixture::Selectable;
    fallback.begin_disabled = &fixture::Disabled; fallback.end_disabled = &fixture::Void;
    fallback.begin_child = &fixture::Begin; fallback.end_child = &fixture::Void; fallback.input_text = &fixture::Input;
    Sky2HostApi host{}; host.size = sizeof(host); host.abi = SKY2_HUB_ABI; host.ui = &fallback;
    host.register_action = &fixture::Register; host.open_page = &fixture::Open; host.language = &fixture::Language;
    Sky2Hub_Host = &host;
    visualHost = &host; visualFallback = &fallback;
    fixture::counts.valid = true;
    fixture::counts.maps = tracker::CountMaps(nullptr, 0);
    for (size_t i = 0; i < fixture::counts.maps.size(); ++i) {
        auto& row = fixture::counts.maps[i];
        row.current = i % 4 == 0 ? row.total : row.total * 2 / 3;
        row.inherited = std::min(row.total, row.current + 2);
        fixture::counts.current += row.current; fixture::counts.inherited += row.inherited;
    }
    fixture::counts.map = "mp6011"; fixture::counts.map_total = 12;
    fixture::counts.map_current = 8; fixture::counts.map_inherited = 11;
    fixture::exploration.mapAvailable = fixture::exploration.travelAvailable = true;
    fixture::exploration.mapEnabled = true;
    fixture::context.available = fixture::context.valid = fixture::context.browsing = fixture::context.returnPointReady = true;
    fixture::context.busy = false;
    fixture::context.browseIdentity = 12; fixture::context.chapter = 4;
    std::memcpy(fixture::context.scene, "mp6011", sizeof("mp6011"));
    fixture::returned.hasRecord = true; fixture::returned.active = false; fixture::returned.count = 3;
    std::memcpy(fixture::returned.record.point.scene, "mp6011", sizeof("mp6011"));
    fixture::returned.record.createdUnixSeconds = 1790670600;
    fixture::returned.record.point.chapter = 4;
    tracker::RegisterHubActions();
    // 页面导航由真实控件路由进入；截图场景最多点一次准备，不执行第二次确认。
    const auto choose = [&](const char* id) {
        host.ui = &fallback; fixture::click = id;
        Sky2Frame frame{sizeof(Sky2Frame), 1600, 1000, 1, GetTickCount64(), 1, 1, 1, 0};
        tracker::HubDrawPage(&frame);
    };
    const auto capture = [&](const wchar_t* file, int width = 1600, int height = 1000) {
        const auto path = outputDirectory / file;
        sky2hub::visual::Capture(path.c_str(), "chest", "宝箱追踪", host, &DrawVisualPage,
            &tracker::HubTick, width, height, nullptr, 5, &DrawVisualHeader);
    };
    choose("page.overview"); capture(L"chest-overview.png");
    choose("page.chests"); capture(L"chest-list.png");
    choose("page.settings"); capture(L"chest-settings.png");
    choose("page.travel"); capture(L"chest-travel.png");
    prepareVisualConfirmation = true; capture(L"chest-confirmation.png");
    choose("page.return"); capture(L"chest-return.png");
    prepareVisualConfirmation = true; capture(L"chest-return-confirmation.png");
    fixture::displayLanguage = 4;
    choose("page.settings"); capture(L"chest-settings-de-narrow.png", 1120, 800);
    choose("page.travel"); capture(L"chest-travel-de-narrow.png", 1120, 800);
    capture(L"chest-travel-de-single-column.png", 900, 1080);
    scrollVisualBottom = true; prepareVisualConfirmation = true;
    capture(L"chest-travel-de-single-column-confirmation.png", 900, 1080);
    scrollVisualBottom = false;
    // 复现用户报告：统计子框能滚，但黄色焦点回到上方实际控件后外层条不动。
    // 独立报告保留每层 Scroll/ScrollMax/ScrollTarget，便于识别缩放后的亚像素
    // 卡片滚动范围；输入只经过生产 DrawHub，绝不修改左摇杆现有语义。
    visualReport.open(outputDirectory / L"chest-navigation-report.txt", std::ios::trunc);
    const auto probe = [&](const wchar_t* name, bool listPage, int width, int height, int language) {
        fixture::displayLanguage = language;
        choose(listPage ? "page.chests" : "page.settings");
        visualReport << "\nCASE " << width << 'x' << height << " language=" << language
            << " page=" << (listPage ? "list" : "settings") << '\n';
        StartNavigationProbe(listPage);
        const auto path = outputDirectory / name;
        sky2hub::visual::Capture(path.c_str(), "chest", "宝箱追踪", host, &DrawVisualPage,
            &tracker::HubTick, width, height, &ProbeFrame, ProbeFrameCount(), &DrawVisualHeader);
        navigationProbe = false;
    };
    probe(L"chest-navigation-list-zh.png", true, 1600, 1000, 0);
    probe(L"chest-navigation-list-zh-1080p.png", true, 1920, 1080, 0);
    probe(L"chest-navigation-list-de-narrow.png", true, 900, 1080, 4);
    probe(L"chest-navigation-settings-zh.png", false, 1600, 1000, 0);
    visualReport << "\nFailures=" << visualFailures << '\n';
    visualReport.close();
    assert(fixture::submitted == 0);
    return visualFailures ? 1 : 0;
}
#endif
