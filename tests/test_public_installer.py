"""在 Windows 合成目录中验证真实安装/卸载脚本，不依赖游戏及第三方 Mod。"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
PWSH = shutil.which('pwsh')
PAYLOAD = b'synthetic chest tracker DLL'
FOREIGN = b'synthetic unrelated mod DLL'
OLD = b'synthetic previous tracker DLL'


def digest(data):
    """独立计算预期哈希，避免测试与被测脚本共用归属判断实现。"""
    return hashlib.sha256(data).hexdigest()


@unittest.skipUnless(os.name == 'nt' and PWSH, '安装器测试需要 Windows 和 PowerShell 7')
class InstallerSafety(unittest.TestCase):
    def setUp(self):
        # 临时目录限定在被 Git 忽略的测试构建根目录；清理前再次核对边界。
        self.test_root = (ROOT / 'build-installer-tests').resolve()
        self.test_root.mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix='case-', dir=self.test_root)
        self.root = Path(self.temp.name).resolve()
        self.assertEqual(self.root.parent, self.test_root)
        self.addCleanup(self.clean_fixture)
        self.package = self.root / 'package'
        self.game = self.root / '游戏目录 [test]'
        self.game.mkdir()
        (self.game / 'sora_2nd.exe').write_text('SKY2 INSTALLER TEST FIXTURE', encoding='utf-8')
        (self.package / 'dist' / 'licenses').mkdir(parents=True)
        (self.package / 'docs').mkdir()
        for name in ('Install-Mod.ps1', 'Uninstall-Mod.ps1', 'README.md', 'LICENSE',
                     'CHANGELOG.md', 'CONTRIBUTING.md', 'THIRD_PARTY_NOTICES.md'):
            shutil.copyfile(ROOT / name, self.package / name)
        for document in ('BUILDING.md', 'ARCHITECTURE.md', 'TESTING.md'):
            shutil.copyfile(ROOT / 'docs' / document, self.package / 'docs' / document)
        self.source = self.package / 'dist' / 'xinput1_4.dll'
        self.source.write_bytes(PAYLOAD)
        (self.package / 'dist' / 'manifest.json').write_text(json.dumps({
            'version': '0.3.2', 'dll_sha256': digest(PAYLOAD)
        }), encoding='utf-8')
        self.target = self.game / 'xinput1_4.dll'
        self.receipt = self.game / 'Sky2ChestTracker' / 'install.json'
        # 模拟其他 Mod 与存档，成功或失败的卸载都必须保留它们。
        (self.game / 'another-mod.txt').write_bytes(FOREIGN)
        (self.game / 'save014.dat').write_bytes(b'synthetic save sentinel')

    def clean_fixture(self):
        """只有本用例创建且仍在预定根目录内的临时目录才允许递归清理。"""
        if self.root.resolve().parent != self.test_root:
            raise RuntimeError('测试清理目录超出边界')
        self.temp.cleanup()

    def write_receipt(self, data=PAYLOAD, product='Sky2ChestTracker'):
        self.receipt.parent.mkdir(exist_ok=True)
        self.receipt.write_text(json.dumps({
            'product': product, 'dll_sha256': digest(data), 'version': 'test'
        }), encoding='utf-8')

    def snapshot(self):
        """记录整个合成目录，失败与预演不能偷偷写入备份、记录或其他文件。"""
        return {str(p.relative_to(self.root)): p.read_bytes()
                for p in self.root.rglob('*') if p.is_file()}

    def run_script(self, action, success, *options, message=None):
        result = subprocess.run([
            PWSH, '-NoLogo', '-NoProfile', '-NonInteractive', '-File',
            str(ROOT / 'tests' / 'Invoke-InstallerFixture.ps1'),
            '-ScriptPath', str(self.package / f'{action}-Mod.ps1'),
            '-GamePath', str(self.game), *options
        ], capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=30)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        if message:
            # 失败原因也必须匹配，避免早期门禁意外失败导致后续保护测试假通过。
            self.assertIn(message, result.stdout + result.stderr)
        return result

    def assert_refused_without_changes(self, message='安装记录'):
        """同一冲突应分别阻止安装与卸载，且两次操作均保留所有原始字节。"""
        before = self.snapshot()
        for action in ('Install', 'Uninstall'):
            with self.subTest(action=action):
                self.run_script(action, False, message=message)
                self.assertEqual(self.snapshot(), before)

    def test_unknown_dll_without_receipt(self):
        """先安装其他同名代理：两种脚本都不能覆盖或删除。"""
        self.target.write_bytes(FOREIGN)
        self.assert_refused_without_changes()

    def test_replaced_dll_with_stale_receipt(self):
        """其他 Mod 后来替换 DLL：即使保留了本 Mod 的记录也必须拒绝。"""
        self.write_receipt()
        self.target.write_bytes(FOREIGN)
        self.assert_refused_without_changes()

    def test_wrong_product_with_matching_hash(self):
        """仅哈希相同仍不足以授权，产品标识也必须匹配。"""
        self.target.write_bytes(FOREIGN)
        self.write_receipt(FOREIGN, product='AnotherMod')
        self.assert_refused_without_changes()

    def test_corrupt_receipt(self):
        self.target.write_bytes(PAYLOAD)
        self.write_receipt()
        self.receipt.write_text('{broken JSON', encoding='utf-8')
        self.assert_refused_without_changes()

    def test_incomplete_receipt(self):
        self.target.write_bytes(PAYLOAD)
        self.write_receipt()
        self.receipt.write_text('{"product": "Sky2ChestTracker"}', encoding='utf-8')
        self.assert_refused_without_changes()

    def make_link(self, link, target, directory=False):
        """链接测试只指向当前合成目录；未授予创建权限时明确报告跳过。"""
        try:
            os.symlink(target, link, target_is_directory=directory)
        except OSError as error:
            if getattr(error, 'winerror', None) == 1314:
                self.skipTest('当前 Windows 账户没有创建符号链接权限')
            raise

    def test_linked_dll_is_refused(self):
        external = self.root / 'external.dll'
        external.write_bytes(PAYLOAD)
        self.make_link(self.target, external)
        self.write_receipt()
        self.assert_refused_without_changes('重解析点')

    def test_linked_receipt_is_refused(self):
        self.target.write_bytes(PAYLOAD)
        self.write_receipt()
        external = self.root / 'external.json'
        self.receipt.rename(external)
        self.make_link(self.receipt, external)
        self.assert_refused_without_changes('重解析点')

    def test_linked_mod_directory_is_refused(self):
        self.target.write_bytes(PAYLOAD)
        self.write_receipt()
        external = self.root / 'external-records'
        # 两个目标都由当前用例创建，并且严格位于测试根目录内。
        self.assertEqual(self.receipt.parent.resolve().parent, self.game.resolve())
        self.assertEqual(external.resolve().parent, self.root)
        self.receipt.parent.rename(external)
        self.make_link(self.receipt.parent, external, directory=True)
        self.assert_refused_without_changes('重解析点')

    def test_fresh_install_and_uninstall(self):
        """正常安装后只卸载被记录的 DLL，保留其他 Mod、存档、文档和记录。"""
        self.run_script('Install', True)
        self.assertEqual(self.target.read_bytes(), PAYLOAD)
        receipt = json.loads(self.receipt.read_text(encoding='utf-8-sig'))
        self.assertEqual(receipt['product'], 'Sky2ChestTracker')
        self.assertEqual(receipt['dll_sha256'].lower(), digest(PAYLOAD))
        before = self.snapshot()
        del before[str(self.target.relative_to(self.root))]
        self.run_script('Uninstall', True)
        self.assertEqual(self.snapshot(), before)

    def test_update_preserves_old_dll(self):
        self.target.write_bytes(OLD)
        self.write_receipt(OLD)
        self.run_script('Install', True)
        self.assertEqual(self.target.read_bytes(), PAYLOAD)
        backups = list((self.package / 'backups').glob('*.dll'))
        self.assertEqual(len(backups), 1)
        self.assertEqual(backups[0].read_bytes(), OLD)

    def test_corrupt_existing_backup_blocks_update(self):
        """已有备份损坏时不能继续更新，以免失去可恢复的旧版副本。"""
        self.target.write_bytes(OLD)
        self.write_receipt(OLD)
        (self.package / 'backups').mkdir()
        (self.package / 'backups' / f'{digest(OLD)}.dll').write_bytes(b'corrupt backup')
        before = self.snapshot()
        self.run_script('Install', False, message='备份校验失败')
        self.assertEqual(self.snapshot(), before)

    def test_same_dll_is_noop(self):
        self.target.write_bytes(PAYLOAD)
        self.write_receipt()
        before = self.snapshot()
        self.run_script('Install', True)
        self.assertEqual(self.snapshot(), before)

    def test_preview_fresh_install(self):
        before = self.snapshot()
        self.run_script('Install', True, '-Preview')
        self.assertEqual(self.snapshot(), before)
        self.assertFalse(self.receipt.parent.exists())

    def test_preview_update_and_uninstall(self):
        self.target.write_bytes(OLD)
        self.write_receipt(OLD)
        before = self.snapshot()
        for action in ('Install', 'Uninstall'):
            self.run_script(action, True, '-Preview')
            self.assertEqual(self.snapshot(), before)
        self.assertFalse((self.package / 'backups').exists())

    def test_tampered_package(self):
        self.source.write_bytes(b'altered payload')
        before = self.snapshot()
        self.run_script('Install', False, message='安装包 DLL 哈希不一致')
        self.assertEqual(self.snapshot(), before)

    def test_wrong_game_version(self):
        before = self.snapshot()
        self.run_script('Install', False, '-UseRealExeHash', message='游戏 EXE 与适配版本不符')
        self.assertEqual(self.snapshot(), before)

    def test_running_game(self):
        self.target.write_bytes(PAYLOAD)
        self.write_receipt()
        before = self.snapshot()
        for action in ('Install', 'Uninstall'):
            self.run_script(action, False, '-Running', message='请先正常退出游戏')
            self.assertEqual(self.snapshot(), before)

    def test_missing_dll_uninstall_is_noop(self):
        before = self.snapshot()
        self.run_script('Uninstall', True)
        self.assertEqual(self.snapshot(), before)


if __name__ == '__main__':
    unittest.main()
