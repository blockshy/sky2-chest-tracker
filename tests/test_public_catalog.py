"""从开发者自己的游戏生成目录后检查归组，不包含任何玩家进度数据。"""
from pathlib import Path
import json
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from extract_map_catalog import build_groups, region_name


@unittest.skipUnless((ROOT / 'data/generated/maps.json').exists(), '需要先从自己的游戏生成目录')
class CatalogGrouping(unittest.TestCase):
    def setUp(self):
        """仅访问被 Git 忽略的本机生成目录。"""
        self.maps = json.loads((ROOT / 'data/generated/maps.json').read_text(encoding='utf-8'))
        self.catalog = json.loads((ROOT / 'data/generated/chests.json').read_text(encoding='utf-8'))

    def test_unique_membership(self):
        """每只箱恰好计入一个地图，拆分道路后总数仍为 566。"""
        groups = self.maps['maps']
        rows = [row for group in groups for row in group['rows']]
        self.assertEqual((len(groups), len(rows), len(set(rows))), (59, 566, 566))
        self.assertEqual(set(rows), {c['row'] for c in self.catalog['chests']})
        self.assertEqual(len(self.maps['chest_map_indices']), 566)
        for chest, index in zip(self.catalog['chests'], self.maps['chest_map_indices']):
            self.assertIn(chest['row'], groups[index]['rows'])
            self.assertEqual(chest['map_id'], groups[index]['scene'])

    def test_region_paths(self):
        """同名道路和特殊区域的归属不能因前缀简化而混淆。"""
        groups = {g['key']: g for g in self.maps['maps']}
        self.assertEqual(groups['mp2000:AinaCauseway']['path'], '卢安地区 / 阿伊纳街道')
        self.assertEqual(groups['mp1000:KroneTrail']['region'], '柏斯地区')
        self.assertEqual(groups['mp2000:KroneTrail']['region'], '卢安地区')
        for group in groups.values():
            self.assertEqual(group['path'], group['region'] + ' / ' + group['name'])
            if group['scene'].startswith('mp610'):
                self.assertEqual(group['region'], '研究所区域')
            if group['scene'].startswith('mp85'):
                self.assertEqual(group['region'], '荣耀号')

    def test_unknown_data_is_rejected(self):
        """遇到未知地图前缀时显式失败，禁止把新增目标静默漏计。"""
        self.catalog['chests'][0].update(map_id='mp2000', name='UnknownRoad_Treasure00')
        with self.assertRaisesRegex(ValueError, '未核对'):
            build_groups(self.catalog, [])
        with self.assertRaisesRegex(ValueError, '未核对'):
            region_name('mp9999', [])


if __name__ == '__main__':
    unittest.main()
