// Direct3D 11 只读信息面板：复用游戏的 Present 时机绘制，由输入层传递组合键动作。
// 每帧释放后缓冲视图，避免持有引用导致窗口缩放、全屏切换时 ResizeBuffers 失败。
#include "tracker.h"
#include "input_bridge.h"
#include "exploration.h"
#include "revisit.h"
#include "revisit_policy.h"
#include "ui_scale.h"
#include <d3d11.h>
#include <dxgi.h>
#include <MinHook.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <mutex>
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
    wchar_t windows[MAX_PATH]{};
    GetWindowsDirectoryW(windows, MAX_PATH);
    std::wstring fontWide = std::wstring(windows) + L"\\Fonts\\msyh.ttc";
    char font[MAX_PATH * 3]{};
    WideCharToMultiByte(CP_UTF8, 0, fontWide.c_str(), -1, font, sizeof(font), nullptr, nullptr);
    io.Fonts->AddFontFromFileTTF(font, 20.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
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
    layout.leftWidth = std::max(textWidth("未到访传送点：等待关闭"),
        layout.keyWidth + std::max(textWidth("切换模式"), textWidth("地图清单")));
    const float rightWidth = std::max(
        layout.keyWidth + std::max(textWidth("显示/隐藏"), textWidth("暂停/恢复")),
        textWidth(controller ? "View + 十字键下" : "Ctrl + F8"));
    float contentWidth = layout.leftWidth + rightWidth + style.CellPadding.x * 4;
    for (const char* text : {"显示模式：继承记录（多周目）", "宝箱标记已暂停（原版显示）",
                            "继承记录当前地区已开  566 / 566", "打开区域地图后显示两组地区统计",
                            "闭合箱标：未开    开启箱标：已开", "宝箱追踪  ·  0.5.0"})
        contentWidth = std::max(contentWidth, textWidth(text));
    if (controller)
        contentWidth = std::max(contentWidth, textWidth("View：双窗口键；RS：按下右摇杆"));
    // 两侧内边距和少量像素取整余量不属于内容列，防止缩放后最后一个字贴边。
    layout.windowWidth = contentWidth + style.WindowPadding.x * 2 + 4.0f;
    return layout;
}

static bool BeginPanelShortcutColumns(const char* id, const PanelShortcutLayout& layout) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp)) return false;
    ImGui::TableSetupColumn("左组", ImGuiTableColumnFlags_WidthFixed, layout.leftWidth);
    ImGui::TableSetupColumn("右组", ImGuiTableColumnFlags_WidthStretch);
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
        const char* actions[] = {"切换模式", "显示/隐藏", "地图清单", "暂停/恢复"};
        for (unsigned i = 0; i < 4; ++i) {
            if (i % 2 == 0) ImGui::TableNextRow();
            ImGui::TableNextColumn();
            DrawShortcutHint(keys[i], actions[i], layout.keyWidth);
        }
        ImGui::EndTable();
    }
    // 修饰键说明仅在手柄模式下保留一行；按住状态复用同一行，不额外撑高面板。
    if (controller) ImGui::TextDisabled("%s", ControllerModifierHeld() ?
        "View 已按住；RS：按下右摇杆" : "View：双窗口键；RS：按下右摇杆");
}

static void DrawExplorationStatus(const ExplorationStatus& exploration, bool controller,
                                   const PanelShortcutLayout& layout) {
    ImGui::TextColored(ImVec4(0.5f, 0.91f, 0.8f, 1), "探索辅助");
    // 名称与状态紧接显示，取消独立状态列；等待状态仍明确标识为未完成请求。
    // 右列使用与宝箱快捷键相同的位置和高亮色，方向名称保留中文以避免箭头缺字。
    if (BeginPanelShortcutColumns("ExplorationStatus", layout)) {
        const auto row = [](const char* name, bool available, bool enabled, bool pending,
                             bool requested, const char* key) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s：", name);
            const char* state = !available ? "不可用" : pending ? (requested ? "等待开启" : "等待关闭") :
                                enabled ? "已开启" : "已关闭";
            const ImVec4 color = !available || pending ? ImVec4(1, 0.74f, 0.34f, 1) :
                enabled ? ImVec4(0.5f, 0.91f, 0.8f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextColored(color, "%s", state);
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.5f, 0.91f, 0.8f, 1), "%s", key);
        };
        row("地图全显", exploration.mapAvailable, exploration.mapEnabled, false, false,
            controller ? "View + 十字键上" : "Ctrl + F6");
        row("未到访传送点", exploration.travelAvailable, exploration.travelEnabled,
            exploration.travelPending, exploration.travelRequested, controller ? "View + 十字键下" : "Ctrl + F8");
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("全传送清单");
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.5f, 0.91f, 0.8f, 1), "%s",
            controller ? "View + 十字键左" : "Ctrl + F10");
        ImGui::EndTable();
    }
    // 底部只有一个提示槽：故障优先，其次等待，正常时才显示简短功能边界。
    // 完整功能范围和使用说明保留在文档中，不在每帧面板反复展开三到四段文字。
    const char* note = !exploration.mapAvailable || !exploration.travelAvailable ?
        "功能校验未通过，详见 tracker.log。" : exploration.travelPending ?
        "等待刷新：打开地图或结束确认/转场。" : "重启关闭；传送可能越过入口剧情。";
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
    const char* actions[] = {"切换模式", "全部/遗漏", "收起清单", "上一页", "下一页"};
    float keyWidth = 0;
    for (const char* key : keys) keyWidth = std::max(keyWidth, ImGui::CalcTextSize(key).x);
    keyWidth += 8.0f;
    if (ImGui::BeginTable("MapListShortcuts", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("模式与上一页", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("筛选与下一页", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("收起清单", ImGuiTableColumnFlags_WidthStretch);
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
    layout.countWidth = std::max(textWidth("继承记录"), textWidth("566 / 566"));
    layout.missingWidth = std::max(textWidth("完成"), textWidth("566"));
    float pathWidth = textWidth("大地图 / 地点");
    for (const auto& map : kMaps) pathWidth = std::max(pathWidth, textWidth(map.path));
    const float tableSpacing = style.CellPadding.x * 8;
    const float statisticsWidth = layout.countWidth * 2 + layout.missingWidth + tableSpacing;
    const float shortcutWidth = (textWidth(controller ? "View + RB" : "PgDn") + 8.0f +
        std::max(textWidth("全部/遗漏"), textWidth("收起清单"))) * 3 + style.CellPadding.x * 6;
    const float summaryWidth = textWidth("已完成 59 / 59 张地图    剩余未开 566 个") +
        style.ItemSpacing.x * 3 + textWidth("第 59 / 59 页");
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
        rowHeight = std::max(rowHeight, ImGui::CalcTextSize(map.definition->path, nullptr, false, layout.nameWidth).y);
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
        ImGui::TextColored(ImVec4(0.5f, 0.91f, 0.8f, 1), "各地图宝箱收集");
        ImGui::Text("%s · %s", mode == Mode::Current ? "本周目" : "继承记录（多周目）",
                    g_missingOnly ? "仅看有遗漏的地图" : "全部地图（有遗漏的在前）");
        if (!counts.valid) {
            ImGui::TextDisabled("等待游戏数据……");
        } else {
            // 页码与已完成统计共用一行，右边缘对齐内容区；先保存行尾，避免文字提交改变游标。
            // 宽度测量已为最长统计与页码留出间距；无游戏数据时不显示没有依据的页码。
            const float rowRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            ImGui::Text("已完成 %u / %u 张地图    剩余未开 %u 个", static_cast<unsigned>(complete),
                        static_cast<unsigned>(counts.maps.size()), 566 - collected);
            const std::string pageText = "第 " + std::to_string(g_mapPage + 1) + " / " +
                std::to_string(pages) + " 页";
            ImGui::SameLine(0.0f, std::max(style.ItemSpacing.x,
                rowRight - ImGui::GetItemRectMax().x - ImGui::CalcTextSize(pageText.c_str()).x));
            ImGui::TextUnformatted(pageText.c_str());
            ImGui::Separator();
            if (visible.empty()) {
                ImGui::TextColored(ImVec4(0.5f, 0.91f, 0.8f, 1), "当前显示模式下，所有宝箱均已开。");
            } else if (ImGui::BeginTable("MapCounts", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
                ImGui::TableSetupColumn("大地图 / 地点", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("本周目", ImGuiTableColumnFlags_WidthFixed, layout.countWidth);
                ImGui::TableSetupColumn("继承记录", ImGuiTableColumnFlags_WidthFixed, layout.countWidth);
                ImGui::TableSetupColumn("未开", ImGuiTableColumnFlags_WidthFixed, layout.missingWidth);
                ImGui::TableHeadersRow();
                const auto end = std::min(visible.size(), (g_mapPage + 1) * rowsPerPage);
                for (size_t i = g_mapPage * rowsPerPage; i < end; ++i) {
                    const auto& map = counts.maps[visible[i]];
                    const auto missing = map.Remaining(mode);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextUnformatted(map.definition->path);
                    ImGui::PopTextWrapPos();
                    ImGui::TableNextColumn();
                    ImGui::Text("%u / %u", map.current, map.total);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u / %u", map.inherited, map.total);
                    ImGui::TableNextColumn();
                    if (missing) ImGui::TextColored(ImVec4(1, 0.74f, 0.34f, 1), "%u", missing);
                    else ImGui::TextColored(ImVec4(0.5f, 0.91f, 0.8f, 1), "完成");
                }
                ImGui::EndTable();
            }
        }
        ImGui::Separator();
        DrawMapListShortcuts(controller);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("计数：已开 / 总数；道路单列，迷宫含各楼层，包含未到达地点。");
        ImGui::PopStyleColor();
    }
    ImGui::End();
}

// 回访清单与地图收集清单互斥显示，沿用按键高亮和设备热切换风格。
// 明确区分“请求已提交”和“已经到达”，不会把关图阶段冒充传送成功。
// 出发点优先使用现有宝箱目录中的地区/地点名称；没有宝箱的城镇场景采用地区名
// 加场景编号作补充识别。记录时间用于手动区分历史行程，不能冒充存档槽位标识。
static const char* ReturnPointName(const RevisitReturnPoint& point) {
    for (const auto& map : kMaps) if (std::strcmp(map.scene, point.scene)==0) return map.path;
    switch (point.region) {
    case 1: return "洛连特地区";
    case 2: return "柏斯地区";
    case 3: return "卢安地区";
    case 4: return "蔡斯地区";
    case 5: return "格兰赛尔地区";
    case 7: return "利贝尔方舟";
    default: return "记录地点";
    }
}
static void DrawRevisitWindow(float left, bool controller) {
    if (!g_revisitWindow) return;
    const auto context = ReadRevisitNativeContext();
    const auto status = ReadRevisitNativeStatus();
    const auto returnStatus = ReadRevisitReturnStatus(context);
    const auto& destination = RevisitDestinationAt(g_revisitSelection);
    const float width = std::min(640.0f, ImGui::GetIO().DisplaySize.x - 32.0f);
    const float x = std::max(16.0f, std::min(left, ImGui::GetIO().DisplaySize.x - width - 16.0f));
    ImGui::SetNextWindowPos(ImVec2(x, 22), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, 0), ImGuiCond_Always);
    constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
    if (ImGui::Begin("Sky2Revisit", nullptr, flags)) {
        const ImVec4 accent(0.5f, 0.91f, 0.8f, 1);
        ImGui::TextColored(accent, "全传送清单");
        ImGui::TextUnformatted(revisit_policy::kUnrestricted ?
            "传送保留最初出发点。" : "按当前剧情开放；传送保留最初出发点。");
        constexpr size_t rows=kRevisitRowsPerPage;
        const size_t page=g_revisitSelection/rows;
        char pages[48]{};
        std::snprintf(pages,sizeof(pages),"第 %zu / %zu 页",page+1,(RevisitDestinationCount()+rows-1)/rows);
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(),width-ImGui::GetStyle().WindowPadding.x-ImGui::CalcTextSize(pages).x));
        ImGui::TextUnformatted(pages);
        ImGui::Separator();
        for (size_t i=page*rows; i<std::min((page+1)*rows,RevisitDestinationCount()); ++i) {
            const auto& row=RevisitDestinationAt(i);
            const bool selected=i==g_revisitSelection;
            const bool allowed=RevisitTargetAllowed(row.id,context);
            ImGui::PushStyleColor(ImGuiCol_Text,selected ? accent :
                ImGui::GetStyleColorVec4(allowed ? ImGuiCol_Text : ImGuiCol_TextDisabled));
            ImGui::Text("%s %s / %s",selected ? ">" : " ",row.group,row.name);
            ImGui::PopStyleColor();
        }
        ImGui::Separator();
        if (destination.id==108)
            ImGui::TextWrapped("克雷德尔居住区：传送后可步行进入市政府，补取遗漏宝箱。");
        if (destination.id==forest::kTarget)
            ImGui::TextWrapped("迷途之森没有普通出口；请使用记录的出发点返程。");
        if (returnStatus.hasRecord) {
            const auto& record=returnStatus.record;
            const time_t stamp=static_cast<time_t>(record.createdUnixSeconds);
            tm local{}; char date[48]{};
            if (localtime_s(&local,&stamp)==0) std::strftime(date,sizeof(date),"%m-%d %H:%M:%S",&local);
            ImGui::TextWrapped("%s：%s",returnStatus.active ? "最初出发点" : "历史返程候选",ReturnPointName(record.point));
            ImGui::TextDisabled("%s  ·  %s  ·  记录 %zu / %zu",record.point.scene,date,returnStatus.index+1,returnStatus.count);
        } else ImGui::TextDisabled("首次出发前自动记录场景、站立坐标与朝向。");
        const char* message = nullptr;
        if (!RevisitReady() || !context.available) message = "回访保护或原生入口校验未通过，暂不可用。";
        else if (status.phase == RevisitNativePhase::Queued) message = "请求已提交，等待原生地图线程核对……";
        else if (status.phase == RevisitNativePhase::ClosingMap) message = "原生地图正在关闭，等待换图……";
        else if (status.phase == RevisitNativePhase::Dispatched) message = "正在换图，等待实际到达确认……";
        else if (!context.valid) message = "等待游戏场景数据……";
        else if (!RevisitContextAllowed(context)) message = "当前场景或章节数据尚未支持。";
        else if (!context.browsing || context.busy) message = "请打开游戏地图，退出子窗口并等待地图动画结束。";
        else if (!RevisitTargetAllowed(destination.id,context)) {
            if (destination.id==kRevisitReturnTarget)
                message=returnStatus.hasRecord ? (revisit_policy::kUnrestricted ?
                    "返程地点数据尚未通过校验，请核对所选记录。" :
                    "当前剧情暂不允许返回此地点，请先完成游戏原生传送剧情。") :
                    "没有本章节的返程记录；请读取正常地区存档后出发。";

            else if (!returnStatus.storageReady) message="返程记录无法安全保存，本次出发已阻止。";
            else if (RevisitRecoveryRequired(context) && !returnStatus.active) message="重启或读档后，请先选择历史返程点返回，再开始新回访。";
            else if (!revisit_policy::kUnrestricted && context.beforeScriptReturnBlocked && !returnStatus.active)
                message="当前传送由剧情接管，请先使用游戏原生传送继续剧情。";
            else if (!context.returnPointReady && !returnStatus.active)
                message="当前站位尚无法安全记录，暂不能传送。";
            else message=RevisitNativeTargetReason(destination.id,context);
        } else if (g_revisitConfirmation.Armed(destination.id, GetTickCount64()))
            message = destination.id==kRevisitReturnTarget ?
                "核对上方地点和时间，再按一次确认返程；记录不绑定存档槽位。" : "再次按确认组合键前往所选地点（8 秒内有效）。";
        else if (status.phase == RevisitNativePhase::ArrivalUnconfirmed)
            message = "上次到达未能自动确认，出发点已保留；现在可重新选择传送或返程。";
        else if (g_revisitSubmissionRejected || status.phase == RevisitNativePhase::Rejected || status.phase == RevisitNativePhase::Expired)
            message = "上次请求未确认成功；原返程记录保留，请重新打开地图核对。";
        else message = "就绪，请选择目的地。";
        ImGui::TextWrapped("%s", message);
        // 两列按操作配对：上一页/下一页、上一项/下一项、确认/收起。
        // 各列独立测量完整组合键，既保留高亮与热切换，又避免最长手柄文字挤到相邻列。
        const char* revisitKeys[] = {controller ? "View + LB" : "PgUp",
            controller ? "View + RB" : "PgDn", controller ? "View + LT" : "Ctrl + PgUp",
            controller ? "View + RT" : "Ctrl + PgDn", controller ? "View + 十字键右" : "Ctrl + F7",
            controller ? "View + 十字键左" : "Ctrl + F10", controller ? "View + Y" : "F10"};
        const char* revisitActions[] = {"上一页", "下一页", "上一项", "下一项", "确认", "收起清单", "切换返程记录"};
        const unsigned shortcutCount = destination.id==kRevisitReturnTarget && !returnStatus.active && returnStatus.count>1 ? 7 : 6;
        float keyWidths[2]{};
        for (unsigned i=0;i<shortcutCount;++i)
            keyWidths[i%2] = std::max(keyWidths[i%2], ImGui::CalcTextSize(revisitKeys[i]).x + 12.0f);
        if (ImGui::BeginTable("RevisitShortcuts", 2, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableSetupColumn("上一页与确认", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("下一页与收起", ImGuiTableColumnFlags_WidthStretch);
            for (unsigned i=0;i<shortcutCount;++i) {
                if (i%2==0) ImGui::TableNextRow();
                ImGui::TableNextColumn();
                DrawShortcutHint(revisitKeys[i], revisitActions[i], keyWidths[i%2]);
            }
            ImGui::EndTable();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("打开游戏地图，选好地点后确认两次；末页可返程。");
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
        ImGui::TextColored(ImVec4(0.5f, 0.91f, 0.8f, 1), "宝箱追踪  ·  0.5.0");
        if (!enabled) ImGui::TextColored(ImVec4(1, 0.72f, 0.3f, 1), "宝箱标记已暂停（原版显示）");
        else ImGui::Text("显示模式：%s", current ? "本周目" : "继承记录（多周目）");
        ImGui::Separator();
        if (counts.valid) {
            ImGui::Text("本周目已开  %u / 566", counts.current);
            ImGui::Text("继承记录已开  %u / 566", counts.inherited);
            if (!counts.map.empty()) {
                // 两组地区进度同时呈现，与上方全局计数保持一致；切换显示模式不隐藏其中一组。
                ImGui::Text("本周目当前地区已开  %u / %u", counts.map_current, counts.map_total);
                ImGui::Text("继承记录当前地区已开  %u / %u", counts.map_inherited, counts.map_total);
                ImGui::TextDisabled("地区统计包含相邻道路");
            } else ImGui::TextDisabled("打开区域地图后显示两组地区统计");
        } else ImGui::TextDisabled("等待游戏数据……");
        ImGui::Separator();
        ImGui::Text("闭合箱标：未开    开启箱标：已开");
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
