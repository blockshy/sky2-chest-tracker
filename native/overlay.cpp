// Direct3D 11 只读信息面板：复用游戏的 Present 时机绘制，由输入层传递组合键动作。
// 每帧释放后缓冲视图，避免持有引用导致窗口缩放、全屏切换时 ResizeBuffers 失败。
#include "tracker.h"
#include "input_bridge.h"
#include "exploration.h"
#include "revisit.h"
#include "revisit_policy.h"
#include "ui_scale.h"
#include "ui_text.h"
#include "localized_names.h"
#include "game_language.h"
#include <d3d11.h>
#include <dxgi.h>
#include <MinHook.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <mutex>
#include <array>
#include <cstring>
#include <ctime>
namespace tracker {
using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
static PresentFn g_originalPresent = nullptr;
static ImGuiContext* g_context = nullptr;
static ID3D11Device* g_device = nullptr;
static ID3D11DeviceContext* g_deviceContext = nullptr;
static HWND g_window = nullptr;
static std::mutex g_renderLock;
// 清单默认收起；筛选只影响清单，不隐藏地图上的宝箱，也不改变两组计数。
static bool g_mapList = false;
static bool g_missingOnly = true;
static size_t g_mapPage = 0;
static constexpr size_t kRowsPerPage = 12;
// 回访界面仍只接收既有过滤层派发的组合键，不抢占游戏鼠标或单独 A/B 按键。
static bool g_revisitWindow = false;
static size_t g_revisitSelection = 0;
// 导航位置只保存在本次进程内：关闭窗口、隐藏主面板及切换清单均不重置。
// 单独记录首次打开，避免每次恢复窗口都覆盖玩家选中的目的地；不持久化到存档。
static bool g_revisitSelectionInitialized = false;
static RevisitConfirmation g_revisitConfirmation;
static RevisitNativeContext g_revisitConfirmContext{};
static uint64_t g_revisitRequestToken = 0;
static bool g_revisitSubmissionRejected = false;
// 逐语言检查实际字形，不能用“找到某份 CJK 字体”推断简繁汉字、假名、韩文均齐全。
static std::array<bool, kLanguageCount> g_languageFontComplete{};

static bool RevisitCanSubmit(const RevisitNativeContext& context) {
    const auto phase = ReadRevisitNativeStatus().phase;
    return RevisitReady() && context.available && RevisitContextAllowed(context) &&
        context.browsing && !context.busy &&
        phase != RevisitNativePhase::Queued && phase != RevisitNativePhase::ClosingMap &&
        phase != RevisitNativePhase::Dispatched;
}
static void CancelRevisitConfirmation() {
    g_revisitConfirmation.Cancel();
    CancelRevisitNativeTravel();
    g_revisitSubmissionRejected = false;
}

// 确认绑定到本次只读场景和进度快照。加载其他存档、转场、退出原生地图都会解除确认；
// 原生线程还会用新的上下文复核，不能把这里的异步快照当成传送许可。
static void UpdateRevisitConfirmation() {
    const auto context = ReadRevisitNativeContext();
    if (!g_revisitWindow || !RevisitCanSubmit(context) ||
        context.chapter != g_revisitConfirmContext.chapter ||
        context.browseIdentity != g_revisitConfirmContext.browseIdentity ||
        context.progressSignature != g_revisitConfirmContext.progressSignature ||
        std::strcmp(context.scene, g_revisitConfirmContext.scene) != 0)
        g_revisitConfirmation.Cancel();
}

// 两种输入来源共用相同的动作处理，确保显示模式切换、筛选和分页行为完全一致。
static void ApplyActions(uint32_t actions) {
    if (actions & ToggleRevisit) {
        g_revisitWindow = !g_revisitWindow;
        CancelRevisitConfirmation();
        if (g_revisitWindow) {
            g_panel.store(true);
            g_mapList = false;
            // 本次进程首次打开时仍优先帮助旧地图存档寻找返程；之后恢复原选中项与页码。
            // 这里只记住浏览位置，关闭窗口时上方仍取消二次确认，不保留传送授权。
            if (!g_revisitSelectionInitialized) {
                g_revisitSelection = RevisitRecoveryRequired(ReadRevisitNativeContext()) ? RevisitDestinationCount() - 1 : 0;
                g_revisitSelectionInitialized = true;
            }
        }
    }
    if (g_revisitWindow) {
        const auto pageActions=actions & (PreviousPage | NextPage);
        if (pageActions) {
            CancelRevisitConfirmation();
            // 与宝箱清单共用PgUp/PgDn及View+LB/RB翻页。相反方向同帧按下不移动，
            // 整页操作优先于逐项，避免肩键与扳机同时输入时意外多移动一项。
            if (pageActions==PreviousPage || pageActions==NextPage)
                g_revisitSelection=RevisitPageSelection(g_revisitSelection,pageActions==NextPage);
        } else if (const auto itemActions=actions & (PreviousTravelItem | NextTravelItem)) {
            CancelRevisitConfirmation();
            if (itemActions==PreviousTravelItem)
                g_revisitSelection = (g_revisitSelection + RevisitDestinationCount() - 1) % RevisitDestinationCount();
            if (itemActions==NextTravelItem) g_revisitSelection = (g_revisitSelection + 1) % RevisitDestinationCount();
        }
        if ((actions & ToggleFilter) && RevisitDestinationAt(g_revisitSelection).id == kRevisitReturnTarget) {
            CancelRevisitConfirmation();
            CycleRevisitReturnRecord(ReadRevisitNativeContext());
        }
        if (actions & ConfirmRevisit) {
            const auto context = ReadRevisitNativeContext();
            const auto target = RevisitDestinationAt(g_revisitSelection).id;
            if (RevisitCanSubmit(context) && RevisitTargetAllowed(target, context)) {
                if (g_revisitConfirmation.Press(target, GetTickCount64())) {
                    g_revisitSubmissionRejected = !QueueRevisitTravel(target, ++g_revisitRequestToken, context);
                    if (g_revisitSubmissionRejected) Log("Revisit: UI request rejected before queue; inspect return-point and context diagnostics.");
                } else {
                    g_revisitConfirmContext = context;
                    g_revisitSubmissionRejected = false;
                }
            }
        }
        // 翻页在两张清单中含义相同，但只交给当前传送窗口，不能同时改宝箱页码。
        // 逐项动作只用于此窗口；关闭后不把Ctrl/扳机组合重新解释成宝箱翻页。
        actions &= ~(PreviousPage | NextPage | ToggleFilter | PreviousTravelItem | NextTravelItem);
    }
    if (actions & ToggleMode) {
        const bool current = g_mode.load() == Mode::Current;
        g_mode.store(current ? Mode::Inherited : Mode::Current);
        g_mapPage = 0;
        Log(current ? "View changed: inherited." : "View changed: current playthrough.");
    }
    if (actions & TogglePanel) {
        g_panel.store(!g_panel.load());
        if (!g_panel.load()) { g_revisitWindow = false; CancelRevisitConfirmation(); }
    }
    if (actions & ToggleEnabled) {
        g_enabled.store(!g_enabled.load());
        Log(g_enabled.load() ? "Chest markers resumed." : "Chest markers paused.");
    }
    // 探索模块自行检查可用性；这里仅提交开关意图，不直接读写游戏对象或存档。
    // 宝箱标记的暂停开关与探索辅助互相独立，避免 F9 同时改变两类功能。
    if (actions & ToggleMapReveal) ToggleExploration(ExplorationFeature::MapReveal);
    if (actions & ToggleTravelUnlock) ToggleExploration(ExplorationFeature::TravelUnlock);
    if (actions & ToggleList) {
        g_revisitWindow = false;
        CancelRevisitConfirmation();
        g_mapList = !g_panel.load() || !g_mapList;
        if (g_mapList) g_panel.store(true);
        Log(g_mapList ? "Map progress list opened." : "Map progress list closed.");
    }
    // 翻页键和筛选键仅在清单可见时生效，避免隐藏期间意外改变页码。
    if (g_mapList && g_panel.load()) {
        if (actions & ToggleFilter) { g_missingOnly = !g_missingOnly; g_mapPage = 0; }
        if ((actions & PreviousPage) && g_mapPage > 0) --g_mapPage;
        if (actions & NextPage) ++g_mapPage;
    }
}

// 仅在前台处理动作；按下沿防止长按连续切换，后台积压的手柄指令直接清空。
static void HandleKeys() {
    static KeyboardFilter keyboard;
    const int keys[] = {VK_F6, VK_F7, VK_F9, VK_F8, VK_F10, VK_PRIOR, VK_NEXT};
    const bool foreground = GetForegroundWindow() == g_window;
    uint32_t actions = TakeInputActions();
    uint32_t down = 0;
    for (int i = 0; i < 7; ++i) {
        if ((GetAsyncKeyState(keys[i]) & 0x8000) != 0) down |= 1u << i;
    }
    const bool control = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    actions |= keyboard.Update(down, control, foreground);
    UpdateRevisitConfirmation();
    if (foreground) ApplyActions(actions);
    else CancelRevisitConfirmation();
}

// 根据当前合并字体的 cmap 检查各语言实际会用到的文字；不修改全局语言，
// 不栅格化整个字库，不提前上传几千个字的纹理。相同码点只查询一次字体源。
// 检查 Mod 文案与原生资源专名，避免日文字体能显示“地图”却缺少简中专名时误报完整。
static void CheckPanelFontCoverage(ImFont* font) {
    std::array<uint8_t, 0x10000> glyphCache{}; // 0 未检查；1 存在；2 缺失。
    const char* names[] = {"Simplified Chinese", "Japanese", "English", "Traditional Chinese",
                           "German", "French", "Spanish", "Korean"};
    for (unsigned language = 0; language < kLanguageCount; ++language) {
        bool complete = font != nullptr;
        unsigned firstMissing = 0;
        const auto checkText = [&](const char* text) {
            if (!text || !*text || !font) return;
            const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
            if (!count) { complete = false; return; }
            std::wstring wide(static_cast<size_t>(count), L'\0');
            if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide.data(), count)) {
                complete = false;
                return;
            }
            for (const wchar_t character : wide) {
                const auto codepoint = static_cast<unsigned>(character);
                if (codepoint < 0x20) continue; // 换行、制表符、终止符不需要可见字形。
                auto& cached = glyphCache[codepoint];
                if (!cached) cached = font->IsGlyphInFont(static_cast<ImWchar>(codepoint)) ? 1 : 2;
                if (cached == 2) { complete = false; if (!firstMissing) firstMissing = codepoint; }
            }
        };
        for (const auto& entry : kUiTexts) {
            const char* texts[] = {entry.chinese, entry.japanese, entry.english, entry.traditionalChinese,
                entry.german, entry.french, entry.spanish, entry.korean};
            checkText(texts[language]);
        }
#if SKY2_HAS_MAP_LOCALIZATION
        for (const auto& map : kLocalizedMaps) checkText(map.paths[language]);
#endif
#if SKY2_HAS_TRAVEL_LOCALIZATION
        for (const auto& target : kLocalizedTravel) {
            checkText(target.names[language]);
            checkText(target.groups[language]);
        }
        for (const auto& region : kLocalizedRegions) checkText(region.names[language]);
#endif
        g_languageFontComplete[language] = complete;
        if (!complete) {
            char diagnostic[256]{};
            std::snprintf(diagnostic, sizeof(diagnostic),
                "Font coverage incomplete for %s (first missing U+%04X). Install matching Windows supplemental fonts.",
                names[language], firstMissing);
            Log(diagnostic);
        }
    }
}

// 只使用玩家机器已安装的字体，不随 Mod 打包或分发 Windows 字体。
// 所有语言合并到同一字库：跟随游戏切换语言时无需重建纹理，也不会重置列表位置。
// 中文、日文、韩文字体分别择优加载一份，避免把所有候选字体同时驻留内存。
static void LoadPanelFonts(ImGuiIO& io) {
    g_languageFontComplete.fill(false);
    wchar_t windows[MAX_PATH]{};
    GetWindowsDirectoryW(windows, MAX_PATH);
    const std::wstring folder = std::wstring(windows) + L"\\Fonts\\";
    static const ImWchar glyphs[] = {
        0x0020, 0x024f, 0x1100, 0x11ff, 0x2000, 0x26ff, 0x3000, 0x318f,
        0x31f0, 0x31ff, 0x3400, 0x9fff, 0xac00, 0xd7af, 0xff00, 0xffef, 0
    };
    bool loaded = false;
    const auto load = [&](const wchar_t* filename) {
        const auto path = folder + filename;
        const DWORD attributes = GetFileAttributesW(path.c_str());
        // AddFontFromFileTTF 对不存在的路径会触发断言，必须先检查再交给 ImGui。
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
        char utf8[MAX_PATH * 3]{};
        if (!WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, utf8, sizeof(utf8), nullptr, nullptr)) return false;
        ImFontConfig config;
        config.MergeMode = loaded;
        if (!io.Fonts->AddFontFromFileTTF(utf8, 20.0f, &config, glyphs)) return false;
        loaded = true;
        return true;
    };
    // Segoe UI 是英文 Windows 的可靠基础字体；缺失时仍保留 ImGui 的内置拉丁字形。
    if (!load(L"segoeui.ttf")) {
        ImFontConfig config;
        config.SizePixels = 20.0f;
        io.Fonts->AddFontDefault(&config);
        loaded = true;
    }
    // 英文原生地点同样包含 ① 等圈号，常规 Segoe UI 不提供这些字符。
    // 单独合并 Windows 的符号字体，使英文系统无需为了这些名称安装中日韩字体。
    load(L"seguisym.ttf");
    bool chinese = false, japanese = false, korean = false;
    for (const auto* name : {L"msyh.ttc", L"msyh.ttf", L"simhei.ttf", L"simsun.ttc", L"Deng.ttf", L"msjh.ttc", L"mingliu.ttc", L"NotoSansSC-Regular.ttf"})
        if (load(name)) { chinese = true; break; }
    for (const auto* name : {L"YuGothM.ttc", L"YuGothR.ttc", L"meiryo.ttc", L"msgothic.ttc", L"NotoSansJP-Regular.ttf"})
        if (load(name)) { japanese = true; break; }
    // 韩文有独立音节区，不能把“有汉字字体”误判为可显示韩文。
    // 即使中日字体已存在也单独补入，确保游戏运行时切到韩文无需重新初始化面板。
    for (const auto* name : {L"malgun.ttf", L"gulim.ttc", L"batang.ttc", L"NotoSansKR-Regular.ttf"})
        if (load(name)) { korean = true; break; }
    // 部分非中日 Windows 只装了其他 CJK 字库；它们仅作最终兜底，不优先改变字形。
    if (!chinese || !japanese || !korean)
        for (const auto* name : {L"NotoSansCJK-Regular.ttc", L"arialuni.ttf"})
            if (load(name)) break;
    // 文件名仅用于候选选择，语言可显示性最终由实际字形判定。
    CheckPanelFontCoverage(io.Fonts->Fonts.Size ? io.Fonts->Fonts[0] : nullptr);
}

static bool InitializeGui(IDXGISwapChain* swap) {
    DXGI_SWAP_CHAIN_DESC description{};
    if (FAILED(swap->GetDesc(&description)) || !description.OutputWindow) return false;
    DWORD owner = 0;
    GetWindowThreadProcessId(description.OutputWindow, &owner);
    RECT area{};
    if (owner != GetCurrentProcessId() || !GetClientRect(description.OutputWindow, &area) ||
        area.right < 320 || area.bottom < 240) return false;
    if (FAILED(swap->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&g_device)))) return false;
    g_device->GetImmediateContext(&g_deviceContext);
    g_window = description.OutputWindow;
    IMGUI_CHECKVERSION();
    g_context = ImGui::CreateContext();
    ImGui::SetCurrentContext(g_context);
    auto& io = ImGui::GetIO();
    // 面板没有可编辑控件，不保存 ImGui 布局文件，也不获取鼠标／键盘的独占输入。
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    LoadPanelFonts(io);
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.WindowPadding = ImVec2(16, 12);
    style.ItemSpacing = ImVec2(8, 7);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.035f, 0.07f, 0.085f, 0.93f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.3f, 0.65f, 0.61f, 0.8f);
    const bool win32Ready = ImGui_ImplWin32_Init(g_window);
    const bool dx11Ready = win32Ready && ImGui_ImplDX11_Init(g_device, g_deviceContext);
    if (!dx11Ready) {
        Log("ImGui backend initialization failed.");
        if (win32Ready) ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(g_context);
        g_context = nullptr;
        g_deviceContext->Release();
        g_device->Release();
        g_deviceContext = nullptr;
        g_device = nullptr;
        g_window = nullptr;
        return false;
    }
    AttachInputWindow(g_window);
    Log("D3D11 panel initialized.");
    return true;
}

// 宝箱快捷键与探索辅助共用两列宽度，使下方组合键精确对齐上方右组按键。
// 左列按最长等待状态预留空间，避免开关状态变化时挤压右列或使快捷键横向跳动。
struct PanelShortcutLayout {
    float keyWidth = 0;
    float leftWidth = 0;
    float windowWidth = 0;
};

// 宽度由实际字体测量决定，键鼠不再为较长的手柄组合键预留空白。
// 同一输入模式按最长状态和三位数计数预留空间，避免开箱、切换模式或等待刷新时跳宽。
static PanelShortcutLayout MeasurePanelShortcutLayout(bool controller) {
    const auto textWidth = [](const char* text) { return ImGui::CalcTextSize(text).x; };
    const auto& style = ImGui::GetStyle();
    PanelShortcutLayout layout;
    layout.keyWidth = textWidth(controller ? "View + RS" : "F9") + 8.0f;
    char name[256]{}, currentArea[256]{}, inheritedArea[256]{}, mode[256]{};
    std::snprintf(name, sizeof(name), UiString(UiText::NameState), UiString(UiText::UnvisitedTravel));
    std::snprintf(currentArea, sizeof(currentArea), UiString(UiText::AreaCurrent), 566u, 566u);
    std::snprintf(inheritedArea, sizeof(inheritedArea), UiString(UiText::AreaInherited), 566u, 566u);
    std::snprintf(mode, sizeof(mode), UiString(UiText::ModeLabel), UiString(UiText::ModeInherited));
    float stateWidth = 0;
    for (const auto id : {UiText::Unavailable, UiText::WaitingOn, UiText::WaitingOff, UiText::On, UiText::Off})
        stateWidth = std::max(stateWidth, textWidth(UiString(id)));
    layout.leftWidth = std::max(textWidth(name) + stateWidth,
        layout.keyWidth + std::max(textWidth(UiString(UiText::SwitchMode)), textWidth(UiString(UiText::MapList))));
    const float rightWidth = std::max(
        layout.keyWidth + std::max(textWidth(UiString(UiText::ShowHide)), textWidth(UiString(UiText::PauseResume))),
        textWidth(controller ? UiString(UiText::DpadDown) : "Ctrl + F8"));
    float contentWidth = layout.leftWidth + rightWidth + style.CellPadding.x * 4;
    for (const char* text : {static_cast<const char*>(mode), UiString(UiText::Paused),
                            static_cast<const char*>(currentArea), static_cast<const char*>(inheritedArea), UiString(UiText::OpenAreaMap),
                            UiString(UiText::MarkerLegend), UiString(UiText::Title)})
        contentWidth = std::max(contentWidth, textWidth(text));
    if (controller)
        contentWidth = std::max(contentWidth, textWidth(UiString(UiText::ControllerLegend)));
    // 两侧内边距和少量像素取整余量不属于内容列，防止缩放后最后一个字贴边。
    layout.windowWidth = contentWidth + style.WindowPadding.x * 2 + 4.0f;
    return layout;
}

static bool BeginPanelShortcutColumns(const char* id, const PanelShortcutLayout& layout) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp)) return false;
    ImGui::TableSetupColumn("##left", ImGuiTableColumnFlags_WidthFixed, layout.leftWidth);
    ImGui::TableSetupColumn("##right", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

// 主面板与地图清单共用按键绘制：完整按键使用同一高亮色，动作保持正文颜色。
// 以相对间距补齐键名宽度，避免表格内绝对偏移重复叠加列起点，导致右列文字裁切。
static void DrawShortcutHint(const char* key, const char* action, float keyWidth) {
    ImGui::TextColored(ImVec4(0.5f, 0.91f, 0.8f, 1), "%s", key);
    ImGui::SameLine(0.0f, keyWidth - ImGui::CalcTextSize(key).x);
    ImGui::TextUnformatted(action);
}

// 每组内部的按键与动作保持对齐；仅改变提示绘制，不修改输入、组合键屏蔽和热切换。
static void DrawChestShortcuts(bool controller, const PanelShortcutLayout& layout) {
    if (BeginPanelShortcutColumns("ChestShortcuts", layout)) {
        const char* keys[] = {controller ? "View + X" : "F6", controller ? "View + B" : "F7",
                              controller ? "View + A" : "F8", controller ? "View + RS" : "F9"};
        const char* actions[] = {UiString(UiText::SwitchMode), UiString(UiText::ShowHide), UiString(UiText::MapList), UiString(UiText::PauseResume)};
        for (unsigned i = 0; i < 4; ++i) {
            if (i % 2 == 0) ImGui::TableNextRow();
            ImGui::TableNextColumn();
            DrawShortcutHint(keys[i], actions[i], layout.keyWidth);
        }
        ImGui::EndTable();
    }
    // 修饰键说明仅在手柄模式下保留一行；按住状态复用同一行，不额外撑高面板。
    if (controller) ImGui::TextDisabled("%s", ControllerModifierHeld() ?
        UiString(UiText::ControllerHeld) : UiString(UiText::ControllerLegend));
}

static void DrawExplorationStatus(const ExplorationStatus& exploration, bool controller,
                                   const PanelShortcutLayout& layout) {
    ImGui::TextColored(ImVec4(0.5f, 0.91f, 0.8f, 1), UiString(UiText::Exploration));
    // 名称与状态紧接显示，取消独立状态列；等待状态仍明确标识为未完成请求。
    // 右列使用与宝箱快捷键相同的位置和高亮色；方向名称随语言切换，不依赖箭头字形。
    if (BeginPanelShortcutColumns("ExplorationStatus", layout)) {
        const auto row = [](const char* name, bool available, bool enabled, bool pending,
                             bool requested, const char* key) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text(UiString(UiText::NameState), name);
            const char* state = !available ? UiString(UiText::Unavailable) : pending ? (requested ? UiString(UiText::WaitingOn) : UiString(UiText::WaitingOff)) :
                                enabled ? UiString(UiText::On) : UiString(UiText::Off);
            const ImVec4 color = !available || pending ? ImVec4(1, 0.74f, 0.34f, 1) :
                enabled ? ImVec4(0.5f, 0.91f, 0.8f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextColored(color, "%s", state);
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.5f, 0.91f, 0.8f, 1), "%s", key);
        };
        row(UiString(UiText::RevealMap), exploration.mapAvailable, exploration.mapEnabled, false, false,
            controller ? UiString(UiText::DpadUp) : "Ctrl + F6");
        row(UiString(UiText::UnvisitedTravel), exploration.travelAvailable, exploration.travelEnabled,
            exploration.travelPending, exploration.travelRequested, controller ? UiString(UiText::DpadDown) : "Ctrl + F8");
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(UiString(UiText::TravelList));
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.5f, 0.91f, 0.8f, 1), "%s",
            controller ? UiString(UiText::DpadLeft) : "Ctrl + F10");
        ImGui::EndTable();
    }
    // 底部只有一个提示槽：故障优先，其次等待，正常时才显示简短功能边界。
    // 完整功能范围和使用说明保留在文档中，不在每帧面板反复展开三到四段文字。
    const char* note = !exploration.mapAvailable || !exploration.travelAvailable ?
        UiString(UiText::FeatureFailed) : exploration.travelPending ?
        UiString(UiText::FeatureWaiting) : UiString(UiText::FeatureNote);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", note);
    ImGui::PopStyleColor();
}

// 清单页脚采用三列两行：模式/筛选/收起在上，上一页/下一页在下。
// 每个手柄操作都保留完整 View 组合，翻页也拆成两个独立提示，不依赖共享前缀。
static void DrawMapListShortcuts(bool controller) {
    const char* keys[] = {controller ? "View + X" : "F6", controller ? "View + Y" : "F10",
                          controller ? "View + A" : "F8", controller ? "View + LB" : "PgUp",
                          controller ? "View + RB" : "PgDn"};
    const char* actions[] = {UiString(UiText::SwitchMode), UiString(UiText::Filter), UiString(UiText::CloseList), UiString(UiText::PreviousPage), UiString(UiText::NextPage)};
    float keyWidth = 0;
    for (const char* key : keys) keyWidth = std::max(keyWidth, ImGui::CalcTextSize(key).x);
    keyWidth += 8.0f;
    if (ImGui::BeginTable("MapListShortcuts", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("##mode_previous", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##filter_next", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##close", ImGuiTableColumnFlags_WidthStretch);
        for (unsigned i = 0; i < 5; ++i) {
            if (i % 3 == 0) ImGui::TableNextRow();
            ImGui::TableNextColumn();
            DrawShortcutHint(keys[i], actions[i], keyWidth);
        }
        ImGui::EndTable();
    }
}

// 从完整地图目录测量路径列，而非按当前页或遗漏筛选测量，翻页时窗口因此保持稳定。
// 计数列只保留列名和最大计数所需宽度；完整手柄快捷键与顶部统计决定清单的最小宽度。
struct MapListLayout {
    float windowWidth = 0;
    float countWidth = 0;
    float missingWidth = 0;
    float nameWidth = 0;
};
static MapListLayout MeasureMapListLayout(bool controller) {
    const auto textWidth = [](const char* text) { return ImGui::CalcTextSize(text).x; };
    const auto& style = ImGui::GetStyle();
    MapListLayout layout;
    layout.countWidth = std::max({textWidth(UiString(UiText::InheritedColumn)),
        textWidth(UiString(UiText::ModeCurrent)), textWidth("566 / 566")});
    layout.missingWidth = std::max({textWidth(UiString(UiText::Complete)),
        textWidth(UiString(UiText::MissingColumn)), textWidth("566")});
    float pathWidth = textWidth(UiString(UiText::PathColumn));
    for (const auto& map : kMaps) pathWidth = std::max(pathWidth, textWidth(MapPath(map)));
    const float tableSpacing = style.CellPadding.x * 8;
    const float statisticsWidth = layout.countWidth * 2 + layout.missingWidth + tableSpacing;
    float actionWidth = 0;
    for (const auto id : {UiText::Filter, UiText::CloseList, UiText::SwitchMode, UiText::PreviousPage, UiText::NextPage})
        actionWidth = std::max(actionWidth, textWidth(UiString(id)));
    const float shortcutWidth = (textWidth(controller ? "View + RB" : "PgDn") + 8.0f +
        actionWidth) * 3 + style.CellPadding.x * 6;
    char summary[256]{}, pages[96]{};
    std::snprintf(summary, sizeof(summary), UiString(UiText::MapSummary), 59u, 59u, 566u);
    std::snprintf(pages, sizeof(pages), UiString(UiText::PageFormat), size_t{59}, size_t{59});
    const float summaryWidth = textWidth(summary) + style.ItemSpacing.x * 3 + textWidth(pages);
    const float contentWidth = std::max({pathWidth + statisticsWidth + 8.0f, shortcutWidth, summaryWidth});
    layout.windowWidth = std::min(contentWidth + style.WindowPadding.x * 2 + 4.0f,
        std::min(1060.0f, ImGui::GetIO().DisplaySize.x - 32.0f));
    // 与表格使用相同的统计列和内边距；视口受限时仍按真实剩余宽度计算路径换行行高。
    layout.nameWidth = std::max(80.0f,
        layout.windowWidth - style.WindowPadding.x * 2 - statisticsWidth - 8.0f);
    return layout;
}

static void DrawMapList(const Counts& counts, float left) {
    if (!g_mapList) return;
    const auto mode = g_mode.load();
    const auto display = ImGui::GetIO().DisplaySize;
    const bool controller = UsingController();
    const auto layout = MeasureMapListLayout(controller);
    const float width = layout.windowWidth;
    // 完整路径在窄窗口自动换行；按最长路径预留行高，使翻页容量不会随当前页抖动。
    const auto& style = ImGui::GetStyle();
    float rowHeight = ImGui::GetTextLineHeight();
    for (const auto& map : counts.maps)
        rowHeight = std::max(rowHeight, ImGui::CalcTextSize(MapPath(*map.definition), nullptr, false, layout.nameWidth).y);
    rowHeight += style.CellPadding.y * 2;
    // 标题、筛选说明、两行快捷键及页脚说明预留固定高度；小窗口减少行数以免裁切。
    const auto rowsPerPage = static_cast<size_t>(std::max(1.0f,
        std::min(static_cast<float>(kRowsPerPage), (display.y - 320.0f) / rowHeight)));
    const auto visible = VisibleMaps(counts.maps, mode, g_missingOnly);
    const unsigned collected = mode == Mode::Current ? counts.current : counts.inherited;
    const auto complete = std::count_if(counts.maps.begin(), counts.maps.end(),
        [&](const auto& map) { return map.Remaining(mode) == 0; });
    const auto pages = std::max<size_t>(1, (visible.size() + rowsPerPage - 1) / rowsPerPage);
    // 换图或暂时读不到数据时，空快照不能代表清单已缩到一页，需保留上次浏览位置。
    // 有效数据恢复后再按真实页数截断，兼顾开箱后遗漏地图减少及窗口尺寸变化。
    if (counts.valid) g_mapPage = std::min(g_mapPage, pages - 1);
    // 窄窗口时允许清单覆盖主面板的一部分，始终让整张清单位于视口内。
    const float x = std::max(16.0f, std::min(left, display.x - width - 16.0f));
    ImGui::SetNextWindowPos(ImVec2(x, 22), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, 0), ImGuiCond_Always);
    constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
    if (ImGui::Begin("Sky2MapProgress", nullptr, flags)) {
        ImGui::TextColored(ImVec4(0.5f, 0.91f, 0.8f, 1), UiString(UiText::MapTitle));
        ImGui::Text("%s · %s", mode == Mode::Current ? UiString(UiText::ModeCurrent) : UiString(UiText::ModeInherited),
                    g_missingOnly ? UiString(UiText::OnlyMissing) : UiString(UiText::AllMaps));
        if (!counts.valid) {
            ImGui::TextDisabled(UiString(UiText::WaitingData));
        } else {
            // 页码与已完成统计共用一行，右边缘对齐内容区；先保存行尾，避免文字提交改变游标。
            // 宽度测量已为最长统计与页码留出间距；无游戏数据时不显示没有依据的页码。
            const float rowRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            ImGui::Text(UiString(UiText::MapSummary), static_cast<unsigned>(complete),
                        static_cast<unsigned>(counts.maps.size()), 566 - collected);
            char pageText[96]{};
            std::snprintf(pageText, sizeof(pageText), UiString(UiText::PageFormat), g_mapPage + 1, pages);
            ImGui::SameLine(0.0f, std::max(style.ItemSpacing.x,
                rowRight - ImGui::GetItemRectMax().x - ImGui::CalcTextSize(pageText).x));
            ImGui::TextUnformatted(pageText);
            ImGui::Separator();
            if (visible.empty()) {
                ImGui::TextColored(ImVec4(0.5f, 0.91f, 0.8f, 1), UiString(UiText::AllOpened));
            } else if (ImGui::BeginTable("MapCounts", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
                ImGui::TableSetupColumn(UiString(UiText::PathColumn), ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(UiString(UiText::ModeCurrent), ImGuiTableColumnFlags_WidthFixed, layout.countWidth);
                ImGui::TableSetupColumn(UiString(UiText::InheritedColumn), ImGuiTableColumnFlags_WidthFixed, layout.countWidth);
                ImGui::TableSetupColumn(UiString(UiText::MissingColumn), ImGuiTableColumnFlags_WidthFixed, layout.missingWidth);
                ImGui::TableHeadersRow();
                const auto end = std::min(visible.size(), (g_mapPage + 1) * rowsPerPage);
                for (size_t i = g_mapPage * rowsPerPage; i < end; ++i) {
                    const auto& map = counts.maps[visible[i]];
                    const auto missing = map.Remaining(mode);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextUnformatted(MapPath(*map.definition));
                    ImGui::PopTextWrapPos();
                    ImGui::TableNextColumn();
                    ImGui::Text("%u / %u", map.current, map.total);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u / %u", map.inherited, map.total);
                    ImGui::TableNextColumn();
                    if (missing) ImGui::TextColored(ImVec4(1, 0.74f, 0.34f, 1), "%u", missing);
                    else ImGui::TextColored(ImVec4(0.5f, 0.91f, 0.8f, 1), UiString(UiText::Complete));
                }
                ImGui::EndTable();
            }
        }
        ImGui::Separator();
        DrawMapListShortcuts(controller);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped(UiString(UiText::MapNote));
        ImGui::PopStyleColor();
    }
    ImGui::End();
}

// 回访清单与地图收集清单互斥显示，沿用按键高亮和设备热切换风格。
// 明确区分“请求已提交”和“已经到达”，不会把关图阶段冒充传送成功。
// 出发点优先按站位地点 ID 与场景匹配原生专名；无唯一匹配时退回唯一地图或地区。
// 场景编号与记录时间补充识别历史行程，不能冒充存档槽位标识。
static const char* ReturnPointName(const RevisitReturnPoint& point) {
    // 站位地点优先；同一原生场景包含多条道路时只退回地区，禁止显示第一条路名。
    return ReturnPointDisplayName(point, kMaps);
}
static void DrawRevisitWindow(float left, bool controller) {
    if (!g_revisitWindow) return;
    const auto context = ReadRevisitNativeContext();
    const auto status = ReadRevisitNativeStatus();
    const auto returnStatus = ReadRevisitReturnStatus(context);
    const auto& destination = RevisitDestinationAt(g_revisitSelection);
    // 从全部原生名称与完整快捷键测量，避免外文长名裁切，也避免翻页时窗口跳宽。
    float contentWidth = 600.0f;
    for (size_t i = 0; i < RevisitDestinationCount(); ++i) {
        const auto& row = RevisitDestinationAt(i);
        const std::string path = std::string("> ") + DestinationGroup(row) + " / " + DestinationName(row);
        contentWidth = std::max(contentWidth, ImGui::CalcTextSize(path.c_str()).x);
    }
    const char* confirmKey = controller ? UiString(UiText::DpadRight) : "Ctrl + F10";
    contentWidth = std::max(contentWidth, (ImGui::CalcTextSize(confirmKey).x + 12.0f +
        ImGui::CalcTextSize(UiString(UiText::CycleRecord)).x + ImGui::GetStyle().CellPadding.x * 2) * 2);
    const float width = std::min(contentWidth + ImGui::GetStyle().WindowPadding.x * 2 + 4.0f,
        std::min(1040.0f, ImGui::GetIO().DisplaySize.x - 32.0f));
    const float x = std::max(16.0f, std::min(left, ImGui::GetIO().DisplaySize.x - width - 16.0f));
    ImGui::SetNextWindowPos(ImVec2(x, 22), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, 0), ImGuiCond_Always);
    constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
    if (ImGui::Begin("Sky2Revisit", nullptr, flags)) {
        const ImVec4 accent(0.5f, 0.91f, 0.8f, 1);
        ImGui::TextColored(accent, UiString(UiText::TravelList));
        ImGui::TextUnformatted(revisit_policy::kUnrestricted ?
            UiString(UiText::TravelOrigin) : UiString(UiText::TravelStoryOrigin));
        constexpr size_t rows=kRevisitRowsPerPage;
        const size_t page=g_revisitSelection/rows;
        char pages[48]{};
        std::snprintf(pages,sizeof(pages),UiString(UiText::PageFormat),page+1,(RevisitDestinationCount()+rows-1)/rows);
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(),width-ImGui::GetStyle().WindowPadding.x-ImGui::CalcTextSize(pages).x));
        ImGui::TextUnformatted(pages);
        ImGui::Separator();
        for (size_t i=page*rows; i<std::min((page+1)*rows,RevisitDestinationCount()); ++i) {
            const auto& row=RevisitDestinationAt(i);
            const bool selected=i==g_revisitSelection;
            const bool allowed=RevisitTargetAllowed(row.id,context);
            ImGui::PushStyleColor(ImGuiCol_Text,selected ? accent :
                ImGui::GetStyleColorVec4(allowed ? ImGuiCol_Text : ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s %s / %s", selected ? ">" : " ", DestinationGroup(row), DestinationName(row));
            ImGui::PopStyleColor();
        }
        ImGui::Separator();
        if (destination.id==108)
            ImGui::TextWrapped("%s", UiString(UiText::BuildingNote));
        if (destination.id==forest::kTarget)
            ImGui::TextWrapped("%s", UiString(UiText::ForestNote));
        if (returnStatus.hasRecord) {
            const auto& record=returnStatus.record;
            const time_t stamp=static_cast<time_t>(record.createdUnixSeconds);
            tm local{}; char date[48]{};
            if (localtime_s(&local,&stamp)==0) std::strftime(date,sizeof(date),"%m-%d %H:%M:%S",&local);
            ImGui::TextWrapped("%s: %s",returnStatus.active ? UiString(UiText::OriginalPoint) : UiString(UiText::HistoryCandidate),ReturnPointName(record.point));
            ImGui::TextDisabled(UiString(UiText::RecordFormat),record.point.scene,date,returnStatus.index+1,returnStatus.count);
        } else ImGui::TextDisabled(UiString(UiText::RecordAuto));
        const char* message = nullptr;
        if (!RevisitReady() || !context.available) message = UiString(UiText::TravelUnavailable);
        else if (status.phase == RevisitNativePhase::Queued) message = UiString(UiText::TravelQueued);
        else if (status.phase == RevisitNativePhase::ClosingMap) message = UiString(UiText::TravelClosing);
        else if (status.phase == RevisitNativePhase::Dispatched) message = UiString(UiText::TravelDispatched);
        else if (!context.valid) message = UiString(UiText::WaitingScene);
        else if (!RevisitContextAllowed(context)) message = UiString(UiText::SceneUnsupported);
        else if (!context.browsing || context.busy) message = UiString(UiText::OpenTravelMap);
        else if (!RevisitTargetAllowed(destination.id,context)) {
            if (destination.id==kRevisitReturnTarget)
                message=returnStatus.hasRecord ? (revisit_policy::kUnrestricted ?
                    UiString(UiText::ReturnInvalid) :
                    UiString(UiText::ReturnStoryBlocked)) :
                    UiString(UiText::ReturnMissing);

            else if (!returnStatus.storageReady) message=UiString(UiText::RecordStorageFailed);
            else if (RevisitRecoveryRequired(context) && !returnStatus.active) message=UiString(UiText::ReturnFirst);
            else if (!revisit_policy::kUnrestricted && context.beforeScriptReturnBlocked && !returnStatus.active)
                message=UiString(UiText::StoryOwnsTravel);
            else if (!context.returnPointReady && !returnStatus.active)
                message=UiString(UiText::PositionNotReady);
            else message=RevisitNativeTargetReason(destination.id,context);
        } else if (g_revisitConfirmation.Armed(destination.id, GetTickCount64()))
            message = destination.id==kRevisitReturnTarget ?
                UiString(UiText::ConfirmReturn) : UiString(UiText::ConfirmTravel);
        else if (status.phase == RevisitNativePhase::ArrivalUnconfirmed)
            message = UiString(UiText::ArrivalUnconfirmed);
        else if (g_revisitSubmissionRejected || status.phase == RevisitNativePhase::Rejected || status.phase == RevisitNativePhase::Expired)
            message = UiString(UiText::RequestUnconfirmed);
        else message = UiString(UiText::TravelReady);
        ImGui::TextWrapped("%s", message);
        // 两列按操作配对：上一页/下一页、上一项/下一项、确认/收起。
        // 各列独立测量完整组合键，既保留高亮与热切换，又避免最长手柄文字挤到相邻列。
        const char* revisitKeys[] = {controller ? "View + LB" : "PgUp",
            controller ? "View + RB" : "PgDn", controller ? "View + LT" : "Ctrl + PgUp",
            controller ? "View + RT" : "Ctrl + PgDn", controller ? UiString(UiText::DpadRight) : "Ctrl + F7",
            controller ? UiString(UiText::DpadLeft) : "Ctrl + F10", controller ? "View + Y" : "F10"};
        const char* revisitActions[] = {UiString(UiText::PreviousPage), UiString(UiText::NextPage), UiString(UiText::PreviousItem), UiString(UiText::NextItem), UiString(UiText::Confirm), UiString(UiText::CloseList), UiString(UiText::CycleRecord)};
        const unsigned shortcutCount = destination.id==kRevisitReturnTarget && !returnStatus.active && returnStatus.count>1 ? 7 : 6;
        float keyWidths[2]{};
        for (unsigned i=0;i<shortcutCount;++i)
            keyWidths[i%2] = std::max(keyWidths[i%2], ImGui::CalcTextSize(revisitKeys[i]).x + 12.0f);
        if (ImGui::BeginTable("RevisitShortcuts", 2, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableSetupColumn("##previous_confirm", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##next_close", ImGuiTableColumnFlags_WidthStretch);
            for (unsigned i=0;i<shortcutCount;++i) {
                if (i%2==0) ImGui::TableNextRow();
                ImGui::TableNextColumn();
                DrawShortcutHint(revisitKeys[i], revisitActions[i], keyWidths[i%2]);
            }
            ImGui::EndTable();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped(UiString(UiText::TravelNote));
        ImGui::PopStyleColor();
    }
    ImGui::End();
}

static void DrawPanel() {
    if (!g_panel.load()) return;
    // 统计每 250 毫秒刷新；地图图标本身在原生调用时即时读取标志。
    static Counts counts;
    static ULONGLONG refreshed = 0;
    const auto now = GetTickCount64();
    if (now - refreshed >= 250) { counts = ReadCounts(); refreshed = now; }
    const bool current = g_mode.load() == Mode::Current;
    const bool enabled = g_enabled.load();
    const bool controller = UsingController();
    const auto exploration = ReadExplorationStatus();
    const auto shortcuts = MeasurePanelShortcutLayout(controller);
    ImGui::SetNextWindowPos(ImVec2(22, 22), ImGuiCond_Always);
    // 输入热切换时同步更新测量宽度，键盘模式收窄后仍保留完整地区计数和探索状态。
    ImGui::SetNextWindowSize(ImVec2(std::min(shortcuts.windowWidth,
        ImGui::GetIO().DisplaySize.x - 44.0f), 0), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.93f);
    constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
    float panelRight = 330;
    if (ImGui::Begin("Sky2ChestTracker", nullptr, flags)) {
        ImGui::TextColored(ImVec4(0.5f, 0.91f, 0.8f, 1), UiString(UiText::Title));
        // 缺少相应字库时使用能显示的英文说明，不以默认问号冒充语言支持已经完整可用。
        const auto language = static_cast<unsigned>(CurrentLanguage());
        if (language < kLanguageCount && !g_languageFontComplete[language]) {
            const char* names[] = {"Simplified Chinese", "Japanese", "English", "Traditional Chinese",
                                   "German", "French", "Spanish", "Korean"};
            ImGui::TextWrapped("Font glyphs missing for %s. Install matching Windows supplemental fonts.", names[language]);
        }
        if (!enabled) ImGui::TextColored(ImVec4(1, 0.72f, 0.3f, 1), UiString(UiText::Paused));
        else ImGui::Text(UiString(UiText::ModeLabel), current ? UiString(UiText::ModeCurrent) : UiString(UiText::ModeInherited));
        ImGui::Separator();
        if (counts.valid) {
            ImGui::Text(UiString(UiText::CurrentOpened), counts.current);
            ImGui::Text(UiString(UiText::InheritedOpened), counts.inherited);
            if (!counts.map.empty()) {
                // 两组地区进度同时呈现，与上方全局计数保持一致；切换显示模式不隐藏其中一组。
                ImGui::Text(UiString(UiText::AreaCurrent), counts.map_current, counts.map_total);
                ImGui::Text(UiString(UiText::AreaInherited), counts.map_inherited, counts.map_total);
                ImGui::TextDisabled(UiString(UiText::AreaNote));
            } else ImGui::TextDisabled(UiString(UiText::OpenAreaMap));
        } else ImGui::TextDisabled(UiString(UiText::WaitingData));
        ImGui::Separator();
        ImGui::Text(UiString(UiText::MarkerLegend));
        DrawChestShortcuts(controller, shortcuts);
        ImGui::Separator();
        DrawExplorationStatus(exploration, controller, shortcuts);
        panelRight = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x + 14;
    }
    ImGui::End();
    DrawMapList(counts, panelRight);
    DrawRevisitWindow(panelRight, controller);
}

static HRESULT WINAPI Present(IDXGISwapChain* swap, UINT interval, UINT options) {
    // DXGI 的 TEST 调用只询问可呈现状态，不应在其中提交绘制命令。
    if (!(options & DXGI_PRESENT_TEST)) {
        std::lock_guard<std::mutex> guard(g_renderLock);
        ImGuiContext* previous = ImGui::GetCurrentContext();
        try {
            const bool ready = g_context || InitializeGui(swap);
            if (ready) {
                DXGI_SWAP_CHAIN_DESC description{};
                swap->GetDesc(&description);
                if (description.OutputWindow == g_window) {
                    ImGui::SetCurrentContext(g_context);
                    // 读取游戏当前文本语言，而非 Windows/Steam 语言；内部节流，不改写游戏配置。
                    RefreshGameLanguage();
                    HandleKeys();
                    // 使用实际后缓冲尺寸，避免窗口坐标、Windows DPI 与渲染分辨率不一致。
                    // 查询后立即释放引用，保持 ResizeBuffers 可用。
                    ID3D11Texture2D* sizeBuffer = nullptr;
                    if (FAILED(swap->GetBuffer(0, __uuidof(ID3D11Texture2D),
                        reinterpret_cast<void**>(&sizeBuffer)))) {
                        ImGui::SetCurrentContext(previous);
                        return g_originalPresent(swap, interval, options);
                    }
                    D3D11_TEXTURE2D_DESC bufferSize{};
                    sizeBuffer->GetDesc(&bufferSize);
                    sizeBuffer->Release();
                    ImGui_ImplDX11_NewFrame();
                    ImGui_ImplWin32_NewFrame();
                    const float scale = UiScale(static_cast<float>(bufferSize.Width),
                                                static_cast<float>(bufferSize.Height));
                    auto& io = ImGui::GetIO();
                    io.DisplaySize = ImVec2(bufferSize.Width / scale, bufferSize.Height / scale);
                    // ImGui 1.92+ 同时按此密度栅格化字体；所有面板继续共用逻辑尺寸。
                    io.DisplayFramebufferScale = ImVec2(scale, scale);
                    ImGui::NewFrame();
                    DrawPanel();
                    ImGui::Render();
                    ID3D11Texture2D* buffer = nullptr;
                    ID3D11RenderTargetView* view = nullptr;
                    if (SUCCEEDED(swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&buffer)))) {
                        g_device->CreateRenderTargetView(buffer, nullptr, &view);
                        buffer->Release();
                    }
                    if (view) {
                        // ImGui 后端恢复着色器等状态；输出目标由本层额外保存／还原。
                        ID3D11RenderTargetView* original[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
                        ID3D11DepthStencilView* depth = nullptr;
                        g_deviceContext->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, original, &depth);
                        g_deviceContext->OMSetRenderTargets(1, &view, nullptr);
                        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
                        g_deviceContext->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, original, depth);
                        for (auto* target : original) if (target) target->Release();
                        if (depth) depth->Release();
                        view->Release();
                    }
                }
            }
        } catch (...) {
            static bool reported = false;
            if (!reported) { Log("Panel exception contained."); reported = true; }
        }
        ImGui::SetCurrentContext(previous);
    }
    return g_originalPresent(swap, interval, options);
}

bool InstallOverlay() {
    // 使用一个不显示的小窗口查询系统 DXGI 虚表；窗口、设备和交换链随即释放。
    // 不扫描或修改显卡驱动，不向另一个进程注入代码。
    const wchar_t* className = L"Sky2ChestTrackerBootstrap";
    WNDCLASSW cls{};
    cls.lpfnWndProc = DefWindowProcW;
    cls.hInstance = g_module;
    cls.lpszClassName = className;
    if (!RegisterClassW(&cls)) return false;
    HWND window = CreateWindowExW(0, className, L"", WS_OVERLAPPED, 0, 0, 64, 64,
                                  nullptr, nullptr, g_module, nullptr);
    if (!window) { UnregisterClassW(className, g_module); return false; }
    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferDesc.Width = 64;
    description.BufferDesc.Height = 64;
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 1;
    description.OutputWindow = window;
    description.Windowed = TRUE;
    description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain* swap = nullptr;
    ID3D11Device* device = nullptr;
    const auto result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &description, &swap, &device, nullptr, nullptr);
    void* target = SUCCEEDED(result) ? (*reinterpret_cast<void***>(swap))[8] : nullptr;
    if (swap) swap->Release();
    if (device) device->Release();
    DestroyWindow(window);
    UnregisterClassW(className, g_module);
    if (!target) return false;
    const auto initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) return false;
    if (MH_CreateHook(target, reinterpret_cast<void*>(&Present), reinterpret_cast<void**>(&g_originalPresent)) != MH_OK)
        return false;
    if (MH_EnableHook(target) != MH_OK) { MH_RemoveHook(target); return false; }
    Log("DXGI Present hook installed.");
    return true;
}
}
