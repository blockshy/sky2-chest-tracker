// 宝箱业务页面：独立窗口负责控件、焦点和输入，本文件组织统计、探索与传送。
// 游戏内存修改仍由原有探索/传送的安全回调消费，绘制线程不调用原生换图函数。
#include "panel_pages.h"
#include "sky2_ui.hpp"
#include "panel_state.h"
#include "controller_logic.h"
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
Section section = Section::Overview;
Counts counts;
uint64_t refreshed = 0, requestToken = 0;
bool active = false, hud = true, missingOnly = true, selectionInitialized = false;
bool submissionRejected = false;
size_t mapPage = 0, travelSelection = 0;
constexpr size_t kMapRows = 8;
char mapSearch[192]{}, travelSearch[192]{};
PanelTravelConfirmation confirmation;
const Sky2UiApi* pageUi = nullptr;
const Sky2UiApi& Ui() { return *pageUi; }

bool ValidFrame(const Sky2Frame* frame) noexcept {
    // 绘制上下文必须完整；公开测试可显式构造值对象，不触碰游戏进程。
    return pageUi && frame && frame->size >= sizeof(Sky2Frame);
}

float ListViewportHeight(const Sky2Frame& frame, bool travel = false, bool twoColumns = false) {
    const float scale = std::max(0.5f, frame.scale);
    const float fallback = (travel ? (twoColumns ? 300.0f : 180.0f) : 250.0f) * scale;
    float width = 0, height = 0;
    if (!sky2ui::ContentSize(Ui(), &width, &height) || !std::isfinite(height)) return fallback;
    // 独立窗口提供的是 Main 固定可视高度，不读取自动增高卡片的剩余高度，避免
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
void DrawMapPreparationButton(const char* id, const char* label, const RevisitNativeContext& context) {
    const auto preparation = ReadRevisitMapPreparation();
    const bool preparing = preparation == RevisitMapPreparationPhase::Queued || preparation == RevisitMapPreparationPhase::Opening;
    const bool canPrepare = active && RevisitReady() && context.available && context.valid &&
        !context.browsing && !context.busy && context.canPrepareMap && !preparing;
    const auto& ui = Ui();
    ui.begin_disabled(!canPrepare);
    // 同一准备入口供传送、返程和等待应用的显示开关使用；只打开真实地图，
    // 不记录出发点、不选择目的地。原生线程仍逐项复核游戏允许的自由行动状态。
    if (ui.button(id, label) && canPrepare) {
        CancelConfirmation();
        QueueRevisitNativeMapPreparation(context);
    }
    ui.end_disabled();
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
    if (status.travelPending) {
        sky2ui::Status(ui, UiString(status.travelRequested ? UiText::WaitingOn : UiText::WaitingOff));
        const auto context = ReadRevisitNativeContext();
        if (!context.browsing) {
            DrawMapPreparationButton("exploration.prepare_map", Localize("打开地图并应用", "マップを開いて適用", "Open map and apply", "開啟地圖並套用",
                "Karte öffnen und anwenden", "Ouvrir la carte et appliquer", "Abrir mapa y aplicar", "지도를 열고 적용"), context);
            ui.text_wrapped(Localize("自动打开游戏区域地图以刷新显示；不会传送或修改真实到访记录。", "地域マップを開いて表示を更新します。移動や訪問記録の変更は行いません。",
                "Opens the game's area map to refresh the display. It does not travel or change visited records.", "自動開啟遊戲區域地圖以更新顯示；不會傳送或修改真實到訪紀錄。",
                "Öffnet die Gebietskarte und aktualisiert die Anzeige. Keine Reise und keine Änderung der Besuchsdaten.",
                "Ouvre la carte de zone et actualise l’affichage, sans déplacement ni modification des visites.",
                "Abre el mapa de zona y actualiza la vista, sin viajar ni cambiar los lugares visitados.", "게임 지역 지도를 열어 표시를 갱신합니다. 이동하거나 실제 방문 기록을 바꾸지 않습니다."));
        }
    }
    if (sky2ui::Disclosure(ui, "exploration.help", HelpLabel())) ui.text_wrapped(UiString(UiText::FeatureNote));
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
            // 路径和计数合并为紧凑行；长地名由绘制控件自动换行，不截断辨认信息。
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
    if (!context.browsing || context.busy) {
        const auto preparation = ReadRevisitMapPreparation();
        if (preparation == RevisitMapPreparationPhase::Queued || preparation == RevisitMapPreparationPhase::Opening)
            return Localize("正在打开游戏区域地图，请稍候；准备完成后仍需确认目的地。", "地域マップを開いています。完了後に移動先を確認してください。",
                "Opening the game's area map. Confirm the destination once it is ready.", "正在開啟遊戲區域地圖，請稍候；準備完成後仍需確認目的地。",
                "Die Gebietskarte wird geöffnet. Bestätige danach das Reiseziel.", "Ouverture de la carte de zone. Confirmez ensuite la destination.",
                "Abriendo el mapa de zona. Confirma el destino cuando esté listo.", "게임 지역 지도를 여는 중입니다. 준비되면 목적지를 확인하세요.");
        if (preparation == RevisitMapPreparationPhase::Rejected || preparation == RevisitMapPreparationPhase::Expired)
            return Localize("地图准备未完成。请回到可自由行动的场景后重试，也可手动打开区域地图。", "マップの準備が完了しませんでした。自由行動中に再試行するか、地域マップを手動で開いてください。",
                "Map preparation did not finish. Retry during free exploration or open the area map manually.", "地圖準備未完成。請回到可自由行動的場景後重試，也可手動開啟區域地圖。",
                "Kartenvorbereitung fehlgeschlagen. Bei freier Erkundung erneut versuchen oder die Gebietskarte selbst öffnen.",
                "La préparation a échoué. Réessayez en exploration libre ou ouvrez la carte manuellement.",
                "No se pudo preparar el mapa. Reintenta en exploración libre o abre el mapa manualmente.", "지도 준비를 완료하지 못했습니다. 자유 탐색 중 다시 시도하거나 지역 지도를 직접 여세요.");
        return Localize("点击「准备传送 / 准备返程」自动打开游戏区域地图，再确认两次。", "「移動を準備 / 帰還を準備」で地域マップを開き、その後 2 回確認してください。",
            "Use Prepare travel / Prepare return to open the area map, then confirm twice.", "點選「準備傳送 / 準備返程」自動開啟遊戲區域地圖，再確認兩次。",
            "Mit „Reise vorbereiten / Rückkehr vorbereiten“ die Gebietskarte öffnen, dann zweimal bestätigen.",
            "Utilisez Préparer le voyage / le retour pour ouvrir la carte, puis confirmez deux fois.",
            "Usa Preparar viaje / regreso para abrir el mapa y confirma dos veces.", "이동 준비 / 귀환 준비로 지역 지도를 연 다음 두 번 확인하세요.");
    }
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
    if (!context.browsing) {
        const char* label = returnView ?
            Localize("准备返程", "帰還を準備", "Prepare return", "準備返程", "Rückkehr vorbereiten", "Préparer le retour", "Preparar regreso", "귀환 준비") :
            Localize("准备传送", "移動を準備", "Prepare travel", "準備傳送", "Reise vorbereiten", "Préparer le voyage", "Preparar viaje", "이동 준비");
        // 准备成功后仍保留原有两次独立确认，不能把打开地图当作出发许可。
        DrawMapPreparationButton("travel.prepare_map", label, context);
    }
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
        Localize("确认目的地", "移動先を確認", "Confirm destination", "確認目的地", "Reiseziel bestätigen", "Confirmer la destination", "Confirmar destino", "목적지 확인");
    if (ui.button("travel.confirm", confirmLabel) && allowed) {
        if (confirmation.Press(destination.id, frame.time_ms, active, allowed, context)) {
            submissionRejected = !QueueRevisitTravel(destination.id, ++requestToken, context);
            if (submissionRejected) Log("Panel travel request rejected; existing native safety checks retained.");
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
        ui.text_wrapped(Localize("可用「准备传送 / 准备返程」自动打开区域地图，也可手动打开。准备本身不会传送，之后仍需核对目的地并确认两次。返程记录在「返程」页。",
            "「移動を準備 / 帰還を準備」で地域マップを開けます。手動でも開けます。準備だけでは移動しません。移動先を確認して 2 回決定してください。帰還記録は「帰還」にあります。",
            "Prepare travel / Prepare return opens the area map; manual opening also works. Preparation does not travel. Check the destination and confirm twice. Return records are on the Return tab.",
            "可用「準備傳送 / 準備返程」自動開啟區域地圖，也可手動開啟。準備本身不會傳送，之後仍需核對目的地並確認兩次。返程紀錄在「返程」頁。",
            "Reise vorbereiten / Rückkehr vorbereiten öffnet die Gebietskarte; manuelles Öffnen bleibt möglich. Die Vorbereitung löst keine Reise aus. Ziel prüfen und zweimal bestätigen. Rückkehrdaten stehen unter Rückkehr.",
            "Préparer le voyage / le retour ouvre la carte ; l’ouverture manuelle reste possible. La préparation ne déplace pas le personnage. Vérifiez la destination et confirmez deux fois. Les points de retour sont dans Retour.",
            "Preparar viaje / regreso abre el mapa; también puedes abrirlo manualmente. Preparar no te transporta. Comprueba el destino y confirma dos veces. Los registros están en Regreso.",
            "이동 준비 / 귀환 준비로 지역 지도를 열거나 직접 열 수 있습니다. 준비만으로 이동하지 않습니다. 목적지를 확인하고 두 번 확인하세요. 귀환 기록은 귀환 탭에 있습니다."));
    }
    sky2ui::EndCard(ui);
    if (!returnView) sky2ui::EndColumns(ui);
}
} // namespace

void SetPanelUi(const Sky2UiApi* ui) noexcept { pageUi = ui; }
const Sky2UiApi& PanelUi() noexcept { return Ui(); }
bool PanelUiReady() noexcept { return pageUi != nullptr; }
void ApplyPanelActions(uint32_t pending) noexcept {
    // 输入层已检查前台与窗口所有权；这里只提交既有开关或页面意图。
    // 传送目的地、返程票据与两次确认始终留在可见页面和安全游戏队列中。
    try {
        if (pending & ToggleMode) SetStandaloneDisplayMode(g_mode.load() == Mode::Current ? Mode::Inherited : Mode::Current);
        if (pending & ToggleEnabled) g_enabled.store(!g_enabled.load());
        if (pending & ToggleMapReveal) ToggleExploration(ExplorationFeature::MapReveal);
        if (pending & ToggleTravelUnlock) ToggleExploration(ExplorationFeature::TravelUnlock);
        if (pending & ToggleRevisit) { ChooseSection(Section::Travel); g_panel.store(true); }
    } catch (...) { CancelConfirmation(); }
}

void PanelVisibilityChanged(int32_t visible) noexcept {
    active = visible != 0;
    if (!active) CancelConfirmation();
}
int StandalonePageIndex() noexcept { return static_cast<int>(section); }
const Counts& StandaloneCountsSnapshot() noexcept { return counts; }
bool StandaloneHudVisible() noexcept { return hud; }
void SetStandaloneHudVisible(bool value) noexcept { hud = value; }
void SetStandaloneDisplayMode(Mode mode) noexcept {
    if (mode != Mode::Current && mode != Mode::Inherited) return;
    if (g_mode.exchange(mode) != mode) mapPage = 0;
}
void StandaloneChoosePage(int page) noexcept {
    // 不接受外壳传来的越界索引，避免未知页面被解释成可提交传送的页面。
    if (page >= static_cast<int>(Section::Overview) && page <= static_cast<int>(Section::Return))
        ChooseSection(static_cast<Section>(page));
}
void TickPanel(const Sky2Frame* frame) {
    if (!ValidFrame(frame)) return;
    active = frame->foreground && frame->panel_open && frame->page_active;
    const auto context = ReadRevisitNativeContext();
    confirmation.Observe(active && IsTravelSection(), CanSubmit(context), context);
    if (frame->time_ms - refreshed >= 250) { counts = ReadCounts(); refreshed = frame->time_ms; }
}
void DrawPanelHeader(const Sky2Frame* frame) {
    if (!ValidFrame(frame) || !frame->panel_open || !frame->page_active) return;
    // 独立窗口先刷新业务快照。固定页头只绘制页签，不重复读取原生上下文。
    const auto& ui = Ui();
    ui.begin_disabled(!frame->foreground);
    // 顶部页栏由独立窗口整体处理 LT/RT 与鼠标选择，不占用具体功能的黄色导航焦点。
    // 所有标签一次返回目标页，再统一切换和取消旧确认，避免逐项绘制时页状态变化。
    const char* labels[]{OverviewLabel(), ListTabLabel(), SettingsLabel(), TravelTabLabel(), ReturnTabLabel()};
    constexpr const char* legacyIds[]{"page.overview", "page.chests", "page.settings", "page.travel", "page.return"};
    const int previousPage = static_cast<int>(section);
    int firstPage = 0;
    int pageCount = static_cast<int>(std::size(legacyIds));
    // 独立版使用左侧用途分组：宝箱概览/清单、设置、传送/返程。页头只展示
    // 当前组内的页签，LB/RB 留给侧栏，LT/RT 不会跨组跳到另一类业务。
    firstPage = previousPage >= static_cast<int>(Section::Travel) ? 3 : (previousPage == 2 ? 2 : 0);
    pageCount = firstPage == 2 ? 1 : 2;
    int nextPage = previousPage;
    if (ui.size >= offsetof(Sky2UiApi, tab_bar) + sizeof(ui.tab_bar) && ui.tab_bar) {
        const int selected = sky2ui::TabBar(ui, "page", labels + firstPage, pageCount, previousPage - firstPage);
        // 先校验组内返回值再转换成全局页号；不能让非法值越过侧栏业务边界。
        if (selected >= 0 && selected < pageCount) nextPage = firstPage + selected;
    } else {
        // 绘制表没有页栏扩展，继续使用原控件 ID；这里只负责当前用途分组的页签。
        for (int index = firstPage; index < firstPage + pageCount; ++index) {
            if (index != firstPage) ui.same_line();
            if (sky2ui::Tab(ui, legacyIds[index], labels[index], previousPage == index)) nextPage = index;
        }
    }
    if (frame->foreground && nextPage >= firstPage && nextPage < firstPage + pageCount && nextPage != previousPage)
        ChooseSection(static_cast<Section>(nextPage));
    ui.spacing();
    ui.end_disabled();
}
void DrawPanelPage(const Sky2Frame* frame) {
    if (!ValidFrame(frame) || !frame->panel_open || !frame->page_active) return;
    // 鼠标侧栏可能在本帧刷新快照后改变页面；绘制前用最终页面状态再次核对，
    // 防止新页沿用上一帧的 inactive 状态，或旧确认绕过当前原生上下文检查。
    TickPanel(frame);
    const auto& ui = Ui();
    // 外壳固定页头绘制后设置标记；测试或简化绘制表未调用页头时在此补绘。
    if (!sky2ui::HeaderDrawn(*frame)) DrawPanelHeader(frame);
    // 切出游戏后保留页面快照，但失焦已由 TickPanel 撤销确认。控件只读，不能
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
} // namespace tracker
