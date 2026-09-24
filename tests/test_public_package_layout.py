"""使用隔离合成 DLL 检查真实打包流程，不读取游戏或公开合成安装包。"""
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
PWSH = shutil.which('pwsh')
PAYLOAD = b'synthetic package layout binary'


@unittest.skipUnless(os.name == 'nt' and PWSH, '打包测试需要 Windows 与 PowerShell 7')
class PackageLayout(unittest.TestCase):
    def setUp(self):
        # 所有测试写入都局限于忽略的构建目录；复制最小项目，不改正式依赖锁定文件。
        self.test_root = (ROOT / 'build-package-tests').resolve()
        self.test_root.mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix='layout-', dir=self.test_root)
        self.root = Path(self.temp.name).resolve()
        self.addCleanup(self.clean_fixture)
        self.project = self.root / 'project'
        self.project.mkdir()
        for directory in ('tools', 'installer', 'licenses', 'docs'):
            (self.project / directory).mkdir()
        for name in ('Package-Mod.ps1', 'Package-Asi.ps1'):
            shutil.copyfile(ROOT / 'tools' / name, self.project / 'tools' / name)
        for name in ('CMakeLists.txt', 'loader-dependency.json', 'LICENSE',
                     'THIRD_PARTY_NOTICES.md', 'README.md', 'Install-Mod.ps1', 'Uninstall-Mod.ps1'):
            shutil.copyfile(ROOT / name, self.project / name)
        for directory in ('installer', 'licenses', 'docs'):
            for source in (ROOT / directory).iterdir():
                if source.is_file():
                    shutil.copyfile(source, self.project / directory / source.name)
        self.binary = self.root / 'fixture.dll'
        self.binary.write_bytes(PAYLOAD)
        lock = json.loads((self.project / 'loader-dependency.json').read_text(encoding='utf-8-sig'))
        # 只在隔离项目中用固定合成指纹替代上游 DLL，测试仍执行真实白名单校验。
        lock['dll_sha256'] = hashlib.sha256(PAYLOAD).hexdigest()
        (self.project / 'loader-dependency.json').write_text(json.dumps(lock), encoding='utf-8')
        self.output = self.root / 'packages'

    def clean_fixture(self):
        """删除测试自建目录前确认其解析后的父路径，避免越过测试边界。"""
        if self.root.resolve().parent != self.test_root:
            raise RuntimeError('测试目录超出预定边界')
        self.temp.cleanup()

    def package(self, distribution):
        result = subprocess.run([PWSH, '-NoProfile', '-File',
            str(self.project / 'tools/Package-Mod.ps1'), '-Distribution', distribution,
            '-DllPath', str(self.binary), '-OutputDirectory', str(self.output)],
            capture_output=True, text=True, encoding='utf-8')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        archives = list(self.output.glob('*.zip'))
        self.assertEqual(len(archives), 1)
        with zipfile.ZipFile(archives[0]) as archive:
            # Zip 路径统一为正斜杠，以内容白名单检查，不依赖目录条目是否存在。
            files = {name.replace('\\', '/'): archive.read(name)
                     for name in archive.namelist() if not name.endswith('/')}
        checksum = archives[0].with_suffix('.sha256').read_text().split()[0]
        self.assertEqual(checksum, hashlib.sha256(archives[0].read_bytes()).hexdigest())
        return files

    def verify(self, distribution, kind, binary_path, license_path):
        files = self.package(distribution)
        payload_paths = {name for name in files if name.startswith('dist/')}
        self.assertEqual(payload_paths, {'dist/' + binary_path, 'dist/' + license_path})
        self.assertEqual(files['dist/' + binary_path], PAYLOAD)
        manifest = json.loads(files['installer/manifest.json'].decode('utf-8-sig'))
        self.assertEqual(manifest['schema'], 2)
        self.assertEqual(manifest['type'], kind)
        self.assertEqual(manifest['path'], binary_path)
        self.assertEqual(len(manifest['files']), 2)
        self.assertEqual({entry['path'] for entry in manifest['files']},
                         {binary_path, license_path})
        for entry in manifest['files']:
            self.assertEqual(entry['sha256'].lower(),
                             hashlib.sha256(files['dist/' + entry['path']]).hexdigest())
        expected_scripts = {'installer/Common.ps1'}
        if distribution == 'Loader':
            expected_scripts |= {'installer/Install-Loader.ps1', 'installer/Uninstall-Loader.ps1'}
        else:
            expected_scripts |= {'Install-Mod.ps1', 'Uninstall-Mod.ps1'}
        self.assertEqual({name for name in files if name.endswith('.ps1')}, expected_scripts)
        for name in expected_scripts:
            self.assertTrue(files[name].startswith(b'\xef\xbb\xbf'), name)
        # 玩家包不包含开发指南、生成资源、诊断探针、收据、存档及运行数据。
        allowed = expected_scripts | payload_paths | {
            'installer/manifest.json', 'installer/known-files.json', 'README.md',
            'docs/INSTALLATION.md', 'docs/USAGE.md', 'docs/TRAVEL.md', 'docs/ASI_LOADER.md'}
        self.assertEqual(set(files), allowed)
        if distribution == 'Loader':
            self.assertEqual(files['dist/' + license_path],
                (ROOT / 'licenses/Ultimate-ASI-Loader.txt').read_bytes())
        else:
            license_text = files['dist/' + license_path].decode('utf-8').replace('\r\n', '\n')
            for name in ('LICENSE', 'THIRD_PARTY_NOTICES.md', 'licenses/Dear-ImGui.txt',
                         'licenses/MinHook.txt', 'licenses/ED9ModManager.txt'):
                self.assertIn((ROOT / name).read_text(encoding='utf-8-sig'), license_text, name)
        return files

    def test_standalone_two_file_payload_and_complete_licenses(self):
        self.verify('Standalone', 'standalone-proxy', 'xinput1_4.dll',
                    'Sky2ChestTracker/LICENSES.txt')

    def test_asi_two_file_payload_and_complete_licenses(self):
        self.verify('Plugin', 'asi-plugin', 'plugins/Sky2ChestTracker.asi',
                    'plugins/Sky2ChestTracker/LICENSES.txt')

    def test_loader_separate_license_ownership(self):
        self.verify('Loader', 'asi-loader', 'xinput1_4.dll',
                    'plugins/Sky2ChestTracker/UltimateASILoader.LICENSE.txt')

    def test_unrecognized_loader_binary_is_not_packaged(self):
        self.binary.write_bytes(b'unknown loader')
        result = subprocess.run([PWSH, '-NoProfile', '-File',
            str(self.project / 'tools/Package-Mod.ps1'), '-Distribution', 'Loader',
            '-DllPath', str(self.binary), '-OutputDirectory', str(self.output)],
            capture_output=True, text=True, encoding='utf-8')
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.output.exists())


if __name__ == '__main__':
    unittest.main()
