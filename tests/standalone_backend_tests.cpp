// 独立窗口后端回归：直接执行生产 overlay.cpp/input_bridge.cpp 和真实 ImGui/WARP。
// 测试窗口始终隐藏，只替换前台、可见性及物理采样；不安装游戏/Present 挂钩，
// 不激活窗口、不移动系统鼠标，不读取游戏或玩家设置。
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <cstdio>
#include <cstdlib>
#include "standalone_ui/hotkeys.h"

namespace {
HWND fixtureForeground = nullptr, fixtureVisible = nullptr, fixtureExtraVisible = nullptr;
unsigned checks = 0, forwardedPresents = 0, visibilityResets = 0, initializations = 0, drawnFrames = 0;
HWND WINAPI FixtureForeground() { return fixtureForeground; }
BOOL WINAPI FixtureVisible(HWND window) { return window && (window == fixtureVisible || window == fixtureExtraVisible); }
SHORT WINAPI FixtureAsync(int) { return 0; }
BOOL WINAPI FixtureCursor(LPPOINT point) { if (!point) return FALSE; *point = {40, 40}; return TRUE; }
HRESULT WINAPI ForwardPresent(IDXGISwapChain*, UINT, UINT) { ++forwardedPresents; return S_OK; }
void Require(bool value, const char* label) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); }
}
}
namespace sky2solo { HotkeyKeyboardState FixtureKeyboard() noexcept { return {}; } }

// 生产实现中的窗口身份、客户区、DPI API、窗口过程挂接和 D3D 资源均为真实系统
// 调用。只把资格检查中的可见/前台改为本地样本，避免测试干扰用户的实际桌面。
#define GetForegroundWindow FixtureForeground
#define IsWindowVisible FixtureVisible
#define GetAsyncKeyState FixtureAsync
#define GetCursorPos FixtureCursor
#define ReadHotkeyKeyboardState FixtureKeyboard
// 两份生产文件在正式构建中是独立翻译单元。夹具合并后出现的局部同名不影响
// 运行语义，只在此包含范围关闭该遮蔽告警，仍保留其他 /W4 检查。
#pragma warning(push)
#pragma warning(disable: 4459)
#include "../native/input_bridge.cpp"
#include "../native/overlay.cpp"
#pragma warning(pop)
#undef ReadHotkeyKeyboardState
#undef GetCursorPos
#undef GetAsyncKeyState
#undef IsWindowVisible
#undef GetForegroundWindow

namespace tracker {
HMODULE g_module = nullptr;
std::atomic<bool> g_enabled{true}, g_panel{false};
std::atomic<Mode> g_mode{Mode::Current};
void Log(const char*) noexcept {}
void RefreshGameLanguage() noexcept {}
void LoadStandaloneFonts(ImGuiIO& io) { io.Fonts->AddFontDefault(); }
// 页面业务和本机字体另有真实夹具；这里只提供可绘制内容，确保生产 Present
// 完整创建/绘制/释放 RTV，不引入任何原生地图、收集状态或磁盘设置依赖。
bool InitializeStandalonePanel() noexcept { ++initializations; return true; }
bool StandaloneModeShortcutEditing() noexcept { return false; }
void PanelVisibilityChanged(int32_t active) noexcept { if (!active) ++visibilityResets; }
void ApplyStandaloneActions(uint32_t pending) noexcept {
    if (pending & TogglePanel) g_panel.store(!g_panel.load());
}
void DrawStandalonePanel(const Sky2Frame&) {
    ++drawnFrames;
    if (!g_panel.load()) return;
    ImGui::Begin("Chest backend fixture");
    ImGui::TextUnformatted("Production Present / real WARP rendering");
    ImGui::End();
}
}

namespace {
struct TestWindow {
    HWND value = nullptr;
    explicit TestWindow(const wchar_t* className) {
        value = CreateWindowExW(0, className, L"", WS_OVERLAPPED, 0, 0, 960, 640,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        Require(value != nullptr, "create hidden test window");
        Require(!IsWindowVisible(value), "test never shows or activates a window");
    }
    ~TestWindow() { if (value) DestroyWindow(value); }
};
struct Swap {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* immediate = nullptr;
    IDXGISwapChain* chain = nullptr;
    explicit Swap(HWND window) {
        DXGI_SWAP_CHAIN_DESC description{};
        description.BufferDesc.Width = 960; description.BufferDesc.Height = 640;
        description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1; description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = 1; description.OutputWindow = window; description.Windowed = TRUE;
        description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        Require(SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &description, &chain, &device, nullptr, &immediate)),
            "create independent WARP device and swap chain");
    }
    ~Swap() { if (immediate) immediate->Release(); if (chain) chain->Release(); if (device) device->Release(); }
    void Render(UINT options = 0) {
        const auto before = forwardedPresents;
        Require(tracker::Present(chain, 0, options) == S_OK && forwardedPresents == before + 1,
            "production Present forwards exactly once");
    }
    void Resize(UINT width, UINT height) {
        // 不在测试端 ClearState 或替生产代码释放资源；真实 ResizeBuffers 只有
        // 在 Mod 已释放本帧的纹理/RTV/OM 引用时才可成功，能够发现悬挂后缓冲。
        Require(SUCCEEDED(chain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0)),
            "ResizeBuffers succeeds without compensating for leaked Mod references");
    }
};
void CleanupGui() {
    using namespace tracker;
    g_panel.store(false); SynchronizeInputPanel(); ReleaseInputMouseButtons();
    if (g_context) {
        ImGui::SetCurrentContext(g_context);
        ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(g_context); g_context = nullptr;
    }
    if (g_deviceContext) { g_deviceContext->Release(); g_deviceContext = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
    g_window = nullptr;
}
}

int main() {
    const wchar_t* mainClass = L"Sky2ChestBackendFixture";
    const wchar_t* otherClass = L"Sky2ChestBackendAuxiliary";
    for (const wchar_t* name : {mainClass, otherClass}) {
        WNDCLASSW definition{};
        definition.lpfnWndProc = DefWindowProcW; definition.hInstance = GetModuleHandleW(nullptr);
        definition.lpszClassName = name;
        Require(RegisterClassW(&definition) != 0, "register private test window class");
    }
    {
        TestWindow original(mainClass), replacement(mainClass), auxiliary(otherClass);
        Swap first(original.value), newDevice(original.value), nextWindow(replacement.value), otherWindow(auxiliary.value);
        fixtureForeground = fixtureVisible = original.value;
        tracker::g_originalPresent = &ForwardPresent;
        tracker::ready.store(true); // 夹具不安装真实 IAT，直接发布输入接入就绪状态。
        auto* callerContext = ImGui::CreateContext();
        first.Render();
        Require(ImGui::GetCurrentContext() == callerContext, "initialization restores caller ImGui context");
        Require(tracker::g_context && tracker::g_device == first.device && tracker::g_window == original.value,
            "first Present initializes the real Win32 and DX11 backends");
        Require(initializations == 1, "first display target initializes the business adapter once");
        tracker::g_panel.store(true); first.Render(); first.Render();
        Require(tracker::InputPanelInteractive(), "bound foreground window accepts input after a healthy frame");
        Require(GetPropW(original.value, tracker::previousWindowProperty) != nullptr,
            "real input bridge installs per-window forwarding identity");
        first.Resize(800, 500); first.Render();
        const auto beforeTest = drawnFrames;
        first.Render(DXGI_PRESENT_TEST);
        Require(drawnFrames == beforeTest, "DXGI test Present does not draw or mutate the backend");

        // 同 HWND 更换 D3D11 设备时，Chest 会重建整个自有 ImGui 上下文；业务面板
        // 可见意图保留。对象分配地址可能复用，因此用真实设备和初始化次数判断重建。
        newDevice.Render(); newDevice.Render();
        Require(tracker::g_device == newDevice.device && tracker::g_device != first.device &&
            tracker::g_context && initializations == 2 && tracker::g_panel.load() && tracker::InputPanelInteractive(),
            "same HWND device replacement rebuilds backend and preserves visible panel intent");
        Require(ImGui::GetCurrentContext() == callerContext, "device replacement restores another Mod context");
        newDevice.Resize(700, 450); newDevice.Render();

        fixtureForeground = fixtureVisible = auxiliary.value;
        const auto rejectedResets = visibilityResets;
        const auto rejectedDraws = drawnFrames;
        otherWindow.Render();
        Require(tracker::g_window == original.value && tracker::inputWindow.load() == original.value &&
            tracker::g_device == newDevice.device && visibilityResets == rejectedResets && drawnFrames == rejectedDraws,
            "different-class auxiliary swap chain cannot hijack or reset the active panel");
        fixtureForeground = fixtureVisible = replacement.value; fixtureExtraVisible = original.value;
        nextWindow.Render();
        Require(tracker::g_window == original.value && initializations == 2,
            "still-visible original main window prevents same-class auxiliary takeover");
        fixtureExtraVisible = nullptr; nextWindow.Render(); nextWindow.Render();
        Require(tracker::g_window == replacement.value && tracker::inputWindow.load() == replacement.value &&
            tracker::g_device == nextWindow.device && tracker::g_context && initializations == 3,
            "eligible replacement HWND rebinds real input and graphics backends");
        Require(visibilityResets > rejectedResets && tracker::g_panel.load() && tracker::InputPanelInteractive(),
            "replacement clears pending confirmation and recovers healthy panel input");
        Require(GetPropW(original.value, tracker::previousWindowProperty) && GetPropW(replacement.value, tracker::previousWindowProperty),
            "old and replacement windows keep separate original procedure identities");
        const auto inputGeneration = tracker::generation.load();
        SendMessageW(original.value, WM_KILLFOCUS, 0, 0);
        Require(tracker::generation.load() == inputGeneration && tracker::InputPanelInteractive(),
            "late old-window focus loss cannot reset or close the replacement panel");
        nextWindow.Resize(820, 540); nextWindow.Render();
        Require(ImGui::GetCurrentContext() == callerContext, "rebinding and rendering preserve caller ImGui context");
        CleanupGui(); ImGui::SetCurrentContext(callerContext); ImGui::DestroyContext(callerContext);
    }
    for (const wchar_t* name : {mainClass, otherClass}) UnregisterClassW(name, GetModuleHandleW(nullptr));
    std::printf("tracker_standalone_backend_tests: %u checks passed\n", checks);
    return 0;
}
