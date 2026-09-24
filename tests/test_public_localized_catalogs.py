"""八语名称生成测试：公开测试使用合成数据，可选本机目录检查真实资源覆盖。"""
from pathlib import Path
import json
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from extract_map_catalog import build_localized_maps, write_localized_maps
from extract_travel_catalog import build_localized_travel, write_localized_travel
from localized_game_names import (FOREST_PLACE, FOREST_TARGET, LANGUAGES, LanguageResources,
                                  cpp_array, read_liber_ark_name, region_names, scene_region,
                                  unique_place_name)


def fixture_resources():
    """八种人工名称故意不互为翻译，用来发现错取中文、臆译和错误的数组顺序。"""
    names = {"sc": ("甲地区", "原始船名・甲板", "原始船名・前部第１层", "原始森林"),
             "jp": ("地方ベータ", "船ガンマ・甲板", "船ガンマ・前部第１層", "森デルタ"),
             "en": ("Region Epsilon", "Ship Zeta - Deck", "Ship Zeta - Bow, 1st Level", "Woods Eta"),
             "tc": ("戊地區", "原始船艦・甲板", "原始船艦・前部第１層", "原始樹林"),
             "de": ("Region Theta", "Schiff Iota – Deck", "Schiff Iota – Bug", "Wald Kappa"),
             "fr": ("Région Lambda", "Navire Mu - pont", "Navire Mu - proue", "Bois Nu"),
             "es": ("Región Xi", "Barco Ómicron - Cubierta", "Barco Ómicron - Proa", "Bosque Pi"),
             "ko": ("로 지구", "시그마・갑판", "시그마・선수", "타우 숲")}
    result = {}
    for language in LANGUAGES:
        region, deck, ship, forest = names[language]
        places = [{"id": 1850000, "scene": "mp8500", "submap": "", "name": deck},
                  {"id": 1850001, "scene": "mp8500_01", "submap": "", "name": ship},
                  {"id": FOREST_PLACE, "scene": "mp0081", "submap": "", "name": forest},
                  {"id": 1206000, "scene": "mp2000", "submap": "mp2060", "name": language + " road"}]
        regions = {index: region + str(index) for index in range(1, 10)}
        result[language] = LanguageResources(places, regions, b"", {language + "/synthetic.tbl": "fixture"})
    return result


def fixture_travel():
    """显式构造相同目的地的多入口及调试行，外文调试行故意不含日文调试标记。"""
    result = {}
    for language in LANGUAGES:
        rows = []
        for index, identifier in enumerate((7, 7, 165, 168)):
            internal = index > 0
            rows.append({"row": index, "id": identifier, "region": 8, "area": 1,
                         "place": 1850001, "scene": "mp8500_01", "name":
                         ("◆" if internal and language != "en" else "") + language + " native " + str(index),
                         "group": "legacy Chinese", "variant": index, "flags": 8 if internal else 0,
                         "map": "mp8500_01", "tags": "E" if internal else "", "preview": "fixture",
                         "extra": "", "yes_flags": [123], "no_flags": [456],
                         "category": "internal" if internal and language != "en" else "menu_row",
                         "kind": 1 if internal and language != "en" else 0})
        result[language] = rows
    return result


def fixture_notemenu():
    """合成完整的八行手册分类表，仅第 7 项是目标，指针采用真实的绝对文件偏移。"""
    start, stride, count = 88, 24, 8
    data = bytearray(start + stride * count)
    data[:8] = b"#TBL" + struct.pack("<I", 1)
    name = b"NoteBattleCategory\0"
    data[8:8 + len(name)] = name
    struct.pack_into("<4I", data, 72, 1, start, stride, count)
    for index in range(count):
        at = start + index * stride
        struct.pack_into("<IQ", data, at, index, 0)
        struct.pack_into("<Q", data, at + 8, len(data))
        data.extend(("Native Ark" if index == 7 else "Unused").encode("utf-8") + b"\0")
    return data


class LocalizedCatalogTests(unittest.TestCase):
    def test_all_game_languages_have_explicit_stable_array_positions(self):
        """运行时枚举依赖固定顺序；禁止字母排序或遗漏新增语言造成错位显示。"""
        self.assertEqual(LANGUAGES, ("sc", "jp", "en", "tc", "de", "fr", "es", "ko"))

    def test_each_missing_language_is_rejected_without_silent_english_fallback(self):
        """缺少任何官方资源时生成必须失败，不能把英文当作该语言已获得支持。"""
        for language in LANGUAGES:
            resources = fixture_resources()
            del resources[language]
            with self.subTest(language=language), self.assertRaisesRegex(ValueError, "覆盖"):
                build_localized_maps([], resources)
            with self.subTest(language=language), self.assertRaisesRegex(ValueError, "覆盖"):
                build_localized_travel(resources, fixture_travel())

    def test_all_native_ship_name_separators_preserve_original_spelling(self):
        """按资源中真实的中点、连字符或 en dash 分隔；不把船名改写为英文名称。"""
        expected = {"sc": "原始船名", "jp": "船ガンマ", "en": "Ship Zeta", "tc": "原始船艦",
                    "de": "Schiff Iota", "fr": "Navire Mu", "es": "Barco Ómicron", "ko": "시그마"}
        for language, resources in fixture_resources().items():
            places = list(resources.places)
            places += [{"id": 900000 + region * 100000, "scene": "mp0000", "submap": "",
                        "name": f"{language} region {region}"} for region in range(1, 6)]
            places += [{"id": 1601000, "scene": "mp6010", "submap": "", "name": language + " training"},
                       {"id": 1610001, "scene": "mp6100_01", "submap": "", "name": language + " lab"}]
            regions = region_names(places, language + " ark", language)
            with self.subTest(language=language):
                self.assertEqual(regions[8], expected[language])
                self.assertEqual(regions[7], language + " ark")
            # 用错误的分隔符替换甲板行，应拒绝生成，不能靠相似字形盲目切分。
            places[0] = dict(places[0], name=expected[language] + " :: wrong separator")
            with self.subTest(language=language), self.assertRaisesRegex(ValueError, "格式变化"):
                region_names(places, language + " ark", language)

    def test_maps_use_exact_native_strings_and_keep_group_order(self):
        """包含楼层的官方名称不被改写为『各层』；地区路径仍按相同分组顺序生成。"""
        resources = fixture_resources()
        groups = [{"key": "mp8500_01", "scene": "mp8500_01"},
                  {"key": "mp2000:MaiveSeasideWay", "scene": "mp2000"}]
        rows = build_localized_maps(groups, resources)
        self.assertEqual([row["key"] for row in rows], [group["key"] for group in groups])
        for language in LANGUAGES:
            self.assertEqual(rows[0]["names"][language], resources[language].places[1]["name"])
            self.assertEqual(rows[1]["names"][language], language + " road")
            self.assertEqual(rows[0]["paths"][language], resources[language].regions[8] + " / " +
                             resources[language].places[1]["name"])

    def test_map_missing_language_and_ambiguous_place_fail(self):
        resources = fixture_resources()
        with self.assertRaisesRegex(ValueError, "覆盖"):
            build_localized_maps([], {"sc": resources["sc"]})
        resources["en"].places.append({"id": 1850001, "scene": "mp8500_01", "submap": "", "name": "Other"})
        with self.assertRaisesRegex(ValueError, "歧义"):
            build_localized_maps([{"key": "mp8500_01", "scene": "mp8500_01"}], resources)

    def test_travel_keeps_native_names_and_aligns_variants_by_identity(self):
        """同 ID 入口变体不能多显示一次；森林本体名称不夹带自译的 Mod 用途后缀。"""
        resources = fixture_resources()
        rows = build_localized_travel(resources, fixture_travel())
        self.assertEqual([row["id"] for row in rows], [7, 165, FOREST_TARGET])
        for language in LANGUAGES:
            self.assertEqual(rows[0]["names"][language], language + " native 0")
            self.assertEqual(rows[1]["names"][language], resources[language].places[1]["name"])
            self.assertEqual(rows[2]["names"][language], resources[language].places[2]["name"])
        self.assertEqual(rows[0]["source"]["table"], "t_mapjump.tbl")
        self.assertEqual(rows[1]["source"]["table"], "t_place.tbl")

    def test_english_debug_names_do_not_change_reviewed_target_scope(self):
        """英文内部行没有 ◆；仍不得因为看起来像正式地名而公开未适配的 ID 168。"""
        rows = build_localized_travel(fixture_resources(), fixture_travel())
        self.assertNotIn(168, [row["id"] for row in rows])
        self.assertTrue(all(not name.startswith("◆") for row in rows for name in row["names"].values()))

    def test_travel_rejects_resource_identity_condition_and_count_drift(self):
        """即使显示名称可用，场景、变体、剧情条件或行数不同也不能配对。"""
        for key, value in (("scene", "mp1000"), ("variant", 11), ("yes_flags", [999]), ("id", 10)):
            parsed = fixture_travel()
            parsed["en"][0][key] = value
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, "不一致"):
                build_localized_travel(fixture_resources(), parsed)
        parsed = fixture_travel()
        parsed["jp"].pop()
        with self.assertRaisesRegex(ValueError, "不一致"):
            build_localized_travel(fixture_resources(), parsed)

    def test_travel_rejects_missing_locale_and_reviewed_native_name(self):
        resources = fixture_resources()
        with self.assertRaisesRegex(ValueError, "覆盖"):
            build_localized_travel(resources, {"sc": fixture_travel()["sc"]})
        resources["jp"].places.pop(1)
        with self.assertRaisesRegex(ValueError, "缺失"):
            build_localized_travel(resources, fixture_travel())

    def test_place_identity_ambiguity_and_debug_names_are_rejected(self):
        rows = [{"id": 42, "scene": "mp1000", "name": "One"},
                {"id": 42, "scene": "mp1001", "name": "Two"}]
        self.assertEqual(unique_place_name(rows, 42, "mp1000"), "One")
        with self.assertRaisesRegex(ValueError, "歧义"):
            unique_place_name(rows, 42)
        rows[0]["name"] = "◆調整用"
        with self.assertRaisesRegex(ValueError, "可展示"):
            unique_place_name(rows, 42, "mp1000")

    def test_notemenu_native_region_is_read_by_id(self):
        self.assertEqual(read_liber_ark_name(fixture_notemenu()), "Native Ark")

    def test_notemenu_rejects_bad_schema_record_pointer_and_duplicate_id(self):
        for mutation in ("stride", "pointer", "duplicate", "truncated", "debug"):
            data = fixture_notemenu()
            if mutation == "stride":
                struct.pack_into("<I", data, 80, 23)
            elif mutation == "pointer":
                struct.pack_into("<Q", data, 88 + 7 * 24 + 8, 88)
            elif mutation == "duplicate":
                struct.pack_into("<I", data, 88, 7)
            elif mutation == "truncated":
                data = data[:-1]
            else:
                struct.pack_into("<Q", data, 88 + 7 * 24 + 8, len(data))
                data.extend("◆内部\0".encode("utf-8"))
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                read_liber_ark_name(data)

    def test_scene_regions_reject_unknown_prefix(self):
        for scene, region in (("mp0000", 1), ("mp6011", 6), ("mp6100_01", 9),
                              ("mp8500_01", 8), ("mp5310_04", 7)):
            self.assertEqual(scene_region(scene), region)
        with self.assertRaisesRegex(ValueError, "未核对"):
            scene_region("mp9999")

    def test_generated_headers_are_self_contained_and_escape_strings(self):
        resources = fixture_resources()
        resources["en"].places[1]["name"] = 'Native "quote" \\ bow'
        maps = build_localized_maps([{"key": "mp8500_01", "scene": "mp8500_01"}], resources)
        travel = build_localized_travel(resources, fixture_travel())
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            write_localized_maps(maps, output)
            write_localized_travel(travel, resources, output)
            self.assertIn("namespace tracker", (output / "map_localization.h").read_text(encoding="utf-8"))
            header = (output / "travel_localization.h").read_text(encoding="utf-8")
            self.assertIn('Native \\"quote\\" \\\\ bow', header)
            self.assertIn("LocalizedRegionDefinition", header)
            result = json.loads((output / "travel_localization.json").read_text(encoding="utf-8"))
            self.assertEqual(result["languages"], list(LANGUAGES))
            self.assertEqual(result["rows"], travel)
            self.assertEqual(len(result["regions"]), 9)
        with self.assertRaisesRegex(ValueError, "依次"):
            cpp_array(["missing languages"])


@unittest.skipUnless((ROOT / "data/generated/travel_localization.json").exists(),
                     "需要先从自己的游戏生成八语名称目录")
class LocalGeneratedNameCoverage(unittest.TestCase):
    def test_all_visible_destinations_have_eight_official_names(self):
        """本机可选验证：名称旁路应覆盖 155 个可展示原生 ID 和森林，不能仅抽样翻译。"""
        generated = ROOT / "data/generated"
        original = json.loads((generated / "travel.json").read_text(encoding="utf-8"))["rows"]
        localized = json.loads((generated / "travel_localization.json").read_text(encoding="utf-8"))["rows"]
        visible = {row["id"] for row in original if (row["kind"] != 1 and not row["flags"] & 8) or
                   row["id"] in (165, 166)}
        self.assertEqual({row["id"] for row in localized}, visible | {FOREST_TARGET})
        self.assertEqual(len(localized), 156)
        for row in localized:
            self.assertEqual(set(row["names"]), set(LANGUAGES))
            self.assertEqual(set(row["groups"]), set(LANGUAGES))
            self.assertTrue(all(name and not name.startswith("◆") for name in row["names"].values()))

    def test_all_chest_groups_have_native_multilingual_paths(self):
        """名称改动不改变 566 个宝箱的归组；同名普通塔和异空间仍能以原文区分。"""
        result = json.loads((ROOT / "data/generated/maps.json").read_text(encoding="utf-8"))
        self.assertEqual([row["key"] for row in result["maps"]],
                         [row["key"] for row in result["localized_maps"]])
        self.assertEqual((len(result["localized_maps"]), len(result["chest_map_indices"])), (59, 566))
        for row in result["localized_maps"]:
            for language in LANGUAGES:
                self.assertTrue(row["names"][language])
                self.assertEqual(row["paths"][language], row["regions"][language] + " / " + row["names"][language])


if __name__ == "__main__":
    unittest.main()
