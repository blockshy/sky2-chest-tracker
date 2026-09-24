"""真实独立版安装器：无收据混用、未知文件保护、精简载荷与更新恢复。"""
import json
import os
import stat
import unittest
from installer_fixture_support import InstallerFixture, PWSH, PS51, PAYLOADS, OLD, FOREIGN, LAYOUT, digest


@unittest.skipUnless(os.name == 'nt' and PWSH, '需要 Windows PowerShell 环境')
class StandaloneSafety(InstallerFixture):
    kind = 'standalone-proxy'

    def test_install_payload_only_then_uninstall(self):
        self.invoke(self.kind)
        files = {p.relative_to(self.game).as_posix() for p in self.game.rglob('*') if p.is_file()}
        self.assertEqual(files, {'sora_2nd.exe', 'save014.dat', 'xinput1_4.dll', 'Sky2ChestTracker/LICENSES.txt'})
        self.invoke(self.kind, 'Uninstall')
        self.assertFalse((self.game / 'Sky2ChestTracker').exists())
        self.assertEqual((self.game / 'save014.dat').read_bytes(), b'save sentinel')

    def test_manual_install_script_update_and_uninstall_without_receipt(self):
        self.manual(self.kind, old=True)
        self.write('Sky2ChestTracker/tracker.log', b'preserve log')
        self.invoke(self.kind)
        self.assertEqual((self.game / 'xinput1_4.dll').read_bytes(), PAYLOADS[self.kind])
        self.assertEqual(len(list((self.packages[self.kind] / 'backups').glob('*.dll'))), 1)
        self.invoke(self.kind, 'Uninstall')
        self.assertEqual((self.game / 'Sky2ChestTracker/tracker.log').read_bytes(), b'preserve log')

    def test_no_backup_update_does_not_create_backup(self):
        self.manual(self.kind, old=True)
        self.invoke(self.kind, flags=('-NoBackup',))
        self.assertFalse((self.packages[self.kind] / 'backups').exists())

    def test_unknown_same_name_never_owned_by_receipt(self):
        self.write('xinput1_4.dll', FOREIGN)
        self.receipt(self.kind, hash_value=digest(FOREIGN))
        for action in ('Install', 'Uninstall'):
            self.refused(self.kind, action)

    def test_stale_valid_receipt_cannot_authorize_foreign_binary(self):
        self.write('xinput1_4.dll', FOREIGN)
        self.receipt(self.kind)
        self.refused(self.kind)
        self.refused(self.kind, 'Uninstall')

    def test_foreign_license_conflict_stops_before_binary_changes(self):
        self.manual(self.kind, old=True)
        self.write('Sky2ChestTracker/LICENSES.txt', FOREIGN)
        self.refused(self.kind)

    def test_user_document_and_runtime_directory_preserved_without_receipt(self):
        self.write('Sky2ChestTracker/my-notes.txt', FOREIGN)
        self.write('Sky2ChestTracker/revisit-return.dat', FOREIGN)
        self.invoke(self.kind)
        self.invoke(self.kind, 'Uninstall')
        self.assertEqual((self.game / 'Sky2ChestTracker/my-notes.txt').read_bytes(), FOREIGN)
        self.assertEqual((self.game / 'Sky2ChestTracker/revisit-return.dat').read_bytes(), FOREIGN)

    def test_legacy_known_license_receipt_cleaned_unknown_notes_preserved(self):
        self.manual(self.kind, old=True)
        self.write('Sky2ChestTracker/LICENSE', b'legacy license')
        self.write('Sky2ChestTracker/README.md', FOREIGN)
        receipt = self.receipt(self.kind)
        self.invoke(self.kind)
        self.assertFalse(receipt.exists())
        self.assertFalse((self.game / 'Sky2ChestTracker/LICENSE').exists())
        self.assertEqual((self.game / 'Sky2ChestTracker/README.md').read_bytes(), FOREIGN)

    def test_unrecognized_dynamic_receipt_is_preserved(self):
        self.manual(self.kind)
        receipt = self.receipt(self.kind, hash_value=digest(FOREIGN))
        before = receipt.read_bytes()
        self.invoke(self.kind)
        self.assertEqual(receipt.read_bytes(), before)

    def test_current_license_in_known_list_never_cleanup_target(self):
        package = self.packages[self.kind]
        known_path = package / 'installer/known-files.json'
        known = json.loads(known_path.read_text())
        license_path = LAYOUT[self.kind][2]
        known['files'].append(dict(type=self.kind, product='Sky2ChestTracker', path=license_path,
                                   sha256=digest((package / 'dist' / license_path).read_bytes())))
        self.write_json(known_path, known)
        self.manual(self.kind)
        self.invoke(self.kind)
        self.assertTrue((self.game / license_path).is_file())

    def test_whatif_no_files_or_directories_created(self):
        before = self.snapshot()
        dirs = {str(p) for p in self.root.rglob('*') if p.is_dir()}
        self.invoke(self.kind, flags=('-Preview',))
        self.assertEqual(self.snapshot(), before)
        self.assertEqual({str(p) for p in self.root.rglob('*') if p.is_dir()}, dirs)

    def test_running_game_and_wrong_exe_leave_everything_untouched(self):
        for flag, message in [('-Running', '退出游戏'), ('-UseRealExeHash', 'EXE')]:
            self.refused(self.kind, message=message, flags=(flag,))

    def test_manifest_path_traversal_duplicate_and_wrong_product_rejected(self):
        path = self.packages[self.kind] / 'installer/manifest.json'
        original = json.loads(path.read_text())
        cases = [dict(original, path='../outside.dll'), dict(original, product='Foreign'),
                 dict(original, files=[original['files'][0], original['files'][0]]),
                 dict(original, files=[original['files'][0], dict(path='../outside.txt', sha256=digest(FOREIGN))])]
        for data in cases:
            with self.subTest(data=data):
                self.write_json(path, data)
                self.refused(self.kind, message='安装包')
        self.write_json(path, original)

    def test_payload_tamper_rejected(self):
        (self.packages[self.kind] / 'dist/xinput1_4.dll').write_bytes(FOREIGN)
        self.refused(self.kind, message='哈希不一致')

    def test_standalone_refuses_asi_and_loader(self):
        self.manual('asi-loader')
        self.refused(self.kind)
        self.manual('asi-plugin')
        self.refused(self.kind, message='已有宝箱 ASI')

    def test_parent_directory_link_rejected(self):
        outside = self.root / 'outside'
        outside.mkdir()
        self.make_link(self.game / 'Sky2ChestTracker', outside, True)
        self.refused(self.kind, message='重解析点')

    def test_file_link_rejected(self):
        outside = self.root / 'outside.dll'
        outside.write_bytes(PAYLOADS[self.kind])
        self.make_link(self.game / 'xinput1_4.dll', outside)
        self.refused(self.kind, message='重解析点')

    @unittest.skipUnless(PS51.exists(), '需要系统 Windows PowerShell 5.1')
    def test_powershell51_install_and_uninstall(self):
        self.invoke(self.kind, executable=PS51)
        self.invoke(self.kind, 'Uninstall', executable=PS51)
        self.assertFalse((self.game / 'xinput1_4.dll').exists())

    def test_readonly_binary_keeps_original_and_retry_succeeds(self):
        """真实只读属性应使原子替换失败；清除属性后相同包能够重试更新。"""
        self.manual(self.kind, old=True)
        target = self.game / 'xinput1_4.dll'
        target.chmod(stat.S_IREAD)
        try:
            self.invoke(self.kind, success=False)
            self.assertEqual(target.read_bytes(), OLD[self.kind])
        finally:
            target.chmod(stat.S_IWRITE | stat.S_IREAD)
        self.invoke(self.kind)
        self.assertEqual(target.read_bytes(), PAYLOADS[self.kind])
