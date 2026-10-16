#!/usr/bin/python3
"""Disposable fixture generation never changes pre-existing user data."""
import json
import pathlib
import subprocess
import tempfile
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parents[2] / 'tools/miller/create-fixture.py'

class Fixtures(unittest.TestCase):
    def generate(self, root, entries=32, depth=3):
        return subprocess.run(['python3', str(SCRIPT), '--root', str(root),
                               '--entries', str(entries), '--depth', str(depth)],
                              capture_output=True, text=True)

    def test_small_manifest_and_contents(self):
        with tempfile.TemporaryDirectory() as parent:
            root = pathlib.Path(parent) / 'fixture'
            result = self.generate(root)
            self.assertEqual(result.returncode, 0, result.stderr)
            manifest = json.loads(result.stdout)
            self.assertEqual(manifest['entries'], 32)
            files = list((root / 'many').iterdir())
            self.assertEqual(len(files), 32)
            self.assertEqual(files[-1].read_bytes(), b'Miller fixture content\n')
            self.assertTrue((root / 'empty').is_dir())

    def test_deep_unicode_and_link(self):
        with tempfile.TemporaryDirectory() as parent:
            root = pathlib.Path(parent) / 'fixture'
            result = self.generate(root, entries=1, depth=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            manifest = json.loads(result.stdout)
            deep = pathlib.Path(manifest['deep'])
            self.assertEqual(len(deep.relative_to(root / 'deep').parts), 30)
            self.assertTrue(deep.is_dir())
            self.assertTrue((root / 'names' / '猫 100% .txt').is_file())
            self.assertTrue((root / 'names' / '$(must-not-run).txt').is_file())
            self.assertTrue((root / 'link').is_symlink())
            self.assertEqual((root / 'link').readlink(), pathlib.Path('deep'))
            self.assertTrue((root / 'deep' / 'cycle').is_symlink())

    def test_reject_nonempty_root(self):
        with tempfile.TemporaryDirectory() as parent:
            root = pathlib.Path(parent)
            sentinel = root / 'keep'
            sentinel.write_bytes(b'untouched')
            result = self.generate(root)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('empty directory', result.stderr)
            self.assertEqual(sentinel.read_bytes(), b'untouched')
            self.assertEqual(list(root.iterdir()), [sentinel])

    def test_reject_symlink_root(self):
        with tempfile.TemporaryDirectory() as parent:
            base=pathlib.Path(parent)
            (base/'target').mkdir()
            (base/'alias').symlink_to('target')
            result=self.generate(base/'alias')
            self.assertNotEqual(result.returncode,0)
            self.assertFalse(list((base/'target').iterdir()))

if __name__ == '__main__':
    unittest.main()
