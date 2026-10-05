// 复用既有业务快照替身，但绘制真实独立窗口、字体和宝箱页面。测试只允许
// 准备一次确认，不进行第二次提交；PNG 与检查报告均保存在调用方指定目录。
#define SKY2_STANDALONE_VISUAL_FIXTURE 1
#include "panel_runtime_tests.cpp"
#include "standalone_capture.h"
#include "standalone_panel.h"
#include "standalone_hud.h"
#include "standalone_hotkeys.h"
#include "panel_fonts.h"
#include "localization.h"
#include "standalone_ui/ui.h"
#include <imgui_internal.h>
#include <imgui_impl_dx11.h>
#include <fstream>

namespace tracker {
// 离屏夹具不安装真实游戏输入挂钩；用相同 UI 编辑状态验证窗口动作分发，
// 物理输入源与跨线程开关由 standalone_bridge_tests 的真实输入桥覆盖。
bool InputModeShortcutAllowed() noexcept {
    return ImGui::GetCurrentContext() && !StandaloneModeShortcutEditing() && !ImGui::GetIO().WantTextInput &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}
}

namespace {
const Sky2UiApi* actual = nullptr;
Sky2UiApi observed{};
ImGuiWindow* mainWindow = nullptr;
ImVec2 headerPosition{};
bool prepareConfirmation = false;
bool expandedHelp = false;
bool hudPage = false, startHudMove = false;
bool mapApplyPage = false;
bool hudLastToggleVisited = false;
int32_t SKY2_CALL ObserveCheckbox(const char* id, const char* text, int32_t* value) {
    const int result = actual->checkbox(id, text, value);
    if (std::strcmp(id, "hud.total_progress") == 0 && GImGui->NavId == GImGui->LastItemData.ID)
        hudLastToggleVisited = true;
    return result;
}
int32_t SKY2_CALL ObserveTabBar(const char* id, const char* const* labels, int32_t count, int32_t selected) {
    headerPosition = ImGui::GetCursorScreenPos();
    const int result = actual->tab_bar(id, labels, count, selected);
    if (std::strcmp(id, "settings.pages") == 0) {
        if (mapApplyPage) return 0;
        if (hudPage) return 1;
    }
    return result;
}
void SKY2_CALL ObserveSize(float* width, float* height) {
    actual->content_size(width, height);
    for (auto* window = ImGui::GetCurrentWindow(); window; window = window->ParentWindow) {
        // 子卡片名称包含父窗口全路径，必须匹配最后一级名称，不能把具有
        // 亚像素 ScrollMax 的自动高度卡片误当作 Main 外层视口。
        const auto* leaf = std::strrchr(window->Name, '/');
        if (leaf && std::strncmp(leaf + 1, "main_", 5) == 0) { mainWindow = window; break; }
    }
}
int32_t SKY2_CALL ObserveButton(const char* id, const char* text) {
    const int clicked = actual->button(id, text);
    if (startHudMove && std::strcmp(id, "hud.move") == 0) { startHudMove = false; return 1; }
    if (prepareConfirmation && std::strcmp(id, "travel.confirm") == 0) {
        prepareConfirmation = false;
        return 1; // 一次准备足以检查警告及按钮排版，绝不模拟第二次业务提交。
    }
    return clicked;
}
int32_t SKY2_CALL ObserveDisclosure(const char* id, const char* text, int32_t open) {
    // 滚动场景固定为玩家已展开说明的合法页面状态。仍由生产折叠控件绘制
    // 全部内容；无需缩小真实列表或伪造滚动条，确认自然出现的外层可滚区域。
    if (expandedHelp && (std::strcmp(id, "chests.help") == 0 || std::strcmp(id, "travel.help") == 0)) open = 1;
    return actual->disclosure(id, text, open);
}
void Capture(const std::filesystem::path& path, int page, int width, int height, bool scroll, std::ostream& report, int hudScenario = 0) {
    using namespace standalone_visual;
    ID3D11Device* device = nullptr; ID3D11DeviceContext* context = nullptr; D3D_FEATURE_LEVEL level{};
    Require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, &level, &context)), "WARP device");
    D3D11_TEXTURE2D_DESC description{};
    description.Width = width; description.Height = height; description.MipLevels = description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM; description.SampleDesc.Count = 1;
    description.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D* texture = nullptr; ID3D11RenderTargetView* target = nullptr;
    Require(SUCCEEDED(device->CreateTexture2D(&description, nullptr, &texture)) &&
        SUCCEEDED(device->CreateRenderTargetView(texture, nullptr, &target)), "render texture");
    auto* gui = ImGui::CreateContext(); auto& io = ImGui::GetIO();
    io.IniFilename = io.LogFilename = nullptr;
    io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    tracker::LoadStandaloneFonts(io); sky2solo::ConfigureTheme();
    const float scale = std::clamp(height / 1080.0f, 0.7f, 2.5f);
    ImGui::GetStyle().FontScaleDpi = scale;
    Require(ImGui_ImplDX11_Init(device, context), "DX11 backend");
    tracker::StandaloneChoosePage(page); tracker::g_panel.store(hudScenario != 2);
    hudPage = hudScenario == 1 || hudScenario == 4;
    hudLastToggleVisited = false;
    mainWindow = nullptr; expandedHelp = scroll; ImVec2 initialHeader{}, initialModePosition{}; float initialScroll = 0;
    ImGuiWindow* modeFooter = nullptr;
    const int frames = hudScenario == 1 ? 32 : (hudScenario == 4 ? 110 : ((scroll || hudScenario == 3) ? 100 : 7));
    tracker::HudPreferences committedHud;
    for (int index = 0; index < frames; ++index) {
        ImGui_ImplDX11_NewFrame(); io.DisplaySize = {float(width), float(height)}; io.DeltaTime = 1.0f / 60;
        XINPUT_GAMEPAD pad{};
        if (scroll && index >= 10 && index < 90) pad.sThumbRY = -30000;
        if (hudScenario == 3) {
            if (index == 10) pad.wButtons = XINPUT_GAMEPAD_RIGHT_SHOULDER;
            if (index == 15) pad.wButtons = XINPUT_GAMEPAD_DPAD_RIGHT;
            if (index >= 20 && index < 65) pad.wButtons = XINPUT_GAMEPAD_DPAD_DOWN;
            if (index >= 70 && index < 92) pad.sThumbRY = -30000;
        }
        if (hudScenario == 4) {
            // 实际 720p 视口不能同时容纳六个显示开关。持续向下应能到达最末
            // 的全局数量复选框，右摇杆则继续滚动当前 Main，不能被卡片截住。
            if (index >= 10 && index < 70) pad.wButtons = XINPUT_GAMEPAD_DPAD_DOWN;
            if (index >= 75 && index < 97) pad.sThumbRY = -30000;
        }
        sky2solo::FeedGamepad(&pad, true);
        if (hudScenario == 1) {
            if (index == 5) startHudMove = true;
            auto* hud = ImGui::FindWindowByName("Sky2ChestHud");
            if (hud && (index == 9 || index == 19)) io.AddMousePosEvent(hud->Pos.x + 20, hud->Pos.y + 15);
            if (index == 10 || index == 20) io.AddMouseButtonEvent(0, true);
            if (index == 11) io.AddMousePosEvent(240, 140);
            if (index == 21) io.AddMousePosEvent(440, 340);
            if (index == 12 || index == 23) io.AddMouseButtonEvent(0, false);
        }
        ImGui::NewFrame();
        const Sky2Frame frame{sizeof(Sky2Frame), float(width), float(height), scale, GetTickCount64(), (hudScenario == 1 && index == 22) ? 0 : 1, 1, 1, (hudScenario == 1 || hudScenario == 2) ? 0 : 1};
        tracker::DrawStandalonePanel(frame);
        if (tracker::g_panel.load()) {
            for (auto* window : GImGui->Windows) {
                const auto* leaf = std::strrchr(window->Name, '/');
                if (window->Active && leaf && std::strncmp(leaf + 1, "aside_footer_", 13) == 0) { modeFooter = window; break; }
            }
            if (index >= 6) {
                Require(modeFooter && modeFooter->ScrollMax.y < 1, "mode footer fits measured height in the active language");
                if (frame.controller && GImGui->NavWindow && GImGui->NavId)
                    Require(std::strstr(GImGui->NavWindow->Name, "/main_"), "mode footer never captures yellow Main focus");
            }
        }
        if (hudScenario == 1 || hudScenario == 4) {
            for (auto* window : GImGui->Windows) {
                const auto* leaf = std::strrchr(window->Name, '/');
                if (window->Active && leaf && std::strncmp(leaf + 1, "main_", 5) == 0) { mainWindow = window; break; }
            }
        }
        if (hudScenario == 3 && index > 11) {
            bool shortcutsVisible = false;
            for (auto* window : GImGui->Windows) if (window->Active && std::strstr(window->Name, "hotkey_editor")) {
                shortcutsVisible = true;
                // 快捷键页不调用业务 content_size，需从实际编辑器回溯当前 Main，
                // 避免沿用进入第四侧栏前的传送页指针而误报外层滚动值。
                for (auto* parent = window; parent; parent = parent->ParentWindow) {
                    const auto* leaf = std::strrchr(parent->Name, '/');
                    if (leaf && std::strncmp(leaf + 1, "main_", 5) == 0) { mainWindow = parent; break; }
                }
            }
            Require(shortcutsVisible, "fourth sidebar shortcut page stays selected across frames");
            Require(tracker::StandaloneModeShortcutEditing(), "embedded shortcut editor is protected without requiring an ImGui popup");
            const auto previousMode = tracker::g_mode.load();
            tracker::ApplyStandaloneActions(tracker::ToggleMode);
            Require(tracker::g_mode.load() == previousMode, "shortcut editing never applies a queued display-mode action");
            Require(GImGui->NavWindow && std::strstr(GImGui->NavWindow->Name, "/main_"), "shortcut navigation focus remains inside Main");
            if (index == 17) Require(std::strstr(GImGui->NavWindow->Name, "hotkey_editor"), "directional navigation reaches shortcut editor column");
        }
        if (hudScenario == 1 && index == 14) {
            committedHud = tracker::StandaloneHudPreferences();
            Require(committedHud.x > 0 && committedHud.y < 1, "real HUD drag changes normalized position");
            tracker::HudPreferences saved;
            Require(tracker::ReadHudPreferences((path.parent_path() / L"hud-state").wstring(), saved) == tracker::HudReadResult::Valid &&
                std::abs(saved.x - committedHud.x) < .00001f && std::abs(saved.y - committedHud.y) < .00001f, "HUD drag saves real preference file");
        }
        if (hudScenario == 1 && index == 24) {
            const auto& current = tracker::StandaloneHudPreferences();
            Require(current.x == committedHud.x && current.y == committedHud.y, "focus loss cancels current HUD drag");
        }
        if (hudScenario == 2 && index == 6) {
            const auto* hud = ImGui::FindWindowByName("Sky2ChestHud");
            Require(hud && (hud->Flags & ImGuiWindowFlags_NoInputs) == ImGuiWindowFlags_NoInputs, "normal HUD remains click-through");
        }
        if (index == 6) { initialHeader = headerPosition; initialModePosition = modeFooter ? modeFooter->Pos : ImVec2{}; initialScroll = mainWindow ? mainWindow->Scroll.y : 0; }
        ImGui::Render(); const float background[]{.025f, .04f, .06f, 1};
        context->OMSetRenderTargets(1, &target, nullptr); context->ClearRenderTargetView(target, background);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
    report << path.filename().string() << " main=" << bool(mainWindow)
        << " scroll=" << (mainWindow ? mainWindow->Scroll.y : -1)
        << " max=" << (mainWindow ? mainWindow->ScrollMax.y : -1) << '\n';
    if (scroll) {
        Require(mainWindow && mainWindow->ScrollMax.y > 0 && mainWindow->Scroll.y > initialScroll, "real Main RS scroll");
        Require(std::abs(headerPosition.x - initialHeader.x) < .25f && std::abs(headerPosition.y - initialHeader.y) < .25f,
            "Header remains fixed while Main scrolls");
        Require(modeFooter && std::abs(modeFooter->Pos.y - initialModePosition.y) < .25f,
            "mode footer remains fixed while Main scrolls");
    }
    if (hudScenario == 4) {
        Require(hudLastToggleVisited, "720p HUD final content toggle reachable with directional navigation");
        Require(mainWindow && mainWindow->ScrollMax.y > 0 && mainWindow->Scroll.y > initialScroll, "720p HUD Main scroll remains reachable");
        Require(GImGui->NavWindow && std::strstr(GImGui->NavWindow->Name, "/main_"), "HUD settings focus remains inside Main");
    }
    SavePng(device, context, texture, path);
    ImGui_ImplDX11_Shutdown(); ImGui::DestroyContext(gui);
    target->Release(); texture->Release(); context->Release(); device->Release();
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path output(argv[1]); std::filesystem::create_directories(output);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    assert(tracker::InitializeStandalonePanel());
    // 图形重建可能再次调用初始化；不能把已经打开的独立窗口悄悄关闭。
    tracker::g_panel.store(true);
    assert(tracker::InitializeStandalonePanel() && tracker::g_panel.load());
    const auto hudFolder = output / L"hud-state"; std::filesystem::create_directories(hudFolder);
    tracker::HudPreferences hudDefaults;
    hudDefaults.areaName = hudDefaults.totalProgress = true;
    assert(tracker::WriteHudPreferences(hudFolder.wstring(), hudDefaults));
    tracker::InitializeStandaloneHud(hudFolder.wstring());
    assert(tracker::InitializeStandaloneHotkeys(hudFolder.wstring()));
    actual = &tracker::PanelUi(); observed = *actual;
    observed.tab_bar = &ObserveTabBar; observed.content_size = &ObserveSize; observed.button = &ObserveButton;
    observed.disclosure = &ObserveDisclosure;
    observed.checkbox = &ObserveCheckbox;
    tracker::SetPanelUi(&observed);
    fixture::counts.valid = true; fixture::counts.maps = tracker::CountMaps(nullptr, 0);
    for (size_t index = 0; index < fixture::counts.maps.size(); ++index) {
        auto& row = fixture::counts.maps[index]; row.current = index % 4 == 0 ? row.total : row.total * 2 / 3;
        row.inherited = std::min(row.total, row.current + 2);
        fixture::counts.current += row.current; fixture::counts.inherited += row.inherited;
    }
    fixture::counts.map = "mp6011"; fixture::counts.map_total = 12;
    fixture::counts.map_current = 8; fixture::counts.map_inherited = 11;
    fixture::exploration.mapAvailable = fixture::exploration.travelAvailable = fixture::exploration.mapEnabled = true;
    fixture::context.available = fixture::context.valid = fixture::context.browsing = fixture::context.returnPointReady = true;
    fixture::context.busy = false; fixture::context.browseIdentity = 12; fixture::context.chapter = 4;
    std::memcpy(fixture::context.scene, "mp6011", sizeof("mp6011"));
    fixture::returned.hasRecord = true; fixture::returned.count = 3;
    std::memcpy(fixture::returned.record.point.scene, "mp6011", sizeof("mp6011"));
    fixture::returned.record.createdUnixSeconds = 1790670600;
    std::ofstream report(output / L"standalone-visual-report.txt");
    Capture(output / L"chest-list-1000p.png", 1, 1600, 1000, false, report);
    prepareConfirmation = true;
    Capture(output / L"chest-travel-1000p.png", 3, 1600, 1000, false, report);
    tracker::PanelVisibilityChanged(0);
    Capture(output / L"chest-list-720p.png", 1, 1280, 720, false, report);
    Capture(output / L"chest-list-720p-scrolled.png", 1, 1280, 720, true, report);
    prepareConfirmation = true;
    Capture(output / L"chest-travel-720p.png", 3, 1280, 720, false, report);
    tracker::PanelVisibilityChanged(0);
    tracker::SetDisplayLanguage(tracker::Language::German);
    Capture(output / L"chest-travel-de-720p-scrolled.png", 3, 1280, 720, true, report);
    tracker::SetDisplayLanguage(tracker::Language::Chinese);
    Capture(output / L"chest-hud-settings-720p.png", 2, 1280, 720, false, report, 1);
    Capture(output / L"chest-hud-720p.png", 2, 1280, 720, false, report, 2);
    Capture(output / L"chest-shortcuts-720p.png", 3, 1280, 720, false, report, 3);
    Capture(output / L"chest-hud-settings-720p-scrolled.png", 2, 1280, 720, false, report, 4);
    // 左侧底区独立测量八语长标签；720p 是最容易暴露按钮换行/底部裁剪的视口。
    for (unsigned language = 0; language < 8; ++language) {
        tracker::SetDisplayLanguage(static_cast<tracker::Language>(language));
        Capture(output / (L"chest-overview-language-" + std::to_wstring(language) + L"-720p.png"), 0, 1280, 720, false, report);
    }
    tracker::SetDisplayLanguage(tracker::Language::Chinese);
    fixture::context.browsing = false; fixture::context.canPrepareMap = true;
    Capture(output / L"chest-travel-prepare-720p.png", 3, 1280, 720, false, report);
    Capture(output / L"chest-return-prepare-720p.png", 4, 1280, 720, false, report);
    fixture::exploration.travelPending = true;
    mapApplyPage = true;
    Capture(output / L"chest-map-apply-720p.png", 2, 1280, 720, false, report);
    assert(fixture::submitted == 0);
    report << "All production page checks passed; no travel submitted.\n";
    CoUninitialize();
}
