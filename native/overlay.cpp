// Direct3D 11 独立窗口：原 ASI/Standalone 继续拥有自己的 Present 与字体后端。
// 完整页面复用业务函数表，固定布局/导航使用仓库内源码；不需要另外加载公共界面程序。
#include "tracker.h"
#include "input_bridge.h"
#include "standalone_panel.h"
#include "standalone_ui/ui.h"
#include "standalone_ui/input.h"
#include "panel_pages.h"
#include "ui_scale.h"
#include "ui_text.h"
#include "localized_names.h"
#include "game_language.h"
#include "panel_fonts.h"
#include <d3d11.h>
#include <dxgi.h>
#include <MinHook.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <mutex>
#include <array>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
namespace tracker {
using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
static PresentFn g_originalPresent = nullptr;
static ImGuiContext* g_context = nullptr;
static ID3D11Device* g_device = nullptr;
static ID3D11DeviceContext* g_deviceContext = nullptr;
static HWND g_window = nullptr;
// Win32 后端可能在同一线程同步递送窗口消息，因此允许本线程重入；其它
// 线程仍串行访问本模块的 ImGui 上下文，退出回调时恢复前一个 Mod 的上下文。
static std::recursive_mutex g_renderLock;
static void WindowMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    try {
        std::lock_guard<std::recursive_mutex> guard(g_renderLock);
        auto* previous = ImGui::GetCurrentContext();
        if (g_context && window == g_window) {
            ImGui::SetCurrentContext(g_context);
            ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam);
        }
        ImGui::SetCurrentContext(previous);
    } catch (...) { /* 输入回调不把可选界面的异常传播到游戏窗口过程。 */ }
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
    // 窗口位置由本次进程的外壳状态保存；不向游戏目录写入 imgui.ini。
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    LoadStandaloneFonts(io);
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    sky2solo::ConfigureTheme();
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
    if (!InitializeStandalonePanel()) {
        ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(g_context); g_context = nullptr;
        g_deviceContext->Release(); g_device->Release();
        g_deviceContext = nullptr; g_device = nullptr; g_window = nullptr;
        return false;
    }
    AttachInputWindow(g_window, &WindowMessage);
    Log("D3D11 panel initialized.");
    return true;
}

static HRESULT WINAPI Present(IDXGISwapChain* swap, UINT interval, UINT options) {
    // DXGI 的 TEST 调用只询问可呈现状态，不应在其中提交绘制命令。
    if (!(options & DXGI_PRESENT_TEST)) {
        std::lock_guard<std::recursive_mutex> guard(g_renderLock);
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
                    PumpInputKeyboard();
                    ApplyStandaloneActions(TakeInputActions());
                    SynchronizeInputPanel();
                    if (ConsumeInputReset()) {
                        PanelVisibilityChanged(0);
                        ImGui::GetIO().ClearInputKeys();
                        ImGui::GetIO().ClearInputMouse();
                    }
                    // 使用实际后缓冲尺寸，避免窗口坐标、Windows DPI 与渲染分辨率不一致。
                    // 查询后立即释放引用，保持 ResizeBuffers 可用。
                    ID3D11Texture2D* sizeBuffer = nullptr;
                    if (FAILED(swap->GetBuffer(0, __uuidof(ID3D11Texture2D),
                        reinterpret_cast<void**>(&sizeBuffer)))) {
                        SetInputFrameHealth(false);
                        ImGui::SetCurrentContext(previous);
                        return g_originalPresent(swap, interval, options);
                    }
                    D3D11_TEXTURE2D_DESC bufferSize{};
                    sizeBuffer->GetDesc(&bufferSize);
                    sizeBuffer->Release();
                    ImGui_ImplDX11_NewFrame();
                    ImGui_ImplWin32_NewFrame();
                    auto& io = ImGui::GetIO();
                    // 交互坐标以游戏客户区显示像素为准；后缓冲可能使用不同渲染
                    // 分辨率，只通过 FramebufferScale 映射，避免鼠标命中位置偏移。
                    const float width = io.DisplaySize.x, height = io.DisplaySize.y;
                    const float scale = std::clamp(height / 1080.0f, 0.7f, 2.5f);
                    io.DisplayFramebufferScale = ImVec2(bufferSize.Width / std::max(1.0f, width),
                        bufferSize.Height / std::max(1.0f, height));
                    ImGui::GetStyle().FontScaleDpi = scale;
                    XINPUT_GAMEPAD pad{};
                    const bool freshPad = ReadInputPad(pad);
                    sky2solo::FeedGamepad(freshPad ? &pad : nullptr, InputPanelInteractive());
                    io.MouseDrawCursor = InputPanelInteractive();
                    ImGui::NewFrame();
                    Sky2Frame frame{sizeof(Sky2Frame), width, height, scale, GetTickCount64(),
                        (g_panel.load() ? InputPanelInteractive() : GetForegroundWindow() == g_window) ? 1 : 0, g_panel.load() ? 1 : 0,
                        g_panel.load() ? 1 : 0, UsingController() ? 1 : 0};
                    DrawStandalonePanel(frame);
                    SynchronizeInputPanel();
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
                        SetInputFrameHealth(true);
                    } else SetInputFrameHealth(false);
                }
            } else SetInputFrameHealth(false);
        } catch (...) {
            SetInputFrameHealth(false);
            PanelVisibilityChanged(0);
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
    // 三个 ASI 的 MinHook 实例各自独立，但 Present 入口属于同一进程。
    // 创建和启用必须串行，确保后装者看见完整前链，而非同时覆盖原始入口。
    sky2solo::PresentInstallGuard installGuard;
    if (!installGuard) { Log("Present hook install lock unavailable."); return false; }
    const auto initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) return false;
    if (MH_CreateHook(target, reinterpret_cast<void*>(&Present), reinterpret_cast<void**>(&g_originalPresent)) != MH_OK)
        return false;
    if (MH_EnableHook(target) != MH_OK) { MH_RemoveHook(target); return false; }
    Log("DXGI Present hook installed.");
    return true;
}
}
