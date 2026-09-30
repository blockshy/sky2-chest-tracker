// 独立版与 ASI 自带公共 UI 源码，不依赖另一份 Mod。
// 此层只把旧开关/入口映射到既有业务页面；原生写入仍由探索和传送安全队列执行。
#include "standalone_panel.h"
#include "standalone_ui/ui.h"
#include "standalone_ui/input.h"
#include "standalone_ui/hotkeys.h"
#include "standalone_hud.h"
#include "panel_pages.h"
#include "panel_state.h"
#include "controller_logic.h"
#include "tracker.h"
#include "ui_text.h"
#include <array>
#include <cstring>

namespace tracker {
namespace {
sky2solo::WindowState windowState;
// 每组记住上次子页，切换侧栏不会丢失清单、传送和返程的浏览位置。
int rememberedPages[]{0, 2, 3};
int settingsTab = 0;
int lastBusinessPage = 0;
sky2solo::HotkeyEditorState hotkeyEditor;
int GroupForPage(int page) noexcept { return page >= 3 ? 2 : (page == 2 ? 1 : 0); }

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

bool InitializeStandalonePanel() noexcept {
    // 图形上下文重建不会重置玩家的窗口可见性、当前页签或未完成的浏览位置。
    if (PanelUiReady()) return true;
    SetPanelUi(sky2solo::UiApi());
    // 原版启动即显示统计。新布局把它保留为简洁 HUD，大窗口需玩家主动打开。
    g_panel.store(false);
    return true;
}

void ApplyStandaloneActions(uint32_t pending) noexcept {
    // 窗口打开期间不接收旧业务组合，尤其不能让 View+A 的旧清单动作与 A 确认
    // 同时生效。危险操作仅通过 Main 中的确认按钮进入两次确认流程。
    if (pending & TogglePanel) {
        g_panel.store(!g_panel.load());
        sky2solo::ResetHotkeyEditor(hotkeyEditor);
        windowState.resetFocus = true;
        PanelVisibilityChanged(0);
        return;
    }
    if (g_panel.load() || sky2solo::InputOwner()) return;
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
