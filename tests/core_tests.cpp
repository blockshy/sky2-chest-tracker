// 验证容易造成误判的跨周目与读档场景，而不是检查实现细节。
#include "core.h"
#ifdef SKY2_HAVE_CATALOG
#include "progress.h"
#endif
#include <array>
#include <cstdio>
#include <string>

int main() {
    unsigned failures = 0;
    auto check = [&](bool condition, const char* description) {
        if (!condition) { std::printf("FAIL: %s\n", description); ++failures; }
    };
    using namespace tracker;
    check(Icon(Mode::Current, false, true) == kClosedIcon, "上周目已开不能把本周目未开箱标为已开");
    check(Icon(Mode::Inherited, false, true) == kOpenedIcon, "继承视图应显示历史已收集");
    check(Icon(Mode::Inherited, true, false) == kOpenedIcon, "新开箱应立即计入继承视图");
    check(Icon(Mode::Current, true, true) == kOpenedIcon, "当前已开显示开启图标");
    check(Icon(Mode::Current, false, true) == kClosedIcon, "读旧档后必须恢复未开图标");
    std::array<uint8_t, 4096> bits{};
    bits[1505 / 8] = 1u << (1505 % 8);
    check(Flag(bits.data(), bits.size(), 1505), "首个正式宝箱标志可读");
    check(!Flag(bits.data(), bits.size(), 1506), "相邻宝箱不会串位");
    check(!Flag(bits.data(), bits.size(), 32768), "越界标志不得读取");
    check(!Flag(bits.data(), bits.size(), 0), "未指定标志保持无效");

    // 目录从开发者本机游戏生成；CI 无需持有游戏资源即可运行前面的核心语义测试。
#ifdef SKY2_HAVE_CATALOG
    // 完整目录必须恰好分配一次，拆分相邻道路后仍与全局 566 箱保持一致。
    bits.fill(0);
    auto maps = CountMaps(bits.data(), bits.size());
    unsigned total = 0;
    for (const auto& map : maps) total += map.total;
    check(maps.size() == 59 && total == 566, "地图拆分后不得漏计或重复计算宝箱");
    auto aina = [](const auto& list) -> const MapProgress& {
        for (const auto& map : list) if (std::string(map.definition->key) == "mp2000:AinaCauseway") return map;
        throw "找不到已核对的阿伊纳街道分组";
    };
    check(aina(maps).total == 3, "阿伊纳街道应单独统计三箱，不混入卢安其他道路");
    // 三只箱都在继承记录里、仅一只在本周目打开：筛选结果必须随口径不同。
    for (unsigned id : {2754u, 2755u, 2756u, 1554u}) bits[id / 8] |= 1u << (id % 8);
    maps = CountMaps(bits.data(), bits.size());
    check(aina(maps).current == 1 && aina(maps).inherited == 3, "同一地图独立保留两组计数");
    check(aina(maps).Remaining(Mode::Current) == 2 && aina(maps).Remaining(Mode::Inherited) == 0,
          "未开数量必须跟随所选统计口径");
    check(VisibleMaps(maps, Mode::Current, true).size() == 59 && VisibleMaps(maps, Mode::Inherited, true).size() == 58,
          "继承已完成的地图在本周目筛选中仍可能存在遗漏");
    check(VisibleMaps(maps, Mode::Inherited, false).size() == 59, "全部地图筛选仍保留已完成地图");
    // 合成目录故意让地区再次出现，并让第一地区首项已完成。遗漏筛选只能删行，
    // 不能拿筛选后的首项重建地区次序，也不能把完成地图移到整个清单最后。
    const MapDefinition orderDefinitions[]{
        {"a1", "", "", "region-a", ""}, {"b1", "", "", "region-b", ""},
        {"a2", "", "", "region-a", ""}, {"c1", "", "", "region-c", ""},
        {"b2", "", "", "region-b", ""}};
    std::vector<MapProgress> orderedMaps;
    for (const auto& definition : orderDefinitions) orderedMaps.push_back({&definition, 0, 0, 1});
    orderedMaps[0].current = orderedMaps[0].inherited = 1;
    orderedMaps[1].inherited = 1;
    const std::vector<size_t> expectedAll{0, 2, 1, 4, 3};
    check(VisibleMaps(orderedMaps, Mode::Current, false) == expectedAll, "完整清单按地区归组且保留地区内目录顺序");
    check(VisibleMaps(orderedMaps, Mode::Current, true) == std::vector<size_t>({2, 1, 4, 3}), "首项已完成也不会改变遗漏清单地区次序");
    check(VisibleMaps(orderedMaps, Mode::Inherited, true) == std::vector<size_t>({2, 4, 3}), "继承筛选保持相同地区顺序");
    check(VisibleMaps(orderedMaps, Mode::Inherited, false) == expectedAll, "关闭遗漏筛选完整恢复原地区排列");
    orderedMaps[2].current = 1;
    check(VisibleMaps(orderedMaps, Mode::Current, false) == expectedAll, "新开箱不得使完整清单行跳动");
    // 在实际 59 地图目录中验证跨剧情阶段出现的同地区地点已合并为连续区间。
    std::vector<std::string> closedRegions;
    std::string previousRegion;
    for (const auto index : VisibleMaps(maps, Mode::Inherited, false)) {
        const std::string region = maps[index].definition->region;
        if (region == previousRegion) continue;
        check(std::find(closedRegions.begin(), closedRegions.end(), region) == closedRegions.end(), "真实目录相同地区不能再次分散出现");
        if (!previousRegion.empty()) closedRegions.push_back(previousRegion);
        previousRegion = region;
    }
    // 新开箱尚未设置继承位也必须立即计入累计；重新传入旧位图则应回退。
    bits.fill(0);
    bits[1554 / 8] = 1u << (1554 % 8);
    maps = CountMaps(bits.data(), bits.size());
    check(aina(maps).current == 1 && aina(maps).inherited == 1, "新开箱应同时影响两组地图统计");
    bits.fill(0);
    maps = CountMaps(bits.data(), bits.size());
    check(aina(maps).current == 0 && aina(maps).inherited == 0, "读档回退不得保留先前地图计数");
    for (const auto& chest : kChests) bits[chest.opened / 8] |= 1u << (chest.opened % 8);
    maps = CountMaps(bits.data(), bits.size());
    check(VisibleMaps(maps, Mode::Current, true).empty() && VisibleMaps(maps, Mode::Inherited, true).empty(),
          "全部收集时遗漏清单应为空，不能出现剩余数下溢");
#else
    std::printf("Catalog checks skipped: generate the catalog from your own game for full coverage.\n");
#endif
    std::printf("%u failure(s)\n", failures);
    return failures ? 1 : 0;
}
