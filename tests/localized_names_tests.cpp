// 语言切换只改变名称，不改变统计地图、传送 ID、排序或精确返程哨兵。
// 无游戏的公开 CI 使用合成名称；有本机原生目录时额外验证每个展示项八语覆盖。
#include "localized_names.h"
#include <array>
#include <cstdio>
#include <set>
#include <string>
#if __has_include("map_catalog.h")
#include "map_catalog.h"
#endif
using namespace tracker;
int main() {
    unsigned failed = 0;
    const auto check = [&](bool ok, const char* label) {
        if (!ok) { ++failed; std::printf("FAIL %s\n", label); }
    };
    const char* names[kLanguageCount]{"中文", "日本語", "English", "繁體中文", "Deutsch", "Français", "Español", "한국어"};
    const char* expected[kLanguageCount]{"中文", "日本語", "English", "繁體中文", "Deutsch", "Français", "Español", "한국어"};
    const auto count = RevisitDestinationCount();
    // 同一室外场景可包含多条道路。构造与真实目录无关的 key/ID，确保公开 CI 和
    // 本机带目录测试都检查查找逻辑，而不是偶然命中本地资源中的其他名称。
    struct TestMap { const char* key; const char* scene; const char* path; };
    const TestMap splitMaps[]{{"fixture-road-a", "mp0000", "Wrong first road"},
                             {"fixture-road-b", "mp0000", "Other road"}};
    const TestMap singleMap[]{{"fixture-dungeon", "mp6011", "Single dungeon"}};
    const std::array<TravelCatalogRow, 0> noTravel{};
    RevisitReturnPoint point{};
    std::memcpy(point.scene, "mp0000", sizeof("mp0000"));
    point.region = 1;
    point.mapPlace = 1008000;
    check(std::strcmp(ReturnPointDisplayName(point, splitMaps, noTravel), RegionName(1)) == 0,
          "ambiguous mp0000 falls back to region, never first chest road");
    const TravelCatalogRow preciseRoad[]{
        {999, 1, 1, 1008000, "mp0000", "Exact native road", "Native region", 0, 0, 0},
        {999, 1, 1, 1008000, "mp0000", "Same destination variant", "Native region", 1, 0, 0}};
    check(std::strcmp(ReturnPointDisplayName(point, splitMaps, preciseRoad), "Exact native road") == 0,
          "precise mapPlace uses native destination and deduplicates same-ID variants");
    point.mapPlace = 1006000;
    check(std::strcmp(ReturnPointDisplayName(point, splitMaps, preciseRoad), RegionName(1)) == 0,
          "unmatched place never borrows another road in same scene");
    point.mapPlace = 1008000;
    std::memcpy(point.scene, "mp6011", sizeof("mp6011"));
    point.region = 6;
    check(std::strcmp(ReturnPointDisplayName(point, singleMap, preciseRoad), "Single dungeon") == 0,
          "same place number from another scene is not considered an exact match");
    const TravelCatalogRow ambiguousEntrances[]{
        {998, 6, 1, 1008000, "mp6011", "Entrance", "Native region", 0, 0, 0},
        {999, 6, 1, 1008000, "mp6011", "Deepest point", "Native region", 0, 0, 0}};
    check(std::strcmp(ReturnPointDisplayName(point, singleMap, ambiguousEntrances), "Single dungeon") == 0,
          "different destinations sharing place ID fall back to the unique dungeon");
    const TravelCatalogRow hiddenEntrance[]{
        {999, 6, 1, 1008000, "mp6011", "Unreviewed entrance", "Native region", 0, 8, 1}};
    check(std::strcmp(ReturnPointDisplayName(point, singleMap, hiddenEntrance), "Single dungeon") == 0,
          "internal unreviewed entrance is not exposed as a return location");
    std::set<uint32_t> baseline;
    for (size_t i = 0; i < count; ++i) baseline.insert(RevisitDestinationAt(i).id);
    for (unsigned index = 0; index < kLanguageCount; ++index) {
        SetDisplayLanguage(static_cast<Language>(index));
        check(std::strcmp(LocalizedName(names), expected[index]) == 0, "language array order");
        check(RevisitDestinationCount() == count, "switching language preserves row count");
        std::set<uint32_t> ids;
        for (size_t i = 0; i < count; ++i) {
            const auto& item = RevisitDestinationAt(i);
            ids.insert(item.id);
            check(*DestinationName(item) && *DestinationGroup(item), "all display rows have labels");
#if SKY2_HAS_TRAVEL_LOCALIZATION
            if (item.id != kRevisitReturnTarget) {
                const LocalizedTravelDefinition* native = nullptr;
                for (const auto& row : kLocalizedTravel) if (row.id == item.id) native = &row;
                check(native != nullptr, "every real destination has native-language provenance");
                if (native) {
                    check(std::strcmp(DestinationName(item), native->names[index]) == 0,
                          "display uses exact native name for selected language");
                    check(std::strcmp(DestinationGroup(item), native->groups[index]) == 0,
                          "display uses exact native region for selected language");
                }
            }
#endif
        }
        check(ids == baseline, "language changes never alter destination identity");
#if SKY2_HAS_MAP_LOCALIZATION && __has_include("map_catalog.h")
        for (const auto& map : kMaps) {
            const LocalizedMapDefinition* native = nullptr;
            for (const auto& row : kLocalizedMaps) if (std::strcmp(row.key, map.key) == 0) native = &row;
            check(native != nullptr, "every chest group has a native-language path");
            if (native) check(std::strcmp(MapPath(map), native->paths[index]) == 0,
                              "map labels follow native name without regrouping chests");
        }
#endif
        const auto& last = RevisitDestinationAt(count - 1);
        check(last.id == kRevisitReturnTarget && *DestinationName(last), "exact return remains last");
    }
    SetDisplayLanguage(Language::Chinese);
    std::printf("%u localized-name failure(s)\n", failed);
    return failed ? 1 : 0;
}
