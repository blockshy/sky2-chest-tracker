// 玩家界面中的游戏专名仅取自本机八语原生表；这里不翻译、不更改目的地身份。
// 名称与统计/传送规则分离，语言切换不会重建清单或改变页码、ID 和返程记录。
#pragma once
#include "localization.h"
#include "revisit_catalog.h"
#include <cstring>

#if __has_include("map_localization.h")
#include "map_localization.h"
#define SKY2_HAS_MAP_LOCALIZATION 1
#else
#define SKY2_HAS_MAP_LOCALIZATION 0
#endif
#if __has_include("travel_localization.h")
#include "travel_localization.h"
#define SKY2_HAS_TRAVEL_LOCALIZATION 1
#else
#define SKY2_HAS_TRAVEL_LOCALIZATION 0
#endif

namespace tracker {
// 每个数组固定为简中、日、英、繁中、德、法、西、韩；类型检查防止遗漏语言列。
inline const char* LocalizedName(const char* const (&names)[kLanguageCount]) noexcept {
    return Localize(names[0], names[1], names[2], names[3], names[4], names[5], names[6], names[7]);
}

// 模板不要求公开 CI 持有 MapDefinition/游戏目录；真实调用者传入生成的地图定义。
// 使用稳定 key 查找而非翻译后的名称或索引，防止同名道路跨地区归属混淆。
template<class Map>
inline const char* MapPath(const Map& map) noexcept {
#if SKY2_HAS_MAP_LOCALIZATION
    if (map.key) for (const auto& row : kLocalizedMaps)
        if (std::strcmp(row.key, map.key) == 0) return LocalizedName(row.paths);
#endif
    return map.path ? map.path : "";
}

inline const char* RegionName(uint32_t id) noexcept {
#if SKY2_HAS_TRAVEL_LOCALIZATION
    for (const auto& row : kLocalizedRegions)
        if (row.id == id) return LocalizedName(row.names);
#else
    (void)id;
#endif
    // 未知地区不编造译名；界面另外显示原始场景编号，供玩家识别记录。
    return Localize("记录地点", "記録地点", "Recorded location", "記錄地點",
                    "Gespeicherter Ort", "Lieu enregistré", "Lugar guardado", "기록된 장소");
}

inline const char* DestinationName(const RevisitDestination& destination) noexcept {
    // 返程是 Mod 自有动作，并非原生地点，因此使用界面翻译而不是伪造资源记录。
    if (destination.id == kRevisitReturnTarget)
        return Localize("返回记录的出发点", "記録した出発地点へ戻る", "Return to recorded starting point",
                        "返回記錄的出發點", "Zum gespeicherten Ausgangspunkt zurückkehren",
                        "Revenir au point de départ enregistré", "Volver al punto de partida guardado",
                        "기록된 출발 지점으로 돌아가기");
#if SKY2_HAS_TRAVEL_LOCALIZATION
    for (const auto& row : kLocalizedTravel)
        if (row.id == destination.id) return LocalizedName(row.names);
#endif
    return destination.name ? destination.name : "";
}

inline const char* DestinationGroup(const RevisitDestination& destination) noexcept {
    if (destination.id == kRevisitReturnTarget)
        return Localize("返程", "帰還", "Return", "返程", "Rückkehr", "Retour", "Regreso", "귀환");
#if SKY2_HAS_TRAVEL_LOCALIZATION
    for (const auto& row : kLocalizedTravel)
        if (row.id == destination.id) return LocalizedName(row.groups);
#endif
    return destination.group ? destination.group : "";
}

// 返程记录保存的是整张原生场景及站位地点 ID；同一室外场景常包含城市和多条道路。
// 必须先匹配 mapPlace + scene，不能把同场景宝箱清单的第一条路名当成玩家出发点。
// 模板允许公开测试注入小型目录，不要求 CI 下载、持有或虚构完整游戏资源。
template<class Maps, class TravelRows>
inline const char* ReturnPointDisplayName(const RevisitReturnPoint& point, const Maps& maps,
                                          const TravelRows& travelRows) noexcept {
    const TravelCatalogRow* exact = nullptr;
    bool ambiguousPlace = false;
    for (const auto& row : travelRows) {
        if (row.place != point.mapPlace || !row.scene || std::strcmp(row.scene, point.scene) != 0 ||
            !DisplayableTravelRow(row)) continue;
        // 相同原生 ID 的入口变体仍代表同一地点；不同 ID 共用地点编号时，不猜测入口
        // 或最深处，例如水道两端。之后优先退回唯一迷宫统计组，再退回官方地区名。
        if (!exact) exact = &row;
        else if (exact->id != row.id) ambiguousPlace = true;
    }
    if (exact && !ambiguousPlace) {
        const RevisitDestination destination{exact->id, exact->name, exact->scene, 0, exact->group};
        return DestinationName(destination);
    }
    const char* uniqueMap = nullptr;
    for (const auto& map : maps) {
        if (!map.scene || std::strcmp(map.scene, point.scene) != 0) continue;
        if (uniqueMap) return RegionName(point.region);
        uniqueMap = MapPath(map);
    }
    // 界面下一行仍显示原始场景编号和记录时间，地区回退不冒充具体道路或建筑。
    return uniqueMap ? uniqueMap : RegionName(point.region);
}

template<class Maps>
inline const char* ReturnPointDisplayName(const RevisitReturnPoint& point, const Maps& maps) noexcept {
    return ReturnPointDisplayName(point, maps, kTravelCatalog);
}
} // namespace tracker
