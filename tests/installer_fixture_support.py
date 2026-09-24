"""只构建合成载荷，为三个发行模式的真实 PowerShell 脚本提供共享测试设施。"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
PWSH = shutil.which('pwsh')
PS51 = Path(os.environ.get('SystemRoot', 'C:/Windows')) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
EXE_HASH = 'd8b2911d1576216bdc22d070550e4f531e105de7ed2981885849669f4acf8aaf'
LOADER_HASH = '031a3e5576d91dce1e438d36b9a3d462c7334ab4791990a8ff1e3ddc0e132daf'
PAYLOADS = {'standalone-proxy': b'synthetic chest proxy', 'asi-plugin': b'synthetic chest ASI', 'asi-loader': b'synthetic verified UAL'}
OLD = {'standalone-proxy': b'synthetic previous proxy', 'asi-plugin': b'synthetic previous ASI'}
FOREIGN = b'another product, never touch this file'
LAYOUT = {
    'standalone-proxy': ('Sky2ChestTracker', 'xinput1_4.dll', 'Sky2ChestTracker/LICENSES.txt', 'Sky2ChestTracker'),
    'asi-plugin': ('Sky2ChestTracker', 'plugins/Sky2ChestTracker.asi', 'plugins/Sky2ChestTracker/LICENSES.txt', 'Sky2Mods/Sky2ChestTracker'),
    'asi-loader': ('UltimateASILoader', 'xinput1_4.dll', 'plugins/Sky2ChestTracker/UltimateASILoader.LICENSE.txt', 'Sky2ModLoader'),
}


def digest(data):
    """测试独立计算实际字节的 SHA-256，不复用被测脚本的判断。"""
    return hashlib.sha256(data).hexdigest()


def valid_record(ticket=0x1234):
    """生成真实稳定容器格式，包括游戏指纹与 CRC，测试迁移不会接管未知 .dat。"""
    data = bytearray(144)
    data[:8] = b'SKY2RET1'
    struct.pack_into('<II', data, 8, 1, 144)
    data[16:48] = bytes.fromhex(EXE_HASH)
    struct.pack_into('<QQ', data, 48, 1770000000, ticket)
    data[64:70] = b't0000\x00'
    struct.pack_into('<I', data, 140, zlib.crc32(data[:140]))
    return bytes(data)


class InstallerFixture(unittest.TestCase):
    """每例独占受控临时目录，所有清理都先确认仍位于测试根目录。"""
    def setUp(self):
        self.test_root = (ROOT / 'build-installer-tests').resolve()
        self.test_root.mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix='slim-case-', dir=self.test_root)
        self.root = Path(self.temp.name).resolve()
        self.addCleanup(self.cleanup_fixture)
        self.game = self.root / '游戏目录 [test]'
        self.game.mkdir()
        (self.game / 'sora_2nd.exe').write_text('SKY2 INSTALLER TEST FIXTURE', encoding='utf-8')
        (self.game / 'save014.dat').write_bytes(b'save sentinel')
        self.packages = {kind: self.make_package(kind) for kind in LAYOUT}

    def cleanup_fixture(self):
        if self.root.parent != self.test_root:
            raise RuntimeError('测试目录越界，拒绝递归清理')
        self.temp.cleanup()

    def write(self, relative, data):
        path = self.game / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    @staticmethod
    def write_json(path, obj):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(obj), encoding='utf-8')

    def make_package(self, kind):
        package = self.root / ('package-' + kind)
        shutil.copytree(ROOT / 'installer', package / 'installer')
        for name in ('Install-Mod.ps1', 'Uninstall-Mod.ps1'):
            shutil.copyfile(ROOT / name, package / name)
        common = package / 'installer/Common.ps1'
        common.write_text(common.read_text(encoding='utf-8-sig').replace(LOADER_HASH, digest(PAYLOADS['asi-loader'])), encoding='utf-8-sig')
        product, binary, license_path, legacy = LAYOUT[kind]
        files = []
        for path, data in [(binary, PAYLOADS[kind]), (license_path, (kind + ' complete license').encode())]:
            dest = package / 'dist' / path
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)
            files.append(dict(path=path, sha256=digest(data)))
        manifest = dict(schema=2, type=kind, product=product, version='9.7.4' if kind == 'asi-loader' else '0.6.0',
                        path=binary, sha256=digest(PAYLOADS[kind]), exe_sha256=EXE_HASH, files=files)
        self.write_json(package / 'installer/manifest.json', manifest)
        known = []
        for entry_kind, (entry_product, entry_binary, _, entry_legacy) in LAYOUT.items():
            for data in [PAYLOADS[entry_kind]] + ([OLD[entry_kind]] if entry_kind in OLD else []):
                known.append(dict(type=entry_kind, product=entry_product, path=entry_binary, sha256=digest(data)))
            known.append(dict(type=entry_kind, product=entry_product, path=entry_legacy + '/LICENSE', sha256=digest(b'legacy license')))
        self.write_json(package / 'installer/known-files.json', dict(schema=1, files=known))
        return package

    def snapshot(self):
        return {str(p.relative_to(self.root)): p.read_bytes() for p in self.root.rglob('*') if p.is_file()}

    def invoke(self, kind, action='Install', success=True, flags=(), message=None, executable=None):
        package = self.packages[kind]
        script = package / (f'installer/{action}-Loader.ps1' if kind == 'asi-loader' else f'{action}-Mod.ps1')
        env = os.environ.copy()
        if executable == PS51:
            env = {k: v for k, v in env.items() if k.upper() != 'PSMODULEPATH'}
        command = [str(executable or PWSH), '-NoLogo', '-NoProfile', '-NonInteractive', '-File',
                   str(ROOT / 'tests/Invoke-InstallerFixture.ps1'), '-ScriptPath', str(script), '-GamePath', str(self.game), *flags]
        result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=40, env=env)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        if message:
            self.assertIn(message, result.stdout + result.stderr)
        return result

    def refused(self, kind, action='Install', message='未知同名', flags=()):
        before = self.snapshot()
        self.invoke(kind, action, False, flags, message)
        self.assertEqual(self.snapshot(), before)

    def manual(self, kind, old=False):
        _, binary, license_path, _ = LAYOUT[kind]
        self.write(binary, OLD[kind] if old else PAYLOADS[kind])
        source = self.packages[kind] / 'dist' / license_path
        self.write(license_path, source.read_bytes())

    def receipt(self, kind, hash_value=None, **changes):
        product, binary, _, legacy = LAYOUT[kind]
        obj = dict(schema=1, type=kind, product=product, version='0.6.0', path=binary,
                   sha256=hash_value or digest(OLD.get(kind, PAYLOADS[kind])), exe_sha256=EXE_HASH)
        if kind == 'standalone-proxy':
            obj['dll_sha256'] = obj['sha256']
        obj.update(changes)
        path = self.game / legacy / ('plugin-install.json' if kind == 'asi-plugin' else 'install.json')
        self.write_json(path, obj)
        return path

    def make_link(self, path, target, directory=False):
        try:
            os.symlink(target, path, target_is_directory=directory)
        except OSError as error:
            if getattr(error, 'winerror', None) == 1314:
                self.skipTest('当前账户没有创建符号链接权限')
            raise
