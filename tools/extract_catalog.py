"""从玩家本机资源生成宝箱目录及原生插件使用的标志表。

只解码每个场景的 Actor 子树，避免展开光照／导航数据；保留所有位置变体。
游戏表格的行序号参与标志 ID 计算，绝不能过滤后重新编号。
"""
from pathlib import Path
import argparse
import collections
import hashlib
import json
import struct
from formats import Fpac, Bjson, plain


def extract(game: Path, output: Path):
    """读取指定版本，输出 JSON、C++ 头文件和关联验证结果。"""
    pac_root = game / "pac" / "steam"
    table = Fpac(pac_root / "table_en.pac").read("table_en/t_tbox.tbl")
    if table[:4] != b"#TBL" or struct.unpack_from("<I", table, 4)[0] != 1:
        raise ValueError("宝箱表头发生变化，停止生成")
    if table[8:72].split(b"\0")[0] != b"TBoxParam":
        raise ValueError("宝箱表类型发生变化")
    _, start, stride, count = struct.unpack_from("<4I", table, 72)
    if stride != 120 or start + count * stride > len(table):
        raise ValueError("本版本宝箱行结构不受支持")

    def string(offset: int) -> str:
        end = table.find(b"\0", offset)
        if offset < start + count * stride or end < offset:
            raise ValueError("宝箱表字符串指针越界")
        return table[offset:end].decode("utf-8")

    records = []
    excluded = []
    for index in range(count):
        row = table[start + index * stride:start + (index + 1) * stride]
        map_id, name = (string(struct.unpack_from("<Q", row, offset)[0]) for offset in (0, 8))
        flags = string(struct.unpack_from("<Q", row, 24)[0])
        opened_override, discovered_override = struct.unpack_from("<HH", row, 40)
        # 游戏仅在两个覆盖值同时非零时采用它们；只提供一个仍走默认编号。
        use_override = bool(opened_override and discovered_override)
        record = {"row": index, "key": map_id + "/" + name, "map_id": map_id, "name": name,
                  "attributes": flags, "open_flag": opened_override if use_override else 1500 + index,
                  "discovered_flag": discovered_override if use_override else 2100 + index,
                  "inherited_flag": None if use_override else 2700 + index}
        if "D" in flags:
            excluded.append(record)
        else:
            records.append(record)

    # 与已核对的 566 目标不一致时停止，防止更新或其他 Mod 改表后产生错误标志。
    if len(records) != 566 or len({r["key"] for r in records}) != len(records):
        raise ValueError("正式宝箱数量／标识不符合当前适配版本")
    scenes = Fpac(pac_root / "scene.pac")
    by_name = collections.defaultdict(list)
    scene_errors = []
    for name, entry in scenes.entries.items():
        if not name.endswith(".json"):
            continue
        try:
            decoder = Bjson(scenes.read(name))
            for child in decoder.node(decoder.root_offset)["children"]:
                node = decoder.node(child)
                if node.get("name") != "Actor":
                    continue
                for actor_offset in node["children"]:
                    actor = plain(decoder.tree(actor_offset))
                    if isinstance(actor, dict) and actor.get("type") == "MapObject" and actor.get("obj_type") == 5:
                        by_name[actor["name"]].append({"scene": name, "actor_id": int(actor["id"]),
                                                      "position": actor["translation"],
                                                      "rotation": actor["rotation"], "model": actor["model_path"]})
        except ValueError as error:
            scene_errors.append({"scene": name, "error": str(error)})
    missing = []
    for record in records:
        matches = by_name[record["name"]]
        # 同名箱可能分属于两张地图；优先选择同一系统场景，防止位置串图。
        exact = [m for m in matches if m["scene"] == f"scene/{record['map_id']}_sys.json"]
        record["placements"] = exact or matches
        if not record["placements"]:
            missing.append(record["key"])
    if missing:
        raise ValueError(f"存在未找到场景坐标的宝箱：{missing}")
    output.mkdir(parents=True, exist_ok=True)
    result = {"schema_version": 1, "exe_sha256": hashlib.sha256((game / "sora_2nd.exe").read_bytes()).hexdigest(),
              "tbox_sha256": hashlib.sha256(table).hexdigest(), "count": len(records),
              "excluded_development_rows": excluded, "scene_errors": scene_errors, "chests": records}
    (output / "chests.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    lines = ["// 从本机 t_tbox.tbl 自动生成；原始表行号决定标志编号，禁止手工重排。", "#pragma once",
             "#include <cstdint>", "struct ChestRecord { uint32_t row, opened, discovered, inherited; const char* map; const char* name; };",
             "inline constexpr ChestRecord kChests[] = {"]
    for r in records:
        lines.append(f"    {{{r['row']}, {r['open_flag']}, {r['discovered_flag']}, {r['inherited_flag'] or 0}, {json.dumps(r['map_id'])}, {json.dumps(r['name'])}}},")
    lines += ["};", f"inline constexpr char kExeSha256[] = {json.dumps(result['exe_sha256'])};"]
    (output / "chest_catalog.h").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(json.dumps({"chests": len(records), "excluded": len(excluded), "scene_errors": len(scene_errors),
                      "placements": sum(len(r['placements']) for r in records), "output": str(output.resolve())}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=Path("data/generated"))
    args = parser.parse_args()
    extract(args.game, args.out)
