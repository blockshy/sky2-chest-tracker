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
#include "../native/input_bridge.cpp"
#undef GetTickCount64
#undef GetForegroundWindow
#undef GetAsyncKeyState
#undef ReadHotkeyKeyboardState
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
    inputWindow.store(sample::foreground); ready.store(true); SetInputFrameHealth(true);
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
    XINPUT_STATE state{};
    XINPUT_GAMEPAD navigation{};
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
    sample::now += 501;
    assert(!InputPanelInteractive() && sky2solo::InputOwner() == 0 && g_panel.load());
    sample::pad.wButtons = XINPUT_GAMEPAD_A; FilteredGetState(0, &state);
    assert(state.Gamepad.wButtons == XINPUT_GAMEPAD_A); // 不可见/卡住渲染不得锁输入。
    for (const auto& entry : fs::directory_iterator(config)) { assert(entry.is_regular_file()); fs::remove(entry.path()); }
    fs::remove(config);
    std::puts("Standalone real input bridge checks passed.");
}
