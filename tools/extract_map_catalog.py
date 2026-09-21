"""将本机宝箱目录关联到简体中文地图名称，生成收集清单的稳定分组。

室外大区包含多条道路，按已核对的宝箱命名前缀拆分；迷宫保留整图汇总。
分组只影响显示与统计，不改变宝箱原始行号、开箱标志或地图图标逻辑。
"""
from pathlib import Path
import argparse
import json
import struct
from formats import Fpac


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


def read_places(data: bytes) -> list[dict]:
    """验证地点表布局与每个字符串指针，再读取分组所需的少数字段。"""
    if len(data) < 88 or data[:8] != b"#TBL\x01\x00\x00\x00" or data[8:72].split(b"\0")[0] != b"PlaceTableData":
        raise ValueError("地点表头不属于当前版本")
    _, start, stride, count = struct.unpack_from("<4I", data, 72)
    if stride != 168 or count != 616 or start + stride * count > len(data):
        raise ValueError("地点表记录大小或数量发生变化")

    def string(at: int) -> str:
        offset = struct.unpack_from("<Q", data, at)[0]
        if not start + stride * count <= offset < len(data):
            raise ValueError("地点名称指针越界")
        end = data.find(b"\0", offset)
        if end < 0:
            raise ValueError("地点名称缺少结束符")
        return data[offset:end].decode("utf-8")

    return [{"id": struct.unpack_from("<I", data, at)[0], "scene": string(at + 8),
             "submap": string(at + 16), "name": string(at + 96)}
            for at in range(start, start + stride * count, stride)]


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


def extract(game: Path, catalog_path: Path, out: Path) -> None:
    """输出 JSON 供离线核对，以及无需游戏运行时加载文件的 C++ 常量表。"""
    table = Fpac(game / "pac/steam/table_sc.pac").read("table_sc/t_place.tbl")
    result = build_groups(json.loads(catalog_path.read_text(encoding="utf-8")), read_places(table))
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
    print(json.dumps({"maps": len(result["maps"]), "chests": len(result["chest_map_indices"])}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, required=True)
    parser.add_argument("--catalog", type=Path, default=Path("data/generated/chests.json"))
    parser.add_argument("--out", type=Path, default=Path("data/generated"))
    args = parser.parse_args()
    extract(args.game, args.catalog, args.out)
