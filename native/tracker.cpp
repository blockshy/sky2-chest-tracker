// 《空之轨迹 the 2nd》宝箱地图插件。
// 宝箱模块读取原生标志；可选探索模块独立控制，不修改物品、奖励、成就或保存数据。
#include "tracker.h"
#include "input_bridge.h"
#include "exploration.h"
#include "chest_catalog.h"
#include <bcrypt.h>
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <climits>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <vector>

namespace tracker {
HMODULE g_module = nullptr;
std::atomic<bool> g_enabled{true};
std::atomic<bool> g_panel{true};
std::atomic<Mode> g_mode{Mode::Current};
static uintptr_t g_base = 0;
static std::atomic<unsigned> g_lastRow{UINT_MAX};
static std::atomic<ULONGLONG> g_lastIconTick{0};
static std::wstring g_folder;
using GetIconFn = uint32_t(__fastcall*)(void*);
static GetIconFn g_originalIcon = nullptr;
using MapIconFn = uint32_t(__fastcall*)(void*, const void*);
static MapIconFn g_originalMapIcon = nullptr;

// SEH 仅包围最小的原始读取，切图时对象释放或地址失效会返回 false。
// 函数内没有需要析构的 C++ 对象，避免 SEH 与 C++ 栈展开冲突。
static bool ReadBytes(uintptr_t address, void* output, size_t size) noexcept {
    if (address < 0x10000) return false;
    __try { std::memcpy(output, reinterpret_cast<const void*>(address), size); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T> static bool Read(uintptr_t address, T& value) noexcept {
    return ReadBytes(address, &value, sizeof(value));
}

void Log(const char* message) noexcept {
    // 日志只写入 Mod 自己的目录；错误不可传播到游戏的调用栈。
    try {
        static std::mutex lock;
        std::lock_guard<std::mutex> guard(lock);
        FILE* file = nullptr;
        if (_wfopen_s(&file, (g_folder + L"\\tracker.log").c_str(), L"a") == 0 && file) {
            std::fprintf(file, "[%llu] %s\n", GetTickCount64(), message);
            std::fclose(file);
        }
    } catch (...) {}
}

static bool Snapshot(std::array<uint8_t, 4096>& flags) noexcept {
    uintptr_t manager = 0;
    // 此全局位置由当前 EXE 中的 RIP 引用得出，使用前已校验完整文件 SHA-256。
    return Read(g_base + 0xC60E58, manager) && ReadBytes(manager + 0x100, flags.data(), flags.size());
}

// 区域地图以 t_tbox.tbl 行指针生成图标，而不是逐帧调用场景宝箱的虚函数。
// 依据原生表管理器的合法范围还原行号，避免依赖对象是否已加载或是否靠近玩家。
static const ChestRecord* RecordFromTable(uintptr_t rowPointer) noexcept {
    uintptr_t tables = 0, holder = 0, file = 0, buffer = 0, headers = 0;
    uint32_t headerIndex = 0, offset = 0, stride = 0, count = 0;
    if (!Read(g_base + 0xC5D778, tables) || !Read(tables + 0x108, holder) ||
        !Read(holder + 8, file) || !Read(file + 0x10, buffer) || !Read(file + 0x20, headers) ||
        !Read(file + 0x28, headerIndex) || headerIndex > 4096) return nullptr;
    const auto header = headers + static_cast<uintptr_t>(headerIndex) * 80;
    if (!Read(header + 0x44, offset) || !Read(header + 0x48, stride) || !Read(header + 0x4C, count) ||
        stride != 120 || count != 571) return nullptr;
    const auto start = buffer + offset;
    if (rowPointer < start || rowPointer - start >= static_cast<uintptr_t>(stride) * count ||
        (rowPointer - start) % stride != 0) return nullptr;
    const auto row = static_cast<uint32_t>((rowPointer - start) / stride);
    for (const auto& chest : kChests) if (chest.row == row) return &chest;
    return nullptr;
}

static uint32_t __fastcall SelectMapIcon(void* manager, const void* tableRow) noexcept {
    if (!g_enabled.load(std::memory_order_relaxed)) return g_originalMapIcon(manager, tableRow);
    const auto* chest = RecordFromTable(reinterpret_cast<uintptr_t>(tableRow));
    if (!chest) return g_originalMapIcon(manager, tableRow);
    uintptr_t flags = 0;
    uint8_t currentByte = 0, inheritedByte = 0;
    if (!Read(g_base + 0xC60E58, flags) ||
        !Read(flags + 0x100 + chest->opened / 8, currentByte)) return g_originalMapIcon(manager, tableRow);
    const bool current = (currentByte & (1u << (chest->opened % 8))) != 0;
    bool inherited = false;
    if (chest->inherited && Read(flags + 0x100 + chest->inherited / 8, inheritedByte))
        inherited = (inheritedByte & (1u << (chest->inherited % 8))) != 0;
    g_lastRow.store(chest->row, std::memory_order_relaxed);
    g_lastIconTick.store(GetTickCount64(), std::memory_order_relaxed);
    static std::atomic<bool> reported{false};
    if (!reported.exchange(true)) Log("Regional map chest callback verified.");
    return Icon(g_mode.load(std::memory_order_relaxed), current, inherited);
}

static uint32_t __fastcall SelectIcon(void* behavior) noexcept {
    if (!g_enabled.load(std::memory_order_relaxed)) return g_originalIcon(behavior);
    uintptr_t owner = 0;
    uint32_t actorFlags = 0, openedId = 0, inheritedId = 0;
    const auto self = reinterpret_cast<uintptr_t>(behavior);
    if (!Read(self + 8, owner) || !Read(owner + 0x2E0, actorFlags) || !Read(self + 0x98, openedId))
        return g_originalIcon(behavior);
    // 保留剧情隐藏状态；显示全宝箱不意味着让暂未出现的场景对象参与游戏。
    if (actorFlags & 0x800) return 0;
    const ChestRecord* chest = nullptr;
    for (const auto& item : kChests) if (item.opened == openedId) { chest = &item; break; }
    if (!chest || !Read(self + 0xA0, inheritedId)) return g_originalIcon(behavior);
    uintptr_t manager = 0;
    uint8_t currentByte = 0, inheritedByte = 0;
    if (!Read(g_base + 0xC60E58, manager) || openedId >= 32768 || !Read(manager + 0x100 + openedId / 8, currentByte))
        return g_originalIcon(behavior);
    const bool opened = (currentByte & (1u << (openedId % 8))) != 0;
    bool inherited = false;
    if (inheritedId != 0 && inheritedId < 32768 && Read(manager + 0x100 + inheritedId / 8, inheritedByte))
        inherited = (inheritedByte & (1u << (inheritedId % 8))) != 0;
    g_lastRow.store(chest->row, std::memory_order_relaxed);
    g_lastIconTick.store(GetTickCount64(), std::memory_order_relaxed);
    return Icon(g_mode.load(std::memory_order_relaxed), opened, inherited);
}

Counts ReadCounts() {
    Counts result;
    std::array<uint8_t, 4096> flags{};
    if (!Snapshot(flags)) return result;
    result.valid = true;
    const auto row = g_lastRow.load(std::memory_order_relaxed);
    if (GetTickCount64() - g_lastIconTick.load(std::memory_order_relaxed) < 2000) {
        for (const auto& chest : kChests) if (chest.row == row) { result.map = chest.map; break; }
    }
    result.maps = CountMaps(flags.data(), flags.size());
    for (const auto& group : result.maps) {
        result.current += group.current;
        result.inherited += group.inherited;
        if (!result.map.empty() && result.map == group.definition->scene) {
            result.map_total += group.total;
            result.map_current += group.current;
            result.map_inherited += group.inherited;
        }
    }
    return result;
}

static bool CheckExecutable() {
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return false;
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return false;
    std::array<unsigned char, 32> digest{};
    const auto status = BCryptHash(algorithm, nullptr, 0, bytes.data(), static_cast<ULONG>(bytes.size()), digest.data(), 32);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) return false;
    char hex[65]{};
    for (size_t i = 0; i < digest.size(); ++i) sprintf_s(hex + i * 2, sizeof(hex) - i * 2, "%02x", digest[i]);
    return std::strcmp(hex, kExeSha256) == 0;
}

static DWORD WINAPI Initialize(void*) noexcept {
    try {
        // 在线程实际运行、加载锁已释放后固定本模块；退出进程时由系统统一回收。
        // 地图虚表会指向本模块，禁止中途卸载以免留下悬空函数指针。
        HMODULE pinned = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                               reinterpret_cast<LPCWSTR>(&Start), &pinned)) return 0;
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(g_module, path, MAX_PATH);
        g_folder = path;
        g_folder.resize(g_folder.find_last_of(L"\\/"));
        g_folder += L"\\Sky2ChestTracker";
        CreateDirectoryW(g_folder.c_str(), nullptr);
        if (!CheckExecutable()) { Log("Unsupported executable; all hooks skipped."); return 0; }
        g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        // 双重校验：磁盘哈希正确且内存中虚表仍指向预期函数，避免覆盖其他 Mod 的挂钩。
        auto slot = reinterpret_cast<void**>(g_base + 0xB04E10 + 9 * sizeof(void*));
        void* old = nullptr;
        if (!Read(reinterpret_cast<uintptr_t>(slot), old) || old != reinterpret_cast<void*>(g_base + 0x2C6BC0)) {
            Log("Icon vtable conflict; all hooks skipped."); return 0;
        }
        const unsigned char expected[] = {0x48,0x83,0xEC,0x28,0x48,0x8B,0x41,0x08,0x4C,0x8B,0xC1};
        unsigned char actual[sizeof(expected)]{};
        if (!ReadBytes(g_base + 0x2C6BC0, actual, sizeof(actual)) || std::memcmp(expected, actual, sizeof(actual))) {
            Log("Icon function conflict; all hooks skipped."); return 0;
        }
        // 面板安装成功后才启用地图修改，否则用户无法辨认当前选择的是哪种统计口径。
        if (!InstallOverlay()) { Log("Overlay initialization failed; map hook skipped."); return 0; }
        // 输入接入失败时保留键盘操作与地图功能，并在日志中说明，不扩大挂钩范围。
        if (!InstallInputBridge(g_base)) Log("Controller shortcuts unavailable; keyboard remains active.");
        InstallExploration(g_base);
        // 正式地图使用此表驱动函数；同时保留下方对象虚表挂钩，覆盖按对象取图标的路径。
        auto mapFunction = reinterpret_cast<void*>(g_base + 0x3DD0E0);
        const unsigned char mapExpected[] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10};
        unsigned char mapActual[sizeof(mapExpected)]{};
        if (!ReadBytes(reinterpret_cast<uintptr_t>(mapFunction), mapActual, sizeof(mapActual)) ||
            std::memcmp(mapExpected, mapActual, sizeof(mapExpected)) ||
            MH_CreateHook(mapFunction, reinterpret_cast<void*>(&SelectMapIcon), reinterpret_cast<void**>(&g_originalMapIcon)) != MH_OK ||
            MH_EnableHook(mapFunction) != MH_OK) {
            g_enabled.store(false);
            Log("Regional map hook conflict; map modification disabled."); return 0;
        }
        Log("Regional map table hook installed.");
        DWORD protection = 0;
        if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection)) return 0;
        g_originalIcon = reinterpret_cast<GetIconFn>(old);
        InterlockedExchangePointer(slot, reinterpret_cast<void*>(&SelectIcon));
        DWORD unused = 0;
        VirtualProtect(slot, sizeof(void*), protection, &unused);
        Log("Sky2ChestTracker 0.4.0 active: chest tracking, map reveal, native travel rules with live refresh.");
    } catch (...) { Log("Initialization failed; exception contained."); }
    return 0;
}

void Start() noexcept {
    static std::once_flag once;
    try {
        std::call_once(once, [] {
            // 只排入工作线程并立即返回，不等待线程，也不在加载锁中执行图形初始化。
            if (HANDLE thread = CreateThread(nullptr, 0, Initialize, nullptr, 0, nullptr)) CloseHandle(thread);
        });
    } catch (...) {}
}
}
