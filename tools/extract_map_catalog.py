"""将本机宝箱目录关联到官方八语地图名称，生成收集清单的稳定分组。

室外大区包含多条道路，按已核对的宝箱命名前缀拆分；迷宫保留整图汇总。
分组只影响显示与统计，不改变宝箱原始行号、开箱标志或地图图标逻辑。
"""
from pathlib import Path
import argparse
import json
from localized_game_names import (LANGUAGES, cpp_array, load_language_resources,
                                  read_places, scene_region)


# 显式对应原生地点 ID，避免用中英文模糊匹配把同名道路或隧道归错组。
# 两侧的古罗尼山道属于不同大区，分别保留柏斯、卢安的归属。
SPLIT_PLACES = {
    "mp0000": {"Malga": 1005000, "Milch": 1006000, "Elize": 1004000, "Mistwald": 1008000},
    "mp1000": {"EastBoseHwy": 1103000, "EisenRoad": 1106000, "WestBoseHwy": 1104000,
               "NewAnselPath": 1107000, "RavennueTrail": 1108000, "KroneTrail": 1109000},
    "mp2000": {"MaiveSeasideWay": 1206000, "ManoriaByroad": 1205000, "VistaForestRd": 1207000,
               "AinaCauseway": 1208000, "KroneTrail": 1204000},
    "mp3000": {"TrattPlainsRd": 1304000, "RitterRoadway": 1305000, "SoldatArmyRoad": 1306000},
    "mp3030": {"KaldiaTunnel": 1303000, "KaldiaLimestone": 1303100},
    "mp4000": {"KirscheAvenue": 1405000, "ErbeScenicRoute": 1406000},
    "mp4030": {"GrancelSewers_W": 1403100, "GrancelSewers_E": 1403200, "GrancelSewers_N": 1403300},
    "mp5000": {"Cradle": 1530000, "Factoria": 1520000},
}


def region_name(scene: str, places: list[dict]) -> str:
    """补充玩家可用于大地图定位的归属，不把研究所误归入训练场。

    普通五地区按场景编号所属系列对应地点表的地区名称；特殊区域独立列出。
    训练场、研究所与荣耀号在 t_mapjump.tbl 中分别属于 6、9、8 组。
    """
    if scene.startswith("mp601"):
        return next(p["name"] for p in places if p["id"] == 1601000)
    if scene.startswith("mp610"):
        return "研究所区域"
    if scene.startswith("mp85"):
        return "荣耀号"
    if scene.startswith("mp5"):
        return "利贝尔方舟"
    for prefix, root in (("mp0", "mp0000"), ("mp1", "mp1000"), ("mp2", "mp2000"),
                         ("mp3", "mp3000"), ("mp4", "mp4000")):
        if scene.startswith(prefix):
            return next(p["name"] for p in places if p["scene"] == root and not p["submap"])
    raise ValueError(f"未核对的大地图归属：{scene}")


def build_groups(catalog: dict, places: list[dict]) -> dict:
    """每个正式宝箱必须唯一归组；无法识别的新命名前缀直接报错，禁止静默漏计。"""
    if len(catalog["chests"]) != 566:
        raise ValueError("宝箱目录必须包含 566 个正式目标")
    groups, membership = [], []
    by_key = {}
    for chest in catalog["chests"]:
        scene = chest["map_id"]
        prefix = chest["name"].split("_Treasure")[0]
        if scene in SPLIT_PLACES:
            if prefix not in SPLIT_PLACES[scene]:
                raise ValueError(f"未核对的宝箱前缀：{chest['key']}")
            place_id = SPLIT_PLACES[scene][prefix]
            candidates = [p for p in places if p["scene"] == scene and p["id"] == place_id]
            key = scene + ":" + prefix
        else:
            candidates = [p for p in places if p["scene"] == scene and not p["submap"]]
            key = scene
        names = {p["name"] for p in candidates}
        if len(names) != 1:
            raise ValueError(f"地图名称缺失或有歧义：{key} {names}")
        name = next(iter(names))
        if prefix == "KroneTrail":
            name += "（卢安）" if scene == "mp2000" else "（柏斯）"
        # 原生大地图的默认名称只写第一层，但该场景同时包含其他楼层。
        if scene in ("mp8500_01", "mp8500_02"):
            name = name.replace("第１层", "（各层）")
        if name.startswith("◆"):
            raise ValueError(f"不应向玩家展示开发用地图名称：{key}")
        if key not in by_key:
            by_key[key] = len(groups)
            region = region_name(scene, places)
            groups.append({"key": key, "scene": scene, "name": name, "region": region,
                           "path": region + " / " + name, "rows": []})
        index = by_key[key]
        groups[index]["rows"].append(chest["row"])
        membership.append(index)
    rows = [row for group in groups for row in group["rows"]]
    if len(set(rows)) != 566 or len(rows) != 566 or any(not g["rows"] for g in groups):
        raise ValueError("分组出现重复、遗漏或空组")
    return {"schema_version": 2, "maps": groups, "chest_map_indices": membership}


def build_localized_maps(groups: list[dict], resources: dict) -> list[dict]:
    """只本地化展示文字，不改变宝箱分组、场景标识或统计下标。

    道路使用显式地点 ID；迷宫要求同场景的原生主行名称唯一。所有名称完整
    保留游戏原文，不添加中文『各层』『卢安』等曾经使用的 Mod 展示别名。
    跨楼层统计的含义应由界面自己的多语言说明表达，而不是改写官方地名。
    """
    if set(resources) != set(LANGUAGES):
        raise ValueError("地图名称资源必须覆盖简体中文、日文、英文、繁体中文、德文、法文、西班牙文、韩文")
    result = []
    for group in groups:
        scene, key = group["scene"], group["key"]
        localized = {"key": key, "scene": scene, "names": {}, "regions": {}, "paths": {},
                     "source": {"table": "t_place.tbl", "scene": scene}}
        for language in LANGUAGES:
            source = resources[language]
            if scene in SPLIT_PLACES:
                prefix = key.partition(":")[2]
                if prefix not in SPLIT_PLACES[scene]:
                    raise ValueError(f"多语言地图包含未核对前缀：{key}")
                identifier = SPLIT_PLACES[scene][prefix]
                candidates = [p for p in source.places if p["scene"] == scene and p["id"] == identifier]
                localized["source"]["id"] = identifier
            else:
                candidates = [p for p in source.places if p["scene"] == scene and not p["submap"]]
            names = {p["name"] for p in candidates}
            if len(names) != 1 or not next(iter(names)) or next(iter(names)).startswith("◆"):
                raise ValueError(f"{language} 地图名称缺失、有歧义或属于内部行：{key}")
            name = next(iter(names))
            region = source.regions[scene_region(scene)]
            localized["names"][language] = name
            localized["regions"][language] = region
            localized["paths"][language] = region + " / " + name
        result.append(localized)
    return result


def write_localized_maps(rows: list[dict], output: Path) -> None:
    """生成自包含旁路头文件；旧 MapDefinition 的布局和中文兼容字段保持不变。"""
    lines = ["// 从玩家本机官方地点资源生成；数组顺序固定为简体中文、日文、英文、繁体中文、德文、法文、西班牙文、韩文。",
             "#pragma once", "namespace tracker {",
             "struct LocalizedMapDefinition { const char* key; const char* names[8]; const char* regions[8]; const char* paths[8]; };",
             "inline constexpr LocalizedMapDefinition kLocalizedMaps[] = {"]
    for row in rows:
        arrays = ", ".join(cpp_array([row[key][language] for language in LANGUAGES])
                           for key in ("names", "regions", "paths"))
        lines.append("    {" + json.dumps(row["key"], ensure_ascii=False) + ", " + arrays + "},")
    lines += ["};", "}", ""]
    (output / "map_localization.h").write_text("\n".join(lines), encoding="utf-8")


def extract(game: Path, catalog_path: Path, out: Path) -> None:
    """输出 JSON 供离线核对，以及无需游戏运行时加载文件的 C++ 常量表。"""
    resources = load_language_resources(game)
    result = build_groups(json.loads(catalog_path.read_text(encoding="utf-8")), resources["sc"].places)
    result["schema_version"] = 3
    result["languages"] = list(LANGUAGES)
    result["localized_maps"] = build_localized_maps(result["maps"], resources)
    result["localization_sources"] = {language: source.sources for language, source in resources.items()}
    out.mkdir(parents=True, exist_ok=True)
    (out / "maps.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    lines = ["// 从本机地点表生成；每个宝箱仅归属一个统计地图，道路按原生命名前缀拆分。",
             "#pragma once", "#include <cstdint>",
             "struct MapDefinition { const char* key; const char* scene; const char* name; const char* region; const char* path; };",
             "inline constexpr MapDefinition kMaps[] = {"]
    for group in result["maps"]:
        values = ", ".join(json.dumps(group[k], ensure_ascii=False) for k in ("key", "scene", "name", "region", "path"))
        lines.append("    {" + values + "},")
    lines += ["};", "// 下标与 kChests 完全一致，不能按名称重新排序。",
              "inline constexpr uint16_t kChestMapIndices[] = {"]
    for start in range(0, len(result["chest_map_indices"]), 24):
        lines.append("    " + ", ".join(map(str, result["chest_map_indices"][start:start + 24])) + ",")
    lines += ["};"]
    (out / "map_catalog.h").write_text("\n".join(lines) + "\n", encoding="utf-8")
    write_localized_maps(result["localized_maps"], out)
    print(json.dumps({"maps": len(result["maps"]), "chests": len(result["chest_map_indices"]),
                      "languages": list(LANGUAGES)}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, required=True)
    parser.add_argument("--catalog", type=Path, default=Path("data/generated/chests.json"))
    parser.add_argument("--out", type=Path, default=Path("data/generated"))
    args = parser.parse_args()
    extract(args.game, args.catalog, args.out)
