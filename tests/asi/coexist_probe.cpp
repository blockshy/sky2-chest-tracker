// 独立、只读的第二 ASI 插件。它只记录模块身份、存续心跳与前台 F11 按下边沿。
// 不安装 Present/WndProc/IAT 挂钩，不创建窗口，不读写游戏场景或存档，不轮询 XInput。
// 因而它证明共同装载与独立存续，不能代替两个实际 UI Mod 的输入/绘制冲突测试。
#include <Windows.h>
#include <atomic>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

namespace {
std::atomic<unsigned> g_calls{0};
std::atomic<unsigned> g_initializations{0};
std::atomic<unsigned> g_heartbeats{0};
std::atomic<bool> g_started{false};
std::atomic<bool> g_chestPresentOnEntry{false};
std::wstring g_logPath;

std::wstring ModulePath(HMODULE module) {
    wchar_t path[32768]{};
    const DWORD count = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
    return count && count < std::size(path) ? std::wstring(path, count) : L"";
}

std::string Utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const auto size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string output(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                        output.data(), size, nullptr, nullptr);
    return output;
}

void Log(const std::string& message) noexcept {
    // 日志由唯一工作线程顺序写入；诊断失败不得传播至游戏调用栈。
    FILE* file = nullptr;
    if (_wfopen_s(&file, g_logPath.c_str(), L"ab") != 0 || !file) return;
    std::fprintf(file, "[%llu pid=%lu] %s\n", GetTickCount64(), GetCurrentProcessId(), message.c_str());
    std::fclose(file);
}

std::string VersionIdentity(const std::wstring& path) {
    DWORD unused = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &unused);
    if (!size) return "version-resource=unavailable";
    std::vector<unsigned char> bytes(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, bytes.data())) return "version-resource=unavailable";
    VS_FIXEDFILEINFO* fixed = nullptr;
    UINT length = 0;
    std::string result;
    if (VerQueryValueW(bytes.data(), L"\\", reinterpret_cast<void**>(&fixed), &length) &&
        length >= sizeof(VS_FIXEDFILEINFO) && fixed->dwSignature == 0xFEEF04BD) {
        char version[96]{};
        sprintf_s(version, "version=%u.%u.%u.%u", HIWORD(fixed->dwFileVersionMS),
                  LOWORD(fixed->dwFileVersionMS), HIWORD(fixed->dwFileVersionLS), LOWORD(fixed->dwFileVersionLS));
        result = version;
    }
    struct Translation { WORD language; WORD codepage; };
    Translation* translations = nullptr;
    if (VerQueryValueW(bytes.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &length) &&
        length >= sizeof(Translation)) {
        for (const auto* name : {L"ProductName", L"FileDescription"}) {
            wchar_t key[128]{};
            swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\%s", translations[0].language, translations[0].codepage, name);
            wchar_t* text = nullptr;
            UINT chars = 0;
            if (VerQueryValueW(bytes.data(), key, reinterpret_cast<void**>(&text), &chars) && chars > 1 && text)
                result += " " + Utf8(name) + "=" + Utf8(text);
        }
    }
    return result.empty() ? "version-resource=unavailable" : result;
}

DWORD WINAPI Observe(void*) noexcept {
    try {
        // 心跳线程会一直存续至进程退出，因此固定自身模块；不支持进程内热卸载。
        HMODULE pinned = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                               reinterpret_cast<LPCWSTR>(&Observe), &pinned)) return 0;
        auto root = ModulePath(nullptr);
        const auto slash = root.find_last_of(L"\\/");
        if (slash == std::wstring::npos) return 0;
        root.resize(slash);
        const auto folder = root + L"\\Sky2CoexistProbe";
        if (!CreateDirectoryW(folder.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return 0;
        g_logPath = folder + L"\\probe.log";
        g_initializations.fetch_add(1);
        Log("InitializeASI worker started; observer-only; no game hooks or save access.");
        Log(g_chestPresentOnEntry.load() ? "chest-at-first-entry=present" : "chest-at-first-entry=absent");
        Log("self=" + Utf8(ModulePath(pinned)));
        const HMODULE loader = GetModuleHandleW(L"xinput1_4.dll");
        Log(loader ? "xinput-module=" + Utf8(ModulePath(loader)) + " " + VersionIdentity(ModulePath(loader)) :
                     "xinput-module=absent");
        HMODULE lastChest = reinterpret_cast<HMODULE>(~static_cast<ULONG_PTR>(0));
        DWORD lastForegroundPid = MAXDWORD;
        ULONGLONG nextHeartbeat = 0;
        bool previousF11 = false;
        for (;;) {
            const HMODULE chest = GetModuleHandleW(L"Sky2ChestTracker.asi");
            if (chest != lastChest) {
                Log(chest ? "chest=loaded path=" + Utf8(ModulePath(chest)) : "chest=absent");
                lastChest = chest;
            }
            DWORD foregroundPid = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
            if (foregroundPid != lastForegroundPid) {
                Log("foreground-pid=" + std::to_string(foregroundPid));
                lastForegroundPid = foregroundPid;
            }
            // 仅查看当前物理按下高位；不使用全局“自上次查询以来按过”的低位，不消费按键。
            const bool f11 = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
            if (foregroundPid == GetCurrentProcessId() && f11 && !previousF11)
                Log("F11 observed; key was not intercepted or consumed.");
            previousF11 = f11;
            const auto now = GetTickCount64();
            if (now >= nextHeartbeat) {
                const auto heartbeat = g_heartbeats.fetch_add(1) + 1;
                Log("heartbeat=" + std::to_string(heartbeat) + " init-calls=" + std::to_string(g_calls.load()));
                nextHeartbeat = now + 2000;
            }
            Sleep(25);
        }
    } catch (...) { Log("Observer stopped after a contained diagnostic exception."); }
    return 0;
}
}

extern "C" __declspec(dllexport) void InitializeASI() noexcept {
    g_calls.fetch_add(1);
    if (g_started.exchange(true)) return;
    // 在入口调用当下记录，避免异步工作线程的调度先后被误认为插件装载先后。
    g_chestPresentOnEntry.store(GetModuleHandleW(L"Sky2ChestTracker.asi") != nullptr);
    // UAL 入口立即返回，不等待日志线程；重试入口只在创建线程确实失败时允许。
    if (const HANDLE thread = CreateThread(nullptr, 0, Observe, nullptr, 0, nullptr)) CloseHandle(thread);
    else g_started.store(false);
}
extern "C" __declspec(dllexport) unsigned Sky2ProbeInitializations() noexcept { return g_initializations.load(); }
extern "C" __declspec(dllexport) unsigned Sky2ProbeHeartbeats() noexcept { return g_heartbeats.load(); }

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    return TRUE;
}
