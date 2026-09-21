// 仅转发本游戏导入的 XInput ordinal 2 和 3；手柄输入保持系统 API 的原始语义。
#include "tracker.h"
#include <Xinput.h>
#include <mutex>

static HMODULE SystemXInput() noexcept {
    static HMODULE module = nullptr;
    static std::once_flag once;
    try { std::call_once(once, [] {
        // 强制从 System32 加载，避免再次加载游戏目录内的代理 DLL。
        module = LoadLibraryExW(L"xinput1_4.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }); } catch (...) {}
    return module;
}

extern "C" DWORD WINAPI TrackerGetState(DWORD index, XINPUT_STATE* state) {
    tracker::Start();
    using Fn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
    static Fn fn = reinterpret_cast<Fn>(GetProcAddress(SystemXInput(), "XInputGetState"));
    return fn ? fn(index, state) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" DWORD WINAPI TrackerSetState(DWORD index, XINPUT_VIBRATION* vibration) {
    tracker::Start();
    using Fn = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
    static Fn fn = reinterpret_cast<Fn>(GetProcAddress(SystemXInput(), "XInputSetState"));
    return fn ? fn(index, vibration) : ERROR_DEVICE_NOT_CONNECTED;
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        tracker::g_module = module;
        DisableThreadLibraryCalls(module);
        // Steam Input 可能完全接管 XInput 调用；不能依赖首次 GetState 来启动。
        // Windows 在 DLL 初始化完成后才运行新线程；此处不等待它，避免加载锁死锁。
        tracker::Start();
    }
    return TRUE;
}
