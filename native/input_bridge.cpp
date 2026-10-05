// 游戏 IAT 合作链保留 Steam 映射样本：各层先观察，只有最外层最终执行捕获。
// 本模块只在健康前台窗口拥有输入时接管键鼠；后台保留页面，但不继续吞游戏输入。
#include "input_bridge.h"
#include "standalone_input_policy.h"
#include "standalone_hotkeys.h"
#include "standalone_ui/input.h"
#include "tracker.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <iterator>

namespace tracker {
namespace {
using GetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
using AsyncKeyFn = SHORT(WINAPI*)(int);
GetStateFn nextState = nullptr;
AsyncKeyFn nextAsync = nullptr;
SRWLOCK padLock = SRWLOCK_INIT;
StandalonePadPolicy filters[4];
XINPUT_GAMEPAD rawPads[4]{}, outputs[4]{};
DWORD packets[4]{};
uint64_t padTimes[4]{}, padGeneration[4]{};
bool navigationReady[4]{};
WORD previousButtons[4]{};
bool shortcutsArmed[4]{};
std::atomic<uint32_t> actions{0}, connected{0}, modifier{0};
std::atomic<uint32_t> actionLatches{0};
std::atomic<bool> controller{false}, ready{false}, keyboardTail{false}, resetPending{true};
std::atomic<bool> availableBefore{false}, suspendedOwnership{false};
std::atomic<uint64_t> lastFrame{0}, generation{1};
std::atomic<uint64_t> bindingRevision{0};
std::atomic<int> navigationDevice{-1};
std::atomic<HWND> inputWindow{nullptr};
std::atomic<InputWindowMessage> windowCallback{nullptr};
std::atomic<uint32_t> panelMouseButtons{0};
std::atomic<bool> modeShortcutEditing{false};
// 原窗口过程挂在其 HWND 上，不能只保存一份全局链尾：游戏重新创建窗口时，
// 旧窗口仍可能存在并被另一个 ASI 包在外层，贸然还原其过程会破坏合作链。
constexpr wchar_t previousWindowProperty[] = L"Sky2ChestTracker.InputNext.v1";
sky2solo::InputLease lease(1); // 宝箱稳定身份；与其它独立 Mod 的非零令牌保持不同。
bool previousPanel = false;
sky2solo::HotkeyKeyboardTracker keyboard;
uint64_t keyboardGeneration = 0;

bool Foreground() noexcept { const auto window = inputWindow.load(); return window && GetForegroundWindow() == window; }
void ResetInput() noexcept {
    actions.store(0); actionLatches.store(0); keyboardTail.store(false); resetPending.store(true); generation.fetch_add(1);
}
bool Available() noexcept {
    const bool current = Foreground() && sky2solo::FrameHealthy(lastFrame.load(), GetTickCount64());
    const bool previous = availableBefore.exchange(current);
    if (current != previous) {
        if (!current && lease.Owns()) { suspendedOwnership.store(true); lease.Release(); }
        ResetInput();
    }
    return current;
}
PadSample Sample(const XINPUT_GAMEPAD& pad) noexcept {
    return {pad.wButtons, pad.bLeftTrigger, pad.bRightTrigger, pad.sThumbLX, pad.sThumbLY, pad.sThumbRX, pad.sThumbRY};
}
sky2solo::HotkeySnapshot Bindings() noexcept {
    auto snapshot = sky2solo::ReadHotkeys();
    // 提交配置后废弃所有旧输入世代。手柄与键盘各自等待本世代完全释放，
    // 防止保存按钮或正在按住的新组合直接变成业务操作。
    // IAT 与渲染线程可能交错读取快照。版本只允许前进；拿到过期快照的线程
    // 重新读取，不能把已提交的新版本写回旧值并重复清空另一线程的输入。
    auto observed = bindingRevision.load();
    for (;;) {
        if (snapshot.revision < observed) { snapshot = sky2solo::ReadHotkeys(); continue; }
        if (snapshot.revision == observed) break;
        if (bindingRevision.compare_exchange_weak(observed, snapshot.revision)) { ResetInput(); break; }
    }
    return snapshot;
}
bool KeyboardCaptured() noexcept {
    if (!Available()) return false;
    const auto owner = sky2solo::InputOwner();
    if (owner && !lease.Owns()) return false;
    return (g_panel.load() && lease.Owns()) || keyboardTail.load();
}
DWORD WINAPI FilteredGetState(DWORD index, XINPUT_STATE* state) noexcept {
    sky2solo::GamepadCall call;
    const auto error = nextState(index, state);
    if (index >= 4 || !state) return error;
    const bool available = Available();
    const auto bindings = Bindings();
    const bool owns = lease.Owns();
    AcquireSRWLockExclusive(&padLock);
    if (error != ERROR_SUCCESS) {
        filters[index].Reset(); rawPads[index] = {}; padTimes[index] = 0; navigationReady[index] = false;
        shortcutsArmed[index] = false; previousButtons[index] = 0;
        modifier.fetch_and(~(1u << index)); connected.fetch_and(~(1u << index));
        int lost = static_cast<int>(index); navigationDevice.compare_exchange_strong(lost, -1);
        if (!connected.load()) controller.store(false);
    } else {
        connected.fetch_or(1u << index);
        const auto currentGeneration = generation.load();
        if (padGeneration[index] != currentGeneration) {
            filters[index].Reset(); navigationReady[index] = false; padGeneration[index] = currentGeneration;
            shortcutsArmed[index] = false; previousButtons[index] = 0;
        }
        rawPads[index] = state->Gamepad; padTimes[index] = GetTickCount64();
        int absent = -1; navigationDevice.compare_exchange_strong(absent, static_cast<int>(index));
        const auto raw = Sample(state->Gamepad);
        uint32_t requested = 0;
        if (!available) shortcutsArmed[index] = false;
        else if (!shortcutsArmed[index]) shortcutsArmed[index] = StandaloneNeutral(raw);
        else requested = StandaloneInputActions(sky2solo::HotkeyPadPressedMask(bindings, raw.buttons, previousButtons[index]));
        previousButtons[index] = raw.buttons;
        const auto result = filters[index].Update(raw, g_panel.load() && owns, available,
            sky2solo::InputOwner() != 0 && !owns, requested, InputModeShortcutAllowed());
        navigationReady[index] = result.navigate;
        if (raw.buttons & kView) modifier.fetch_or(1u << index); else modifier.fetch_and(~(1u << index));
        // 允许连接但闲置的槽 0 让位给真实使用的槽 1。完全相同的镜像样本
        // 不反复更换导航来源；动作按组合跨槽锁存，直至各新鲜样本均释放。
        const int selected = navigationDevice.load();
        if (available && result.activity && !StandaloneNeutral(raw) &&
            (selected < 0 || selected == static_cast<int>(index) ||
             std::memcmp(&rawPads[selected], &rawPads[index], sizeof(XINPUT_GAMEPAD)) != 0))
            navigationDevice.store(static_cast<int>(index));
        uint32_t held = 0;
        for (unsigned slot = 0; slot < 4; ++slot)
            if (padGeneration[slot] == currentGeneration && padTimes[slot] &&
                GetTickCount64() >= padTimes[slot] && GetTickCount64() - padTimes[slot] <= 250)
                held |= StandaloneInputActions(sky2solo::HotkeyPadHeldMask(bindings, rawPads[slot].wButtons));
        actionLatches.fetch_and(held);
        if (available) {
            if (result.activity) controller.store(true);
            const uint32_t panelActions = TogglePanel | (InputModeShortcutAllowed() ? ToggleMode : 0u);
            const auto allowed = sky2solo::InputOwner() ? (result.actions & panelActions) : result.actions;
            const auto already = actionLatches.fetch_or(allowed);
            actions.fetch_or(allowed & ~already);
        }
        // 合作调用的内层不改样本；原始 View 单按补发也通过同一聚合协议传到
        // 最外层。捕获优先于补发，关闭窗口的按键尾巴不会再次弹出原生地图。
        call.RequestCapture(result.capture);
        call.RequestViewReplay(result.replayView);
        call.Filter(state->Gamepad);
        if (call.Outermost()) {
            if (std::memcmp(&outputs[index], &state->Gamepad, sizeof(state->Gamepad))) { outputs[index] = state->Gamepad; ++packets[index]; }
            state->dwPacketNumber = packets[index];
        }
    }
    ReleaseSRWLockExclusive(&padLock);
    return error;
}
SHORT WINAPI FilteredAsyncKey(int key) noexcept {
    const auto value = nextAsync(key);
    if (KeyboardCaptured()) return 0;
    // 六项动作均可改成普通字母/数字，匹配的业务主键也必须从游戏轮询中
    // 隔离。先筛选可能命中的主键，再读取一次实体快照，避免逐键全量扫描。
    // 窗口入口可切换所有权；普通业务仅在没有合作窗口时参与拦截。
    const auto bindings = sky2solo::ReadHotkeys();
    if (!Available()) return value;
    const bool business = !g_panel.load() && !sky2solo::InputOwner();
    uint32_t candidates = 0;
    for (size_t index = 0; index < std::min(bindings.count, sky2solo::MaxHotkeys); ++index)
        if ((index == 0 || business) && key != 0 && key == bindings.bindings[index].key) candidates |= 1u << index;
    if (candidates) {
        const auto physical = sky2solo::ReadHotkeyKeyboardState();
        for (size_t index = 0; index < std::min(bindings.count, sky2solo::MaxHotkeys); ++index)
            if ((candidates & (1u << index)) && sky2solo::HotkeyKeyHeld(bindings.bindings[index], physical)) return 0;
    }
    return value;
}
bool InputMessage(UINT message) noexcept {
    return (message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
        (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) || message == WM_INPUT;
}
uint32_t MouseButton(UINT message, WPARAM wparam, bool down) noexcept {
    if (message == UINT(down ? WM_LBUTTONDOWN : WM_LBUTTONUP) || (down && message == WM_LBUTTONDBLCLK)) return 1;
    if (message == UINT(down ? WM_RBUTTONDOWN : WM_RBUTTONUP) || (down && message == WM_RBUTTONDBLCLK)) return 2;
    if (message == UINT(down ? WM_MBUTTONDOWN : WM_MBUTTONUP) || (down && message == WM_MBUTTONDBLCLK)) return 4;
    if (message == UINT(down ? WM_XBUTTONDOWN : WM_XBUTTONUP) || (down && message == WM_XBUTTONDBLCLK))
        return HIWORD(wparam) == XBUTTON1 ? 8 : 16;
    return 0;
}
LRESULT CALLBACK ObserveWindow(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    const auto next = reinterpret_cast<WNDPROC>(GetPropW(window, previousWindowProperty));
    const auto forward = [&]() {
        const auto result = next ? CallWindowProcW(next, window, message, wparam, lparam) : DefWindowProcW(window, message, wparam, lparam);
        // HWND 销毁后该句柄值可能被系统复用；最后一条消息结束后删除旧链记录。
        if (message == WM_NCDESTROY) RemovePropW(window, previousWindowProperty);
        return result;
    };
    if (window != inputWindow.load()) return forward();
    const bool available = Available();
    bool keyboardMouse = ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && !(lparam & (1LL << 30))) ||
        message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN ||
        message == WM_XBUTTONDOWN || message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL || message == WM_MOUSEMOVE;
    if (available && message == WM_INPUT) {
        // 游戏已注册 Raw Input 时读取其真实鼠标增量，不另行注册设备或读取
        // 另一套系统输入。这样连接着手柄时移动鼠标仍能恢复鼠标提示与光标。
        RAWINPUT raw{}; UINT size = sizeof(raw);
        const auto read = GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER));
        if (read != UINT(-1) && read >= sizeof(RAWINPUTHEADER) && raw.header.dwType == RIM_TYPEMOUSE)
            keyboardMouse |= raw.data.mouse.lLastX != 0 || raw.data.mouse.lLastY != 0 || raw.data.mouse.usButtonFlags != 0;
    }
    if (available && keyboardMouse) controller.store(false);
    if (message == WM_KILLFOCUS) {
        if (lease.Owns()) { suspendedOwnership.store(true); lease.Release(); }
        availableBefore.store(false); ResetInput(); controller.store(false);
        ReleaseInputMouseButtons();
    }
    const auto released = MouseButton(message, wparam, false);
    const bool paired = (panelMouseButtons.load() & released) != 0;
    if (auto callback = windowCallback.load(); callback &&
        ((available && g_panel.load() && lease.Owns()) || message == WM_KILLFOCUS || paired)) {
        panelMouseButtons.fetch_or(MouseButton(message, wparam, true)); panelMouseButtons.fetch_and(~released);
        callback(window, message, wparam, lparam);
    }
    if (paired) return 0;
    if (InputMessage(message) && KeyboardCaptured()) return message == WM_INPUT ? DefWindowProcW(window, message, wparam, lparam) : 0;
    if (message == WM_NCDESTROY) {
        SetInputFrameHealth(false);
        inputWindow.store(nullptr);
        panelMouseButtons.store(0);
    }
    return forward();
}
bool ReplaceSlot(void** slot, void* replacement, void* previous) noexcept {
    DWORD protection = 0;
    if (!previous || !VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection)) return false;
    const bool installed = InterlockedCompareExchangePointer(slot, replacement, previous) == previous;
    DWORD ignored = 0; VirtualProtect(slot, sizeof(void*), protection, &ignored);
    return installed;
}
}

bool InstallInputBridge(uintptr_t base) noexcept {
    const unsigned char expected[]{0xFF, 0x15, 0x3A, 0x61, 0x21, 0x00};
    if (!base || std::memcmp(reinterpret_cast<void*>(base + 0x6A55A8), expected, sizeof(expected))) return false;
    auto slot = reinterpret_cast<void**>(base + 0x8BB6E8);
    bool installed = false;
    // 多个 ASI 的初始化线程可能同时接入。CAS 失败后重新读取真实链尾，
    // 仅在自己的入口尚未发布时重试，不覆盖后来者，也不形成自调用环。
    for (unsigned attempt = 0; attempt < 16 && !installed; ++attempt) {
        nextState = reinterpret_cast<GetStateFn>(*slot);
        installed = nextState && ReplaceSlot(slot, reinterpret_cast<void*>(&FilteredGetState), reinterpret_cast<void*>(nextState));
    }
    if (!installed) return false;
    auto keys = reinterpret_cast<void**>(base + 0x8BB5D8);
    installed = false;
    for (unsigned attempt = 0; attempt < 16 && !installed; ++attempt) {
        nextAsync = reinterpret_cast<AsyncKeyFn>(*keys);
        installed = nextAsync && ReplaceSlot(keys, reinterpret_cast<void*>(&FilteredAsyncKey), reinterpret_cast<void*>(nextAsync));
    }
    if (!installed) { ReplaceSlot(slot, reinterpret_cast<void*>(nextState), reinterpret_cast<void*>(&FilteredGetState)); return false; }
    ready.store(true);
    Log("Standalone panel input installed: cooperative game XInput and keyboard IAT chain.");
    return true;
}
bool ReadInputMousePosition(HWND window, POINT& point) noexcept {
    return sky2window::ReadMousePosition(window, point);
}
void NotifyInputMousePosition(HWND window, const POINT& point) noexcept {
    // 这些基线只由持有渲染锁的 Present 线程维护；输入线程通过既有原子世代
    // 宣布失焦/重绑，不会读写 POINT，避免窗口回调和渲染回调之间的数据竞争。
    static HWND sampledWindow = nullptr;
    static uint64_t sampledGeneration = 0;
    static POINT previous{};
    static bool valid = false;
    if (window != inputWindow.load() || !InputPanelInteractive()) { valid = false; return; }
    const auto currentGeneration = generation.load();
    if (valid && sampledWindow == window && sampledGeneration == currentGeneration &&
        (previous.x != point.x || previous.y != point.y)) controller.store(false);
    previous = point; sampledWindow = window; sampledGeneration = currentGeneration; valid = true;
}
void ReleaseInputMouseButtons() noexcept {
    const auto held = panelMouseButtons.exchange(0);
    const auto window = inputWindow.load();
    if (auto callback = windowCallback.load(); callback && window && held) {
        // 实际松开可能发生在别的窗口，或发生在新后端建立之后。这里只补齐本
        // 后端曾接收的按钮，不能发送游戏自己的隐藏期 mouse-up 或直接释放别人的捕获。
        const UINT releases[]{WM_LBUTTONUP, WM_RBUTTONUP, WM_MBUTTONUP, WM_XBUTTONUP, WM_XBUTTONUP};
        for (unsigned index = 0; index < 5; ++index) if (held & (1u << index))
            callback(window, releases[index], index < 3 ? 0 : MAKEWPARAM(0, index == 3 ? XBUTTON1 : XBUTTON2), 0);
    }
}
bool AttachInputWindow(HWND window, InputWindowMessage callback) noexcept {
    if (!window || !IsWindow(window)) return false;
    if (GetPropW(window, previousWindowProperty)) {
        windowCallback.store(callback);
        if (inputWindow.exchange(window) != window) ResetInput();
        return true;
    }
    const auto previous = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC));
    if (!previous || !SetPropW(window, previousWindowProperty, reinterpret_cast<HANDLE>(previous))) return false;
    SetLastError(0);
    const auto installed = SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&ObserveWindow));
    if (!installed && GetLastError()) {
        RemovePropW(window, previousWindowProperty); Log("Panel window input unavailable."); return false;
    }
    if (installed) SetPropW(window, previousWindowProperty, reinterpret_cast<HANDLE>(installed));
    windowCallback.store(callback);
    inputWindow.store(window);
    panelMouseButtons.store(0);
    ResetInput();
    return true;
}
void SetInputModeShortcutEditing(bool editing) noexcept {
    if (modeShortcutEditing.exchange(editing) == editing) return;
    // 进入或离开编辑都清除按下沿并等待释放，按住候选键离开弹窗不能补发模式切换。
    ResetInput();
}
bool InputModeShortcutAllowed() noexcept {
    return !modeShortcutEditing.load() && Available() && g_panel.load() && lease.Owns();
}
void PumpInputKeyboard() noexcept {
    const bool available = Available();
    const auto bindings = Bindings();
    const auto currentGeneration = generation.load();
    if (keyboardGeneration != currentGeneration) { keyboard.Reset(); keyboardGeneration = currentGeneration; }
    const auto physical = sky2solo::ReadHotkeyKeyboardState();
    const auto matched = keyboard.Update(bindings, physical, available);
    auto value = StandaloneInputActions(matched);
    if (g_panel.load() || sky2solo::InputOwner())
        value &= TogglePanel | (InputModeShortcutAllowed() ? ToggleMode : 0u);
    if (value) { controller.store(false); actions.fetch_or(value); }
    if (keyboardTail.load()) {
        if (!sky2solo::AnyHotkeyKeyboardDown(physical) || (sky2solo::InputOwner() && !lease.Owns())) keyboardTail.store(false);
    }
}
void SynchronizeInputPanel() noexcept {
    const bool available = Available();
    bool open = g_panel.load();
    if (open && !previousPanel) {
        if (!available || !ready.load() || !lease.Claim()) { g_panel.store(false); open = false; }
        else { suspendedOwnership.store(false); ResetInput(); }
    } else if (open && available && !lease.Owns()) {
        // 前台恢复只在没有其他窗口接管时续接自己的租约；如果用户已打开
        // 另一个 Mod，旧窗口关闭但保留其页面与位置，不能下一帧又抢回输入。
        if (suspendedOwnership.exchange(false) && !sky2solo::InputOwner()) lease.Claim();
        else { g_panel.store(false); open = false; resetPending.store(true); }
    }
    if (!open && previousPanel) { lease.Release(); suspendedOwnership.store(false); keyboardTail.store(available); resetPending.store(true); }
    previousPanel = open;
}
void SetInputFrameHealth(bool healthy) noexcept { lastFrame.store(healthy ? GetTickCount64() : 0); if (!healthy) Available(); }
bool InputPanelInteractive() noexcept { return Available() && g_panel.load() && lease.Owns(); }
bool ConsumeInputReset() noexcept { return resetPending.exchange(false); }
bool ReadInputPad(XINPUT_GAMEPAD& output) noexcept {
    output = {}; bool fresh = false;
    AcquireSRWLockShared(&padLock);
    const int index = navigationDevice.load();
    if (index >= 0 && index < 4 && navigationReady[index] && padGeneration[index] == generation.load() &&
        GetTickCount64() >= padTimes[index] && GetTickCount64() - padTimes[index] <= 250) {
        output = rawPads[index]; fresh = true;
    }
    ReleaseSRWLockShared(&padLock);
    return fresh;
}
uint32_t TakeInputActions() noexcept { return actions.exchange(0); }
bool UsingController() noexcept { return controller.load(); }
bool InputBridgeReady() noexcept { return ready.load(); }
bool ControllerModifierHeld() noexcept { return modifier.load() != 0; }
}
