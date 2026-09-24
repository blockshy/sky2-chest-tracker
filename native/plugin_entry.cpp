// ASI 专用入口：公共 Loader 负责转发系统 XInput，本插件只启动宝箱功能。
// InitializeASI 在 LoadLibrary 完成后由 UAL 调用；DllMain 不挂钩、不创建图形设备。
#include "tracker.h"

extern "C" __declspec(dllexport) void InitializeASI() noexcept {
    // Start 内部使用 once_flag；重复枚举或重复调用入口不会再次安装挂钩。
    tracker::Start(true);
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        tracker::g_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
