#!/usr/bin/python3
"""Observe actual preview reads and remote protocol in disposable sessions."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
SESSION = ROOT / 'tools/miller/with-test-session.py'
HOME_SESSION = ROOT / 'tools/miller/with-test-home.py'
BACKEND = ROOT / 'tools/miller/with-delayed-backend.py'
BINARY = ROOT / 'build-miller/test/automated/display/test-miller-preview'


class PreviewIO(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not BINARY.is_file():
            raise unittest.SkipTest('Build test-miller-preview first')

    def private(self, command):
        result = subprocess.run([sys.executable, str(SESSION), '--', sys.executable,
            str(HOME_SESSION), '--', *command], cwd=ROOT, capture_output=True,
            text=True, timeout=60)
        self.assertEqual(result.returncode, 0, (result.stdout + result.stderr)[-6000:])
        return result

    @unittest.skipUnless(shutil.which('strace'), 'Requires strace for actual read observations')
    def test_native_reads_stop_at_128k_each(self):
        result = self.private(['env', 'GTK_A11Y=test',
            f'GSETTINGS_SCHEMA_DIR={ROOT / "build-miller/data"}',
            'strace', '-f', '-qq', '-yy', '-s', '0', '-e', 'trace=openat,read,close',
            str(BINARY), '-p', '/miller/preview/text-bounds'])
        self.assertIn('ok 1 /miller/preview/text-bounds', result.stdout)
        # Threads share descriptors. Match annotated fixture paths, not arbitrary
        # descriptor numbers or unrelated subprocess/system-file reads.
        handles, current, pending = [], {}, {}
        for line in result.stderr.splitlines():
            prefix = re.match(r'\[pid\s+(\d+)\]\s+(.*)', line)
            pid, call = (prefix.group(1), prefix.group(2)) if prefix else ('main', line)
            if '<unfinished ...>' in call:
                pending[pid] = call.split('<unfinished ...>')[0]
                continue
            resumed = re.match(r'<\.\.\. (\w+) resumed>(.*)', call)
            if resumed:
                call = pending.pop(pid, '') + resumed.group(2)
            opened = re.search(r'openat\(.*\)\s+=\s+(\d+)<([^>]+/first\.txt)>', call)
            if opened and 'O_RDONLY' in call:
                self.assertIn('O_NONBLOCK', call)
                self.assertIn('O_NOFOLLOW', call)
                fd = opened.group(1)
                handle = {'bytes': 0, 'reads': 0}
                handles.append(handle)
                self.assertNotIn(fd, current)
                current[fd] = handle
            read = re.search(r'read\((\d+)<[^>]+/first\.txt>,.*\)\s+=\s+(\d+)', call)
            if read:
                self.assertIn(read.group(1), current)
                count = int(read.group(2))
                current[read.group(1)]['bytes'] += count
                current[read.group(1)]['reads'] += 1
            closed = re.search(r'close\((\d+)<[^>]+/first\.txt>\)\s+=\s+0', call)
            if closed:
                current.pop(closed.group(1), None)
        self.assertFalse(current, 'Preview left a fixture descriptor open')
        self.assertEqual(len(handles), 2, f'Expected two preview opens: {handles}')
        self.assertEqual([item['bytes'] for item in handles], [128 * 1024, 128 * 1024])
        print(f'Observed native preview reads: {handles}', flush=True)

    def test_remote_off_queries_metadata_without_opening_content(self):
        result = self.private([sys.executable, str(BACKEND), '--', 'env',
            'GTK_A11Y=test', 'G_DBUS_DEBUG=message',
            f'GSETTINGS_SCHEMA_DIR={ROOT / "build-miller/data"}',
            str(BINARY), '-p', '/miller/preview/remote-off'])
        self.assertIn('ok 1 /miller/preview/remote-off', result.stdout)
        members = re.findall(r"member -> '([^']+)'", result.stdout)
        self.assertIn('QueryInfo', members, 'No actual backend metadata query observed')
        self.assertNotIn('OpenForRead', members, 'Remote content opened despite disabled policy')
        print(f'Observed remote QueryInfo calls: {members.count("QueryInfo")}; OpenForRead: 0', flush=True)


if __name__ == '__main__':
    unittest.main()
