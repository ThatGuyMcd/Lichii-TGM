import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('manifest', HERE / 'verify_nightly_manifest.py')
manifest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(manifest)

class ManifestTests(unittest.TestCase):
    tag = 'Lichii-tgm-v1.0.4'
    sha = 'a' * 40

    def fixture(self, root, count=16, fail=False, skip=False):
        info = {'source_commit': self.sha, 'release_tag': self.tag}
        (root / 'release-manifest.json').write_text(json.dumps({**info,
            'validation': {'windows_ctest_groups': count, 'linux_ctest_groups': count}}))
        for platform in ('windows', 'linux'):
            cases = ''.join(f'<testcase name="group{i}">{"<failure/>" if fail and i == 0 else "<skipped/>" if skip and i == 0 else ""}</testcase>' for i in range(count))
            (root / (platform + '-ctest.xml')).write_text(f'<testsuite tests="{count}" failures="{int(fail)}">{cases}</testsuite>')
        with zipfile.ZipFile(root / (self.tag + '-windows-x86_64.zip'), 'w') as package:
            package.writestr('build-info.json', json.dumps(info))
            package.writestr('LICENSE.txt', 'Synthetic fixture license')
            for name in ('lichii-tgm.exe','lichii-tgm-cli.exe','lichii-tgm-shader-prepare.exe'):
                package.writestr(name, 'Synthetic fixture executable identity: ' + self.sha)
        data = bytearray(256)
        data[:4] = b'\x7fELF'
        data[8:11] = b'AI\x02'
        data[128:132] = b'hsqs'
        struct.pack_into('<HH', data, 128 + 28, 4, 0)
        struct.pack_into('<Q', data, 128 + 40, 96)
        (root / (self.tag + '-linux-x86_64.AppImage')).write_bytes(data)
        self.sums(root)

    def sums(self, root):
        paths = sorted(p for p in root.iterdir() if p.name != 'SHA256SUMS')
        (root / 'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + p.name + '\n' for p in paths))

    def test_verified_pair_accepts_current_and_additional_groups(self):
        for count in (16,17):
            with tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                self.fixture(root, count=count)
                self.assertEqual(manifest.verify(root, self.tag, self.sha), 128)

    def test_old_counts_failures_and_skips_block(self):
        for kwargs in ({'count':13}, {'fail':True}, {'skip':True}):
            with tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                self.fixture(root, **kwargs)
                with self.assertRaises(ValueError):
                    manifest.verify(root, self.tag, self.sha)

    def test_tampering_and_unlisted_assets_block(self):
        for name in ('release-manifest.json', 'extra.txt'):
            with tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                self.fixture(root)
                (root / name).write_text('Changed synthetic file')
                with self.assertRaises(ValueError):
                    manifest.verify(root, self.tag, self.sha)

    def test_wrong_source_and_missing_receipt_block(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.fixture(root)
            with self.assertRaises(ValueError):
                manifest.verify(root, self.tag, 'b' * 40)
            (root / 'windows-ctest.xml').unlink()
            self.sums(root)
            with self.assertRaises(ValueError):
                manifest.verify(root, self.tag, self.sha)

    def test_invalid_image_and_checksum_traversal_block(self):
        with self.assertRaises(ValueError):
            manifest.appimage_offset(b'not an AppImage')
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.fixture(root)
            with (root / 'SHA256SUMS').open('a') as out:
                out.write('a' * 64 + '  ../outside\n')
            with self.assertRaises(ValueError):
                manifest.verify(root, self.tag, self.sha)

if __name__ == '__main__':
    unittest.main()
