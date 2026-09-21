// 只拦截游戏自身的 XInputGetState 导入槽，继续调用原入口以保留 Steam Input 的映射。
// 不轮询另一套设备列表，不修改手柄振动，也不将 Mod 组合键传给其他应用。
#include "input_bridge.h"
#include "tracker.h"
#include <Xinput.h>
#include <atomic>
#include <cstring>

namespace tracker {
using GetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
static GetStateFn g_nextState = nullptr;
static SRWLOCK g_padLock = SRWLOCK_INIT;
static PadFilter g_filters[4];
static XINPUT_GAMEPAD g_outputs[4]{};
static DWORD g_packets[4]{};
static std::atomic<uint32_t> g_actions{0}, g_connected{0}, g_modifier{0};
static std::atomic<bool> g_controller{false}, g_ready{false};
static std::atomic<HWND> g_inputWindow{nullptr};
static std::atomic<WNDPROC> g_nextWindowProc{nullptr};

static DWORD WINAPI FilteredGetState(DWORD index, XINPUT_STATE* state) noexcept {
    const auto error = g_nextState(index, state);
    if (index >= 4 || !state) return error;
    AcquireSRWLockExclusive(&g_padLock);
    if (error != ERROR_SUCCESS) {
        g_filters[index].Reset();
        g_modifier.fetch_and(~(1u << index));
        if ((g_connected.fetch_and(~(1u << index)) & ~(1u << index)) == 0) g_controller.store(false);
        ReleaseSRWLockExclusive(&g_padLock);
        return error;
    }
    g_connected.fetch_or(1u << index);
    const auto& p = state->Gamepad;
    const PadSample raw{p.wButtons, p.bLeftTrigger, p.bRightTrigger, p.sThumbLX, p.sThumbLY, p.sThumbRX, p.sThumbRY};
    const HWND window = g_inputWindow.load();
    const bool foreground = window && GetForegroundWindow() == window;
    const auto filtered = g_filters[index].Update(raw, foreground);
    if (filtered.activity) g_controller.store(true);
    if (filtered.modifier) g_modifier.fetch_or(1u << index);
    else g_modifier.fetch_and(~(1u << index));
    g_actions.fetch_or(filtered.actions);
    const auto& f = filtered.game;
    XINPUT_GAMEPAD output{f.buttons, f.leftTrigger, f.rightTrigger, f.lx, f.ly, f.rx, f.ry};
    // 补发 View 的按下与松开可能发生在同一原生数据包内，因此维护输出状态自己的序号。
    if (std::memcmp(&g_outputs[index], &output, sizeof(output))) {
        g_outputs[index] = output;
        ++g_packets[index];
    }
    state->Gamepad = output;
    state->dwPacketNumber = g_packets[index];
    ReleaseSRWLockExclusive(&g_padLock);
    static std::atomic<bool> reported{false};
    if (!reported.exchange(true)) Log("Controller input callback verified.");
    return error;
}

static LRESULT CALLBACK ObserveWindow(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (GetForegroundWindow() == window) {
        bool keyboardMouse = ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && !(lparam & (1LL << 30))) ||
            message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN ||
            message == WM_XBUTTONDOWN || message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL;
        if (message == WM_INPUT) {
            // 原始输入由游戏注册；只读取已有消息，不额外注册设备，也不阻止游戏再次读取。
            // 用真实鼠标增量判断切换，排除游戏重置光标位置引起的 WM_MOUSEMOVE。
            RAWINPUT raw{};
            UINT size = sizeof(raw);
            const auto read = GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER));
            if (read != UINT(-1) && read >= sizeof(RAWINPUTHEADER) && raw.header.dwType == RIM_TYPEMOUSE)
                keyboardMouse |= raw.data.mouse.lLastX != 0 || raw.data.mouse.lLastY != 0 || raw.data.mouse.usButtonFlags != 0;
        }
        if (keyboardMouse) g_controller.store(false);
    }
    if (message == WM_KILLFOCUS) { g_actions.store(0); g_controller.store(false); }
    return CallWindowProcW(g_nextWindowProc.load(), window, message, wparam, lparam);
}

void AttachInputWindow(HWND window) noexcept {
    g_inputWindow.store(window);
    if (g_nextWindowProc.load()) return;
    // 只观察输入来源，所有消息仍交回原窗口过程；不接管游戏的鼠标、文本或快捷键处理。
    const auto previous = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC));
    if (!previous) { Log("Input source observer unavailable."); return; }
    g_nextWindowProc = previous;
    SetLastError(0);
    const auto installed = SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&ObserveWindow));
    if (!installed && GetLastError()) { g_nextWindowProc = nullptr; Log("Input source observer failed."); }
    else if (installed) g_nextWindowProc = reinterpret_cast<WNDPROC>(installed);
}

bool InstallInputBridge(uintptr_t base) noexcept {
    // 当前 EXE 唯一的 XInputGetState 调用位于 0x6A55A8；槽位可能已经由 Steam 重定向。
    // 校验调用指令后保存原槽目标，用原子比较交换安装，避免覆盖安装期间的新挂钩。
    const unsigned char expected[] = {0xFF, 0x15, 0x3A, 0x61, 0x21, 0x00};
    if (std::memcmp(reinterpret_cast<void*>(base + 0x6A55A8), expected, sizeof(expected))) return false;
    auto slot = reinterpret_cast<void**>(base + 0x8BB6E8);
    void* previous = *slot;
    if (!previous) return false;
    DWORD protection = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection)) return false;
    g_nextState = reinterpret_cast<GetStateFn>(previous);
    const bool installed = InterlockedCompareExchangePointer(slot, reinterpret_cast<void*>(&FilteredGetState), previous) == previous;
    DWORD unused = 0;
    VirtualProtect(slot, sizeof(void*), protection, &unused);
    g_ready.store(installed);
    Log(installed ? "Controller import bridge installed." : "Controller import bridge conflict.");
    return installed;
}

uint32_t TakeInputActions() noexcept { return g_actions.exchange(0); }
bool UsingController() noexcept { return g_controller.load(); }
bool InputBridgeReady() noexcept { return g_ready.load(); }
bool ControllerModifierHeld() noexcept { return g_modifier.load() != 0; }
}
