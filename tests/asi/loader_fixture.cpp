// UAL 集成测试专用最小插件：只统计入口调用，不读取或修改任何游戏对象。
// 同一源码以 A/B 两个身份独立链接，确保测试的确加载了两个不同的 PE 模块。
#include <Windows.h>
#include <cstdio>
#include <iterator>
#include <string>

#ifndef SKY2_FIXTURE_ID
#error SKY2_FIXTURE_ID must be the numeric ASCII value of A or B.
#endif

namespace {
volatile LONG g_calls = 0;
volatile LONG g_initializations = 0;

void RecordOrder() noexcept {
    // 文件仅在隔离宿主目录中创建；每次初始化追加一个身份字母，供外层验证顺序。
    // UAL 当前顺序调用入口，但仍通过 FILE_APPEND_DATA 避免普通 seek/write 覆盖。
    wchar_t path[32768]{};
    if (!GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)))) return;
    std::wstring output(path);
    const auto slash = output.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return;
    output.resize(slash + 1);
    output += L"fixture-order.txt";
    const HANDLE file = CreateFileW(output.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    const char identity[] = {static_cast<char>(SKY2_FIXTURE_ID), '\n'};
    DWORD written = 0;
    WriteFile(file, identity, sizeof(identity), &written, nullptr);
    CloseHandle(file);
}
}

extern "C" __declspec(dllexport) void InitializeASI() noexcept {
    InterlockedIncrement(&g_calls);
    // 重复调用导出入口不重复初始化，模拟生产插件应该遵守的生命周期契约。
    if (InterlockedCompareExchange(&g_initializations, 1, 0) == 0) RecordOrder();
}
extern "C" __declspec(dllexport) LONG Sky2FixtureCalls() noexcept {
    return InterlockedCompareExchange(&g_calls, 0, 0);
}
extern "C" __declspec(dllexport) LONG Sky2FixtureInitializations() noexcept {
    return InterlockedCompareExchange(&g_initializations, 0, 0);
}
extern "C" __declspec(dllexport) LONG Sky2FixtureIdentity() noexcept { return SKY2_FIXTURE_ID; }

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    // 故意不从 DllMain 初始化；宿主必须证明真正由 UAL 调用了 InitializeASI。
    return TRUE;
}
