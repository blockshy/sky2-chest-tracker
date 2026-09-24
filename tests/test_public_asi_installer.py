"""验证公共 Loader 生命周期、无收据手动混用及旧 ASI 数据迁移边界。"""
import json
import os
import unittest
from installer_fixture_support import InstallerFixture, PWSH, PS51, PAYLOADS, OLD, FOREIGN, LAYOUT, digest, valid_record


@unittest.skipUnless(os.name == 'nt' and PWSH, '需要 Windows PowerShell 环境')
class AsiSafety(InstallerFixture):
    def setUp(self):
        super().setUp()
        self.manual('asi-loader')

    def test_manual_plugin_script_uninstall_preserves_shared_loader_license(self):
        self.manual('asi-plugin')
        loader = (self.game / 'xinput1_4.dll').read_bytes()
        license_path = self.game / LAYOUT['asi-loader'][2]
        license_data = license_path.read_bytes()
        self.invoke('asi-plugin', 'Uninstall')
        self.assertEqual((self.game / 'xinput1_4.dll').read_bytes(), loader)
        self.assertEqual(license_path.read_bytes(), license_data)
        self.assertFalse((self.game / LAYOUT['asi-plugin'][2]).exists())

    def test_loader_requires_separate_uninstall_and_removes_own_license(self):
        self.invoke('asi-plugin')
        self.refused('asi-loader', 'Uninstall', message='仍有 ASI')
        self.invoke('asi-plugin', 'Uninstall')
        self.invoke('asi-loader', 'Uninstall')
        self.assertFalse((self.game / 'xinput1_4.dll').exists())
        self.assertFalse((self.game / 'plugins').exists())

    def test_other_nested_plugin_protects_loader(self):
        self.write('scripts/other/Other.asi', FOREIGN)
        self.refused('asi-loader', 'Uninstall', message='仍有 ASI')

    def test_unknown_loader_and_plugin_refused_without_receipts(self):
        self.write('plugins/Sky2ChestTracker.asi', FOREIGN)
        self.refused('asi-plugin')
        self.refused('asi-plugin', 'Uninstall')
        self.write('xinput1_4.dll', FOREIGN)
        self.refused('asi-loader')
        self.refused('asi-loader', 'Uninstall')

    def test_plugin_does_not_replace_unknown_loader(self):
        self.write('xinput1_4.dll', FOREIGN)
        self.refused('asi-plugin', message='公共 Loader')

    def test_loader_migrates_published_standalone_without_receipt(self):
        self.write('xinput1_4.dll', OLD['standalone-proxy'])
        self.write('Sky2ChestTracker/revisit-return.dat', b'independent standalone data')
        self.invoke('asi-loader')
        self.assertEqual((self.game / 'xinput1_4.dll').read_bytes(), PAYLOADS['asi-loader'])
        self.assertEqual((self.game / 'Sky2ChestTracker/revisit-return.dat').read_bytes(), b'independent standalone data')
        self.assertFalse((self.game / 'plugins/Sky2ChestTracker/revisit-return.dat').exists())

    def add_legacy_data(self):
        self.write('plugins/Sky2ChestTracker.asi', OLD['asi-plugin'])
        self.write('Sky2Mods/Sky2ChestTracker/tracker.log', b'old ASI log')
        self.write('Sky2Mods/Sky2ChestTracker/revisit-return.dat', valid_record())
        self.write('Sky2Mods/Sky2ChestTracker/revisit-history/0000000000001234.dat', valid_record())
        self.write('Sky2Mods/Sky2ChestTracker/LICENSE', b'legacy license')
        self.receipt('asi-plugin')

    def test_old_asi_migration_moves_valid_data_and_cleans_only_old_owned_files(self):
        self.add_legacy_data()
        self.write('Sky2ChestTracker/revisit-return.dat', b'standalone sentinel')
        self.invoke('asi-plugin')
        self.assertFalse((self.game / 'Sky2Mods').exists())
        self.assertEqual((self.game / 'plugins/Sky2ChestTracker/revisit-return.dat').read_bytes(), valid_record())
        self.assertEqual((self.game / 'plugins/Sky2ChestTracker/revisit-history/0000000000001234.dat').read_bytes(), valid_record())
        self.assertEqual((self.game / 'Sky2ChestTracker/revisit-return.dat').read_bytes(), b'standalone sentinel')
        self.assertEqual(len(list((self.packages['asi-plugin'] / 'backups').glob('*.asi'))), 1)

    def test_identical_destination_data_deduplicated(self):
        self.add_legacy_data()
        self.write('plugins/Sky2ChestTracker/revisit-return.dat', valid_record())
        self.invoke('asi-plugin')
        self.assertFalse((self.game / 'Sky2Mods').exists())
        self.assertEqual((self.game / 'plugins/Sky2ChestTracker/revisit-return.dat').read_bytes(), valid_record())

    def test_different_destination_data_stops_all_mutation(self):
        self.add_legacy_data()
        self.write('plugins/Sky2ChestTracker/revisit-return.dat', valid_record(0x5678))
        self.refused('asi-plugin', message='新旧 ASI 数据冲突')

    def test_unknown_old_record_and_bad_crc_are_not_moved_or_deleted(self):
        self.add_legacy_data()
        path = self.game / 'Sky2Mods/Sky2ChestTracker/revisit-return.dat'
        for payload in [FOREIGN, valid_record()[:-1] + b'X']:
            path.write_bytes(payload)
            self.refused('asi-plugin', message='旧返程数据')

    def test_history_name_must_match_ticket(self):
        self.add_legacy_data()
        path = self.game / 'Sky2Mods/Sky2ChestTracker/revisit-history/0000000000001234.dat'
        path.write_bytes(valid_record(0x5678))
        self.refused('asi-plugin', message='旧返程数据校验失败')

    def test_unknown_legacy_notes_and_invalid_receipt_preserved(self):
        self.add_legacy_data()
        self.write('Sky2Mods/Sky2ChestTracker/user-note.txt', FOREIGN)
        receipt = self.receipt('asi-plugin', hash_value=digest(FOREIGN))
        data = receipt.read_bytes()
        self.invoke('asi-plugin')
        self.assertEqual(receipt.read_bytes(), data)
        self.assertEqual((self.game / 'Sky2Mods/Sky2ChestTracker/user-note.txt').read_bytes(), FOREIGN)

    def test_whatif_migration_and_uninstall_are_read_only(self):
        self.add_legacy_data()
        for action in ['Install', 'Uninstall']:
            before = self.snapshot()
            self.invoke('asi-plugin', action, flags=('-Preview',))
            self.assertEqual(self.snapshot(), before)

    def test_legacy_loader_receipt_and_license_migrated(self):
        self.write('Sky2ModLoader/LICENSE', b'legacy license')
        self.receipt('asi-loader')
        self.invoke('asi-loader')
        self.assertFalse((self.game / 'Sky2ModLoader').exists())
        self.assertTrue((self.game / LAYOUT['asi-loader'][2]).is_file())

    def test_loader_license_foreign_content_never_overwritten_or_removed(self):
        self.write(LAYOUT['asi-loader'][2], FOREIGN)
        self.refused('asi-loader')
        self.invoke('asi-loader', 'Uninstall')
        self.assertEqual((self.game / LAYOUT['asi-loader'][2]).read_bytes(), FOREIGN)

    def test_loader_install_does_not_migrate_asi_runtime_data(self):
        self.add_legacy_data()
        data = (self.game / 'Sky2Mods/Sky2ChestTracker/revisit-return.dat').read_bytes()
        self.invoke('asi-loader')
        self.assertEqual((self.game / 'Sky2Mods/Sky2ChestTracker/revisit-return.dat').read_bytes(), data)
        self.assertFalse((self.game / 'plugins/Sky2ChestTracker/revisit-return.dat').exists())

    def test_absent_plugin_does_not_claim_old_runtime_directory(self):
        self.write('Sky2Mods/Sky2ChestTracker/revisit-return.dat', FOREIGN)
        self.invoke('asi-plugin')
        self.assertEqual((self.game / 'Sky2Mods/Sky2ChestTracker/revisit-return.dat').read_bytes(), FOREIGN)
        self.assertFalse((self.game / 'plugins/Sky2ChestTracker/revisit-return.dat').exists())

    def test_reparse_point_in_migration_directory_refused(self):
        self.write('plugins/Sky2ChestTracker.asi', OLD['asi-plugin'])
        outside = self.root / 'outside'
        outside.mkdir()
        self.make_link(self.game / 'Sky2Mods', outside, True)
        self.refused('asi-plugin', message='重解析点')

    @unittest.skipUnless(PS51.exists(), '需要系统 Windows PowerShell 5.1')
    def test_powershell51_migration_and_loader_lifecycle(self):
        self.add_legacy_data()
        self.invoke('asi-plugin', executable=PS51)
        self.invoke('asi-plugin', 'Uninstall', executable=PS51)
        self.invoke('asi-loader', 'Uninstall', executable=PS51)
        self.assertEqual((self.game / 'plugins/Sky2ChestTracker/revisit-return.dat').read_bytes(), valid_record())

    def test_locked_binary_after_data_copy_preserves_source_and_retry_succeeds(self):
        """迁移先复制数据、最后提交入口；入口被占用时原始记录和旧插件均仍可用。"""
        self.add_legacy_data()
        self.invoke('asi-plugin', success=False, flags=('-LockRelativePath', 'plugins/Sky2ChestTracker.asi'))
        self.assertEqual((self.game / 'plugins/Sky2ChestTracker.asi').read_bytes(), OLD['asi-plugin'])
        self.assertEqual((self.game / 'Sky2Mods/Sky2ChestTracker/revisit-return.dat').read_bytes(), valid_record())
        self.assertEqual((self.game / 'plugins/Sky2ChestTracker/revisit-return.dat').read_bytes(), valid_record())
        self.invoke('asi-plugin')
        self.assertFalse((self.game / 'Sky2Mods').exists())
        self.assertEqual((self.game / 'plugins/Sky2ChestTracker.asi').read_bytes(), PAYLOADS['asi-plugin'])

    def test_locked_migration_source_cleanup_preserves_both_copies_and_retry(self):
        """源记录清理阶段失败也不能丢返程点；下次安装识别相同副本后继续完成。"""
        self.add_legacy_data()
        self.invoke('asi-plugin', success=False,
                    flags=('-LockRelativePath', 'Sky2Mods/Sky2ChestTracker/revisit-return.dat'))
        self.assertEqual((self.game / 'Sky2Mods/Sky2ChestTracker/revisit-return.dat').read_bytes(), valid_record())
        self.assertEqual((self.game / 'plugins/Sky2ChestTracker/revisit-return.dat').read_bytes(), valid_record())
        self.invoke('asi-plugin')
        self.assertFalse((self.game / 'Sky2Mods').exists())
