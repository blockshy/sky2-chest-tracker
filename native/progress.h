// 地图收集统计的纯计算层：只接收同一帧的标志快照，读旧档后自然回退。
// 不保存历史并集，也不读取游戏地址，方便用真实快照和合成状态验证。
#pragma once
#include "core.h"
#include "chest_catalog.h"
#include "map_catalog.h"
#include <algorithm>
#include <iterator>
#include <vector>

namespace tracker {
struct MapProgress {
    const MapDefinition* definition = nullptr;
    unsigned current = 0;
    unsigned inherited = 0;
    unsigned total = 0;

    // “未开”与地图图标使用同一口径，继承视图包含本周目新打开的箱子。
    unsigned Collected(Mode mode) const noexcept { return mode == Mode::Current ? current : inherited; }
    unsigned Remaining(Mode mode) const noexcept { return total - Collected(mode); }
};

inline std::vector<MapProgress> CountMaps(const uint8_t* bits, size_t size) {
    static_assert(std::size(kChests) == std::size(kChestMapIndices), "宝箱与地图索引必须一一对应");
    std::vector<MapProgress> result;
    result.reserve(std::size(kMaps));
    for (const auto& map : kMaps) result.push_back({&map, 0, 0, 0});
    for (size_t i = 0; i < std::size(kChests); ++i) {
        const auto& chest = kChests[i];
        auto& group = result.at(kChestMapIndices[i]);
        const bool current = Flag(bits, size, chest.opened);
        const bool inherited = current || Flag(bits, size, chest.inherited);
        ++group.total;
        group.current += current;
        group.inherited += inherited;
    }
    return result;
}

inline std::vector<size_t> VisibleMaps(const std::vector<MapProgress>& maps, Mode mode, bool missingOnly) {
    std::vector<size_t> indices;
    for (size_t i = 0; i < maps.size(); ++i)
        if (!missingOnly || maps[i].Remaining(mode)) indices.push_back(i);
    // 有遗漏的地图排在前面；同类保持目录顺序，避免计数变化时整张清单跳动。
    std::stable_partition(indices.begin(), indices.end(), [&](size_t i) { return maps[i].Remaining(mode) != 0; });
    return indices;
}
}
