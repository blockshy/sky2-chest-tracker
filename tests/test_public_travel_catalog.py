"""传送目录解析边界测试：仅使用合成表，不读取游戏资源、进程或玩家存档。"""
from pathlib import Path
import json
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from extract_travel_catalog import parse_travel_table, write_catalog, SPOT_FLAGS


def make_table(rows=None, copies=1):
    """构造小型、完整的绝对偏移 TBL，保留变体和条件数组便于定点损坏测试。"""
    if rows is None:
        rows = [dict(id=7, name="合成城市", map="mp1000", tags="F", variant=0),
                dict(id=7, name="◆合成入口分支", map="", tags="E", variant=2),
                dict(id=8, name="合成旧地图", map="", tags="AT", variant=0)]
    stride = 0x98
    header_end = 8 + copies * 80
    data = bytearray(header_end + copies * stride * len(rows))
    data[:8] = b"#TBL" + struct.pack("<I", copies)

    def string(value):
        pointer = len(data)
        data.extend(value.encode("utf-8") + b"\0")
        return pointer

    def array(value):
        pointer = len(data)
        data.extend(struct.pack("<" + "H" * len(value), *value) or b"\0")
        return pointer

    for block in range(copies):
        descriptor = 8 + block * 80
        start = header_end + block * stride * len(rows)
        name = b"MapJumpSpotData\0"
        data[descriptor:descriptor + len(name)] = name
        struct.pack_into("<4I", data, descriptor + 64, 1, start, stride, len(rows))
        for index, source in enumerate(rows):
            at = start + index * stride
            struct.pack_into("<III", data, at, source["id"], 2, 1)
            for offset, value in ((0x10, source["name"]), (0x20, source["map"]),
                                  (0x38, "mp1000"), (0x58, source["tags"]),
                                  (0x68, "jump_fixture"), (0x90, "")):
                struct.pack_into("<Q", data, at + offset, string(value))
            struct.pack_into("<I4f", data, at + 0x40, 1101000, 1.25, 2.5, -3.5, 90.0)
            data[at + 0x60] = source["variant"]
            for offset, values in ((0x70, [123, 456]), (0x80, [789])):
                struct.pack_into("<QQ", data, at + offset, array(values), len(values))
    return data


class TravelCatalogTests(unittest.TestCase):
    def test_keeps_row_order_variant_and_raw_internal_name(self):
        rows = parse_travel_table(make_table())
        self.assertEqual([row["id"] for row in rows], [7, 7, 8])
        self.assertEqual([row["variant"] for row in rows], [0, 2, 0])
        self.assertEqual([row["flags"] for row in rows], [4, 8, 17])
        self.assertEqual([row["category"] for row in rows], ["menu_row", "internal", "no_map"])
        self.assertEqual([row["kind"] for row in rows], [0, 1, 2])
        self.assertEqual(rows[0]["yes_flags"], [123, 456])
        self.assertEqual(rows[0]["no_flags"], [789])
        self.assertEqual(rows[1]["name"], "◆合成入口分支")
        self.assertEqual(rows[0]["group"], "柏斯地区")

    def test_first_matching_section_is_used_without_triplication(self):
        rows = parse_travel_table(make_table(copies=3))
        self.assertEqual(len(rows), 3)
        self.assertEqual(rows[0]["row"], 0)

    def test_all_verified_tag_bits_are_preserved(self):
        rows = [dict(id=1, name="合成测试", map="mp1000", tags="".join(SPOT_FLAGS), variant=255)]
        self.assertEqual(parse_travel_table(make_table(rows))[0]["flags"], 255)

    def test_rejects_unknown_tag_and_region(self):
        with self.assertRaises(ValueError):
            parse_travel_table(make_table([dict(id=1, name="合成", map="mp1000", tags="Z", variant=0)]))
        data = make_table()
        struct.pack_into("<I", data, 88 + 4, 10)
        with self.assertRaises(ValueError):
            parse_travel_table(data)

    def test_rejects_truncation_bad_header_and_stride(self):
        for data in (b"", b"#TBL\x00\x00\x00\x00", bytes(make_table())[:120]):
            with self.subTest(size=len(data)), self.assertRaises(ValueError):
                parse_travel_table(data)
        data = make_table()
        struct.pack_into("<I", data, 80, 151)
        with self.assertRaises(ValueError):
            parse_travel_table(data)

    def test_rejects_record_pointer_as_string_and_unterminated_string(self):
        for pointer in (88, 0, 2**63):
            data = make_table()
            struct.pack_into("<Q", data, 88 + 0x10, pointer)
            with self.subTest(pointer=pointer), self.assertRaises(ValueError):
                parse_travel_table(data)
        data = make_table()
        struct.pack_into("<Q", data, 88 + 0x10, len(data))
        data.extend(b"unterminated")
        with self.assertRaises(ValueError):
            parse_travel_table(data)

    def test_rejects_overlapping_later_sections_and_bad_later_rows(self):
        data = make_table(copies=2)
        first = struct.unpack_from("<I", data, 76)[0]
        struct.pack_into("<I", data, 156, first)
        with self.assertRaises(ValueError):
            parse_travel_table(data)
        data = make_table(copies=2)
        second = struct.unpack_from("<I", data, 156)[0]
        struct.pack_into("<f", data, second + 0x44, float("nan"))
        with self.assertRaises(ValueError):
            parse_travel_table(data)

    def test_rejects_condition_array_overflow(self):
        for pointer, count in ((0, 1), (88, 1), (2**63, 1), (600, 2**63)):
            data = make_table()
            struct.pack_into("<QQ", data, 88 + 0x70, pointer, count)
            with self.subTest(pointer=pointer, count=count), self.assertRaises(ValueError):
                parse_travel_table(data)

    def test_output_requires_no_game_or_private_files(self):
        rows = parse_travel_table(make_table())
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            summary = write_catalog(rows, output)
            self.assertEqual(summary["rows"], 3)
            self.assertEqual(summary["ids"], 2)
            header = (output / "travel_catalog.h").read_text(encoding="utf-8")
            self.assertIn("namespace tracker", header)
            self.assertIn("TravelCatalogRow kTravelCatalog[]", header)
            self.assertNotIn("#include", header)
            self.assertIn('7, 2, 1, 1101000, "mp1000", "合成城市", "柏斯地区", 0, 4, 0', header)
            self.assertIn('7, 2, 1, 1101000, "mp1000", "◆合成入口分支", "柏斯地区", 2, 8, 1', header)
            self.assertEqual(json.loads((output / "travel.json").read_text(encoding="utf-8"))["rows"], rows)


if __name__ == "__main__":
    unittest.main()
