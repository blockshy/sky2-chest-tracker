// Direct3D 11 只读信息面板：复用游戏的 Present 时机绘制，由输入层传递组合键动作。
// 每帧释放后缓冲视图，避免持有引用导致窗口缩放、全屏切换时 ResizeBuffers 失败。
#include "tracker.h"
#include "input_bridge.h"
#include <d3d11.h>
#include <dxgi.h>
#include <MinHook.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <mutex>

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

// 两种输入来源共用相同的动作处理，确保口径切换、筛选和分页行为完全一致。
static void ApplyActions(uint32_t actions) {
    if (actions & ToggleMode) {
        const bool current = g_mode.load() == Mode::Current;
        g_mode.store(current ? Mode::Inherited : Mode::Current);
        g_mapPage = 0;
        Log(current ? "View changed: inherited." : "View changed: current playthrough.");
    }
    if (actions & TogglePanel) g_panel.store(!g_panel.load());
    if (actions & ToggleEnabled) {
        g_enabled.store(!g_enabled.load());
        Log(g_enabled.load() ? "Map hook resumed." : "Map hook paused.");
    }
    if (actions & ToggleList) {
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
    static bool previous[7]{};
    const int keys[] = {VK_F6, VK_F7, VK_F9, VK_F8, VK_F10, VK_PRIOR, VK_NEXT};
    const bool foreground = GetForegroundWindow() == g_window;
    uint32_t actions = TakeInputActions();
    for (int i = 0; i < 7; ++i) {
        const bool down = (GetAsyncKeyState(keys[i]) & 0x8000) != 0;
        if (down && !previous[i]) actions |= 1u << i;
        previous[i] = down;
    }
    if (foreground) ApplyActions(actions);
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

static void DrawMapList(const Counts& counts, float left) {
    if (!g_mapList) return;
    const auto mode = g_mode.load();
    const auto display = ImGui::GetIO().DisplaySize;
    const bool controller = UsingController();
    const float width = std::min(1060.0f, display.x - 32.0f);
    // 完整路径在窄窗口自动换行；按最长路径预留行高，使翻页容量不会随当前页抖动。
    const auto& style = ImGui::GetStyle();
    const float nameWidth = std::max(80.0f, width - style.WindowPadding.x * 2 - 288.0f - style.CellPadding.x * 8 - 8);
    float rowHeight = ImGui::GetTextLineHeight();
    for (const auto& map : counts.maps)
        rowHeight = std::max(rowHeight, ImGui::CalcTextSize(map.definition->path, nullptr, false, nameWidth).y);
    rowHeight += style.CellPadding.y * 2;
    // 标题、筛选说明和页脚预留固定高度；小窗口减少行数，防止底部被裁切。
    const auto rowsPerPage = static_cast<size_t>(std::max(1.0f,
        std::min(static_cast<float>(kRowsPerPage), (display.y - 320.0f) / rowHeight)));
    const auto visible = VisibleMaps(counts.maps, mode, g_missingOnly);
    const unsigned collected = mode == Mode::Current ? counts.current : counts.inherited;
    const auto complete = std::count_if(counts.maps.begin(), counts.maps.end(),
        [&](const auto& map) { return map.Remaining(mode) == 0; });
    const auto pages = std::max<size_t>(1, (visible.size() + rowsPerPage - 1) / rowsPerPage);
    g_mapPage = std::min(g_mapPage, pages - 1);
    // 窄窗口时允许清单覆盖主面板的一部分，始终让整张清单位于视口内。
    const float x = std::max(16.0f, std::min(left, display.x - width - 16.0f));
    ImGui::SetNextWindowPos(ImVec2(x, 22), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, 0), ImGuiCond_Always);
    constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
    if (ImGui::Begin("Sky2MapProgress", nullptr, flags)) {
        ImGui::TextColored(ImVec4(0.5f, 0.91f, 0.8f, 1), "各地图宝箱收集");
        ImGui::Text("%s · %s", mode == Mode::Current ? "当前周目" : "继承记录（多周目）",
                    g_missingOnly ? "仅看有遗漏的地图" : "全部地图（有遗漏的在前）");
        if (!counts.valid) {
            ImGui::TextDisabled("等待游戏数据……");
        } else {
            ImGui::Text("已完成 %u / %u 张地图    待收集 %u 个", static_cast<unsigned>(complete),
                        static_cast<unsigned>(counts.maps.size()), 566 - collected);
            ImGui::Separator();
            if (visible.empty()) {
                ImGui::TextColored(ImVec4(0.5f, 0.91f, 0.8f, 1), "当前统计口径下，全部地图已收集完成。");
            } else if (ImGui::BeginTable("MapCounts", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
                ImGui::TableSetupColumn("大地图 / 地点", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("本周目", ImGuiTableColumnFlags_WidthFixed, 104);
                ImGui::TableSetupColumn("继承记录", ImGuiTableColumnFlags_WidthFixed, 104);
                ImGui::TableSetupColumn("待收集", ImGuiTableColumnFlags_WidthFixed, 80);
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
            ImGui::Separator();
            ImGui::Text("第 %u / %u 页    %s 翻页", static_cast<unsigned>(g_mapPage + 1), static_cast<unsigned>(pages),
                        controller ? "View + LB / RB" : "PgUp / PgDn");
        }
        ImGui::TextDisabled("%s", controller ? "按住 View：X 切换口径 · Y 全部 / 遗漏 · A 收起清单" :
                                               "F6 切换口径 · F10 全部 / 遗漏 · F8 收起清单");
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("按大地图前缀定位；道路单列，迷宫含各楼层，也包含尚未到达的地点。");
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
    ImGui::SetNextWindowPos(ImVec2(22, 22), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.93f);
    constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
    float panelRight = 330;
    if (ImGui::Begin("Sky2ChestTracker", nullptr, flags)) {
        ImGui::TextColored(ImVec4(0.5f, 0.91f, 0.8f, 1), "宝箱追踪  ·  0.3.2");
        if (!enabled) ImGui::TextColored(ImVec4(1, 0.72f, 0.3f, 1), "地图修改已暂停（原版显示）");
        else ImGui::Text("地图口径：%s", current ? "当前周目" : "继承记录（多周目）");
        ImGui::Separator();
        if (counts.valid) {
            ImGui::Text("本周目已开  %u / 566", counts.current);
            ImGui::Text("继承记录已开  %u / 566", counts.inherited);
            if (!counts.map.empty()) {
                // 两组地区进度同时呈现，与上方全局计数保持一致；切换图标口径不隐藏其中一组。
                ImGui::Text("本周目当前地区已开  %u / %u", counts.map_current, counts.map_total);
                ImGui::Text("继承记录当前地区已开  %u / %u", counts.map_inherited, counts.map_total);
                ImGui::TextDisabled("地区统计包含相邻道路");
            } else ImGui::TextDisabled("打开区域地图后显示两组地区统计");
        } else ImGui::TextDisabled("等待游戏数据……");
        ImGui::Separator();
        ImGui::Text("闭合箱标：未取    开启箱标：已取");
        if (UsingController()) {
            ImGui::TextDisabled("View + X 切换口径 · View + B 显隐");
            ImGui::TextDisabled("View + RS 暂停 / 恢复地图修改");
            ImGui::TextColored(ImVec4(0.5f, 0.91f, 0.8f, 1), "View + A 各地图收集情况");
            ImGui::TextDisabled("%s", ControllerModifierHeld() ? "View 已按住，请按功能键" : "View = 双窗口键，按住后再组合");
            ImGui::TextDisabled("RS = 按下右摇杆");
        } else {
            ImGui::TextDisabled("F6 切换口径 · F7 显隐 · F9 暂停");
            ImGui::TextColored(ImVec4(0.5f, 0.91f, 0.8f, 1), "F8 各地图收集情况");
            if (InputBridgeReady()) ImGui::TextDisabled("手柄：按住 View + A 打开清单");
        }
        panelRight = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x + 14;
    }
    ImGui::End();
    DrawMapList(counts, panelRight);
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
                    ImGui_ImplDX11_NewFrame();
                    ImGui_ImplWin32_NewFrame();
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
