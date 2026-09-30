// Hub 宝箱页面：宿主负责实际控件、焦点和输入，本文件仅组织已有业务能力。
// 游戏内存修改仍由原有探索/传送的安全回调消费，绘制线程不调用原生换图函数。
#include "hub_panel.h"
#include "sky2_ui.hpp"
#include "hub_ui_state.h"
#include "hosted_activity.h"
#include "tracker.h"
#include "exploration.h"
#include "revisit.h"
#include "revisit_policy.h"
#include "localized_names.h"
#include "ui_text.h"
#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace tracker {
namespace {
// 二级页面只管理展示状态；业务开关和传送安全队列仍由原实现拥有。
enum class Section { Overview, Chests, Settings, Travel, Return };
enum class Action { Markers, Mode, Hud, Chests, Reveal, Unvisited, Travel, Return };
Section section = Section::Overview;
Counts counts;
uint64_t refreshed = 0, requestToken = 0;
bool active = false, hud = true, missingOnly = true, selectionInitialized = false;
bool submissionRejected = false;
size_t mapPage = 0, travelSelection = 0;
constexpr size_t kMapRows = 8;
char mapSearch[192]{}, travelSearch[192]{};
HubTravelConfirmation confirmation;
// 用户功能偏好与模块活动状态分开：停用不覆盖标记/HUD/揭示偏好；需要原生菜单
// 重建的传送辅助另存用户意图，恢复时重新排入原队列，不直接恢复旧对象指针。
int32_t activity = 1;
enum class ActivityNotice { None, TripActive, Dispatched, Restoring, RestoreFailed, TimedOut };
ActivityNotice activityNotice = ActivityNotice::None;
bool savedTravelPreference = false;
uint64_t stopObservedAt = 0;

const Sky2UiApi& Ui() { return *Sky2Hub_Host->ui; }

bool ValidFrame(const Sky2Frame* frame) noexcept {
    // header_drawn 是 v1 的可选尾部，旧宿主只需提供原来的 controller 字段。
    // 不能用 sizeof(新版 Sky2Frame) 拒绝仍能完整提供原功能的旧宿主。
    return frame && frame->size >= offsetof(Sky2Frame, controller) + sizeof(Sky2Frame::controller);
}

float ListViewportHeight(const Sky2Frame& frame, bool travel = false, bool twoColumns = false) {
    const float scale = std::max(0.5f, frame.scale);
    const float fallback = (travel ? (twoColumns ? 300.0f : 180.0f) : 250.0f) * scale;
    float width = 0, height = 0;
    if (!sky2ui::ContentSize(Ui(), &width, &height) || !std::isfinite(height)) return fallback;
    // 宿主提供的是 Main 固定可视高度，不读取自动增高卡片的剩余高度，避免
    // 列表随自身 ContentSize 或滚动位置反复涨缩。高度上限防止宽屏列表过长；
    // 下限保证仍有可浏览行，极窄窗口的其它功能继续由 Main 外层滚动到达。
    if (!travel) return std::clamp(height - 240.0f * scale, 120.0f * scale, 520.0f * scale);
    if (twoColumns) return std::clamp(height - 150.0f * scale, 180.0f * scale, 520.0f * scale);
    // 单列的行程条件与两次确认位于目录下方，只给目录约四成可视高度，
    // 不能让自适应填满的列表把确认区挤成几乎无法发现的第二屏。
    return std::clamp(height * 0.40f, 140.0f * scale, 280.0f * scale);
}

// 格式串只来自内部已审核文案；所有游戏专名以 %s 参数传递，不能被解释为格式。
template<class... Args> std::string Format(const char* pattern, Args... args) {
    char text[2048]{};
    std::snprintf(text, sizeof(text), pattern, args...);
    return text;
}
const char* HudLabel() {
    return Localize("显示宝箱简报", "宝箱 HUD を表示", "Show chest HUD", "顯示寶箱簡報",
        "Truhen-HUD anzeigen", "Afficher le résumé des coffres", "Mostrar resumen de cofres", "보물 상자 HUD 표시");
}
const char* SearchLabel() {
    return Localize("搜索地点", "場所を検索", "Search locations", "搜尋地點",
        "Orte suchen", "Rechercher un lieu", "Buscar lugares", "장소 검색");
}
const char* NoMatchesLabel() {
    return Localize("没有匹配地点", "該当する場所がありません", "No matching locations",
        "沒有符合的地點", "Keine passenden Orte", "Aucun lieu correspondant", "No hay lugares coincidentes", "일치하는 장소가 없습니다");
}
const char* MarkersLabel() {
    return Localize("宝箱地图标记", "マップの宝箱マーカー", "Chest map markers", "寶箱地圖標記",
        "Truhenmarkierungen", "Marqueurs de coffres", "Marcadores de cofres", "지도 보물 상자 표시");
}
const char* ReturnLabel() {
    return Localize("查看返程记录", "帰還記録を確認", "View return records", "查看返程記錄",
        "Rückkehrdaten ansehen", "Voir les points de retour", "Ver registros de regreso", "귀환 기록 보기");
}
const char* OverviewLabel() {
    return Localize("概览", "概要", "Overview", "概覽", "Übersicht", "Aperçu", "Resumen", "개요");
}
const char* SettingsLabel() {
    return Localize("设置", "設定", "Settings", "設定", "Optionen", "Réglages", "Ajustes", "설정");
}
const char* HelpLabel() {
    return Localize("使用说明", "使い方", "How it works", "使用說明", "Hinweise", "Mode d’emploi", "Cómo funciona", "사용 안내");
}
const char* ReturnTabLabel() {
    return Localize("返程", "帰還", "Return", "返程", "Rückkehr", "Retour", "Regreso", "귀환");
}
const char* TravelTabLabel() {
    return Localize("传送", "移動", "Travel", "傳送", "Reisen", "Voyage", "Viaje", "이동");
}
const char* ListTabLabel() {
    return Localize("清单", "一覧", "List", "清單", "Liste", "Liste", "Lista", "목록");
}
bool IsTravelSection() noexcept { return section == Section::Travel || section == Section::Return; }

void CancelConfirmation() noexcept {
    confirmation.Cancel();
    CancelRevisitNativeTravel();
    submissionRejected = false;
}
void SelectTravel(size_t index) noexcept {
    if (index == travelSelection) return;
    CancelConfirmation();
    travelSelection = std::min(index, RevisitDestinationCount() - 1);
}
void ChooseSection(Section next) noexcept {
    if (next != section) CancelConfirmation();
    section = next;
    if (next == Section::Return) {
        SelectTravel(RevisitDestinationCount() - 1);
        travelSearch[0] = '\0';
        selectionInitialized = true;
    } else if (next == Section::Travel) {
        // 返程有独立页面。重新进入目的地页时清除返程选择，防止确认按钮仍指向
        // 列表之外的隐藏返程目标；进入页面本身不会提交任何游戏操作。
        if (!selectionInitialized || RevisitDestinationAt(travelSelection).id == kRevisitReturnTarget)
            SelectTravel(0);
        selectionInitialized = true;
    }
}
bool CanSubmit(const RevisitNativeContext& context) noexcept {
    const auto phase = ReadRevisitNativeStatus().phase;
    return RevisitReady() && context.available && RevisitContextAllowed(context) &&
        context.browsing && !context.busy && phase != RevisitNativePhase::Queued &&
        phase != RevisitNativePhase::ClosingMap && phase != RevisitNativePhase::Dispatched;
}

void SKY2_CALL Invoke(void* data) noexcept {
    if (activity != 1) return; // 停用/收尾时拒绝宿主中尚未消费的旧动作。
    try {
        switch (static_cast<Action>(reinterpret_cast<uintptr_t>(data))) {
        case Action::Markers: g_enabled.store(!g_enabled.load()); break;
        case Action::Mode:
            g_mode.store(g_mode.load() == Mode::Current ? Mode::Inherited : Mode::Current);
            mapPage = 0; break;
        case Action::Hud: hud = !hud; break;
        case Action::Reveal: ToggleExploration(ExplorationFeature::MapReveal); break;
        case Action::Unvisited: ToggleExploration(ExplorationFeature::TravelUnlock); break;
        case Action::Chests:
            ChooseSection(Section::Chests); Sky2Hub_Host->open_page(Sky2Hub_Host->owner); break;
        case Action::Travel:
            ChooseSection(Section::Travel); Sky2Hub_Host->open_page(Sky2Hub_Host->owner); break;
        case Action::Return:
            ChooseSection(Section::Return);
            Sky2Hub_Host->open_page(Sky2Hub_Host->owner); break;
        }
    } catch (...) { CancelConfirmation(); }
}
const char* SKY2_CALL ActionTitle(void* data) noexcept {
    switch (static_cast<Action>(reinterpret_cast<uintptr_t>(data))) {
    case Action::Markers: return UiString(UiText::PauseResume);
    case Action::Mode: return UiString(UiText::SwitchMode);
    case Action::Hud: return HudLabel();
    case Action::Chests: return UiString(UiText::MapList);
    case Action::Reveal: return UiString(UiText::RevealMap);
    case Action::Unvisited: return UiString(UiText::UnvisitedTravel);
    case Action::Travel: return UiString(UiText::TravelList);
    case Action::Return: return ReturnLabel();
    }
    return "";
}
int32_t SKY2_CALL ActionAvailable(void* data) noexcept {
    if (activity != 1) return 0;
    const auto action = static_cast<Action>(reinterpret_cast<uintptr_t>(data));
    const auto status = ReadExplorationStatus();
    if (action == Action::Reveal) return status.mapAvailable ? 1 : 0;
    if (action == Action::Unvisited) return status.travelAvailable ? 1 : 0;
    return 1;
}

int32_t SKY2_CALL ActionState(void* data) noexcept {
    // 状态回调只读取业务当前值，不复用 Invoke，也不因首页刷新而切换开关或落盘。
    // HUD 与动作调用均由宿主 Present 线程访问；其余值来自原子变量或状态快照。
    switch (static_cast<Action>(reinterpret_cast<uintptr_t>(data))) {
    case Action::Markers: return HostedEffectsEnabled() && g_enabled.load(std::memory_order_relaxed) ? 1 : 0;
    case Action::Hud: return HostedEffectsEnabled() && hud ? 1 : 0;
    case Action::Reveal: return HostedEffectsEnabled() && ReadExplorationStatus().mapEnabled ? 1 : 0;
    case Action::Unvisited: {
        const auto status = ReadExplorationStatus();
        // 未访问地点的传送选项需要游戏线程安全刷新。开启与关闭请求都可能等待，
        // 此时统一返回“待生效”；完成后必须报告 applied 值，不能把用户意图当结果。
        return !HostedEffectsEnabled() ? 0 : (status.travelPending ? 2 : (status.travelEnabled ? 1 : 0));
    }
    // 统计口径切换与打开页面都不是布尔开关，不把“当前/继承”误标为关闭/开启。
    default: return -1;
    }
}

void DrawExploration() {
    const auto& ui = Ui();
    sky2ui::Section(ui, UiString(UiText::Exploration));
    const auto status = ReadExplorationStatus();
    int32_t reveal = status.mapEnabled ? 1 : 0;
    ui.begin_disabled(!status.mapAvailable);
    if (ui.checkbox("exploration.reveal", UiString(UiText::RevealMap), &reveal) && status.mapAvailable)
        ToggleExploration(ExplorationFeature::MapReveal);
    ui.end_disabled();
    int32_t unvisited = status.travelRequested ? 1 : 0;
    ui.begin_disabled(!status.travelAvailable);
    if (ui.checkbox("exploration.unvisited", UiString(UiText::UnvisitedTravel), &unvisited) && status.travelAvailable)
        ToggleExploration(ExplorationFeature::TravelUnlock);
    ui.end_disabled();
    if (!status.mapAvailable || !status.travelAvailable) sky2ui::Status(ui, UiString(UiText::FeatureFailed), 2);
    if (status.travelPending)
        sky2ui::Status(ui, UiString(status.travelRequested ? UiText::WaitingOn : UiText::WaitingOff));
    if (sky2ui::Disclosure(ui, "exploration.help", HelpLabel())) ui.text_wrapped(UiString(UiText::FeatureNote));
}

// 口径选择属于清单和设置的共同控件；稳定 ID 不随语言或选中状态变化。
void DrawMode() {
    const auto& ui = Ui();
    if (sky2ui::Tab(ui, "chests.mode.current", UiString(UiText::ModeCurrent), g_mode.load() == Mode::Current)) {
        g_mode.store(Mode::Current); mapPage = 0;
    }
    ui.same_line();
    if (sky2ui::Tab(ui, "chests.mode.inherited", UiString(UiText::InheritedColumn), g_mode.load() == Mode::Inherited)) {
        g_mode.store(Mode::Inherited); mapPage = 0;
    }
    ui.spacing();
}

void DrawOverview() {
    const auto& ui = Ui();
    const int columns = sky2ui::Columns(ui, "overview.columns");
    sky2ui::BeginCard(ui, "overview.total");
    sky2ui::Section(ui, Localize("收集进度", "収集状況", "Collection progress", "收集進度",
        "Sammelfortschritt", "Progression de collecte", "Progreso de colección", "수집 진행도"));
    if (counts.valid) {
        const float total = static_cast<float>(std::size(kChests));
        sky2ui::Progress(ui, counts.current / total, Format(UiString(UiText::CurrentOpened), counts.current).c_str());
        sky2ui::Progress(ui, counts.inherited / total, Format(UiString(UiText::InheritedOpened), counts.inherited).c_str());
        unsigned complete = 0, remaining = 0;
        for (const auto& row : counts.maps) { complete += row.Remaining(g_mode.load()) == 0; remaining += row.Remaining(g_mode.load()); }
        ui.text_wrapped(Format(UiString(UiText::MapSummary), complete, static_cast<unsigned>(counts.maps.size()), remaining).c_str());
    } else sky2ui::Status(ui, UiString(UiText::WaitingData));
    if (ui.button("overview.list", UiString(UiText::MapList))) ChooseSection(Section::Chests);
    sky2ui::EndCard(ui);
    if (columns > 1) sky2ui::NextColumn(ui);
    sky2ui::BeginCard(ui, "overview.area");
    sky2ui::Section(ui, Localize("当前地区", "現在のエリア", "Current area", "目前地區",
        "Aktuelles Gebiet", "Zone actuelle", "Zona actual", "현재 지역"));
    if (counts.valid && !counts.map.empty()) {
        const float total = static_cast<float>(std::max(1u, counts.map_total));
        sky2ui::Progress(ui, counts.map_current / total, Format(UiString(UiText::AreaCurrent), counts.map_current, counts.map_total).c_str());
        sky2ui::Progress(ui, counts.map_inherited / total, Format(UiString(UiText::AreaInherited), counts.map_inherited, counts.map_total).c_str());
        ui.text_wrapped(UiString(UiText::AreaNote));
    } else sky2ui::Status(ui, UiString(counts.valid ? UiText::OpenAreaMap : UiText::WaitingData));
    sky2ui::EndCard(ui);
    sky2ui::EndColumns(ui);
    sky2ui::BeginCard(ui, "overview.state");
    sky2ui::Status(ui, g_enabled.load() ? MarkersLabel() : UiString(UiText::Paused), g_enabled.load() ? 1 : 2);
    ui.text_wrapped(Format(UiString(UiText::ModeLabel), UiString(g_mode.load() == Mode::Current ? UiText::ModeCurrent : UiText::ModeInherited)).c_str());
    if (ui.button("overview.settings", SettingsLabel())) ChooseSection(Section::Settings);
    sky2ui::EndCard(ui);
}

void DrawSettings() {
    const auto& ui = Ui();
    const int columns = sky2ui::Columns(ui, "settings.columns");
    sky2ui::BeginCard(ui, "settings.tracking");
    sky2ui::Section(ui, MarkersLabel());
    int32_t markers = g_enabled.load() ? 1 : 0;
    if (ui.checkbox("chests.markers", MarkersLabel(), &markers)) g_enabled.store(markers != 0);
    int32_t hudValue = hud ? 1 : 0;
    if (ui.checkbox("chests.hud", HudLabel(), &hudValue)) hud = hudValue != 0;
    DrawMode();
    if (sky2ui::Disclosure(ui, "tracking.help", HelpLabel())) ui.text_wrapped(UiString(UiText::MarkerLegend));
    sky2ui::EndCard(ui);
    if (columns > 1) sky2ui::NextColumn(ui);
    sky2ui::BeginCard(ui, "settings.exploration");
    DrawExploration();
    sky2ui::EndCard(ui);
    sky2ui::EndColumns(ui);
}

void DrawChestList(const Sky2Frame& frame) {
    const auto& ui = Ui();
    sky2ui::BeginCard(ui, "chests.catalog");
    sky2ui::Section(ui, UiString(UiText::MapTitle));
    DrawMode();
    int32_t missing = missingOnly ? 1 : 0;
    if (ui.checkbox("chests.missing", UiString(UiText::OnlyMissing), &missing)) {
        missingOnly = missing != 0;
        mapPage = 0;
    }
    if (ui.input_text("chests.search", SearchLabel(), mapSearch, sizeof(mapSearch))) mapPage = 0;
    if (!counts.valid) { sky2ui::Status(ui, UiString(UiText::WaitingData)); sky2ui::EndCard(ui); return; }
    auto visible = VisibleMaps(counts.maps, g_mode.load(), missingOnly);
    visible.erase(std::remove_if(visible.begin(), visible.end(), [](size_t index) {
        return mapSearch[0] && std::string(MapPath(*counts.maps[index].definition)).find(mapSearch) == std::string::npos;
    }), visible.end());
    const size_t pages = std::max(size_t{1}, (visible.size() + kMapRows - 1) / kMapRows);
    mapPage = std::min(mapPage, pages - 1);
    ui.begin_disabled(mapPage == 0);
    if (ui.button("chests.previous", UiString(UiText::PreviousPage)) && mapPage) --mapPage;
    ui.end_disabled(); ui.same_line();
    ui.begin_disabled(mapPage + 1 >= pages);
    if (ui.button("chests.next", UiString(UiText::NextPage)) && mapPage + 1 < pages) ++mapPage;
    ui.end_disabled(); ui.same_line();
    ui.text(Format(UiString(UiText::PageFormat), mapPage + 1, pages).c_str());
    if (ui.begin_child("chests.rows", ListViewportHeight(frame))) {
        for (size_t i = mapPage * kMapRows; i < std::min((mapPage + 1) * kMapRows, visible.size()); ++i) {
            const auto& row = counts.maps[visible[i]];
            const auto collected = row.Collected(g_mode.load());
            // 路径和计数合并为紧凑行；长地名由宿主自动换行，不截断玩家辨认所需信息。
            ui.text_wrapped(Format("%s   ·   %u / %u   ·   %s %u", MapPath(*row.definition), collected,
                row.total, UiString(UiText::MissingColumn), row.Remaining(g_mode.load())).c_str());
        }
        if (visible.empty()) ui.text(mapSearch[0] ? NoMatchesLabel() : UiString(UiText::AllOpened));
    }
    ui.end_child();
    if (sky2ui::Disclosure(ui, "chests.help", HelpLabel())) ui.text_wrapped(UiString(UiText::MapNote));
    sky2ui::EndCard(ui);
}

// 与独立版使用完全相同的准入说明次序，不能把“页面能打开”显示成“可以传送”。
const char* TravelMessage(const RevisitNativeContext& context, const RevisitNativeStatus& status,
                          const RevisitReturnStatus& returned, uint32_t target, uint64_t now) {
    if (!RevisitReady() || !context.available) return UiString(UiText::TravelUnavailable);
    if (status.phase == RevisitNativePhase::Queued) return UiString(UiText::TravelQueued);
    if (status.phase == RevisitNativePhase::ClosingMap) return UiString(UiText::TravelClosing);
    if (status.phase == RevisitNativePhase::Dispatched) return UiString(UiText::TravelDispatched);
    if (!context.valid) return UiString(UiText::WaitingScene);
    if (!RevisitContextAllowed(context)) return UiString(UiText::SceneUnsupported);
    if (!context.browsing || context.busy) return UiString(UiText::OpenTravelMap);
    if (!RevisitTargetAllowed(target, context)) {
        if (target == kRevisitReturnTarget)
            return UiString(returned.hasRecord ? (revisit_policy::kUnrestricted ? UiText::ReturnInvalid : UiText::ReturnStoryBlocked) : UiText::ReturnMissing);
        if (!returned.storageReady) return UiString(UiText::RecordStorageFailed);
        if (RevisitRecoveryRequired(context) && !returned.active) return UiString(UiText::ReturnFirst);
        if (!revisit_policy::kUnrestricted && context.beforeScriptReturnBlocked && !returned.active)
            return UiString(UiText::StoryOwnsTravel);
        if (!context.returnPointReady && !returned.active) return UiString(UiText::PositionNotReady);
        return RevisitNativeTargetReason(target, context);
    }
    if (confirmation.Armed(target, now))
        return target == kRevisitReturnTarget ? UiString(UiText::ConfirmReturn) :
            Localize("核对目的地后再次确认（8 秒内有效）。", "移動先を確認し、8 秒以内にもう一度確認してください。",
                "Check the destination and confirm again within 8 seconds.", "核對目的地後再次確認（8 秒內有效）。",
                "Ziel prüfen und innerhalb von 8 Sekunden erneut bestätigen.",
                "Vérifiez la destination et confirmez à nouveau sous 8 secondes.",
                "Revisa el destino y vuelve a confirmar en 8 segundos.", "목적지를 확인하고 8초 안에 다시 확인하세요.");
    if (status.phase == RevisitNativePhase::ArrivalUnconfirmed) return UiString(UiText::ArrivalUnconfirmed);
    if (submissionRejected || status.phase == RevisitNativePhase::Rejected || status.phase == RevisitNativePhase::Expired)
        return UiString(UiText::RequestUnconfirmed);
    return UiString(UiText::TravelReady);
}

void DrawTravel(const Sky2Frame& frame) {
    const auto& ui = Ui();
    const auto context = ReadRevisitNativeContext();
    const auto returned = ReadRevisitReturnStatus(context);
    const bool returnView = section == Section::Return;
    const int columns = returnView ? 1 : sky2ui::Columns(ui, "travel.columns", 310.0f);
    std::vector<size_t> visible;
    if (returnView) visible.push_back(RevisitDestinationCount() - 1);
    else {
        sky2ui::BeginCard(ui, "travel.catalog");
        sky2ui::Section(ui, Localize("选择目的地", "移動先を選択", "Choose a destination", "選擇目的地",
            "Ziel auswählen", "Choisir une destination", "Elegir destino", "목적지 선택"));
        // 搜索只过滤展示；不会改写真实地点 ID，也不会自动选中/提交另一个目的地。
        if (ui.input_text("travel.search", SearchLabel(), travelSearch, sizeof(travelSearch))) CancelConfirmation();
        for (size_t i = 0; i < RevisitDestinationCount(); ++i) {
            const auto& destination = RevisitDestinationAt(i);
            if (destination.id == kRevisitReturnTarget) continue;
            const std::string path = std::string(DestinationGroup(destination)) + " / " + DestinationName(destination);
            if (!travelSearch[0] || path.find(travelSearch) != std::string::npos) visible.push_back(i);
        }
        size_t selectedPosition = 0;
        const auto found = std::find(visible.begin(), visible.end(), travelSelection);
        if (found != visible.end()) selectedPosition = static_cast<size_t>(found - visible.begin());
        const size_t pages = std::max(size_t{1}, (visible.size() + kRevisitRowsPerPage - 1) / kRevisitRowsPerPage);
        size_t page = selectedPosition / kRevisitRowsPerPage;
        ui.begin_disabled(page == 0);
        if (ui.button("travel.previous", UiString(UiText::PreviousPage)) && page) {
            --page; SelectTravel(visible[page * kRevisitRowsPerPage]);
        }
        ui.end_disabled(); ui.same_line();
        ui.begin_disabled(page + 1 >= pages);
        if (ui.button("travel.next", UiString(UiText::NextPage)) && page + 1 < pages) {
            ++page; SelectTravel(visible[page * kRevisitRowsPerPage]);
        }
        ui.end_disabled(); ui.same_line();
        ui.text(Format(UiString(UiText::PageFormat), page + 1, pages).c_str());
        // 窄窗单列时缩短目录视口，为随后出现的行程条件与确认按钮保留空间。
        if (ui.begin_child("travel.rows", ListViewportHeight(frame, true, columns > 1))) {
            for (size_t p = page * kRevisitRowsPerPage; p < std::min((page + 1) * kRevisitRowsPerPage, visible.size()); ++p) {
                const auto index = visible[p];
                const auto& row = RevisitDestinationAt(index);
                const auto label = std::string(DestinationGroup(row)) + " / " + DestinationName(row);
                const auto id = "travel.target." + std::to_string(row.id);
                // 不禁用地点选择：即使当前暂不可前往，玩家仍可选中并查看具体原因。
                if (ui.selectable(id.c_str(), label.c_str(), index == travelSelection)) SelectTravel(index);
            }
            if (visible.empty()) ui.text_wrapped(NoMatchesLabel());
        }
        ui.end_child();
        sky2ui::EndCard(ui);
        if (columns > 1) sky2ui::NextColumn(ui);
    }
    sky2ui::BeginCard(ui, "travel.details");
    sky2ui::Section(ui, returnView ? ReturnLabel() : Localize("行程确认", "移動の確認", "Trip confirmation", "行程確認",
        "Reise bestätigen", "Confirmer le trajet", "Confirmar viaje", "이동 확인"));
    const auto& destination = RevisitDestinationAt(travelSelection);
    ui.text_wrapped((std::string(DestinationGroup(destination)) + " / " + DestinationName(destination)).c_str());
    if (destination.id == 108) sky2ui::Status(ui, UiString(UiText::BuildingNote), 2);
    if (destination.id == forest::kTarget) sky2ui::Status(ui, UiString(UiText::ForestNote), 2);
    ui.spacing();
    if (returned.hasRecord) {
        const auto& record = returned.record;
        const time_t stamp = static_cast<time_t>(record.createdUnixSeconds);
        tm local{}; char date[48]{};
        if (localtime_s(&local, &stamp) == 0) std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", &local);
        ui.text_wrapped(Format("%s: %s", UiString(returned.active ? UiText::OriginalPoint : UiText::HistoryCandidate),
            ReturnPointDisplayName(record.point, kMaps)).c_str());
        // 返程页始终展示记录身份；目的地页把日期等补充信息折叠，核心出发点仍可见。
        if (returnView || sky2ui::Disclosure(ui, "travel.record.details", ReturnLabel()))
            ui.text_wrapped(Format(UiString(UiText::RecordFormat), record.point.scene, date,
                returned.index + 1, returned.count).c_str());
    } else ui.text_wrapped(UiString(UiText::RecordAuto));
    if (destination.id == kRevisitReturnTarget && !returned.active && returned.count > 1 &&
        ui.button("travel.history", UiString(UiText::CycleRecord))) {
        CancelConfirmation();
        CycleRevisitReturnRecord(context);
    }
    // 搜索隐藏当前选项时保留导航记忆，但禁止确认看不见的旧目的地；玩家必须选中
    // 当前结果或清空搜索。选中名称仍显示在下方，避免搜索被解释成自动切换目的地。
    const bool selectedVisible = std::find(visible.begin(), visible.end(), travelSelection) != visible.end();
    const bool allowed = selectedVisible && active && CanSubmit(context) && RevisitTargetAllowed(destination.id, context);
    const bool armed = confirmation.Armed(destination.id, frame.time_ms);
    ui.spacing();
    sky2ui::Status(ui, selectedVisible ? TravelMessage(context, ReadRevisitNativeStatus(), returned, destination.id, frame.time_ms) :
        Localize("请先选中搜索结果中的地点，或清空搜索", "検索結果の場所を選ぶか検索を解除してください",
            "Select a search result or clear the search first", "請先選中搜尋結果中的地點，或清空搜尋",
            "Zuerst ein Suchergebnis auswählen oder die Suche leeren", "Sélectionnez un résultat ou effacez la recherche",
            "Selecciona un resultado o borra la búsqueda", "검색 결과를 선택하거나 검색어를 지우세요"),
        armed || !allowed ? 2 : 1);
    ui.begin_disabled(!allowed);
    // 控件不启用长按重复；第一次只进入八秒确认，第二次独立激活才排队。
    const char* confirmLabel = armed ?
        Localize("再次确认", "もう一度確認", "Confirm again", "再次確認", "Erneut bestätigen", "Confirmer à nouveau", "Confirmar de nuevo", "다시 확인") :
        Localize("准备行程", "移動を準備", "Prepare trip", "準備行程", "Reise vorbereiten", "Préparer le trajet", "Preparar viaje", "이동 준비");
    if (ui.button("travel.confirm", confirmLabel) && allowed) {
        if (confirmation.Press(destination.id, frame.time_ms, active, allowed, context)) {
            submissionRejected = !QueueRevisitTravel(destination.id, ++requestToken, context);
            if (submissionRejected) Log("Hub travel request rejected; existing native safety checks retained.");
        } else submissionRejected = false;
    }
    ui.end_disabled();
    if (confirmation.Armed(destination.id, frame.time_ms)) {
        ui.same_line();
        if (ui.button("travel.cancel", Localize("取消确认", "確認を取り消す", "Cancel confirmation", "取消確認",
            "Bestätigung abbrechen", "Annuler la confirmation", "Cancelar confirmación", "확인 취소"))) CancelConfirmation();
    }
    if (sky2ui::Disclosure(ui, "travel.help", HelpLabel())) {
        ui.text_wrapped(UiString(revisit_policy::kUnrestricted ? UiText::TravelOrigin : UiText::TravelStoryOrigin));
        ui.text_wrapped(Localize("先打开游戏区域地图，再选择目的地并确认两次。返程记录在「返程」页。",
            "ゲームの地域マップを開き、移動先を選んで 2 回確認してください。帰還記録は「帰還」にあります。",
            "Open the game's area map, choose a destination and confirm twice. Return records are on the Return tab.",
            "先開啟遊戲區域地圖，再選擇目的地並確認兩次。返程紀錄在「返程」頁。",
            "Gebietskarte öffnen, ein Ziel wählen und zweimal bestätigen. Rückkehrdaten stehen unter Rückkehr.",
            "Ouvrez la carte de zone, choisissez une destination et confirmez deux fois. Les points de retour sont dans Retour.",
            "Abre el mapa de zona, elige un destino y confirma dos veces. Los registros están en Regreso.",
            "게임의 지역 지도를 열고 목적지를 선택한 뒤 두 번 확인하세요. 귀환 기록은 귀환 탭에 있습니다."));
    }
    sky2ui::EndCard(ui);
    if (!returnView) sky2ui::EndColumns(ui);
}
} // namespace

int32_t HubActivityState() noexcept { return activity; }
const char* HubActivityMessage() noexcept {
    // 每次查询按当前游戏语言返回静态文本；查询无副作用，也不临时分配借用字符串。
    switch (activityNotice) {
    case ActivityNotice::TripActive:
        return Localize("请先完成返程，再停用宝箱模块。", "帰還を完了してから宝箱モジュールを停止してください。",
            "Complete your return trip before disabling the chest module.", "請先完成返程，再停用寶箱模組。",
            "Vor dem Deaktivieren des Truhenmoduls zuerst zurückkehren.", "Terminez le retour avant de désactiver le module de coffres.",
            "Completa el regreso antes de desactivar el módulo de cofres.", "귀환을 완료한 후 보물 상자 모듈을 비활성화하세요.");
    case ActivityNotice::Dispatched:
        return Localize("传送已经派发，请待到达后再尝试停用。", "移動処理中です。到着後に停止してください。",
            "Travel is already in progress. Try disabling after arrival.", "傳送已經派發，請待到達後再嘗試停用。",
            "Die Reise läuft bereits. Erst nach der Ankunft deaktivieren.", "Le déplacement est en cours. Réessayez après l’arrivée.",
            "El viaje está en curso. Intenta desactivar después de llegar.", "이동이 진행 중입니다. 도착한 후 비활성화하세요.");
    case ActivityNotice::Restoring:
        return Localize("正在恢复原生状态。请打开区域地图并等待；重新启用可取消停用。",
            "ゲーム本来の状態に復元中です。エリアマップを開いてお待ちください。再度有効にすると停止を取り消せます。",
            "Restoring native state. Open the area map and wait; enabling again cancels the stop.",
            "正在恢復原生狀態。請開啟區域地圖並等待；重新啟用可取消停用。",
            "Spielzustand wird wiederhergestellt. Gebietskarte öffnen und warten; erneutes Aktivieren bricht den Stopp ab.",
            "Restauration en cours. Ouvrez la carte de zone et patientez ; réactiver annule l’arrêt.",
            "Restaurando el estado original. Abre el mapa de zona y espera; reactivar cancela la desactivación.",
            "원래 상태를 복원 중입니다. 지역 지도를 열고 기다리세요. 다시 활성화하면 중지 요청이 취소됩니다.");
    case ActivityNotice::RestoreFailed:
        return Localize("无法确认原生状态已恢复，模块仍保持启用。", "元の状態への復元を確認できないため、モジュールは有効のままです。",
            "Native state restoration could not be verified. The module remains enabled.", "無法確認原生狀態已恢復，模組仍保持啟用。",
            "Wiederherstellung nicht bestätigt. Das Modul bleibt aktiviert.", "La restauration n’a pas pu être vérifiée. Le module reste actif.",
            "No se pudo verificar la restauración. El módulo sigue activo.", "원래 상태 복원을 확인할 수 없어 모듈을 활성 상태로 유지합니다.");
    case ActivityNotice::TimedOut:
        return Localize("停用等待超时，已保留原设置并恢复启用；请在区域地图稳定后重试。",
            "停止待機がタイムアウトしました。設定を保持して再開しました。エリアマップが安定してから再試行してください。",
            "Stopping timed out. Original settings were retained; retry once the area map is stable.",
            "停用等待逾時，已保留原設定並恢復啟用；請在區域地圖穩定後重試。",
            "Zeitlimit beim Stoppen. Einstellungen bleiben erhalten; bei stabiler Gebietskarte erneut versuchen.",
            "Délai d’arrêt dépassé. Réglages conservés ; réessayez lorsque la carte de zone est stable.",
            "La espera terminó. Se conservaron los ajustes; reintenta cuando el mapa de zona esté estable.",
            "중지 대기 시간이 초과되었습니다. 기존 설정을 유지했습니다. 지역 지도가 안정되면 다시 시도하세요.");
    default: return nullptr;
    }
}

namespace {
void ResumeActivity() noexcept {
    // 不重装挂钩、不重置返程历史，只重新开放有效业务；辅助开关的恢复仍需安全刷新。
    RequestHostedTravelEnabled(savedTravelPreference);
    g_hostedEffectsEnabled.store(true, std::memory_order_release);
    ResumeHostedNativeTravel();
    activity = 1;
    stopObservedAt = 0;
}
void AdvanceActivity(uint64_t now) noexcept {
    if (activity != 3) return;
    if (!stopObservedAt) stopObservedAt = now;
    const int exploration = HostedExplorationPauseStatus();
    if (exploration < 0) {
        ResumeActivity(); activityNotice = ActivityNotice::RestoreFailed; return;
    }
    if (!HostedNativeTravelPausePending() && exploration == 0) {
        // 仅在传送交接已清理、菜单已恢复后关闭额外效果。此后仍保留原生观察回调，
        // 恢复只需要打开门闩；没有正在执行的代码或游戏持有的地址被释放。
        g_hostedEffectsEnabled.store(false, std::memory_order_release);
        activity = 0; activityNotice = ActivityNotice::None; stopObservedAt = 0; return;
    }
    if (now >= stopObservedAt && now - stopObservedAt >= 15000) {
        ResumeActivity(); activityNotice = ActivityNotice::TimedOut;
    }
}
}

int32_t HubRequestEnabled(int32_t enabled) noexcept {
    if (enabled != 0 && enabled != 1) return 0;
    if (enabled) {
        if (activity != 1) ResumeActivity();
        activityNotice = ActivityNotice::None;
        return 1;
    }
    if (activity == 0 || activity == 3) return 1;
    CancelConfirmation();
    // 与实际派发共用 native 锁。若已进入不可撤回的换图，拒绝停用并保留返程能力。
    if (!TryPauseHostedNativeTravel()) { activityNotice = ActivityNotice::Dispatched; return 0; }
    const auto context = ReadRevisitNativeContext();
    if (HostedRevisitTripActive() || RevisitRecoveryRequired(context)) {
        ResumeHostedNativeTravel(); activityNotice = ActivityNotice::TripActive; return 0;
    }
    savedTravelPreference = ReadExplorationStatus().travelRequested;
    RequestHostedTravelEnabled(false);
    activity = 3; active = false; stopObservedAt = 0;
    activityNotice = ActivityNotice::Restoring;
    return 1;
}

bool RegisterHubActions() noexcept {
    // 全局入口均为普通开关或打开页面；传送确认不注册为全局动作，不能绕过活动页。
    constexpr const char* ids[]{"chest.toggle_markers", "chest.cycle_mode", "chest.toggle_hud", "chest.open_list",
        "chest.toggle_map_reveal", "chest.toggle_unvisited", "chest.open_travel", "chest.open_return"};
    for (uintptr_t i = 0; i < std::size(ids); ++i) {
        auto* data = reinterpret_cast<void*>(i);
        const uint32_t flags = SKY2_ACTION_GLOBAL | ((i == 0 || i == 3 || i == 6) ? SKY2_ACTION_FAVORITE : 0);
        const Sky2Action action{sizeof(Sky2Action), ids[i], ActionTitle(data), flags, data,
            &Invoke, &ActionAvailable, &ActionTitle, &ActionState};
        if (!Sky2Hub_Host->register_action(Sky2Hub_Host->owner, &action)) return false;
    }
    return true;
}

void HubVisibilityChanged(int32_t visible) noexcept {
    active = visible != 0 && activity == 1;
    if (!active) CancelConfirmation();
}
void HubTick(const Sky2Frame* frame) {
    if (!ValidFrame(frame)) return;
    SetDisplayLanguage(HubLanguage(Sky2Hub_Host->language()));
    AdvanceActivity(frame->time_ms);
    active = activity == 1 && frame->foreground && frame->panel_open && frame->page_active;
    const auto context = ReadRevisitNativeContext();
    confirmation.Observe(active && IsTravelSection(), CanSubmit(context), context);
    if (activity == 1 && frame->time_ms - refreshed >= 250) { counts = ReadCounts(); refreshed = frame->time_ms; }
}
void HubDrawHeader(const Sky2Frame* frame) {
    if (!ValidFrame(frame) || !frame->panel_open || !frame->page_active) return;
    // 宿主已在 tick_ui 更新业务快照；旧宿主的内嵌回退也先经过 HubDrawPage。
    // 固定页头仅绘制/切换页签，不再次推进生命周期或读取原生游戏上下文。
    if (activity != 1) return;
    const auto& ui = Ui();
    ui.begin_disabled(!frame->foreground);
    // 顶部页栏由宿主整体处理 LT/RT 与鼠标选择，不占用具体功能的黄色导航焦点。
    // 所有标签一次返回目标页，再统一切换和取消旧确认，避免逐项绘制时页状态变化。
    const char* labels[]{OverviewLabel(), ListTabLabel(), SettingsLabel(), TravelTabLabel(), ReturnTabLabel()};
    constexpr const char* legacyIds[]{"page.overview", "page.chests", "page.settings", "page.travel", "page.return"};
    constexpr int pageCount = static_cast<int>(std::size(legacyIds));
    const int previousPage = static_cast<int>(section);
    int nextPage = previousPage;
    if (ui.size >= offsetof(Sky2UiApi, tab_bar) + sizeof(ui.tab_bar) && ui.tab_bar) {
        nextPage = sky2ui::TabBar(ui, "page", labels, pageCount, previousPage);
    } else {
        // 老宿主没有页栏扩展，继续使用原控件 ID；口径选择仍是普通功能 Tab。
        for (int index = 0; index < pageCount; ++index) {
            if (index) ui.same_line();
            if (sky2ui::Tab(ui, legacyIds[index], labels[index], previousPage == index)) nextPage = index;
        }
    }
    if (frame->foreground && nextPage >= 0 && nextPage < pageCount && nextPage != previousPage)
        ChooseSection(static_cast<Section>(nextPage));
    ui.spacing();
    ui.end_disabled();
}
void HubDrawPage(const Sky2Frame* frame) {
    if (!ValidFrame(frame) || !frame->panel_open || !frame->page_active) return;
    // 鼠标侧栏可能在宿主本帧 tick 后改变页面；绘制前用最终页面状态再次核对，
    // 防止新页沿用上一帧的 inactive 状态，或旧确认绕过当前原生上下文检查。
    HubTick(frame);
    const auto& ui = Ui();
    if (activity != 1) {
        // 停用中也保留状态页，但不绘制会修改设置的功能控件；不能依赖宿主禁用样式
        // 作为唯一守卫，旧动作及原生请求入口同样已经关闭准入。
        sky2ui::Status(ui, HubActivityMessage() ? HubActivityMessage() : UiString(UiText::Paused), 2);
        return;
    }
    // 新宿主先在固定 Header 调用 HubDrawHeader，再令此标记为真。旧宿主
    // 没有尾部字段或没有执行页头回调时，继续在页面内部绘制原来的五项页签。
    if (!sky2ui::HeaderDrawn(*frame)) HubDrawHeader(frame);
    // 切出游戏后保留页面快照，但失焦已由 HubTick 撤销确认。控件只读，不能
    // 把“仍在绘制”解释为活动页；传送提交另有 active 守卫，不依赖控件禁用。
    ui.begin_disabled(!frame->foreground);
    switch (section) {
    case Section::Overview: DrawOverview(); break;
    case Section::Chests: DrawChestList(*frame); break;
    case Section::Settings: DrawSettings(); break;
    case Section::Travel: case Section::Return: DrawTravel(*frame); break;
    }
    ui.end_disabled();
}
void HubDrawOverlay(const Sky2Frame* frame) {
    // HUD 只提供精简信息，控制中心打开时隐藏，避免与统一面板重叠；左下角独立锚定。
    if (!ValidFrame(frame) || !frame->foreground || frame->panel_open || !hud || activity != 1) return;
    const auto& ui = Ui();
    const float scale = std::max(0.5f, frame->scale);
    const float font = 17.0f * scale;
    const auto title = std::string(UiString(UiText::Title)) + " | " +
        UiString(g_enabled.load() ? (g_mode.load() == Mode::Current ? UiText::ModeCurrent : UiText::ModeInherited) : UiText::Paused);
    const auto details = !counts.valid ? std::string(UiString(UiText::WaitingData)) :
        (!counts.map.empty() ? Format(UiString(g_mode.load() == Mode::Current ? UiText::AreaCurrent : UiText::AreaInherited),
            g_mode.load() == Mode::Current ? counts.map_current : counts.map_inherited, counts.map_total) :
         Format(UiString(g_mode.load() == Mode::Current ? UiText::CurrentOpened : UiText::InheritedOpened),
            g_mode.load() == Mode::Current ? counts.current : counts.inherited));
    float tw = 0, th = 0, dw = 0, dh = 0;
    ui.measure_text(title.c_str(), font, &tw, &th); ui.measure_text(details.c_str(), font, &dw, &dh);
    const float x = 18 * scale, y = std::max(0.0f, frame->height - (th + dh + 48 * scale));
    const float width = std::min(std::max(tw, dw) + 24 * scale, frame->width - x);
    ui.rect(x, y, x + width, y + th + dh + 26 * scale, 0xEE251A10, 8 * scale, 1, 1);
    ui.rect(x, y + 8 * scale, x + 3 * scale, y + th + dh + 18 * scale, 0xFFD6C678, 2 * scale, 1, 1);
    ui.draw_text(x + 12 * scale, y + 8 * scale, font, 0xFFE2DFC7, title.c_str());
    ui.draw_text(x + 12 * scale, y + th + 14 * scale, font, 0xFFB3DCEB, details.c_str());
}
} // namespace tracker
