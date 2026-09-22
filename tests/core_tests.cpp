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
