"""只读提取游戏支持的全部八种语言的地点名称，供地图和传送目录共同使用。

名称必须按同一资源 ID 对齐；这里不翻译专名、不移除调试标记来伪造玩家名称。
仅生成实际使用的名称及来源信息，不输出完整游戏表，也不读取存档或游戏进程。
"""
from dataclasses import dataclass
from pathlib import Path
import hashlib
import json
import struct

from formats import Fpac


# 语言数组顺序是生成头文件与运行时约定，不能按系统区域或字母顺序重新排列。
LANGUAGES = ("sc", "jp", "en", "tc", "de", "fr", "es", "ko")
TABLE_PACKAGES = {language: "table" if language == "jp" else f"table_{language}"
                  for language in LANGUAGES}
FOREST_TARGET = 0xFFFFFFFD
FOREST_PLACE = 1008100


def read_places(data: bytes) -> list[dict]:
    """核验已支持版本的地点表布局，完整保留原文和重复 ID 对应的场景变体。"""
    if (len(data) < 88 or data[:8] != b"#TBL\x01\x00\x00\x00" or
            data[8:72].split(b"\0")[0] != b"PlaceTableData"):
        raise ValueError("地点表头不属于当前版本")
    _, start, stride, count = struct.unpack_from("<4I", data, 72)
    pool_start = start + stride * count
    if start < 88 or stride != 168 or count != 616 or pool_start > len(data):
        raise ValueError("地点表记录大小或数量发生变化")

    def string(at: int) -> str:
        """名称和场景指针均须指向记录之后的数据池；严格解码避免替代字符入库。"""
        offset = struct.unpack_from("<Q", data, at)[0]
        if not pool_start <= offset < len(data):
            raise ValueError("地点名称指针越界")
        end = data.find(b"\0", offset, min(offset + 4096, len(data)))
        if end < 0:
            raise ValueError("地点名称缺少结束符或过长")
        value = data[offset:end].decode("utf-8")
        if any(ord(char) < 32 for char in value):
            raise ValueError("地点名称或场景包含控制字符")
        return value

    return [{"id": struct.unpack_from("<I", data, at)[0], "scene": string(at + 8),
             "submap": string(at + 16), "name": string(at + 96)}
            for at in range(start, pool_start, stride)]


def unique_place_name(places: list[dict], place_id: int, scene: str | None = None) -> str:
    """只接受唯一原生名称；相同 ID 的不同楼层不能被静默取第一行。"""
    names = {row["name"] for row in places if row["id"] == place_id and
             (scene is None or row["scene"] == scene)}
    if len(names) != 1:
        raise ValueError(f"地点名称缺失或有歧义：{place_id} / {scene}: {names}")
    name = next(iter(names))
    if not name or name.startswith("◆"):
        raise ValueError(f"地点 {place_id} 不具备可展示的原生名称")
    return name


def read_liber_ark_name(data: bytes) -> str:
    """读取战斗手册地区分类的 ID 7，避开地点表里未本地化的调试用地区名。

    t_place 的简中、日文 mp5000 主行仍是『◆リベルアーク』。游戏实际面向
    玩家展示的地区名在 t_notemenu/NoteBattleCategory 中，不能拿调试名自译。
    """
    if len(data) < 8 or data[:4] != b"#TBL":
        raise ValueError("手册表头不完整或类型错误")
    sections = struct.unpack_from("<I", data, 4)[0]
    header_end = 8 + 80 * sections
    if not 1 <= sections <= 64 or header_end > len(data):
        raise ValueError("手册表描述符数量越界")
    descriptors, occupied = [], []
    for index in range(sections):
        at = 8 + 80 * index
        raw_name = data[at:at + 64]
        if b"\0" not in raw_name:
            raise ValueError("手册表类型名缺少结束符")
        name = raw_name.split(b"\0", 1)[0].decode("ascii")
        _, start, stride, count = struct.unpack_from("<4I", data, at + 64)
        end = start + stride * count
        if not 0 < stride <= 4096 or count > 10000 or start < header_end or end > len(data):
            raise ValueError("手册表记录区间越界")
        if count and any(start < right and left < end for left, right in occupied):
            raise ValueError("手册表记录区间重叠")
        if count:
            occupied.append((start, end))
        descriptors.append((name, start, stride, count))
    selected = [row for row in descriptors if row[0] == "NoteBattleCategory"]
    if len(selected) != 1 or selected[0][2:] != (24, 8):
        raise ValueError("手册地区分类布局发生变化")
    _, start, stride, count = selected[0]
    rows = [start + index * stride for index in range(count)
            if struct.unpack_from("<I", data, start + index * stride)[0] == 7]
    if len(rows) != 1:
        raise ValueError("手册中利贝尔方舟地区 ID 缺失或重复")
    offset = struct.unpack_from("<Q", data, rows[0] + 8)[0]
    pool_start = max([header_end] + [end for _, end in occupied])
    if not pool_start <= offset < len(data):
        raise ValueError("手册地区名称指针越界")
    end = data.find(b"\0", offset, min(offset + 1024, len(data)))
    if end < 0:
        raise ValueError("手册地区名称缺少结束符或过长")
    value = data[offset:end].decode("utf-8")
    if not value or value.startswith("◆") or any(ord(char) < 32 for char in value):
        raise ValueError("手册地区名称不是可展示原文")
    return value


def region_names(places: list[dict], liber_ark: str, language: str) -> dict[int, str]:
    """用正式地点 ID 取得地区名，船名仅截取原文中的实体名称，不翻译或改写。

    研究所直接使用正式地点名，替代旧版 Mod 自编的『研究所区域』。
    荣耀号甲板行以各语言原生分隔符划分船名和甲板名；要求明确存在两个部分。
    """
    result = {region: unique_place_name(places, 900000 + region * 100000)
              for region in range(1, 6)}
    result[6] = unique_place_name(places, 1601000)
    result[7] = liber_ark
    deck = unique_place_name(places, 1850000)
    # 游戏德文使用 en dash（U+2013），英/法/西文使用 ASCII 连字符，东亚语言
    # 使用日文中点。仅按已核对的各语言格式切分，避免把船名中的字词自行改写。
    if language not in LANGUAGES:
        raise ValueError(f"不支持的名称资源语言：{language}")
    separator = " – " if language == "de" else " - " if language in ("en", "fr", "es") else "・"
    parts = deck.split(separator)
    if len(parts) != 2 or not all(parts):
        raise ValueError("荣耀号甲板名称格式变化，不能确认原生船名")
    result[8] = parts[0]
    result[9] = unique_place_name(places, 1610001)
    return result


def scene_region(scene: str) -> int:
    """映射已核对的地图系列到原生地区号；未知场景直接失败，不猜测所属地区。"""
    if scene.startswith("mp601"):
        return 6
    if scene.startswith("mp610"):
        return 9
    if scene.startswith("mp85"):
        return 8
    if len(scene) >= 3 and scene.startswith("mp") and scene[2] in "012345":
        return 7 if scene[2] == "5" else int(scene[2]) + 1
    raise ValueError(f"未核对的大地图归属：{scene}")


@dataclass(frozen=True)
class LanguageResources:
    """单种语言的已核验最小资源集合；原始传送表仅交给现有严格解析器消费。"""
    places: list[dict]
    regions: dict[int, str]
    travel: bytes
    sources: dict[str, str]


def load_language_resources(game: Path) -> dict[str, LanguageResources]:
    """要求八语资源全部存在且地点身份逐行一致，防止缺失语言时误标为已翻译。"""
    result = {}
    for language in LANGUAGES:
        package = TABLE_PACKAGES[language]
        archive = Fpac(game / "pac/steam" / f"{package}.pac")
        tables = {name: archive.read(f"{package}/{name}.tbl")
                  for name in ("t_place", "t_notemenu", "t_mapjump")}
        places = read_places(tables["t_place"])
        if result:
            identity = lambda rows: [(row["id"], row["scene"], row["submap"]) for row in rows]
            if identity(places) != identity(result["sc"].places):
                raise ValueError(f"{language} 地点身份与简中表不一致，不能按位置配对名称")
        regions = region_names(places, read_liber_ark_name(tables["t_notemenu"]), language)
        sources = {f"{package}/{name}.tbl": hashlib.sha256(data).hexdigest()
                   for name, data in tables.items()}
        result[language] = LanguageResources(places, regions, tables["t_mapjump"], sources)
    return result


def cpp_array(values: list[str]) -> str:
    """以 UTF-8 字面量输出八语数组，同时转义引号和反斜杠，禁止直接拼接原始名称。"""
    if len(values) != len(LANGUAGES):
        raise ValueError("名称数组必须依次包含简体中文、日文、英文、繁体中文、德文、法文、西班牙文、韩文")
    return "{" + ", ".join(json.dumps(value, ensure_ascii=False) for value in values) + "}"
