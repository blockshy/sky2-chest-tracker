// 直接执行正式输入桥，以固定时间/前台/实体键样本替换系统读取。
// 不安装真实窗口或游戏 IAT，不发送系统输入；合作 TLS 仍使用正式实现。
#include <Windows.h>
#include <Xinput.h>
#include <cstdint>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <cstring>
#include <cwchar>
#include "standalone_ui/hotkeys.h"
namespace sample {
uint64_t now = 1000;
HWND foreground = reinterpret_cast<HWND>(uintptr_t{1});
SHORT keys[256]{};
XINPUT_GAMEPAD pad{};
unsigned keyboardSamples = 0;
ULONGLONG WINAPI Now() { return now; }
HWND WINAPI Foreground() { return foreground; }
SHORT WINAPI Key(int key) { return key >= 0 && key < 256 ? keys[key] : 0; }
DWORD WINAPI State(DWORD, XINPUT_STATE* output) { output->Gamepad = pad; return ERROR_SUCCESS; }
// 不创建桌面窗口，以两个不同 DPI/位置的 HWND 模型执行正式坐标转换和合作链。
// 所有系统副作用均在本 fixture 内，测试不会移动玩家光标或影响窗口焦点。
struct Window {
    HANDLE next = nullptr;
    LONG_PTR procedure = 0;
    LONG originX = 0, originY = 0;
    HANDLE dpi = reinterpret_cast<HANDLE>(uintptr_t{1});
    bool visible = true;
    DWORD owner = GetCurrentProcessId();
    const wchar_t* className = L"GameWindow";
    RECT area{0, 0, 1280, 720};
};
Window windows[3]{};
HANDLE threadDpi = reinterpret_cast<HANDLE>(uintptr_t{3});
POINT cursor{100, 200};
bool cursorReadable = true, conversionWorks = true, subclassWorks = true;
unsigned callbacks = 0, originalCalls = 0, forwardedOld = 0;
size_t WindowIndex(HWND window) { return reinterpret_cast<uintptr_t>(window); }
BOOL WINAPI WindowExists(HWND window) { return WindowIndex(window) > 0 && WindowIndex(window) < std::size(windows); }
BOOL WINAPI WindowVisible(HWND window) { return WindowExists(window) && windows[WindowIndex(window)].visible; }
DWORD WINAPI WindowProcess(HWND window, LPDWORD owner) { *owner = windows[WindowIndex(window)].owner; return 1; }
int WINAPI WindowClass(HWND window, LPWSTR text, int count) {
    wcsncpy_s(text, static_cast<size_t>(count), windows[WindowIndex(window)].className, _TRUNCATE);
    return static_cast<int>(std::wcslen(text));
}
BOOL WINAPI ClientRect(HWND window, LPRECT area) { *area = windows[WindowIndex(window)].area; return TRUE; }
HANDLE WINAPI Property(HWND window, LPCWSTR) { return windows[WindowIndex(window)].next; }
BOOL WINAPI SetProperty(HWND window, LPCWSTR, HANDLE value) { windows[WindowIndex(window)].next = value; return TRUE; }
HANDLE WINAPI RemoveProperty(HWND window, LPCWSTR) { auto& next = windows[WindowIndex(window)].next; const auto result = next; next = nullptr; return result; }
LONG_PTR WINAPI WindowLong(HWND window, int) { return windows[WindowIndex(window)].procedure; }
LONG_PTR WINAPI SetWindowLong(HWND window, int, LONG_PTR value) {
    if (!subclassWorks) { SetLastError(ERROR_ACCESS_DENIED); return 0; }
    auto& procedure = windows[WindowIndex(window)].procedure;
    const auto before = procedure; procedure = value; return before;
}
LRESULT CALLBACK Original(HWND, UINT, WPARAM, LPARAM) { ++originalCalls; return 77; }
LRESULT WINAPI Forward(WNDPROC procedure, HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (window != foreground) ++forwardedOld;
    return procedure(window, message, wparam, lparam);
}
LRESULT WINAPI Default(HWND, UINT, WPARAM, LPARAM) { return 88; }
void Message(HWND, UINT, WPARAM, LPARAM) noexcept { ++callbacks; }
HMODULE WINAPI Module(LPCWSTR) { return reinterpret_cast<HMODULE>(uintptr_t{1}); }
HANDLE WINAPI WindowDpi(HWND window) { return windows[WindowIndex(window)].dpi; }
HANDLE WINAPI ThreadDpi(HANDLE next) { const auto before = threadDpi; threadDpi = next; return before; }
FARPROC WINAPI Procedure(HMODULE, LPCSTR name) {
    if (!std::strcmp(name, "GetWindowDpiAwarenessContext")) return reinterpret_cast<FARPROC>(&WindowDpi);
    if (!std::strcmp(name, "SetThreadDpiAwarenessContext")) return reinterpret_cast<FARPROC>(&ThreadDpi);
    return nullptr;
}
LONG CoordinateScale() { return static_cast<LONG>(reinterpret_cast<uintptr_t>(threadDpi)); }
BOOL WINAPI Cursor(POINT* point) {
    if (!cursorReadable) return FALSE;
    point->x = cursor.x / CoordinateScale(); point->y = cursor.y / CoordinateScale(); return TRUE;
}
BOOL WINAPI ToClient(HWND window, POINT* point) {
    if (!conversionWorks) return FALSE;
    point->x -= windows[WindowIndex(window)].originX / CoordinateScale();
    point->y -= windows[WindowIndex(window)].originY / CoordinateScale(); return TRUE;
}
}
namespace sky2solo {
// 只替换系统采样；动态绑定读取、匹配、版本与磁盘提交仍使用生产组件。
HotkeyKeyboardState ReadFixtureHotkeyKeyboardState() noexcept {
    ++sample::keyboardSamples;
    HotkeyKeyboardState state;
    for (unsigned key = 0; key < state.down.size(); ++key) state.down[key] = (sample::keys[key] & 0x8000) != 0;
    if (state.down[VK_CONTROL]) state.modifiers |= HotkeyCtrl;
    if (state.down[VK_SHIFT]) state.modifiers |= HotkeyShift;
    if (state.down[VK_MENU]) state.modifiers |= HotkeyAlt;
    state.windows = state.down[VK_LWIN] || state.down[VK_RWIN];
    return state;
}
}
#define GetTickCount64 sample::Now
#define GetForegroundWindow sample::Foreground
#define GetAsyncKeyState sample::Key
#define ReadHotkeyKeyboardState ReadFixtureHotkeyKeyboardState
#define GetModuleHandleW sample::Module
#define GetProcAddress sample::Procedure
#define GetCursorPos sample::Cursor
#define ScreenToClient sample::ToClient
#define IsWindow sample::WindowExists
#define IsWindowVisible sample::WindowVisible
#define GetWindowThreadProcessId sample::WindowProcess
#define GetClassNameW sample::WindowClass
#define GetClientRect sample::ClientRect
#define GetPropW sample::Property
#define SetPropW sample::SetProperty
#define RemovePropW sample::RemoveProperty
#define GetWindowLongPtrW sample::WindowLong
#define SetWindowLongPtrW sample::SetWindowLong
#define CallWindowProcW sample::Forward
#define DefWindowProcW sample::Default
#include "../native/input_bridge.cpp"
#undef GetTickCount64
#undef GetForegroundWindow
#undef GetAsyncKeyState
#undef ReadHotkeyKeyboardState
#undef GetModuleHandleW
#undef GetProcAddress
#undef GetCursorPos
#undef ScreenToClient
#undef IsWindow
#undef IsWindowVisible
#undef GetWindowThreadProcessId
#undef GetClassNameW
#undef GetClientRect
#undef GetPropW
#undef SetPropW
#undef RemovePropW
#undef GetWindowLongPtrW
#undef SetWindowLongPtrW
#undef CallWindowProcW
#undef DefWindowProcW
namespace tracker {
HMODULE g_module = nullptr;
std::atomic<bool> g_enabled{true}, g_panel{false};
std::atomic<Mode> g_mode{Mode::Current};
void Log(const char*) noexcept {}
}

int main() {
    using namespace tracker;
    namespace fs = std::filesystem;
    const auto config = fs::current_path() / ("chest-hotkeys-fixture-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    assert(fs::create_directory(config));
    assert(InitializeStandaloneHotkeys(config.wstring()));
    nextState = &sample::State; nextAsync = &sample::Key;
    sample::windows[1].procedure = reinterpret_cast<LONG_PTR>(&sample::Original);
    sample::windows[2].procedure = reinterpret_cast<LONG_PTR>(&sample::Original);
    assert(AttachInputWindow(sample::foreground, &sample::Message));
    ready.store(true); SetInputFrameHealth(true);
    POINT pointer{};
    assert(ReadInputMousePosition(sample::foreground, pointer) && pointer.x == 100 && pointer.y == 200);
    assert(sample::threadDpi == reinterpret_cast<HANDLE>(uintptr_t{3}));
    // 移到左上方显示器，屏幕原点和 DPI 同时改变；客户区仍应精确命中同一按钮。
    sample::windows[1].originX = -1920; sample::windows[1].originY = -1080;
    sample::windows[1].dpi = reinterpret_cast<HANDLE>(uintptr_t{2});
    sample::cursor = {-1720, -680};
    assert(ReadInputMousePosition(sample::foreground, pointer) && pointer.x == 100 && pointer.y == 200);
    assert(sample::threadDpi == reinterpret_cast<HANDLE>(uintptr_t{3}));
    sample::cursorReadable = false; pointer = {10, 20};
    assert(!ReadInputMousePosition(sample::foreground, pointer) && pointer.x == 10 && pointer.y == 20);
    assert(sample::threadDpi == reinterpret_cast<HANDLE>(uintptr_t{3}));
    sample::cursorReadable = true; sample::conversionWorks = false;
    assert(!ReadInputMousePosition(sample::foreground, pointer) && pointer.x == 10 && pointer.y == 20);
    sample::conversionWorks = true;
    PumpInputKeyboard();
    sample::keys[VK_F7] = -32768; PumpInputKeyboard();
    assert(TakeInputActions() == TogglePanel);
    sample::keys[VK_F7] = 0; PumpInputKeyboard();
    // 直接执行正式键盘分发，防止只在页面消费端隐藏旧清单入口。
    sample::keys[VK_F8] = -32768; PumpInputKeyboard();
    assert(TakeInputActions() == 0);
    sample::keys[VK_F8] = 0; PumpInputKeyboard();
    // 新开窗键必须精确匹配裸 F7；已存在的 Ctrl+F8 探索动作不受清单入口
    // 删除影响。逐次释放保证测试真正经过生产按下沿，而非复用按住状态。
    for (const int key : {VK_CONTROL, VK_SHIFT, VK_MENU, VK_LWIN, VK_RWIN}) {
        sample::keys[key] = -32768; sample::keys[VK_F7] = -32768; PumpInputKeyboard();
        assert((TakeInputActions() & TogglePanel) == 0);
        assert(FilteredAsyncKey(VK_F7) == -32768);
        sample::keys[key] = 0; sample::keys[VK_F7] = 0; PumpInputKeyboard();
    }
    sample::keys[VK_CONTROL] = -32768; sample::keys[VK_F8] = -32768; PumpInputKeyboard();
    assert(TakeInputActions() == ToggleTravelUnlock);
    sample::keys[VK_CONTROL] = 0; sample::keys[VK_F8] = 0; PumpInputKeyboard();
    for (const int modifierKey : {0, VK_CONTROL, VK_SHIFT, VK_MENU, VK_LWIN, VK_RWIN}) {
        // F9 暂停及它原先忽略修饰键产生的别名全部停用；不能只屏蔽裸 F9。
        if (modifierKey) sample::keys[modifierKey] = -32768;
        sample::keys[VK_F9] = -32768; PumpInputKeyboard(); assert(TakeInputActions() == 0);
        if (modifierKey) sample::keys[modifierKey] = 0;
        sample::keys[VK_F9] = 0; PumpInputKeyboard();
    }
    // 动态绑定统一精确修饰键：旧的 Shift/Alt 偶然别名不再触发默认业务。
    sample::keys[VK_SHIFT] = -32768; sample::keys[VK_F6] = -32768; PumpInputKeyboard();
    assert(TakeInputActions() == 0);
    sample::keys[VK_SHIFT] = 0; sample::keys[VK_F6] = 0; PumpInputKeyboard();
    sample::keys[VK_CONTROL] = -32768; sample::keys[VK_MENU] = -32768;
    sample::keys[VK_F6] = -32768; PumpInputKeyboard(); assert(TakeInputActions() == 0);
    sample::keys[VK_F6] = 0; PumpInputKeyboard();
    sample::keys[VK_F10] = -32768; PumpInputKeyboard(); assert(TakeInputActions() == 0);
    sample::keys[VK_F10] = 0; sample::keys[VK_CONTROL] = 0; sample::keys[VK_MENU] = 0; PumpInputKeyboard();
    sample::keys[VK_F6] = -32768; PumpInputKeyboard(); assert(TakeInputActions() == ToggleMode);
    sample::keys[VK_F6] = 0; PumpInputKeyboard();
    sample::keys[VK_CONTROL] = -32768; sample::keys[VK_F6] = -32768; PumpInputKeyboard();
    assert(TakeInputActions() == ToggleMapReveal);
    sample::keys[VK_F6] = 0; PumpInputKeyboard();
    sample::keys[VK_F10] = -32768; PumpInputKeyboard(); assert(TakeInputActions() == ToggleRevisit);
    sample::keys[VK_F10] = 0; sample::keys[VK_CONTROL] = 0; PumpInputKeyboard();
    g_panel.store(true); SynchronizeInputPanel();
    assert(InputPanelInteractive() && sky2solo::InputOwner() == 1);
    controller.store(true);
    NotifyInputMousePosition(sample::foreground, {100, 200});
    assert(controller.load()); // 第一份位置只是基线，不能抢走刚使用的手柄身份。
    NotifyInputMousePosition(sample::foreground, {100, 200}); assert(controller.load());
    NotifyInputMousePosition(sample::foreground, {101, 200}); assert(!controller.load());
    controller.store(true); ResetInput();
    NotifyInputMousePosition(sample::foreground, {300, 400}); assert(controller.load());
    NotifyInputMousePosition(sample::foreground, {301, 400}); assert(!controller.load());
    PumpInputKeyboard(); // 开窗后的新输入世代必须先确认所有实体键已释放。
    // 固定底栏不参与黄色内容导航，显示模式绑定在自己的窗口内仍能切换。
    sample::keys[VK_F6] = -32768; PumpInputKeyboard(); assert(TakeInputActions() == ToggleMode);
    sample::keys[VK_F6] = 0; PumpInputKeyboard();
    SetInputModeShortcutEditing(true);
    sample::keys[VK_F6] = -32768; PumpInputKeyboard(); assert(TakeInputActions() == 0);
    SetInputModeShortcutEditing(false);
    PumpInputKeyboard(); assert(TakeInputActions() == 0); // 关掉编辑弹出层时仍按住键，不能补触发。
    sample::keys[VK_F6] = 0; PumpInputKeyboard();
    sample::keys[VK_F6] = -32768; PumpInputKeyboard(); assert(TakeInputActions() == ToggleMode);
    sample::keys[VK_F6] = 0; PumpInputKeyboard();
    sample::keys[VK_CONTROL] = -32768; sample::keys[VK_F6] = -32768; PumpInputKeyboard();
    assert(TakeInputActions() == 0); // 窗口内并未放开地图等其他业务键。
    sample::keys[VK_CONTROL] = 0; sample::keys[VK_F6] = 0; PumpInputKeyboard();
    XINPUT_STATE state{};
    XINPUT_GAMEPAD navigation{};
    sample::pad = {}; FilteredGetState(0, &state);
    sample::pad.wButtons = kView | XINPUT_GAMEPAD_X; FilteredGetState(0, &state);
    assert(TakeInputActions() == ToggleMode && state.Gamepad.wButtons == 0);
    sample::pad = {}; FilteredGetState(0, &state);
    SetInputModeShortcutEditing(true);
    sample::pad.wButtons = kView | XINPUT_GAMEPAD_X; FilteredGetState(0, &state);
    assert(TakeInputActions() == 0);
    SetInputModeShortcutEditing(false);
    FilteredGetState(0, &state); assert(TakeInputActions() == 0);
    sample::pad = {}; FilteredGetState(0, &state);
    sample::pad.wButtons = XINPUT_GAMEPAD_A; FilteredGetState(0, &state);
    assert(state.Gamepad.wButtons == 0 && ReadInputPad(navigation) && navigation.wButtons == XINPUT_GAMEPAD_A);
    assert(TakeInputActions() == 0);
    // 另一合作层包住宝箱 IAT：内层仍返回完整样本，仅最外实际修改输出。
    {
        sky2solo::GamepadCall outside;
        FilteredGetState(0, &state);
        assert(state.Gamepad.wButtons == XINPUT_GAMEPAD_A && outside.CaptureRequested());
        outside.Filter(state.Gamepad); assert(state.Gamepad.wButtons == 0);
    }
    g_panel.store(false); SynchronizeInputPanel();
    FilteredGetState(0, &state); assert(state.Gamepad.wButtons == 0);
    // 通过真实 IAT 过滤路径检查已让出的组合；斜方向不得与其它窗口同时命中。
    for (const WORD button : {WORD{XINPUT_GAMEPAD_A}, WORD{XINPUT_GAMEPAD_B}, WORD{XINPUT_GAMEPAD_DPAD_DOWN},
            WORD{XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_LEFT},
            WORD{XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_A},
            WORD{XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_RIGHT},
            WORD{XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN}}) {
        sample::pad.wButtons = kView | button; FilteredGetState(0, &state);
        assert(TakeInputActions() == 0);
        sample::pad = {}; FilteredGetState(0, &state);
    }
    sample::pad.wButtons = kView | XINPUT_GAMEPAD_X; FilteredGetState(0, &state);
    assert(TakeInputActions() == ToggleMode);
    sample::pad = {}; FilteredGetState(0, &state);
    sample::pad.wButtons = kView | XINPUT_GAMEPAD_RIGHT_THUMB; FilteredGetState(0, &state);
    assert(TakeInputActions() == ToggleEnabled);
    sample::pad = {}; FilteredGetState(0, &state);
    sample::pad = {}; FilteredGetState(0, &state);
    // Windows 保留 VK 0x07 不属于实体键，不得永久阻塞关闭后的松键等待。
    sample::keys[0x07] = -32768; PumpInputKeyboard(); assert(!keyboardTail.load());
    sample::pad.wButtons = XINPUT_GAMEPAD_A; FilteredGetState(0, &state);
    assert(state.Gamepad.wButtons == XINPUT_GAMEPAD_A);
    sample::pad = {}; FilteredGetState(0, &state);
    sample::pad.wButtons = kView; FilteredGetState(0, &state); assert(state.Gamepad.wButtons == 0);
    sample::pad = {}; FilteredGetState(0, &state); assert(state.Gamepad.wButtons == kView);
    FilteredGetState(0, &state); assert(state.Gamepad.wButtons == 0);
    // 两槽镜像在不同 UI 消费时机返回，也只能由组合锁存产生一次开窗动作。
    FilteredGetState(1, &state);
    sample::pad.wButtons = kView | XINPUT_GAMEPAD_DPAD_UP;
    FilteredGetState(0, &state); assert(TakeInputActions() == TogglePanel);
    FilteredGetState(1, &state); assert(TakeInputActions() == 0);
    sample::pad = {}; FilteredGetState(0, &state); FilteredGetState(1, &state);
    sample::pad.wButtons = kView | XINPUT_GAMEPAD_DPAD_UP;
    FilteredGetState(1, &state);
    assert(TakeInputActions() == TogglePanel && navigationDevice.load() == 1);
    sample::pad = {}; FilteredGetState(0, &state); FilteredGetState(1, &state);
    g_panel.store(true); SynchronizeInputPanel(); assert(InputPanelInteractive());
    FilteredGetState(1, &state);
    sample::pad.wButtons = XINPUT_GAMEPAD_DPAD_DOWN; FilteredGetState(1, &state);
    assert(ReadInputPad(navigation) && navigation.wButtons == XINPUT_GAMEPAD_DPAD_DOWN);
    sample::foreground = nullptr; assert(!InputPanelInteractive());
    sample::pad.wButtons = XINPUT_GAMEPAD_A; FilteredGetState(0, &state);
    assert(state.Gamepad.wButtons == XINPUT_GAMEPAD_A && g_panel.load());
    sample::foreground = reinterpret_cast<HWND>(uintptr_t{1}); SynchronizeInputPanel();
    assert(InputPanelInteractive() && g_panel.load());
    FilteredGetState(0, &state); assert(!ReadInputPad(navigation)); // 前台恢复尚未松键。
    sample::pad = {}; FilteredGetState(0, &state);
    sky2solo::InputLease other(2); assert(other.Claim()); SynchronizeInputPanel();
    assert(!g_panel.load() && other.Owns());
    // 其他窗口持有租约时，关闭的宝箱不得用旧快捷键改变业务。
    sample::keys[VK_F6] = -32768; PumpInputKeyboard(); assert(TakeInputActions() == 0);
    other.Release(); sample::keys[VK_F6] = 0; PumpInputKeyboard();
    // 真正提交新配置，输入路径必须立刻废弃旧绑定并等待当前按键全部释放。
    std::string error;
    sample::keys[VK_SHIFT] = -32768; sample::keys[VK_F12] = -32768;
    assert(sky2solo::CommitHotkey(0, {VK_F12, sky2solo::HotkeyShift, XINPUT_GAMEPAD_Y}, error));
    PumpInputKeyboard(); assert(TakeInputActions() == 0);
    sample::keys[VK_SHIFT] = 0; sample::keys[VK_F12] = 0; PumpInputKeyboard();
    sample::keys[VK_F7] = -32768; PumpInputKeyboard(); assert(TakeInputActions() == 0);
    sample::keys[VK_F7] = 0; PumpInputKeyboard();
    sample::keys[VK_SHIFT] = -32768; sample::keys[VK_F12] = -32768; PumpInputKeyboard();
    assert(TakeInputActions() == TogglePanel && FilteredAsyncKey(VK_F12) == 0);
    sample::keys[VK_SHIFT] = 0; sample::keys[VK_F12] = 0; PumpInputKeyboard();
    sample::pad.wButtons = kView | XINPUT_GAMEPAD_Y; FilteredGetState(0, &state);
    assert(TakeInputActions() == 0); // 新世代的第一份按住样本不能激活动作。
    sample::pad = {}; FilteredGetState(0, &state); FilteredGetState(1, &state);
    sample::pad.wButtons = kView | XINPUT_GAMEPAD_DPAD_UP; FilteredGetState(0, &state);
    assert(TakeInputActions() == 0);
    sample::pad = {}; FilteredGetState(0, &state); FilteredGetState(1, &state);
    sample::pad.wButtons = kView | XINPUT_GAMEPAD_Y; FilteredGetState(0, &state);
    assert(TakeInputActions() == TogglePanel);
    FilteredGetState(1, &state); assert(TakeInputActions() == 0);
    sample::pad = {}; FilteredGetState(0, &state); FilteredGetState(1, &state);
    // 已排队但未消费的手柄动作不能穿过配置提交；随后键盘 Pump 应清掉旧世代。
    sample::pad.wButtons = kView | XINPUT_GAMEPAD_X; FilteredGetState(0, &state);
    assert(actions.load() == ToggleMode);
    assert(sky2solo::CommitHotkey(1, {'K', 0, XINPUT_GAMEPAD_X}, error));
    PumpInputKeyboard(); assert(TakeInputActions() == 0);
    sample::pad = {}; FilteredGetState(0, &state); FilteredGetState(1, &state);
    // 普通业务改绑为字母也要屏蔽游戏 IAT；无匹配主键不应全量读取键盘。
    sample::keys['J'] = -32768;
    const auto beforeSample = sample::keyboardSamples;
    assert(FilteredAsyncKey('J') == -32768 && sample::keyboardSamples == beforeSample);
    sample::keys['J'] = 0;
    sample::keys['K'] = -32768; PumpInputKeyboard();
    assert(TakeInputActions() == ToggleMode && FilteredAsyncKey('K') == 0);
    sample::keys['K'] = 0; PumpInputKeyboard();
    sample::keys[VK_SHIFT] = -32768; sample::keys['K'] = -32768; PumpInputKeyboard();
    assert(TakeInputActions() == 0 && FilteredAsyncKey('K') == -32768);
    sample::keys['K'] = 0; sample::keys[VK_SHIFT] = 0; PumpInputKeyboard();
    assert(other.Claim());
    sample::keys['K'] = -32768; PumpInputKeyboard();
    assert(TakeInputActions() == 0 && FilteredAsyncKey('K') == -32768);
    sample::keys['K'] = 0; PumpInputKeyboard(); other.Release();
    g_panel.store(true); SynchronizeInputPanel(); assert(InputPanelInteractive());
    // 字母改绑在自己窗口内生效；文本编辑保护仍阻止它，并在退出后等待真正松开。
    PumpInputKeyboard();
    sample::keys['K'] = -32768; PumpInputKeyboard(); assert(TakeInputActions() == ToggleMode);
    SetInputModeShortcutEditing(true); PumpInputKeyboard(); assert(TakeInputActions() == 0);
    sample::keys['K'] = 0; PumpInputKeyboard();
    sample::keys['K'] = -32768; PumpInputKeyboard(); assert(TakeInputActions() == 0);
    SetInputModeShortcutEditing(false); PumpInputKeyboard(); assert(TakeInputActions() == 0);
    sample::keys['K'] = 0; PumpInputKeyboard();
    // 换 HWND 失败保留旧输入身份和旧链；成功后旧 HWND 必须只透传，不能吞输入。
    const auto firstWindow = sample::foreground;
    const auto secondWindow = reinterpret_cast<HWND>(uintptr_t{2});
    sample::subclassWorks = false;
    assert(!AttachInputWindow(secondWindow, &sample::Message));
    assert(inputWindow.load() == firstWindow && sample::windows[2].next == nullptr);
    sample::subclassWorks = true;
    assert(AttachInputWindow(secondWindow, &sample::Message));
    sample::foreground = secondWindow;
    // 新 HWND 资格判断直接执行正式 helper，拒绝仍在显示的旧主窗、不同进程、
    // 不同窗口类、小型辅助窗、后台窗；仅真正替代主窗的交换链可以接管。
    assert(!sky2window::IsReplacementWindowEligible(firstWindow, secondWindow, L"GameWindow"));
    sample::windows[1].visible = false;
    assert(sky2window::IsReplacementWindowEligible(firstWindow, secondWindow, L"GameWindow"));
    sample::windows[2].owner = GetCurrentProcessId() + 1;
    assert(!sky2window::IsReplacementWindowEligible(firstWindow, secondWindow, L"GameWindow"));
    sample::windows[2].owner = GetCurrentProcessId(); sample::windows[2].className = L"VideoWindow";
    assert(!sky2window::IsReplacementWindowEligible(firstWindow, secondWindow, L"GameWindow"));
    sample::windows[2].className = L"GameWindow"; sample::windows[2].area.right = 64;
    assert(!sky2window::IsReplacementWindowEligible(firstWindow, secondWindow, L"GameWindow"));
    sample::windows[2].area.right = 1280; sample::foreground = firstWindow;
    assert(!sky2window::IsReplacementWindowEligible(firstWindow, secondWindow, L"GameWindow"));
    sample::foreground = secondWindow;
    const auto beforeCallbacks = sample::callbacks;
    assert(ObserveWindow(firstWindow, WM_KEYDOWN, 'Z', 0) == 77);
    assert(sample::callbacks == beforeCallbacks && sample::forwardedOld == 1);
    SynchronizeInputPanel(); assert(InputPanelInteractive());
    controller.store(true);
    NotifyInputMousePosition(secondWindow, {500, 600}); assert(controller.load());
    NotifyInputMousePosition(firstWindow, {501, 600}); assert(controller.load());
    NotifyInputMousePosition(secondWindow, {502, 600}); assert(controller.load()); // 无关旧窗采样使基线失效。
    NotifyInputMousePosition(secondWindow, {503, 600}); assert(!controller.load());
    assert(ObserveWindow(secondWindow, WM_LBUTTONDOWN, 0, 0) == 0);
    assert(sample::callbacks == beforeCallbacks + 1);
    ObserveWindow(secondWindow, WM_LBUTTONUP, 0, 0);
    ObserveWindow(firstWindow, WM_NCDESTROY, 0, 0);
    assert(sample::windows[1].next == nullptr && inputWindow.load() == secondWindow);
    sample::now += 501;
    assert(!InputPanelInteractive() && sky2solo::InputOwner() == 0 && g_panel.load());
    sample::pad.wButtons = XINPUT_GAMEPAD_A; FilteredGetState(0, &state);
    assert(state.Gamepad.wButtons == XINPUT_GAMEPAD_A); // 不可见/卡住渲染不得锁输入。
    for (const auto& entry : fs::directory_iterator(config)) { assert(entry.is_regular_file()); fs::remove(entry.path()); }
    fs::remove(config);
    std::puts("Standalone real input bridge checks passed.");
}
