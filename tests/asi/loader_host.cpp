// 无游戏资源的 Windows 宿主：由实际 UAL 加载插件，宿主不替它调用首次 InitializeASI。
// 该程序不会附加游戏进程；所有文件都由外层脚本放入新建的隔离目录。
#include <Windows.h>
#include <Xinput.h>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

// 专用导入库仅携带序号，复制本游戏 ordinal 2/3 的 PE 导入方式。
extern "C" __declspec(dllimport) DWORD WINAPI Sky2HostGetState(DWORD, XINPUT_STATE*);
extern "C" __declspec(dllimport) DWORD WINAPI Sky2HostSetState(DWORD, XINPUT_VIBRATION*);

namespace {
struct Fixture { std::wstring filename; LONG identity; };

bool Fail(const char* message) { std::fprintf(stderr, "FAIL: %s (win32=%lu)\n", message, GetLastError()); return false; }

void DriveDeferredLoader() {
    // UAL 默认延迟加载，由宿主对其已挂钩的 Win32 IAT API 的调用触发。
    // 不依赖虚构的“Loader Initialize”导出，也不跳过官方 Loader 自己加载插件。
    FILETIME timestamp{};
    GetSystemTimeAsFileTime(&timestamp);
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    Sleep(25);
}

bool CheckFixture(const Fixture& fixture) {
    const HMODULE module = GetModuleHandleW(fixture.filename.c_str());
    if (!module) return Fail("fixture was not loaded by UAL");
    using Read = LONG(*)();
    using Init = void(*)();
    const auto calls = reinterpret_cast<Read>(GetProcAddress(module, "Sky2FixtureCalls"));
    const auto initializations = reinterpret_cast<Read>(GetProcAddress(module, "Sky2FixtureInitializations"));
    const auto identity = reinterpret_cast<Read>(GetProcAddress(module, "Sky2FixtureIdentity"));
    const auto initialize = reinterpret_cast<Init>(GetProcAddress(module, "InitializeASI"));
    if (!calls || !initializations || !identity || !initialize) return Fail("fixture exports missing");
    if (identity() != fixture.identity || calls() != 1 || initializations() != 1)
        return Fail("UAL must invoke each fixture InitializeASI exactly once");
    initialize();
    initialize();
    if (calls() != 3 || initializations() != 1) return Fail("duplicate InitializeASI was not idempotent");
    std::printf("PASS: fixture %c, UAL calls=1; after repeated entry calls=3, initializations=1\n",
                static_cast<char>(fixture.identity));
    return true;
}

bool CheckForwarding(HMODULE loader, bool standalone) {
    // 所有 API 都使用越界设备索引；SetState 另外使用全零震动，因此不会影响设备。
    // 不调用输入启停或按键队列，结果与真实设备连接/Steam Input 的瞬时状态无关。
    // 必须使用绝对路径：仅传 basename 时 Windows 会优先复用已加载的同名 UAL 模块，
    // 即使指定 SEARCH_SYSTEM32，也不能把那次调用当成独立的系统参照实现。
    wchar_t systemDirectory[MAX_PATH]{};
    if (!GetSystemDirectoryW(systemDirectory, MAX_PATH)) return Fail("System32 path unavailable");
    const auto systemPath = std::wstring(systemDirectory) + L"\\xinput1_4.dll";
    const HMODULE system = LoadLibraryExW(systemPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!system || system == loader) return Fail("System32 XInput must be a separate module");
    using State = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
    using SetState = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
    using Capabilities = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);
    const auto proxyState = reinterpret_cast<State>(GetProcAddress(loader, "XInputGetState"));
    const auto systemState = reinterpret_cast<State>(GetProcAddress(system, "XInputGetState"));
    const auto proxySetState = reinterpret_cast<SetState>(GetProcAddress(loader, "XInputSetState"));
    const auto systemSetState = reinterpret_cast<SetState>(GetProcAddress(system, "XInputSetState"));
    const auto proxyCapabilities = reinterpret_cast<Capabilities>(GetProcAddress(loader, "XInputGetCapabilities"));
    const auto systemCapabilities = reinterpret_cast<Capabilities>(GetProcAddress(system, "XInputGetCapabilities"));
    if (!proxyState || !systemState || !proxySetState || !systemSetState ||
        (!standalone && (!proxyCapabilities || !systemCapabilities)))
        return Fail("XInput named/ordinal exports missing");
    XINPUT_STATE a{}, b{}, c{};
    XINPUT_CAPABILITIES ca{}, cb{};
    const DWORD expectedState = systemState(XUSER_MAX_COUNT, &a);
    const DWORD actualState = proxyState(XUSER_MAX_COUNT, &b);
    const DWORD ordinalResult = Sky2HostGetState(XUSER_MAX_COUNT, &c);
    XINPUT_VIBRATION vibration{};
    const DWORD expectedSet = systemSetState(XUSER_MAX_COUNT, &vibration);
    const DWORD actualSet = proxySetState(XUSER_MAX_COUNT, &vibration);
    const DWORD ordinalSet = Sky2HostSetState(XUSER_MAX_COUNT, &vibration);
    const DWORD expectedCapabilities = standalone ? 0 : systemCapabilities(XUSER_MAX_COUNT, 0, &ca);
    const DWORD actualCapabilities = standalone ? 0 : proxyCapabilities(XUSER_MAX_COUNT, 0, &cb);
    if (expectedState != actualState || actualState != ordinalResult ||
        expectedCapabilities != actualCapabilities || expectedSet != actualSet || actualSet != ordinalSet)
        return Fail("XInput forwarding result mismatch");
    if (standalone) {
        const auto ordinalGetAddress = GetProcAddress(loader, MAKEINTRESOURCEA(2));
        const auto ordinalSetAddress = GetProcAddress(loader, MAKEINTRESOURCEA(3));
        if (ordinalGetAddress != reinterpret_cast<FARPROC>(proxyState) ||
            ordinalSetAddress != reinterpret_cast<FARPROC>(proxySetState))
            return Fail("standalone named and ordinal exports must be aliases");
    }
    std::printf("PASS: System32 forwarding, GetState/name/IAT-ordinal2=%lu, SetState/name/IAT-ordinal3=%lu, capabilities=%lu\n",
                actualState, actualSet, actualCapabilities);
    FreeLibrary(system);
    return true;
}

bool CheckProbe() {
    const HMODULE module = GetModuleHandleW(L"Sky2CoexistProbe.asi");
    if (!module) return Fail("probe was not loaded by UAL");
    using Read = unsigned(*)();
    using Init = void(*)();
    const auto initializations = reinterpret_cast<Read>(GetProcAddress(module, "Sky2ProbeInitializations"));
    const auto heartbeats = reinterpret_cast<Read>(GetProcAddress(module, "Sky2ProbeHeartbeats"));
    const auto initialize = reinterpret_cast<Init>(GetProcAddress(module, "InitializeASI"));
    if (!initializations || !heartbeats || !initialize) return Fail("probe exports missing");
    const auto deadline = GetTickCount64() + 5000;
    while ((!heartbeats() || !initializations()) && GetTickCount64() < deadline) Sleep(25);
    if (initializations() != 1 || !heartbeats()) return Fail("probe worker never started");
    initialize();
    initialize();
    const auto before = heartbeats();
    const auto heartbeatDeadline = GetTickCount64() + 5000;
    while (heartbeats() == before && GetTickCount64() < heartbeatDeadline) Sleep(25);
    if (initializations() != 1 || heartbeats() <= before) return Fail("probe heartbeat or idempotence failed");
    std::printf("PASS: independent probe heartbeat %u -> %u, initializations=1\n", before, heartbeats());
    return true;
}
}

int wmain(int argc, wchar_t** argv) {
    // 禁止系统错误弹窗阻塞自动化；若加载失败，返回非零并保留场景目录供排查。
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    std::vector<Fixture> fixtures;
    bool chest = false, probe = false, standalone = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg(argv[i]);
        if (arg == L"--chest") chest = true;
        else if (arg == L"--probe") probe = true;
        else if (arg == L"--standalone") standalone = true;
        else if (arg.rfind(L"--fixture=", 0) == 0 && arg.size() > 12 && arg[arg.size() - 2] == L',')
            fixtures.push_back({arg.substr(10, arg.size() - 12), static_cast<LONG>(arg.back())});
        else { Fail("unrecognized host argument"); return 2; }
    }
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    std::wstring root(executable);
    root.resize(root.find_last_of(L"\\/"));
    const HMODULE loader = LoadLibraryW((root + L"\\xinput1_4.dll").c_str());
    if (!loader) { Fail("actual UAL xinput1_4.dll did not load"); return 3; }
    // 首次加载必须由 UAL 完成；等待阶段只驱动 Win32 API 和合法的代理 API。
    const auto deadline = GetTickCount64() + 10000;
    for (;;) {
        DriveDeferredLoader();
        bool ready = true;
        for (const auto& fixture : fixtures) ready = ready && GetModuleHandleW(fixture.filename.c_str());
        if (chest) ready = ready && GetModuleHandleW(L"Sky2ChestTracker.asi");
        if (probe) ready = ready && GetModuleHandleW(L"Sky2CoexistProbe.asi");
        if (ready || GetTickCount64() >= deadline) break;
    }
    if (!CheckForwarding(loader, standalone)) return 4;
    for (const auto& fixture : fixtures) if (!CheckFixture(fixture)) return 5;
    if (chest) {
        const HMODULE module = GetModuleHandleW(L"Sky2ChestTracker.asi");
        if (const HMODULE duplicate = GetModuleHandleW(L"Sky2ChestTrackerDuplicate.asi")) {
            if (module == duplicate) { Fail("duplicate copy unexpectedly shares a module handle"); return 6; }
            std::printf("PASS: two separately mapped chest modules, original=%p duplicate=%p\n", module, duplicate);
        }
        using Init = void(*)();
        const auto init = module ? reinterpret_cast<Init>(GetProcAddress(module, "InitializeASI")) : nullptr;
        if (!init) { Fail("production chest was not loaded or has no InitializeASI"); return 6; }
        init();
        init();
        // 生产入口在工作线程执行 EXE 校验。外层必须核对拒绝日志且无挂钩安装日志。
        // 此处不能只把模块已加载冒称为其游戏功能初始化成功。
        Sleep(500);
        std::puts("PASS: production chest loaded; repeated InitializeASI dispatched; rejection log must be checked externally");
    }
    if (probe && !CheckProbe()) return 7;
    if (standalone) Sleep(500);
    std::puts("PASS: isolated UAL host completed");
    // 插件按进程生命周期固定，不调用 FreeLibrary 模拟不受支持的热卸载。
    return 0;
}
