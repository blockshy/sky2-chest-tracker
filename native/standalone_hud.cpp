// HUD 内容使用既有统计快照，编辑状态仅属于当前窗口。普通游戏中窗口完全
// 鼠标穿透；只有设置页显式开启调整且主窗拥有输入时，才接收拖动手势。
#include "standalone_hud.h"
#include "panel_pages.h"
#include "tracker.h"
#include "localized_names.h"
#include "ui_text.h"
#include "sky2_ui.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <vector>

namespace tracker {
namespace {
HudPreferences preferences;
std::wstring folder;
bool saveFailed = false, loadFailed = false, moving = false, dragging = false;
float dragStartX = 0, dragStartY = 1;
ImVec2 dragOffset{};

void Save() noexcept {
    saveFailed = !WriteHudPreferences(folder, preferences);
    if (!saveFailed) loadFailed = false;
}
void CancelDrag() noexcept {
    if (dragging) { preferences.x = dragStartX; preferences.y = dragStartY; }
    dragging = false;
}
template<class... Args> std::string Format(const char* pattern, Args... args) {
    char text[2048]{}; std::snprintf(text, sizeof(text), pattern, args...); return text;
}
const char* AreaName(const Counts& counts) {
    const MapDefinition* match = nullptr;
    for (const auto& row : counts.maps) if (row.definition && row.definition->scene && counts.map == row.definition->scene) {
        if (match && match != row.definition)
            return Localize("当前统计地区（含多个地点）", "現在の集計エリア（複数地点）", "Current area (multiple locations)", "目前統計地區（含多個地點）",
                "Aktuelles Gebiet (mehrere Orte)", "Zone actuelle (plusieurs lieux)", "Zona actual (varios lugares)", "현재 집계 지역(여러 장소)");
        match = row.definition;
    }
    return match ? MapPath(*match) : UiString(UiText::OpenAreaMap);
}
std::vector<std::string> Rows(bool preview) {
    std::vector<std::string> rows;
    const auto& counts = StandaloneCountsSnapshot();
    std::string heading = preferences.title ? UiString(UiText::Title) : "";
    if (preferences.status) {
        if (!heading.empty()) heading += " | ";
        heading += UiString(g_enabled.load() ? (g_mode.load() == Mode::Current ? UiText::ModeCurrent : UiText::ModeInherited) : UiText::Paused);
    }
    if (!heading.empty()) rows.push_back(std::move(heading));
    if (preferences.areaName || preferences.areaProgress || preferences.totalProgress) {
        if (!counts.valid) rows.emplace_back(UiString(UiText::WaitingData));
        else {
            if (preferences.areaName) rows.emplace_back(AreaName(counts));
            const bool current = g_mode.load() == Mode::Current;
            if (preferences.areaProgress && !counts.map.empty())
                rows.push_back(Format(UiString(current ? UiText::AreaCurrent : UiText::AreaInherited),
                    current ? counts.map_current : counts.map_inherited, counts.map_total));
            // 沿用旧简报：没有当前地区时，地区计数退回总计；显式开启总计时不重复。
            if (preferences.totalProgress || (preferences.areaProgress && counts.map.empty()))
                rows.push_back(Format(UiString(current ? UiText::CurrentOpened : UiText::InheritedOpened),
                    current ? counts.current : counts.inherited));
        }
    }
    if (rows.empty() && preview) rows.emplace_back(Localize("未选择显示内容", "表示項目が未選択です", "No content selected", "未選擇顯示內容",
        "Kein Inhalt ausgewählt", "Aucun contenu sélectionné", "No se ha seleccionado contenido", "표시할 내용이 없습니다"));
    return rows;
}
}

void InitializeStandaloneHud(const std::wstring& dataFolder) noexcept {
    try {
        folder = dataFolder; preferences = {};
        const auto result = ReadHudPreferences(folder, preferences);
        loadFailed = result == HudReadResult::Invalid || result == HudReadResult::IoError;
        saveFailed = moving = dragging = false;
        SetStandaloneHudVisible(preferences.enabled);
        if (loadFailed) Log("HUD preferences unavailable; safe display defaults retained.");
    } catch (...) { loadFailed = true; }
}
const HudPreferences& StandaloneHudPreferences() noexcept { return preferences; }
const char* StandaloneHudSettingsLabel() noexcept {
    return Localize("HUD 显示", "HUD 表示", "HUD display", "HUD 顯示", "HUD-Anzeige", "Affichage du HUD", "Visualización del HUD", "HUD 표시");
}
void DrawStandaloneHudSettings(const Sky2Frame& frame) {
    const auto& ui = PanelUi();
    ui.begin_disabled(!frame.foreground);
    const int columns = sky2ui::Columns(ui, "hud.settings.columns", 300.0f);
    sky2ui::BeginCard(ui, "hud.content");
    sky2ui::Section(ui, StandaloneHudSettingsLabel(), Localize("勾选简报内容；位置与显示设置会在重启后保留。", "表示項目と位置は再起動後も保持されます。",
        "Choose HUD content. Position and display settings persist after restart.", "勾選簡報內容；位置與顯示設定會在重新啟動後保留。",
        "Inhalte auswählen. Position und Anzeige bleiben nach einem Neustart erhalten.", "Choisissez le contenu. La position et l’affichage sont conservés au redémarrage.",
        "Elige el contenido. La posición y los ajustes se conservan al reiniciar.", "내용을 선택하세요. 위치와 표시 설정은 재시작 후에도 유지됩니다."));
    bool changed = false;
    const auto checkbox = [&](const char* id, const char* label, bool& value) {
        int32_t checked = value ? 1 : 0;
        if (ui.checkbox(id, label, &checked) && frame.foreground) { value = checked != 0; changed = true; }
    };
    preferences.enabled = StandaloneHudVisible();
    checkbox("hud.enabled", Localize("显示 HUD", "HUD を表示", "Show HUD", "顯示 HUD", "HUD anzeigen", "Afficher le HUD", "Mostrar HUD", "HUD 표시"), preferences.enabled);
    checkbox("hud.title", Localize("标题", "タイトル", "Title", "標題", "Titel", "Titre", "Título", "제목"), preferences.title);
    checkbox("hud.status", Localize("统计口径与暂停状态", "集計方法と一時停止状態", "Counting mode and pause status", "統計口徑與暫停狀態",
        "Zählmodus und Pausenstatus", "Mode de comptage et état de pause", "Modo de recuento y estado de pausa", "집계 방식 및 일시 정지 상태"), preferences.status);
    checkbox("hud.area_name", Localize("当前统计地区名称", "現在の集計エリア名", "Current area name", "目前統計地區名稱",
        "Name des aktuellen Gebiets", "Nom de la zone actuelle", "Nombre de la zona actual", "현재 집계 지역 이름"), preferences.areaName);
    checkbox("hud.area_progress", Localize("当前地区收集数量", "現在エリアの収集数", "Current area collection count", "目前地區收集數量",
        "Gesammelte Truhen im Gebiet", "Nombre de coffres dans la zone", "Recuento de cofres de la zona", "현재 지역 수집 개수"), preferences.areaProgress);
    checkbox("hud.total_progress", Localize("全局收集数量", "全体の収集数", "Overall collection count", "全域收集數量",
        "Gesamte gesammelte Truhen", "Nombre total de coffres", "Recuento total de cofres", "전체 수집 개수"), preferences.totalProgress);
    if (changed) { SetStandaloneHudVisible(preferences.enabled); Save(); }
    sky2ui::EndCard(ui);
    if (columns > 1) sky2ui::NextColumn(ui);
    sky2ui::BeginCard(ui, "hud.position");
    sky2ui::Section(ui, Localize("位置与预览", "位置とプレビュー", "Position and preview", "位置與預覽", "Position und Vorschau", "Position et aperçu", "Posición y vista previa", "위치 및 미리 보기"));
    ui.text_wrapped(Localize("此页始终显示预览。选择调整位置后，用鼠标拖动简报；平时不会拦截游戏鼠标。", "このページでは常にプレビューを表示します。位置調整を選び、マウスで HUD をドラッグしてください。通常はゲーム操作を妨げません。",
        "This page always shows a preview. Choose Adjust position, then drag the HUD with the mouse. Normal play remains click-through.", "此頁始終顯示預覽。選擇調整位置後，用滑鼠拖曳簡報；平時不會攔截遊戲滑鼠。",
        "Diese Seite zeigt eine Vorschau. Position anpassen wählen und das HUD mit der Maus ziehen. Im Spiel bleiben Klicks frei.", "Cette page affiche un aperçu. Choisissez Ajuster la position, puis faites glisser le HUD. En jeu, les clics le traversent.",
        "Esta página muestra una vista previa. Elige Ajustar posición y arrastra el HUD. Durante el juego, los clics lo atraviesan.", "이 페이지에는 미리 보기가 표시됩니다. 위치 조정을 선택한 후 마우스로 HUD를 드래그하세요. 평소에는 게임 마우스를 차단하지 않습니다."));
    if (ui.button("hud.move", moving ? Localize("完成调整", "調整を完了", "Finish adjusting", "完成調整", "Anpassung beenden", "Terminer", "Terminar ajuste", "조정 완료") :
        Localize("调整位置", "位置を調整", "Adjust position", "調整位置", "Position anpassen", "Ajuster la position", "Ajustar posición", "위치 조정")) && frame.foreground) { CancelDrag(); moving = !moving; }
    if (ui.button("hud.reset_position", Localize("重置位置", "位置をリセット", "Reset position", "重設位置", "Position zurücksetzen", "Réinitialiser la position", "Restablecer posición", "위치 초기화")) && frame.foreground) {
        CancelDrag(); preferences.x = 0; preferences.y = 1; Save();
    }
    if (moving) sky2ui::Status(ui, Localize("拖动 HUD 可移动；Esc / B 或切出游戏取消当前拖动。", "HUD をドラッグして移動。Esc / B または画面切り替えで現在のドラッグを取り消します。",
        "Drag the HUD to move it. Esc / B or leaving the game cancels the current drag.", "拖曳 HUD 可移動；Esc / B 或切出遊戲取消目前拖曳。",
        "HUD zum Verschieben ziehen. Esc / B oder Fensterwechsel bricht das Ziehen ab.", "Faites glisser le HUD. Échap / B ou quitter la fenêtre annule le déplacement en cours.",
        "Arrastra el HUD. Esc / B o salir de la ventana cancela el movimiento actual.", "HUD를 드래그하여 이동하세요. Esc / B 또는 게임 전환 시 현재 드래그가 취소됩니다."));
    sky2ui::EndCard(ui);
    sky2ui::EndColumns(ui);
    if (loadFailed || saveFailed) {
        sky2ui::Status(ui, saveFailed ? Localize("HUD 设置保存失败，当前选择仅本次有效。", "HUD 設定の保存に失敗しました。変更は今回のみ有効です。", "HUD settings could not be saved; changes last for this session only.", "HUD 設定儲存失敗，目前選擇僅本次有效。",
            "HUD-Einstellungen konnten nicht gespeichert werden; Änderungen gelten nur für diese Sitzung.", "Échec de l’enregistrement du HUD ; les modifications sont limitées à cette session.", "No se pudo guardar el HUD; los cambios solo duran esta sesión.", "HUD 설정을 저장하지 못했습니다. 변경 사항은 이번 실행에만 적용됩니다.") :
            Localize("HUD 设置读取失败，已使用默认显示。", "HUD 設定を読み込めず、既定の表示を使用しています。", "HUD settings could not be read; using display defaults.", "HUD 設定讀取失敗，已使用預設顯示。",
                "HUD-Einstellungen konnten nicht gelesen werden; Standardanzeige wird verwendet.", "Lecture du HUD impossible ; affichage par défaut utilisé.", "No se pudo leer el HUD; se usan los valores predeterminados.", "HUD 설정을 읽지 못해 기본 표시를 사용합니다."), 2);
        if (ui.button("hud.retry", Localize("重试保存", "保存を再試行", "Retry saving", "重試儲存", "Speichern erneut versuchen", "Réessayer l’enregistrement", "Reintentar guardar", "저장 다시 시도")) && frame.foreground) Save();
    }
    ui.end_disabled();
}

void DrawStandaloneHud(const Sky2Frame& frame, bool settingsPage) {
    // 总开关可在旧设置卡片或公共动作中改变，这里同步到同一份持久化偏好。
    if (preferences.enabled != StandaloneHudVisible()) { preferences.enabled = StandaloneHudVisible(); Save(); }
    const bool preview = frame.panel_open && settingsPage;
    const bool interactive = preview && frame.foreground && moving;
    if (!interactive) { CancelDrag(); moving = false; }
    if ((!frame.foreground && !preview) || (!preview && (frame.panel_open || !preferences.enabled))) return;
    const auto rows = Rows(preview);
    if (rows.empty()) return;
    const float scale = std::max(0.5f, frame.scale), fontSize = 17.0f * scale, padding = 12.0f * scale;
    const float maxWidth = std::max(40.0f, std::min(frame.width - padding * 2, 460.0f * scale));
    const float wrap = std::max(16.0f, maxWidth - padding * 2);
    float width = 0, height = padding * 2;
    std::vector<ImVec2> sizes;
    for (const auto& row : rows) {
        const auto size = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, wrap, row.c_str());
        width = std::max(width, size.x); height += size.y + 4 * scale; sizes.push_back(size);
    }
    width = std::min(maxWidth, width + padding * 2); height -= 4 * scale;
    auto position = PlaceHud(preferences, frame.width, frame.height, width, height, 18 * scale);
    auto& io = ImGui::GetIO();
    if (dragging && interactive) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) CancelDrag();
        else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            RememberHudPosition(preferences, {io.MousePos.x - dragOffset.x, io.MousePos.y - dragOffset.y}, frame.width, frame.height, width, height, 18 * scale);
        } else { dragging = false; Save(); }
        position = PlaceHud(preferences, frame.width, frame.height, width, height, 18 * scale);
    }
    ImGui::SetNextWindowPos({position.x, position.y}, ImGuiCond_Always); ImGui::SetNextWindowSize({width, height}, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground;
    if (!interactive) flags |= ImGuiWindowFlags_NoInputs;
    ImGui::Begin("Sky2ChestHud", nullptr, flags);
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled({position.x, position.y}, {position.x + width, position.y + height}, IM_COL32(10, 25, 32, 238), 8 * scale);
    draw->AddRectFilled({position.x, position.y + 8 * scale}, {position.x + 3 * scale, position.y + height - 8 * scale}, IM_COL32(120, 198, 214, 255), 2 * scale);
    float y = position.y + padding;
    for (size_t index = 0; index < rows.size(); ++index) {
        draw->AddText(ImGui::GetFont(), fontSize, {position.x + padding, y}, IM_COL32(211, 225, 225, 255), rows[index].c_str(), nullptr, wrap);
        y += sizes[index].y + 4 * scale;
    }
    if (interactive) {
        draw->AddRect({position.x, position.y}, {position.x + width, position.y + height}, IM_COL32(239, 199, 97, 255), 8 * scale);
        ImGui::InvisibleButton("##hud-drag", {width, height});
        if (ImGui::IsItemActivated()) {
            dragging = true; dragStartX = preferences.x; dragStartY = preferences.y;
            dragOffset = {io.MousePos.x - position.x, io.MousePos.y - position.y};
        }
    }
    ImGui::End(); ImGui::PopStyleVar();
}
}
