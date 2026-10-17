#!/usr/bin/python3
"""Observe actual preview reads and remote protocol in disposable sessions."""
import ast
import json
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

    def trace(self, arguments):
        # Keep syscall output separate from concurrently writing GJS/GVfs/bus
        # diagnostics. Print it only after strace exits, from its owned file.
        capture = '''import json,os,pathlib,subprocess,sys
trace=pathlib.Path(os.environ['XDG_RUNTIME_DIR'])/'preview-observation.strace'
result=subprocess.run(['strace','-o',str(trace),*sys.argv[1:]],capture_output=True,text=True)
print('MILLER_STRACE='+json.dumps(trace.read_text()),flush=True)
sys.stdout.write(result.stdout);sys.stderr.write(result.stderr)
raise SystemExit(result.returncode)
'''
        result = self.private(['env', 'GTK_A11Y=test',
            f'GSETTINGS_SCHEMA_DIR={ROOT / "build-miller/data"}',
            sys.executable, '-c', capture, *arguments])
        match = re.search(r'^MILLER_STRACE=(.*)$', result.stdout, re.MULTILINE)
        self.assertIsNotNone(match, 'Missing dedicated strace output')
        return result, json.loads(match.group(1))

    @unittest.skipUnless(shutil.which('gdb'), 'Requires GDB for actual GIO read observations')
    def test_remote_opt_in_reads_at_most_128k(self):
        result = self.private([sys.executable, str(BACKEND), '--preview-content', '--',
            'env', 'GTK_A11Y=test', f'GSETTINGS_SCHEMA_DIR={ROOT / "build-miller/data"}',
            'gdb', '-q', '-batch', '-x', str(ROOT / 'test/miller/preview-read-observer.gdb'),
            '--args', str(BINARY), '-p', '/miller/preview/remote-bounded-read'])
        self.assertIn('ok 1 /miller/preview/remote-bounded-read', result.stdout)
        self.assertNotIn('# SKIP', result.stdout)
        self.assertIn('PREVIEW_READ_BYTES=131072', result.stdout)
        self.assertIn('PREVIEW_READ_CALLS=32', result.stdout)

    @unittest.skipUnless(shutil.which('gdb'), 'Requires GDB for independent native read observations')
    def test_native_bound_has_independent_gio_count(self):
        result = self.private(['env', 'GTK_A11Y=test',
            f'GSETTINGS_SCHEMA_DIR={ROOT / "build-miller/data"}',
            'gdb', '-q', '-batch', '-x', str(ROOT / 'test/miller/preview-read-observer.gdb'),
            '--args', str(BINARY), '-p', '/miller/preview/text-bounds'])
        self.assertIn('ok 1 /miller/preview/text-bounds', result.stdout)
        self.assertIn('PREVIEW_READ_BYTES=262144', result.stdout)
        self.assertIn('PREVIEW_READ_CALLS=64', result.stdout)

    @unittest.skipUnless(shutil.which('strace'), 'Requires strace for actual read observations')
    def test_native_reads_stop_at_128k_each(self):
        result, trace = self.trace(['-f', '-qq', '-yy', '-s', '0', '-e', 'trace=openat,read,close',
            str(BINARY), '-p', '/miller/preview/text-bounds'])
        self.assertIn('ok 1 /miller/preview/text-bounds', result.stdout)
        # Threads share descriptors. Match annotated fixture paths, not arbitrary
        # descriptor numbers or unrelated subprocess/system-file reads.
        handles, current, pending = [], {}, {}
        for line in trace.splitlines():
            prefix = re.match(r'(?:\[pid\s+(\d+)\]|(\d+))\s+(.*)', line)
            pid, call = ((prefix.group(1) or prefix.group(2)), prefix.group(3)) if prefix else ('main', line)
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

    @unittest.skipUnless(shutil.which('strace'), 'Requires strace for actual provider observations')
    def test_image_provider_runs_with_native_sandbox(self):
        result, trace = self.trace(['-f', '-qq', '-s', '8192', '-e', 'trace=execve',
            str(BINARY), '-p', '/miller/preview/png-jpeg-still'])
        self.assertIn('ok 1 /miller/preview/png-jpeg-still', result.stdout)
        launches = []
        for line in trace.splitlines():
            match = re.search(r'execve\("([^\"]+)", (\[.*\]), 0x', line)
            if not match or '= 0' not in line or Path(match.group(1)).name != 'bwrap':
                continue
            args = ast.literal_eval(match.group(2))
            if any('glycin-loaders/' in arg for arg in args):
                launches.append(args)
        self.assertTrue(launches, 'No successful sandboxed glycin loader launch observed')
        for args in launches:
            self.assertIn('--unshare-all', args)
            self.assertIn('--seccomp', args)
            self.assertIn('--clearenv', args)
            self.assertNotIn('--share-net', args)
            self.assertFalse(any(re.search(r'/nautilus\.[^/]+', arg) for arg in args),
                             'Provider received the original fixture directory')
        print(f'Observed glycin sandbox launches: {len(launches)}; unshare-all/seccomp/clearenv; no source-directory bind.', flush=True)

    @unittest.skipUnless(shutil.which('gdb'), 'Requires GDB for actual library request counts')
    def test_rapid_changes_coalesce_actual_decoder_requests(self):
        result = self.private(['env', 'GTK_A11Y=test',
            f'GSETTINGS_SCHEMA_DIR={ROOT / "build-miller/data"}',
            'gdb', '--batch', '-x', str(ROOT / 'test/miller/preview-loader-observer.gdb'),
            '--args', str(BINARY), '-p', '/miller/preview/rapid-replacement-budget'])
        self.assertIn('ok 1 /miller/preview/rapid-replacement-budget', result.stdout)
        self.assertRegex(result.stdout, r'(?m)^GLYCIN_PREVIEW_REQUESTS=2$')
        print('Observed actual glycin API requests for1,000 selection replacements: 2 (original and latest).', flush=True)

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
