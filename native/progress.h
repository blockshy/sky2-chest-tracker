// 地图收集统计的纯计算层：只接收同一帧的标志快照，读旧档后自然回退。
// 不保存历史并集，也不读取游戏地址，方便用真实快照和合成状态验证。
#pragma once
#include "core.h"
#include "chest_catalog.h"
#include "map_catalog.h"
#include <algorithm>
#include <iterator>
#include <string_view>
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
    // 地区顺序只来自完整游戏目录中首次出现的位置，不读取译名、收集数量或筛选
    // 结果。后期加入的异空间等地点归回同地区，地区内仍沿用原目录地点顺序。
    // 必须在过滤前计算次序，否则隐藏某地区的首项后，该地区可能被误排到后面。
    std::vector<size_t> regionOrder(maps.size());
    const auto region = [&](size_t index) {
        const auto* definition = maps[index].definition;
        return std::string_view(definition && definition->region ? definition->region : "");
    };
    for (size_t i = 0; i < maps.size(); ++i) {
        regionOrder[i] = i;
        for (size_t earlier = 0; earlier < i; ++earlier) {
            if (region(earlier) == region(i)) { regionOrder[i] = regionOrder[earlier]; break; }
        }
    }
    std::vector<size_t> indices;
    for (size_t i = 0; i < maps.size(); ++i)
        if (!missingOnly || maps[i].Remaining(mode)) indices.push_back(i);
    // 筛选只删除行；开箱、读档、切换周目口径都不能重新排列其它地区或地点。
    std::stable_sort(indices.begin(), indices.end(), [&](size_t left, size_t right) { return regionOrder[left] < regionOrder[right]; });
    return indices;
}
}
