"""公开资源解析测试只使用合成的损坏数据，不依赖游戏、存档或实机快照。"""
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from formats import Bjson, Fpac


class FormatBoundaries(unittest.TestCase):
    def test_truncated_archive(self):
        """伪造的超长索引必须在读取成员前被拒绝。"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'bad.pac'
            path.write_bytes(struct.pack('<4sIII', b'FPAC', 100, 16, 1))
            with self.assertRaises(ValueError):
                Fpac(path)

    def test_cyclic_bjson(self):
        """自引用节点不能造成递归溢出。"""
        data = bytearray(33)
        data[:4] = b'JSON'
        struct.pack_into('<Q', data, 8, 20)
        struct.pack_into('<BII', data, 24, 0, 1, 24)
        with self.assertRaisesRegex(ValueError, '循环'):
            Bjson(bytes(data)).tree()

    def test_out_of_bounds_bjson(self):
        """文件外的根节点地址必须在解码前被拒绝。"""
        data = bytearray(24)
        data[:4] = b'JSON'
        struct.pack_into('<Q', data, 8, 999999)
        with self.assertRaises(ValueError):
            Bjson(bytes(data))


if __name__ == '__main__':
    unittest.main()
