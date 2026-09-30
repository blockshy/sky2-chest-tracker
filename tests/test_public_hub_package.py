"""以隔离合成文件核对模块包白名单、清单和哈希，不触碰游戏安装目录。"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PWSH = shutil.which("pwsh")
WINDOWS_POWERSHELL = shutil.which("powershell")


@unittest.skipUnless(os.name == "nt" and PWSH, "模块打包测试需要 Windows 和 PowerShell 7")
class HubPackageTests(unittest.TestCase):
    @unittest.skipUnless(WINDOWS_POWERSHELL, "脚本解析检查需要 Windows PowerShell 5.1")
    def test_public_script_encoding_and_windows_parser(self):
        """公开中文脚本必须保留 UTF-8 BOM，并交给系统 PowerShell 解析而非执行。"""
        script = ROOT / "tools/Package-HubModule.ps1"
        self.assertTrue(script.read_bytes().startswith(b"\xef\xbb\xbf"))
        environment = os.environ.copy()
        environment["SKY2_TEST_HUB_PACKAGE_SCRIPT"] = str(script)
        command = (
            "$scriptTokens = $null; $scriptErrors = $null; "
            "[System.Management.Automation.Language.Parser]::ParseFile("
            "$env:SKY2_TEST_HUB_PACKAGE_SCRIPT, [ref]$scriptTokens, [ref]$scriptErrors) | Out-Null; "
            "if ($scriptErrors.Count) { $scriptErrors | ForEach-Object { $_.Message }; exit 1 }"
        )
        result = subprocess.run([WINDOWS_POWERSHELL, "-NoProfile", "-Command", command],
            env=environment, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_module_payload_and_preserved_data(self):
        """正式打包器只能输出三项载荷，不把独立入口或模拟玩家返程数据打包。"""
        boundary = (ROOT / "build-hub-package-tests").resolve()
        boundary.mkdir(exist_ok=True)
        temporary = tempfile.TemporaryDirectory(prefix="package-", dir=boundary)
        temporary_root = Path(temporary.name).resolve()
        try:
            binary = temporary_root / "Sky2ChestTracker.module.dll"
            binary.write_bytes(b"isolated synthetic hub module")
            # 同目录存在旧入口和返程记录时，也不能通过目录复制进入模块包。
            (temporary_root / "xinput1_4.dll").write_bytes(b"old standalone")
            (temporary_root / "revisit-return.dat").write_bytes(b"player record sentinel")
            output = temporary_root / "output"
            run = subprocess.run([PWSH, "-NoProfile", "-File", str(ROOT / "tools/Package-HubModule.ps1"),
                "-DllPath", str(binary), "-OutputDirectory", str(output)], capture_output=True, text=True, encoding="utf-8")
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            archive = next(output.glob("*.zip"))
            with zipfile.ZipFile(archive) as package:
                files = {name.replace("\\", "/"): package.read(name)
                         for name in package.namelist() if not name.endswith("/")}
            expected = {
                "plugins/Sky2ModHub/modules/Sky2ChestTracker.module.dll",
                "plugins/Sky2ModHub/modules/Sky2ChestTracker.module.ini",
                "plugins/Sky2ModHub/modules/Sky2ChestTracker.LICENSES.txt",
            }
            self.assertEqual({name[5:] for name in files if name.startswith("dist/")}, expected)
            self.assertIn(b"](docs/TRAVEL.md)", files["README.md"])
            self.assertIn("docs/TRAVEL.md", files)
            manifest = json.loads(files["manifest.json"])
            self.assertEqual(manifest["module_id"], "chest")
            self.assertEqual(manifest["abi"], 1)
            self.assertEqual({item["path"] for item in manifest["files"]}, expected)
            for item in manifest["files"]:
                self.assertEqual(hashlib.sha256(files["dist/" + item["path"]]).hexdigest(), item["sha256"])
            config = files["dist/plugins/Sky2ModHub/modules/Sky2ChestTracker.module.ini"].decode("utf-8")
            self.assertIn("Id=chest", config)
            self.assertIn("Binary=Sky2ChestTracker.module.dll", config)
            self.assertEqual((temporary_root / "revisit-return.dat").read_bytes(), b"player record sentinel")
            digest = Path(str(archive) + ".sha256").read_text().split()[0]
            self.assertEqual(digest, hashlib.sha256(archive.read_bytes()).hexdigest())
        finally:
            # 递归清理由临时目录对象执行前，先核对完整解析路径仍在专用测试边界内。
            if temporary_root.parent != boundary:
                raise RuntimeError("模块打包测试目录越界，拒绝清理")
            temporary.cleanup()


if __name__ == "__main__":
    unittest.main()
