// 探索辅助：仅调整原生地图显示和运行时传送菜单；绝不批量写入存档／剧情旗标。
// 地址、结构和调用约定仅适用于已验证完整 SHA-256 的当前游戏构建。
#include "exploration.h"
#include "exploration_logic.h"
#include "travel_refresh.h"
#include "tracker.h"
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <array>
#include <vector>
#include <limits>

extern "C" {
void Sky2MapAlphaShim();
void* Sky2NextMapAlpha = nullptr;
void Sky2RegisterSpotShim();
void* Sky2NextRegisterSpot = nullptr;
void Sky2MapBrowseShim();
void Sky2SpotListShim();
void Sky2AreaListShim();
void* Sky2NextMapBrowse = nullptr;
void* Sky2NextSpotList = nullptr;
void* Sky2NextAreaList = nullptr;
void Sky2BuildTravelShim();
void* Sky2NextBuildTravel = nullptr;
}

namespace tracker {
static uintptr_t g_explorationBase = 0;
static std::atomic<bool> g_mapAvailable{false}, g_travelAvailable{false};
static std::atomic<bool> g_mapReveal{false}, g_travelUnlock{false};
// 输入只改变请求序号；g_travelUnlock 只在原生刷新线程切换，使整次重建采用同一目标。
static TravelRefreshState g_travelRefresh;
// 显式实时刷新只在实际调用 Build 的同步区间授予许可；线程局部状态避免其它线程
// 或 MapJumpState 内部的间接初始化借用许可。普通开图则另按三个已验证返回地址判断。
static thread_local bool g_travelExplicitBuild = false;

// 挂钩只在游戏正在使用这些对象的同一线程读取；SEH 将额外的有界读取故障限制在本模块。
// 不捕获或吞掉原生函数自身的异常，否则可能掩盖游戏真正的故障并继续使用损坏状态。
static bool ReadMemory(uintptr_t address, void* output, size_t length) noexcept {
    if (address < 0x10000) return false;
    __try { std::memcpy(output, reinterpret_cast<const void*>(address), length); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T> static bool Read(uintptr_t address, T& value) noexcept {
    return ReadMemory(address, &value, sizeof(value));
}

// 以下是菜单自身的临时状态行，不是保存数据。仅在游戏原生菜单调用线程中访问。
struct TravelSpotState {
    uint32_t id, area, region;
    uint8_t visible, blocked, registered, reserved;
};
struct TravelAreaState {
    uint32_t id, region;
    uint8_t visible, blocked, reserved[2];
};
static_assert(sizeof(TravelSpotState) == 16 && sizeof(TravelAreaState) == 12);

// 旧登记桥仍参与独立ABI测试及链接；生产不再安装该挂钩，也不在逐点登记期间补显。
// 统一候选必须等整个原生剧情脚本结束后，由显示构建前入口一次处理。
extern "C" void Sky2AfterRegisterSpot(uintptr_t, uint32_t) noexcept {}
extern "C" bool Sky2BeforeBuildTravel(uintptr_t manager, uintptr_t caller) noexcept;

template<size_t N> static bool Matches(uintptr_t address, const unsigned char (&bytes)[N]) noexcept {
    unsigned char actual[N]{};
    return ReadMemory(address, actual, N) && std::memcmp(bytes, actual, N) == 0;
}

// 内部实现保持在同一翻译单元，以共享受保护读取与事务状态；不向绘制层暴露游戏指针。
#include "travel_menu.h"

// 所有状态先复制并核对，再批量提交显示位。固定容量来自当前原生结构边界；不进行
// 动态内存分配，也不在本次调用结束后保存游戏指针。未知/重复状态使整次评估原样退出。
struct NativeTravelSnapshot {
    uintptr_t spotsAddress = 0, areasAddress = 0;
    uint64_t spotCount = 0, areaCount = 0;
    uint32_t region = 0;
    std::array<TravelSpotState, 1001> spots{};
    std::array<TravelAreaState, 64> areas{};
};

static bool ReadNativeTravelSnapshot(uintptr_t manager, NativeTravelSnapshot& snapshot) noexcept {
    if (!Read(manager + 0xF8, snapshot.region) || !snapshot.region || snapshot.region > 9 ||
        !Read(manager + 0xE0, snapshot.spotsAddress) || !Read(manager + 0xE8, snapshot.spotCount) ||
        !snapshot.spotCount || snapshot.spotCount > snapshot.spots.size() ||
        !Read(manager + 0xC8, snapshot.areasAddress) || !Read(manager + 0xD0, snapshot.areaCount) ||
        snapshot.areaCount > snapshot.areas.size() ||
        !ReadMemory(snapshot.spotsAddress, snapshot.spots.data(),
                    static_cast<size_t>(snapshot.spotCount) * sizeof(TravelSpotState)) ||
        (snapshot.areaCount && !ReadMemory(snapshot.areasAddress, snapshot.areas.data(),
                    static_cast<size_t>(snapshot.areaCount) * sizeof(TravelAreaState)))) return false;
    std::array<bool, 1001> seen{};
    for (uint64_t i = 0; i < snapshot.spotCount; ++i) {
        const auto& spot = snapshot.spots[i];
        if (!spot.id || spot.id >= seen.size() || seen[spot.id] || !spot.region || spot.region > 9 ||
            spot.visible > 1 || spot.blocked > 1 || spot.registered > 1) return false;
        seen[spot.id] = true;
    }
    for (uint64_t i = 0; i < snapshot.areaCount; ++i) {
        const auto& area = snapshot.areas[i];
        if (!area.id || !area.region || area.region > 9 || area.visible > 1 || area.blocked > 1) return false;
        for (uint64_t previous = 0; previous < i; ++previous)
            if (snapshot.areas[previous].id == area.id) return false;
    }
    return true;
}

static bool CommitNativeTravelVisibility(uintptr_t manager, const NativeTravelSnapshot& expected,
                                         const std::array<bool, 1001>& revealSpots,
                                         const std::array<bool, 64>& revealAreas) noexcept {
    // 不在含游戏调用或可重入逻辑的区间里提交；这里只对已经核对过的运行时visible
    // 字节赋值。提交前再次比较整个快照和管理器引用，避免依据过期对象修改新菜单。
    __try {
        if (*reinterpret_cast<const uintptr_t*>(manager + 0xE0) != expected.spotsAddress ||
            *reinterpret_cast<const uint64_t*>(manager + 0xE8) != expected.spotCount ||
            *reinterpret_cast<const uintptr_t*>(manager + 0xC8) != expected.areasAddress ||
            *reinterpret_cast<const uint64_t*>(manager + 0xD0) != expected.areaCount ||
            *reinterpret_cast<const uint32_t*>(manager + 0xF8) != expected.region) return false;
        auto* spots = reinterpret_cast<TravelSpotState*>(expected.spotsAddress);
        auto* areas = reinterpret_cast<TravelAreaState*>(expected.areasAddress);
        if (std::memcmp(spots, expected.spots.data(),
                        static_cast<size_t>(expected.spotCount) * sizeof(TravelSpotState)) != 0 ||
            (expected.areaCount && std::memcmp(areas, expected.areas.data(),
                        static_cast<size_t>(expected.areaCount) * sizeof(TravelAreaState)) != 0)) return false;
        // 先向每个待改字节写回相同的0，验证全部写入位置。volatile阻止编译器消除
        // 此预检，确保只读或无效页在任何可见状态改变之前被拒绝；不触碰其他字段。
        for (uint64_t i = 0; i < expected.spotCount; ++i)
            if (revealSpots[i]) *reinterpret_cast<volatile uint8_t*>(&spots[i].visible) = 0;
        for (uint64_t i = 0; i < expected.areaCount; ++i)
            if (revealAreas[i]) *reinterpret_cast<volatile uint8_t*>(&areas[i].visible) = 0;
        for (uint64_t i = 0; i < expected.areaCount; ++i)
            if (revealAreas[i]) areas[i].visible = 1;
        for (uint64_t i = 0; i < expected.spotCount; ++i)
            if (revealSpots[i]) spots[i].visible = 1;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

extern "C" bool Sky2BeforeBuildTravel(uintptr_t manager, uintptr_t caller) noexcept {
    // 初始化调用0x29EE92没有先运行MapJumpState，不能沿用其中可能过期的登记结果。
    // 只接受已核对的三处原生调用，或完整实时刷新在同线程同步Build期间授予的许可。
    const bool afterStateScript = caller == g_explorationBase + 0x3DAA88 ||
        caller == g_explorationBase + 0x3DB001 || caller == g_explorationBase + 0x3DB3AA;
    if ((!afterStateScript && !g_travelExplicitBuild) ||
        !g_travelUnlock.load(std::memory_order_relaxed)) return false;
    NativeTravelSnapshot snapshot{};
    NativeTravelTables tables{};
    if (!ReadNativeTravelSnapshot(manager, snapshot) || !ReadTravelTables(tables)) return false;

    // 先核验全部静态关联和运行时区域。静态表可含同ID备用记录，但采用原生首匹配
    // 语义；动态状态中同ID重复则已经被拒绝，不能自行合并或选择其中一个猜测。
    for (uint64_t i = 0; i < snapshot.areaCount; ++i) {
        NativeTravelRow native{};
        if (!FindNativeTravelRow(tables.areas, snapshot.areas[i].id, false, native) ||
            native.region != snapshot.areas[i].region) return false;
    }
    std::array<bool, 1001> revealSpots{};
    std::array<bool, 64> revealAreas{};
    unsigned addedSpots = 0, addedAreas = 0;
    for (uint64_t i = 0; i < snapshot.spotCount; ++i) {
        const auto& spot = snapshot.spots[i];
        NativeTravelRow native{};
        if (!FindNativeTravelRow(tables.spots, spot.id, true, native) ||
            native.region != spot.region || native.area != spot.area) return false;
        NativeTravelCandidate candidate{};
        candidate.id = spot.id; candidate.region = spot.region; candidate.area = spot.area;
        candidate.visible = spot.visible; candidate.blocked = spot.blocked; candidate.registered = spot.registered;
        candidate.nativeRegion = native.region; candidate.nativeArea = native.area; candidate.nativeFlags = native.flags;
        uint64_t areaIndex = snapshot.areaCount;
        if (spot.area) {
            for (uint64_t j = 0; j < snapshot.areaCount; ++j) {
                if (snapshot.areas[j].id != spot.area) continue;
                const auto& area = snapshot.areas[j];
                candidate.areaExists = true; candidate.areaRegion = area.region;
                candidate.areaVisible = area.visible; candidate.areaBlocked = area.blocked;
                areaIndex = j;
                break;
            }
        }
        if (!CanRevealNativeTravel(candidate, snapshot.region)) continue;
        revealSpots[i] = true;
        ++addedSpots;
        // 已发现的分组保持原样；尚未发现但原生未禁用的分组随第一个候选临时显示。
        // area=0的独立点不进入此路径；不写真实6000/6500位或任何其他保存记录。
        if (areaIndex < snapshot.areaCount && !snapshot.areas[areaIndex].visible && !revealAreas[areaIndex]) {
            revealAreas[areaIndex] = true;
            ++addedAreas;
        }
    }
    if (addedSpots && CommitNativeTravelVisibility(manager, snapshot, revealSpots, revealAreas)) {
        char line[160]{};
        sprintf_s(line, "Exploration: native rules revealed %u temporary destination(s) and %u area(s) in region %u.",
                  addedSpots, addedAreas, snapshot.region);
        Log(line);
    }
    // 原生Build始终执行；这里只准备显示缓存，不接管原生确认、跳转或传送前后脚本。
    return false;
}

// 此入口经 MASM 保存寄存器后调用。原生第四参数为 XMM3 中的 alpha，额外参数来自调用点。
// 精确限定直接分块调用：递归处理子节点时沿用原生 alpha，不再次查询区块或扩大处理范围。
extern "C" float Sky2MapAlpha(void*, uintptr_t map, uintptr_t node, float alpha,
                              uintptr_t caller, uintptr_t chunk) noexcept {
    if (!g_mapReveal.load(std::memory_order_relaxed) || caller != g_explorationBase + 0x3F139D)
        return alpha;
    uintptr_t chunks = 0, chunkNode = 0;
    uint64_t count = 0;
    uint32_t id = 204;
    uint8_t mapEnabled = 0, chunkEnabled = 0;
    float floorAlpha = 0;
    if (!Read(map + 0x108, chunks) || !Read(map + 0x110, count) || !IsMapChunk(chunks, count, chunk) ||
        !Read(chunk + 0x28, chunkNode) || chunkNode != node || !Read(chunk + 0x20, id) ||
        !Read(chunk + 0x48, chunkEnabled) || !Read(map + 0xA8, mapEnabled) || !Read(map + 0xF8, floorAlpha))
        return alpha;
    const float result = RevealedMapAlpha(alpha, floorAlpha, mapEnabled == 1, chunkEnabled == 1, id);
    if (result != alpha) {
        static std::atomic<bool> reported{false};
        if (!reported.exchange(true)) Log("Exploration: native unexplored map alpha override verified.");
    }
    return result;
}

void InstallExploration(uintptr_t gameBase) noexcept {
    g_explorationBase = gameBase;
    // 同时校验函数入口与唯一外部调用点的参数装载，避免覆盖其他 Mod 或错误复用偏移。
    const unsigned char mapEntry[] = {
        0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x40,0x41,0x83,0x78,0x60,0x02,
        0x48,0x8B,0xFA,0x0F,0x29,0x74,0x24,0x30,0x48,0x8B,0xF1,0x0F,0x28,0xF3
    };
    const unsigned char mapCall[] = {
        0x49,0x8B,0x16,0x49,0x8B,0xCF,0x4D,0x8B,0x41,0x28,0xF3,0x0F,0x10,0x9A,0xF8,0,0,0,
        0xF3,0x41,0x0F,0x59,0x59,0x38,0xE8,0xD3,0xEA,0xFF,0xFF,0x49,0x83,0xC1,0x50
    };
    auto target = reinterpret_cast<void*>(gameBase + 0x3EFE70);
    if (Matches(gameBase + 0x3EFE70, mapEntry) && Matches(gameBase + 0x3F1380, mapCall) &&
        MH_CreateHook(target, reinterpret_cast<void*>(&Sky2MapAlphaShim), &Sky2NextMapAlpha) == MH_OK) {
        if (MH_EnableHook(target) == MH_OK) g_mapAvailable.store(true);
        else MH_RemoveHook(target);
    }
    Log(g_mapAvailable.load() ? "Exploration: temporary map reveal ready (default off)." :
        "Exploration: map reveal validation/hook failed; feature unavailable.");
    // 显示构建与三种浏览层的刷新入口作为一组安装，任一步失败会撤销本组挂钩。
    // 原生登记函数不再修改，统一候选只在最终原生状态已经确定后生效。
    g_travelAvailable.store(InstallTravelRefresh(gameBase));
    Log(g_travelAvailable.load() ?
        "Exploration: realtime native travel rules ready (default off, current-region registered non-gray entries)." :
        "Exploration: realtime travel validation/hook failed; feature unavailable.");
}

void ToggleExploration(ExplorationFeature feature) noexcept {
    const bool map = feature == ExplorationFeature::MapReveal;
    auto& available = map ? g_mapAvailable : g_travelAvailable;
    if (!available.load()) {
        Log(map ? "Exploration: map reveal unavailable." : "Exploration: travel unlock unavailable.");
        return;
    }
    bool next = false;
    if (map) {
        next = !g_mapReveal.load();
        g_mapReveal.store(next);
    } else {
        next = g_travelRefresh.ToggleRequest();
    }
    Log(map ? (next ? "Exploration: map reveal enabled." : "Exploration: map reveal disabled.") :
        (next ? "Exploration: travel enable requested; pending native refresh." :
                "Exploration: travel disable requested; pending native refresh."));
}

ExplorationStatus ReadExplorationStatus() noexcept {
    const auto refresh = g_travelRefresh.ReadStatus();
    return {g_mapAvailable.load(), g_travelAvailable.load(), g_mapReveal.load(), refresh.appliedEnabled,
            refresh.requestedEnabled, refresh.pending};
}
}
