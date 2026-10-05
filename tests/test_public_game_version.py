"""只使用合成 EXE，验证版本权威来源与目录/构建边界；无需游戏文件或编译器。"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import extract_catalog
import extract_map_catalog
import extract_travel_catalog
from game_version import VERSION_HEADER, assert_supported_game, read_game_version

PWSH = shutil.which("pwsh")


class GameVersionTests(unittest.TestCase):
    def test_header_rejects_missing_duplicate_and_malformed_fields(self):
        """头文件编辑错误不能静默回退旧哈希，也不能任选重复声明之一。"""
        source = VERSION_HEADER.read_text(encoding="utf-8")
        declaration = next(line for line in source.splitlines() if "kSupportedExeSha256[]" in line)
        current = read_game_version()["exe_sha256"]
        cases = [source.replace(declaration, ""), source + "\n" + declaration + "\n",
                 source.replace(current, "unknown"), source.replace(current, current.upper())]
        with tempfile.TemporaryDirectory() as folder:
            header = Path(folder) / "game_version.h"
            for modified in cases:
                with self.subTest(source=modified):
                    header.write_text(modified, encoding="utf-8")
                    with self.assertRaisesRegex(ValueError, "游戏版本配置无效"):
                        read_game_version(header)

    def test_real_hash_matches_only_current_supported_version(self):
        """测试替换配置读取结果而非哈希算法，确认校验真正读取 EXE 的全部字节。"""
        current = b"synthetic verified current executable"
        previous = b"synthetic previous executable"
        version = read_game_version()
        version["exe_sha256"] = hashlib.sha256(current).hexdigest()
        version["previous_exe_sha256"] = hashlib.sha256(previous).hexdigest()
        with tempfile.TemporaryDirectory() as folder, patch("game_version.read_game_version", return_value=version):
            game = Path(folder)
            exe = game / "sora_2nd.exe"
            exe.write_bytes(current)
            self.assertEqual(assert_supported_game(game), version["exe_sha256"])
            for invalid in (previous, current + b"tampered", b"unknown executable"):
                exe.write_bytes(invalid)
                with self.assertRaisesRegex(ValueError, "EXE 版本不匹配"):
                    assert_supported_game(game)

    def test_all_extractors_reject_unknown_exe_before_reading_resources_or_writing(self):
        """没有任何 PAC 的假游戏也应先报版本错误，并保留已有生成文件不变。"""
        with tempfile.TemporaryDirectory() as folder:
            game = Path(folder)
            (game / "sora_2nd.exe").write_bytes(b"synthetic unsupported executable")
            out = game / "generated"
            out.mkdir()
            sentinel = out / "chest_catalog.h"
            sentinel.write_bytes(b"previous generated output")
            extractors = [lambda: extract_catalog.extract(game, out),
                          lambda: extract_travel_catalog.extract(game, out),
                          lambda: extract_map_catalog.extract(game, game / "missing.json", out)]
            for extract in extractors:
                with self.assertRaisesRegex(ValueError, "EXE 版本不匹配"):
                    extract()
                self.assertEqual(list(out.iterdir()), [sentinel])
                self.assertEqual(sentinel.read_bytes(), b"previous generated output")

    def test_map_extractor_rejects_previous_chest_catalog_before_resources(self):
        """即使当前 EXE 通过检查，也不能把旧版宝箱目录与新版地图资源混用。"""
        version = read_game_version()
        with tempfile.TemporaryDirectory() as folder:
            game = Path(folder)
            catalog = game / "chests.json"
            catalog.write_text(json.dumps({"exe_sha256": version["previous_exe_sha256"]}), encoding="utf-8")
            with patch("extract_map_catalog.assert_supported_game", return_value=version["exe_sha256"]):
                with self.assertRaisesRegex(ValueError, "宝箱目录 EXE 指纹"):
                    extract_map_catalog.extract(game, catalog, game / "output")
            self.assertFalse((game / "output").exists())

    @unittest.skipUnless(PWSH, "PowerShell 脚本检查需要 pwsh")
    def test_powershell_reader_matches_native_header_and_rejects_duplicate(self):
        """调用真实 PowerShell 解析器，与 Python 读取结果交叉校验。"""
        quoted = str(ROOT / "tools/Get-GameVersion.ps1").replace("'", "''")
        result = subprocess.run([PWSH, "-NoProfile", "-Command", f"& '{quoted}' | ConvertTo-Json -Compress"],
                                capture_output=True, text=True, encoding="utf-8", timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        version = read_game_version()
        self.assertEqual(json.loads(result.stdout), {
            "ExeSha256": version["exe_sha256"], "FileVersion": version["file_version"],
            "SteamBuildId": version["steam_build_id"], "PreviousExeSha256": version["previous_exe_sha256"]})
        source = VERSION_HEADER.read_text(encoding="utf-8")
        declaration = next(line for line in source.splitlines() if "kSupportedExeSha256[]" in line)
        with tempfile.TemporaryDirectory() as folder:
            header = Path(folder) / "game_version.h"
            header.write_text(source + "\n" + declaration + "\n", encoding="utf-8")
            result = subprocess.run([PWSH, "-NoProfile", "-File", str(ROOT / "tools/Get-GameVersion.ps1"),
                                     "-HeaderPath", str(header)], capture_output=True, text=True,
                                    encoding="utf-8", timeout=30)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("游戏版本配置无效", result.stdout + result.stderr)

    @unittest.skipUnless(PWSH, "构建预检需要 pwsh，不要求编译器或真实游戏")
    def test_build_rejects_unknown_exe_before_dependencies_or_compiler(self):
        """版本预检失败时不下载依赖、不创建构建目录，错误也不被缺失编译器遮蔽。"""
        with tempfile.TemporaryDirectory() as folder:
            game = Path(folder)
            (game / "sora_2nd.exe").write_bytes(b"unknown game")
            result = subprocess.run([PWSH, "-NoProfile", "-File", str(ROOT / "tools/Build-Mod.ps1"),
                                     "-GamePath", str(game), "-BuildDirectory", str(game / "build"),
                                     "-DependencyDirectory", str(game / "deps")],
                                    capture_output=True, text=True, encoding="utf-8", timeout=30)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("游戏版本不匹配", result.stdout + result.stderr)
            self.assertFalse((game / "build").exists())
            self.assertFalse((game / "deps").exists())


if __name__ == "__main__":
    unittest.main()
