// 独立版与 ASI 自带公共 UI 源码，不依赖另一份 Mod。
// 此层只把旧开关/入口映射到既有业务页面；原生写入仍由探索和传送安全队列执行。
#include "standalone_panel.h"
#include "standalone_ui/ui.h"
#include "standalone_ui/input.h"
#include "standalone_ui/hotkeys.h"
#include "input_bridge.h"
#include "standalone_hud.h"
#include "panel_pages.h"
#include "panel_state.h"
#include "controller_logic.h"
#include "tracker.h"
#include "ui_text.h"
#include <array>
#include <algorithm>
#include <cstring>
#include <string>

namespace tracker {
namespace {
sky2solo::WindowState windowState;
// 每组记住上次子页，切换侧栏不会丢失清单、传送和返程的浏览位置。
int rememberedPages[]{0, 2, 3};
int settingsTab = 0;
int lastBusinessPage = 0;
sky2solo::HotkeyEditorState hotkeyEditor;
int GroupForPage(int page) noexcept { return page >= 3 ? 2 : (page == 2 ? 1 : 0); }

const char* DisplayModeLabel() {
    return Localize("显示模式", "表示モード", "Display mode", "顯示模式",
        "Anzeigemodus", "Mode d’affichage", "Modo de visualización", "표시 모드");
}
std::string DisplayModeShortcut() {
    // 读取当前已提交绑定，玩家改键后无需重开窗口；不展示尚未保存的编辑候选。
    const auto keys = sky2solo::ReadHotkeys();
    for (size_t index = 0; index < keys.count; ++index) {
        const auto* definition = sky2solo::HotkeyInfo(index);
        if (!definition || std::strcmp(definition->id, "chest.cycle_mode") != 0) continue;
        const auto& binding = keys.bindings[index];
        std::string text = binding.key ? sky2solo::HotkeyKeyboardText(binding) : "";
        if (binding.pad) {
            if (!text.empty()) text += " / ";
            text += sky2solo::HotkeyPadText(binding);
        }
        return text;
    }
    return {};
}
float DisplayModeButtonHeight(const char* label, float width) {
    const auto& style = ImGui::GetStyle();
    const float wrap = std::max(1.0f, width - style.FramePadding.x * 2);
    return std::max(ImGui::GetFrameHeight(), ImGui::CalcTextSize(label, nullptr, false, wrap).y + style.FramePadding.y * 2);
}
float MeasureDisplayMode(void*, float width, float) {
    // 标题、按钮与快捷键按同一实际宽度换行。尤其在 720p 与长译名下，固定底区
    // 必须先预留完整高度，不能依赖上一帧尺寸，否则侧栏会随内容更新来回跳动。
    const float gap = ImGui::GetStyle().ItemSpacing.y;
    float height = 1 + gap + ImGui::CalcTextSize(DisplayModeLabel(), nullptr, false, width).y + gap;
    height += DisplayModeButtonHeight(UiString(UiText::ModeCurrent), width) + gap;
    height += DisplayModeButtonHeight(UiString(UiText::InheritedColumn), width) + gap;
    const auto shortcut = DisplayModeShortcut();
    if (!shortcut.empty()) height += ImGui::CalcTextSize(shortcut.c_str(), nullptr, false, width).y + gap;
    return height;
}
void DrawDisplayMode(void*, const Sky2Frame& frame, int) {
    ImGui::Separator();
    ImGui::TextWrapped("%s", DisplayModeLabel());
    const float width = ImGui::GetContentRegionAvail().x;
    const auto button = [&](const char* id, const char* label, Mode mode) {
        const bool selected = g_mode.load() == mode;
        const auto position = ImGui::GetCursorScreenPos();
        const float height = DisplayModeButtonHeight(label, width);
        ImGui::PushID(id);
        // 直接使用无导航的侧栏控件，不交给 Main 的候选登记器。键盘/手柄通过
        // 下方显示的已配置快捷键切换，黄色选中始终留在右侧具体功能内容中。
        if (ImGui::Selectable("##mode", selected, 0, {width, height}) && frame.foreground)
            SetStandaloneDisplayMode(mode);
        const auto color = ImGui::GetColorU32(selected ? ImVec4(.57f, .91f, .80f, 1) : ImVec4(.62f, .70f, .79f, 1));
        const auto padding = ImGui::GetStyle().FramePadding;
        ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
            {position.x + padding.x, position.y + padding.y}, color, label, nullptr, std::max(1.0f, width - padding.x * 2));
        ImGui::PopID();
    };
    button("chests.mode.current", UiString(UiText::ModeCurrent), Mode::Current);
    button("chests.mode.inherited", UiString(UiText::InheritedColumn), Mode::Inherited);
    const auto shortcut = DisplayModeShortcut();
    if (!shortcut.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", shortcut.c_str());
        ImGui::PopStyleColor();
    }
}

int32_t SKY2_CALL LanguageIndex() noexcept {
    // 宝箱旧语言表是简中/日/英/繁中，公共界面按简中/繁中/日/英排列。
    constexpr int32_t table[]{0, 2, 3, 1, 4, 5, 6, 7};
    const auto language = static_cast<unsigned>(CurrentLanguage());
    return language < std::size(table) ? table[language] : 0;
}
void ChangeGroup(void*, int group) {
    // 快捷键离页后丢弃尚未保存的候选；侧栏变化也必须撤销业务确认。
    sky2solo::ResetHotkeyEditor(hotkeyEditor);
    PanelVisibilityChanged(0);
    if (group < 0 || group >= static_cast<int>(std::size(rememberedPages))) return;
    const int page = StandalonePageIndex();
    rememberedPages[GroupForPage(page)] = page;
    StandaloneChoosePage(rememberedPages[group]);
}
void DrawHeader(void*, const Sky2Frame& frame, int group) {
    if (group == 3) return;
    if (group != 1) { DrawPanelHeader(&frame); return; }
    // 设置组把业务开关与 HUD 内容、位置调整分开，页号仍由业务页维护。
    const char* labels[]{Localize("功能开关", "機能", "Features", "功能開關", "Funktionen", "Fonctions", "Funciones", "기능"), StandaloneHudSettingsLabel()};
    const auto& ui = PanelUi();
    const int next = ui.tab_bar("settings.pages", labels, 2, settingsTab);
    if (frame.foreground && next >= 0 && next < 2 && next != settingsTab) { settingsTab = next; PanelVisibilityChanged(0); }
    ui.spacing();
}
void DrawContent(void*, const Sky2Frame& frame, int group) {
    if (group == 3) sky2solo::DrawHotkeySettings(hotkeyEditor, LanguageIndex());
    else if (group == 1 && settingsTab == 1) DrawStandaloneHudSettings(frame);
    else DrawPanelPage(&frame);
}
}

bool StandaloneModeShortcutEditing() noexcept { return g_panel.load() && windowState.section == 3; }

bool InitializeStandalonePanel() noexcept {
    // 图形上下文重建不会重置玩家的窗口可见性、当前页签或未完成的浏览位置。
    if (PanelUiReady()) return true;
    SetPanelUi(sky2solo::UiApi());
    // 原版启动即显示统计。新布局把它保留为简洁 HUD，大窗口需玩家主动打开。
    g_panel.store(false);
    return true;
}

void ApplyStandaloneActions(uint32_t pending) noexcept {
    // 窗口打开期间仅额外接受底栏模式键，其余业务组合保持屏蔽，避免组合键与
    // Main 确认同时生效。危险操作仅通过 Main 中的确认按钮进入两次确认流程。
    if (pending & TogglePanel) {
        g_panel.store(!g_panel.load());
        sky2solo::ResetHotkeyEditor(hotkeyEditor);
        windowState.resetFocus = true;
        PanelVisibilityChanged(0);
        return;
    }
    if (g_panel.load()) {
        // 显示模式固定在侧栏底部，因此打开窗口时仍允许其快捷键；编辑文本或
        // 弹出候选期间由输入层拒绝，防止同一次输入同时编辑字段和切换口径。
        if (InputModeShortcutAllowed()) ApplyPanelActions(pending & ToggleMode);
        return;
    }
    if (sky2solo::InputOwner()) return;
    ApplyPanelActions(pending);
    if (g_panel.load()) windowState.resetFocus = true;

}

void DrawStandalonePanel(const Sky2Frame& inputFrame) {
    if (!PanelUiReady()) return;
    Sky2Frame frame = inputFrame;
    frame.panel_open = g_panel.load() ? 1 : 0;
    frame.page_active = frame.panel_open && windowState.section != 3;
    TickPanel(&frame);
    if (!frame.panel_open) {
        // 输入所有权被其它窗口接管也属于离页，不能在重新打开时留下未保存候选。
        sky2solo::ResetHotkeyEditor(hotkeyEditor);
        DrawStandaloneHud(frame, false);
        return;
    }
    const int page = StandalonePageIndex();
    const int group = GroupForPage(page);
    rememberedPages[group] = page;
    // 内容页的快捷按钮也能跨组打开设置；下次绘制同步侧栏，并让焦点回到新内容。
    if (windowState.section != group && (windowState.section != 3 || page != lastBusinessPage)) {
        windowState.section = group; windowState.resetFocus = true;
        sky2solo::ResetHotkeyEditor(hotkeyEditor);
    }
    lastBusinessPage = page;
    const char* sections[]{
        Localize("宝箱追踪", "宝箱追跡", "Chest tracking", "寶箱追蹤", "Truhensuche", "Suivi des coffres", "Seguimiento de cofres", "보물 상자 추적"),
        Localize("功能设置", "機能設定", "Settings", "功能設定", "Einstellungen", "Réglages", "Ajustes", "기능 설정"),
        Localize("传送与返程", "移動と帰還", "Travel and return", "傳送與返程", "Reise und Rückkehr", "Voyage et retour", "Viaje y regreso", "이동과 귀환"),
        Localize("快捷键", "ショートカット", "Shortcuts", "快捷鍵", "Tastenkürzel", "Raccourcis", "Atajos", "단축키")
    };
    sky2solo::WindowSpec spec{};
    spec.id = "Sky2ChestTracker"; spec.title = UiString(UiText::Title);
    spec.description = Localize("查看收集进度，管理探索辅助与安全传送。", "収集状況、探索補助、移動を管理します。",
        "Review collection progress, exploration helpers and travel.", "查看收集進度，管理探索輔助與安全傳送。",
        "Sammelfortschritt, Erkundungshilfen und Reisen verwalten.", "Consultez la progression, les aides d’exploration et les voyages.",
        "Consulta el progreso, las ayudas de exploración y los viajes.", "수집 진행도, 탐색 보조와 이동을 관리합니다.");
    spec.sections = sections; spec.sectionCount = static_cast<int>(std::size(sections));
    spec.header = &DrawHeader; spec.draw = &DrawContent; spec.changed = &ChangeGroup;
    spec.measureAsideFooter = &MeasureDisplayMode; spec.asideFooter = &DrawDisplayMode;
    spec.language = LanguageIndex();
    if (!sky2solo::DrawWindow(windowState, spec, frame)) {
        g_panel.store(false);
        sky2solo::ResetHotkeyEditor(hotkeyEditor);
        PanelVisibilityChanged(0);
    }
    frame.panel_open = g_panel.load() ? 1 : 0;
    // HUD 在主窗之后绘制，调整模式下即使与主窗重叠也能看到并拖动预览。
    DrawStandaloneHud(frame, windowState.section == 1 && settingsTab == 1);
}
}
