// 特殊宝箱保护仅截获原生已确定的 TBoxProcess 启动调用；不替换脚本、不绕过收尾。
#include "revisit_event_guard.h"
#include "revisit_event_guard_rules.h"
#include "tracker.h"
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstring>
#include <limits>

extern "C" {
void Sky2RevisitScriptStartShim();
void* Sky2NextRevisitScriptStart = nullptr;
}

namespace tracker {
namespace {
uintptr_t g_revisitGuardBase = 0;
std::atomic<bool> g_revisitGuardInstalled{false};
std::atomic<bool> g_revisitGuardActive{true};
// 必须在原生加载目标场景前发布，森林的首次 MapReinit 可能立即读取此状态。
// 不从磁盘恢复，防止同章普通主线存档被旧返程记录误认为实验行程。
// 0 表示无行程，其余值为合法章节 + 1。把开关与章节合并为一个原子值，避免
// 并发读到“新开关配旧章节”；保护入口可独立结束已经跨章的旧行程。
std::atomic<uint32_t> g_experimentalRevisitTripChapter{0};

// 地址读取与唯一一次临时参数写入采用窄 SEH。原生脚本启动仍在汇编桥尾跳后执行，
// 不在保护范围内；不能吞掉原生异常并假装已正常获取物品。
bool GuardReadBytes(uintptr_t address, void* output, size_t size) noexcept {
    if (address < 0x10000 || size > std::numeric_limits<uintptr_t>::max() - address) return false;
    __try { std::memcpy(output, reinterpret_cast<const void*>(address), size); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T> bool GuardRead(uintptr_t address, T& value) noexcept {
    return GuardReadBytes(address, &value, sizeof(value));
}
template<size_t N> bool GuardString(uintptr_t address, char (&text)[N]) noexcept {
    // 按字节读取至 NUL，避免为了验证短字符串而跨入它后方不可读的页面。
    for (size_t i = 0; i < N; ++i) {
        if (address > std::numeric_limits<uintptr_t>::max() - i ||
            !GuardRead(address + i, text[i])) return false;
        if (!text[i]) return true;
    }
    return false;
}
template<size_t N> bool GuardMatches(uintptr_t address, const unsigned char (&expected)[N]) noexcept {
    unsigned char actual[N]{};
    return GuardReadBytes(address, actual, sizeof(actual)) && !std::memcmp(actual, expected, N);
}

bool GuardTableRow(uintptr_t rowPointer, uint32_t& row) noexcept {
    uintptr_t tables = 0, holder = 0, file = 0, buffer = 0, headers = 0;
    uint32_t headerIndex = 0, offset = 0, stride = 0, count = 0;
    // 与宝箱图标已有适配使用相同的 t_tbox 管理器入口；行号从真实表范围还原。
    if (!GuardRead(g_revisitGuardBase + 0xC5D778, tables) || tables < 0x10000 ||
        !GuardRead(tables + 0x108, holder) || holder < 0x10000 ||
        !GuardRead(holder + 8, file) || file < 0x10000 ||
        !GuardRead(file + 0x10, buffer) || buffer < 0x10000 ||
        !GuardRead(file + 0x20, headers) || headers < 0x10000 ||
        !GuardRead(file + 0x28, headerIndex) || headerIndex > 4096) return false;
    const uintptr_t header = headers + static_cast<uintptr_t>(headerIndex) * 80;
    if (!GuardRead(header + 0x44, offset) || !GuardRead(header + 0x48, stride) ||
        !GuardRead(header + 0x4C, count) || stride != 120 || count != 571 ||
        offset > 0x1000000 || buffer > std::numeric_limits<uintptr_t>::max() - offset) return false;
    const uintptr_t start = buffer + offset;
    if (rowPointer < start || rowPointer - start >= static_cast<uintptr_t>(stride) * count ||
        (rowPointer - start) % stride != 0) return false;
    row = static_cast<uint32_t>((rowPointer - start) / stride);
    return true;
}

// 同一 ID 在 t_place 中可能有不同剧情阶段的重复行。必须匹配完整身份，而不是
// 只取首行：荣耀号终章的主地点地区 7 与旧地区 8 正是这种合法重复记录。
bool GuardPlaceIdentity(uint32_t id, uint32_t region, uint8_t variant,
                        const char* scene) noexcept {
    uintptr_t tables = 0, holder = 0, file = 0, buffer = 0, headers = 0;
    uint32_t headerIndex = 0, offset = 0, stride = 0, count = 0;
    if (!id || !GuardRead(g_revisitGuardBase + 0xC5D778, tables) ||
        !GuardRead(tables + 0x60, holder) || !GuardRead(holder + 8, file) ||
        !GuardRead(file + 0x10, buffer) || !GuardRead(file + 0x20, headers) ||
        !GuardRead(file + 0x2C, headerIndex) || headerIndex > 1024) return false;
    const uintptr_t header = headers + static_cast<uintptr_t>(headerIndex) * 0x50;
    if (!GuardRead(header + 0x44, offset) || offset > 0x1000000 ||
        !GuardRead(header + 0x48, stride) || stride != 0xA8 ||
        !GuardRead(header + 0x4C, count) || !count || count > 4096 ||
        buffer < 0x10000 || buffer > std::numeric_limits<uintptr_t>::max() - offset ||
        buffer + offset > std::numeric_limits<uintptr_t>::max() - static_cast<uintptr_t>(count) * stride)
        return false;
    for (uint32_t i = 0; i < count; ++i) {
        const uintptr_t row = buffer + offset + static_cast<uintptr_t>(i) * stride;
        uint32_t rowId = 0, rowRegion = 0;
        uint8_t rowVariant = 0;
        uintptr_t name = 0;
        char rowScene[32]{};
        if (!GuardRead(row, rowId)) return false;
        if (rowId != id) continue;
        if (!GuardRead(row + 0x98, rowRegion) || !GuardRead(row + 0x90, rowVariant) ||
            !GuardRead(row + 8, name) || !GuardString(name, rowScene)) return false;
        if (rowRegion == region && rowVariant == variant && !std::strcmp(rowScene, scene)) return true;
    }
    return false;
}

bool GuardContext(char (&scene)[32], uint32_t& chapter, bool& completed,
                  bool& experimentalTrip) noexcept {
    uintptr_t field = 0, sceneData = 0, sceneName = 0, savedata = 0, root = 0, rootName = 0;
    uint32_t sceneLength = 0, region = 0, rootRegion = 0, changing = 0, place = 0, rootPlace = 0;
    uint8_t rootValid = 0, variant = 0, rootVariant = 0;
    char staticScene[32]{}, rootScene[32]{};
    std::array<uint8_t, 4096> flags{};
    // 同时核对字段管理器当前场景和静态地图数据，避免转场过程中把旧地图名配新对象。
    if (!GuardRead(g_revisitGuardBase + 0xC60E08, field) || field < 0x10000 ||
        !GuardRead(field + 0x1BC8, changing) || changing != 0 ||
        !GuardRead(field + 0x190, sceneLength) || !sceneLength || sceneLength >= sizeof(scene) ||
        !GuardString(field + 0x170, scene) || std::strlen(scene) != sceneLength ||
        !revisit_eventguard::SupportedScene(scene) ||
        !GuardRead(field + 0x648, sceneData) || sceneData < 0x10000 ||
        !GuardRead(sceneData + 8, sceneName) || !GuardString(sceneName, staticScene) ||
        std::strcmp(scene, staticScene) || !GuardRead(sceneData, place) ||
        !GuardRead(sceneData + 0x98, region) || !GuardRead(sceneData + 0x90, variant) ||
        !GuardRead(field + 0x108, root) || root < 0x10000 ||
        !GuardRead(root + 0xE77, rootValid) || rootValid != 1 ||
        !GuardRead(root + 0x808, rootPlace) || !GuardRead(root + 0x810, rootName) ||
        !GuardString(rootName, rootScene) || std::strcmp(scene, rootScene) ||
        !GuardRead(root + 0x8A0, rootRegion) || !GuardRead(root + 0x898, rootVariant) ||
        rootVariant != variant ||
        !GuardPlaceIdentity(place, region, variant, scene) ||
        !GuardPlaceIdentity(rootPlace, rootRegion, rootVariant, scene) ||
        !GuardRead(g_revisitGuardBase + 0xC60E58, savedata) || savedata < 0x10000 ||
        !GuardRead(savedata + 0x11100 + 12 * 4, chapter) ||
        !GuardReadBytes(savedata + 0x100, flags.data(), flags.size())) return false;
    // 先将经过 t_place 验证的地形/主地点关系归一，再检查剧情。地区 0 不是通行证，
    // 也不允许普通场景借用荣耀号的 7/8 对应关系；读取失败保持原生开箱参数。
    // 不能使用渲染线程的延迟章节或无章节的记账 getter。即使面板完全隐藏，
    // 本次开箱读取到另一合法章节时也要立刻撤销跨章例外，且以后不会自动复活。
    experimentalTrip = (chapter >> 30) == 1 && (chapter & 0x3FFFFFFFu) <= 9 &&
        ExperimentalRevisitTripActive(chapter & 0x3FFFFFFFu);
    const bool experimental = revisit_eventguard::ExperimentalTripChapter(chapter, experimentalTrip);
    region = experimental ? revisit_eventguard::ResolveKnownSceneRegion(scene, region, rootRegion) :
        revisit_eventguard::ResolveRevisitStoryRegion(chapter, scene, region, rootRegion);
    completed = revisit_eventguard::RequiredRevisitStoryComplete(
        chapter, region, scene, flags.data(), flags.size());
    // 实验行程只绕过已完成剧情门槛；表身份、精确场景及章节编码仍全部有效。
    return region != 0 && (completed || experimental);
}

bool NormalizeTemporaryArgument(uintptr_t address, uint32_t expected) noexcept {
    __try {
        auto* argument = reinterpret_cast<uint32_t*>(address);
        // 与验证阶段再次比较；只写调用方栈上的这一枚整数，不写 t_tbox 或游戏旗标。
        if (*argument != expected) return false;
        *argument = revisit_eventguard::kIntegerTag;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}

// 汇编桥保留原始 RSI（该调用点的 t_tbox 行）和原始入口 RSP。此入口不会启动、
// 停止或跳过脚本：它最多归零一个已验证的临时参数，随后由原生线程正常执行开箱。
extern "C" void Sky2GuardTBoxScriptStart(uintptr_t caller, uintptr_t tableRow,
                                         uintptr_t function, uintptr_t parameters,
                                         uint32_t count, uintptr_t originalRsp) noexcept {
    using namespace revisit_eventguard;
    // 通用脚本入口调用频繁；其它来源先以地址比较退出，不读取地图、表格或存档。
    if (caller != g_revisitGuardBase + kTBoxStartReturnRva ||
        !g_revisitGuardInstalled.load(std::memory_order_relaxed) ||
        !g_revisitGuardActive.load(std::memory_order_relaxed) ||
        count != 1 ||
        originalRsp < 0x10000 || originalRsp > std::numeric_limits<uintptr_t>::max() - 0x4C ||
        parameters != originalRsp + 0x4C) return;

    uint32_t row = 0, parameter = 0, argument = 0, chapter = 0;
    uintptr_t mapName = 0, chestName = 0;
    bool completed = false;
    bool experimentalTrip = false;
    char scene[32]{}, tableScene[32]{}, name[64]{}, functionName[32]{};
    if (!GuardTableRow(tableRow, row) || !GuardContext(scene, chapter, completed, experimentalTrip) ||
        !GuardRead(tableRow, mapName) || !GuardRead(tableRow + 8, chestName) ||
        !GuardString(mapName, tableScene) || !GuardString(chestName, name) ||
        !GuardRead(tableRow + 0x10, parameter) || !GuardRead(parameters, argument) ||
        !GuardString(function, functionName)) return;
    if (ShouldNormalize(caller - g_revisitGuardBase, chapter, completed, scene, row,
                        tableScene, name, parameter, functionName, count, argument, experimentalTrip) &&
        NormalizeTemporaryArgument(parameters, argument)) {
        Log("Revisit: special chest uses native ordinary finish; reward and autosave remain active.");
    }
}

bool InstallRevisitEventGuard(uintptr_t gameBase) noexcept {
    if (g_revisitGuardInstalled.load()) return g_revisitGuardBase == gameBase;
    g_revisitGuardBase = gameBase;
    // 不只验证通用启动函数，还核对唯一调用点的 RSI 取参、整数编码、临时栈槽和
    // 参数数量。任一变化均停止安装，防止把别的脚本或别的调用约定当成宝箱入口。
    const unsigned char start[] = {
        0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,
        0x57,0x48,0x83,0xEC,0x30,0x0F,0xB6,0x81,0x08,0x04,0,0
    };
    const unsigned char parameter[] = {
        0x8B,0x46,0x10,0x48,0x8D,0x3D,0x9C,0x45,0x82,0,0x48,0x8B,0x1D,0xC5,0xF7,0x97,0,
        0x4C,0x8D,0x4C,0x24,0x40,0x25,0xFF,0xFF,0xFF,0x3F,0x44,0x89,0x64,0x24,0x40,
        0x0F,0xBA,0xE8,0x1E
    };
    const unsigned char call[] = {
        0x4C,0x8D,0x4C,0x24,0x44,0xC7,0x44,0x24,0x20,0x01,0,0,0,
        0x4C,0x89,0xA0,0xD0,0x02,0,0,0x48,0x8B,0x83,0x90,0x16,0x01,0,
        0x4C,0x89,0xA0,0xD8,0x02,0,0,0x48,0x8B,0x8B,0x90,0x16,0x01,0,
        0x48,0x8B,0x01,0xFF,0x50,0x10
    };
    char function[32]{};
    if (!GuardMatches(gameBase + 0x4CD730, start) ||
        !GuardMatches(gameBase + 0x2E1682, parameter) || !GuardMatches(gameBase + 0x2E173D, call) ||
        !GuardString(gameBase + 0xB05C28, function) || std::strcmp(function, "system.TBoxProcess")) {
        Log("Revisit: special chest hook validation failed; revisit must remain unavailable.");
        return false;
    }
    auto* target = reinterpret_cast<void*>(gameBase + 0x4CD730);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&Sky2RevisitScriptStartShim),
                      &Sky2NextRevisitScriptStart) != MH_OK) return false;
    if (MH_EnableHook(target) != MH_OK) { MH_RemoveHook(target); return false; }
    g_revisitGuardInstalled.store(true);
    Log("Revisit: exact special-chest guard ready for verified scene and travel-session rules.");
    return true;
}

void SetRevisitEventGuardActive(bool active) noexcept {
    g_revisitGuardActive.store(active, std::memory_order_relaxed);
}

void SetExperimentalRevisitTripActive(bool active, uint32_t chapter) noexcept {
    const uint32_t token = revisit_policy::kUnrestricted && active && chapter <= 9 ? chapter + 1 : 0;
    g_experimentalRevisitTripChapter.store(token, std::memory_order_release);
}

bool ExperimentalRevisitTripActive(uint32_t chapter) noexcept {
    if constexpr (!revisit_policy::kUnrestricted) return false;
    uint32_t token = g_experimentalRevisitTripChapter.load(std::memory_order_acquire);
    if (!token || token > 10) return false;
    if (chapter == UINT32_MAX) return true; // 仅供提交/回滚记账；保护入口必须指定当前章。
    if (chapter > 9) return false;
    if (token == chapter + 1) return true;
    // 只清除刚观察到的旧 token，不覆盖另一个线程刚刚确立的新行程。
    g_experimentalRevisitTripChapter.compare_exchange_strong(token, 0,
        std::memory_order_acq_rel, std::memory_order_acquire);
    return false;
}
}
