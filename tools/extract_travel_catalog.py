"""从本机原生传送表生成只读目录，保留重复 ID 的入口变体。

目录不是传送许可：是否可选仍须由游戏线程检查本次剧情状态及原生过滤规则。
本工具不读取进程或存档，不复制完整游戏资源；生成结果放入 Git 忽略目录。
"""
from pathlib import Path
import argparse
from collections import Counter
import hashlib
import json
import math
import re
import struct

from formats import Fpac


EXE_SHA256 = "d8b2911d1576216bdc22d070550e4f531e105de7ed2981885849669f4acf8aaf"
# 原生构造器的 ASCII 分派表确认了以下位值，不能把字母序或单独的 E 位猜作全表规则。
SPOT_FLAGS = {"A": 1, "N": 2, "F": 4, "E": 8, "T": 16, "L": 32, "P": 64, "R": 128}
REGIONS = {1: "洛连特地区", 2: "柏斯地区", 3: "卢安地区", 4: "蔡斯地区",
           5: "格兰赛尔地区", 6: "卢・洛克尔训练场", 7: "利贝尔方舟",
           8: "荣耀号", 9: "研究所区域"}
SPOT_STRIDE = 0x98


def parse_travel_table(data: bytes) -> list[dict]:
    """验证描述符、字符串和条件数组后，返回首个 MapJumpSpotData 段的全部行。

    当前资源含多个同名段；原生构造器选择首个匹配段。后续同名段仍检查结构，
    但不合并到运行时目录，否则同一行会被重复三次。允许小型合成表用于公开测试。
    """
    if len(data) < 8 or data[:4] != b"#TBL":
        raise ValueError("传送表头不完整或类型错误")
    sections = struct.unpack_from("<I", data, 4)[0]
    header_end = 8 + sections * 80
    if not 1 <= sections <= 64 or header_end > len(data):
        raise ValueError("传送表描述符数量越界")
    descriptors, occupied = [], []
    for index in range(sections):
        at = 8 + index * 80
        raw_name = data[at:at + 64]
        if b"\0" not in raw_name:
            raise ValueError("传送表类型名缺少结束符")
        name = raw_name.split(b"\0", 1)[0].decode("ascii")
        _, start, stride, count = struct.unpack_from("<4I", data, at + 64)
        end = start + stride * count
        if not 0 < stride <= 4096 or count > 4096 or start < header_end or end > len(data):
            raise ValueError("传送表记录区间越界")
        if count and any(start < other_end and other_start < end for other_start, other_end in occupied):
            raise ValueError("传送表记录区间重叠")
        if count:
            occupied.append((start, end))
        descriptors.append((name, start, stride, count))
    pool_start = max([header_end] + [end for _, end in occupied])

    def text_at(pointer: int) -> str:
        """原始指针必须指向尾部数据池，不能把记录内的零字节误当合法空字符串。"""
        if not pool_start <= pointer < len(data):
            raise ValueError("传送表字符串指针越界")
        end = data.find(b"\0", pointer, min(pointer + 4096, len(data)))
        if end < 0:
            raise ValueError("传送表字符串缺少结束符或过长")
        value = data[pointer:end].decode("utf-8")
        if any(ord(char) < 32 for char in value):
            raise ValueError("传送名称或标签包含控制字符")
        return value

    def condition_at(row: bytes, offset: int) -> list[int]:
        """条件是 uint16 旗标数组；不能按字符串或进程指针解析。"""
        pointer, count = struct.unpack_from("<QQ", row, offset)
        if count > 4096 or (pointer == 0 and count) or (pointer and
                (pointer < pool_start or pointer + count * 2 > len(data))):
            raise ValueError("传送条件数组越界")
        return list(struct.unpack_from("<" + "H" * count, data, pointer)) if count else []

    blocks = []
    for name, start, stride, count in descriptors:
        if name != "MapJumpSpotData":
            continue
        if stride != SPOT_STRIDE or not 1 <= count <= 1024:
            raise ValueError("传送点布局或数量不受支持")
        records = []
        for index in range(count):
            row = data[start + index * stride:start + (index + 1) * stride]
            identifier, region, area = struct.unpack_from("<III", row)
            display_name, map_scene, scene, tags, preview, extra = (
                text_at(struct.unpack_from("<Q", row, offset)[0])
                for offset in (0x10, 0x20, 0x38, 0x58, 0x68, 0x90))
            place = struct.unpack_from("<I", row, 0x40)[0]
            # 0x60 的变体仅为一个字节；保留原生选择顺序，不以 ID 去重或排序。
            variant = row[0x60]
            position = struct.unpack_from("<4f", row, 0x44)
            yes, no = condition_at(row, 0x70), condition_at(row, 0x80)
            if (not 1 <= identifier <= 1000 or region not in REGIONS or area > 64 or
                    not 1 <= place <= 9999999 or not display_name or
                    not re.fullmatch(r"mp[0-9_]{4,18}", scene) or
                    (map_scene and not re.fullmatch(r"mp[0-9_]{4,18}", map_scene))):
                raise ValueError("传送点 ID、地区、场景或名称不受支持")
            if (not all(math.isfinite(v) for v in position) or
                    any(abs(v) >= 100000 for v in position[:3]) or abs(position[3]) > 360):
                raise ValueError("传送落点坐标或角度异常")
            if any(char not in SPOT_FLAGS for char in tags):
                raise ValueError("传送点出现未知标签，必须重新核对原生位映射")
            flags = 0
            for char in tags:
                flags |= SPOT_FLAGS[char]
            # 原名和内部行完整保留，只有展示层可以另选中文别名；本字段不授予许可。
            category = "internal" if display_name.startswith("◆") else "no_map" if not map_scene else "menu_row"
            # 静态分类只供保守过滤：0 普通展示行、1 内部行、2 无地图特殊行。
            # 已审查旧图例外由运行时单独授权，不能把 kind=0 解释为当前剧情可达。
            kind = {"menu_row": 0, "internal": 1, "no_map": 2}[category]
            records.append({"row": index, "id": identifier, "region": region, "area": area,
                            "place": place, "scene": scene, "name": display_name,
                            "group": REGIONS[region], "variant": variant, "flags": flags,
                            "map": map_scene, "tags": tags, "preview": preview, "extra": extra,
                            "yes_flags": yes, "no_flags": no, "category": category, "kind": kind})
        blocks.append(records)
    if not blocks:
        raise ValueError("传送表缺少 MapJumpSpotData")
    return blocks[0]


def write_catalog(rows: list[dict], output: Path) -> dict:
    """生成派生头文件和核对用 JSON；头文件由定义好结构体的 revisit_catalog.h 包含。"""
    summary = {"rows": len(rows), "ids": len({row["id"] for row in rows}),
               "categories": dict(Counter(row["category"] for row in rows)),
               "tags": dict(Counter(row["tags"] for row in rows))}
    output.mkdir(parents=True, exist_ok=True)
    (output / "travel.json").write_text(json.dumps({"schema_version": 1, "summary": summary,
                                                    "rows": rows}, ensure_ascii=False, indent=2), encoding="utf-8")
    lines = ["// 从本机原生传送表生成；保留全部行及入口变体，目录本身不代表剧情许可。",
             "// 包含前须先定义 tracker::TravelCatalogRow，不依赖玩家进程或存档。",
             "#pragma once", "namespace tracker {", "inline constexpr TravelCatalogRow kTravelCatalog[] = {"]
    for row in rows:
        strings = ", ".join(json.dumps(row[key], ensure_ascii=False) for key in ("scene", "name", "group"))
        lines.append("    {" + ", ".join(str(row[key]) for key in ("id", "region", "area", "place")) +
                     ", " + strings + f", {row['variant']}, {row['flags']}, {row['kind']}" + "},")
    lines += ["};", "}", ""]
    (output / "travel_catalog.h").write_text("\n".join(lines), encoding="utf-8")
    return summary


def extract(game: Path, output: Path) -> None:
    """只允许已核对的 EXE 构建，避免新资源布局被误认为已经完成运行时适配。"""
    if hashlib.sha256((game / "sora_2nd.exe").read_bytes()).hexdigest() != EXE_SHA256:
        raise ValueError("EXE 版本不匹配，停止生成传送目录")
    table = Fpac(game / "pac/steam/table_sc.pac").read("table_sc/t_mapjump.tbl")
    print(json.dumps(write_catalog(parse_travel_table(table), output), ensure_ascii=False))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=Path("data/generated"))
    args = parser.parse_args()
    extract(args.game, args.out)
