#!/usr/bin/python3
"""Actual canonical Sushi with genuine private Wayland input in both profiles."""
from pathlib import Path
import re
import subprocess
import sys
import unittest
from urllib.parse import unquote, urlparse
from gi.repository import GLib

ROOT = Path(__file__).resolve().parents[2]
SESSION = ROOT / 'tools/miller/with-test-session.py'
HOME_SESSION = ROOT / 'tools/miller/with-test-home.py'


class QuickLook(unittest.TestCase):
    def actual(self, build, mode, paths, expected):
        binary = ROOT / build / 'test/automated/display/test-miller-preview-ui'
        if not binary.is_file() or not Path('/usr/libexec/org.gnome.NautilusPreviewer').is_file():
            self.skipTest('Requires both built profiles and installed canonical Sushi')
        controlled = mode in ('missing', 'restart', 'pending')
        command = [sys.executable, str(SESSION)]
        if controlled:
            command.append('--without-previewer')
        command += ['--', sys.executable, str(HOME_SESSION), '--', 'env',
            f'MILLER_TEST_QUICK_LOOK={mode}', 'GTK_A11Y=test',
            'G_DBUS_DEBUG=message', f'GSETTINGS_SCHEMA_DIR={ROOT / build / "data"}',
        ]
        if mode in ('restart', 'pending'):
            command.append('MILLER_TEST_OWN_SUSHI=1')
        command.append(str(binary))
        for path in paths:
            command += ['-p', '/miller/quicklook/' + path]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=90)
        self.assertEqual(result.returncode, 0, (result.stdout + result.stderr)[-7000:])
        for number, path in enumerate(paths, 1):
            self.assertIn(f'ok {number} /miller/quicklook/{path}', result.stdout)
        self.assertNotIn('# SKIP', result.stdout)
        if mode != 'missing':
            processes = set(re.findall(r'Actual canonical Sushi process (\d+) \(/usr/bin/gjs-console\)', result.stdout))
            self.assertGreaterEqual(len(processes), len(paths) + 1 if mode in ('restart', 'pending') else 1)
        calls, replies, selections = [], set(), set()
        for message in result.stdout.split('GDBus-debug:Message:')[1:]:
            if "member -> 'ShowFile'" in message and 'SENT D-Bus' in message:
                self.assertIn("interface -> 'org.gnome.NautilusPreviewer2'", message)
                self.assertIn("path -> objectpath '/org/gnome/NautilusPreviewer'", message)
                body = re.search(r"Body: (.*)", message)
                self.assertIsNotNone(body, message)
                destination = re.search(r"destination -> '([^']+)'", message).group(1)
                serial = int(re.search(r'Serial:\s+(\d+)', message).group(1))
                uri, handle, close, token = GLib.Variant.parse(
                    GLib.VariantType.new('(ssbs)'), body.group(1), None, None).unpack()
                self.assertTrue(handle.startswith('wayland:'), 'Missing actual foreign parent handle')
                calls.append((destination, serial, Path(unquote(urlparse(uri).path)).name,
                              'true' if close else 'false'))
            if re.search(r'Type:\s+method-return', message):
                serial = re.search(r'reply-serial -> uint32 (\d+)', message)
                sender = re.search(r"sender -> '([^']+)'", message)
                if serial and sender:
                    replies.add((sender.group(1), int(serial.group(1))))
            if "member -> 'SelectionEvent'" in message and 'RECEIVED D-Bus' in message:
                sender = re.search(r"sender -> '([^']+)'", message)
                if sender:
                    selections.add(sender.group(1))
        self.assertEqual([(name, close) for _, _, name, close in calls], expected)
        for destination, serial, _, _ in calls:
            self.assertIn((destination, serial), replies, 'Actual provider did not acknowledge ShowFile')
            if mode == '1':
                self.assertIn(destination, selections, 'No selection-navigation signal from actual provider')
        print(f'{build}: {paths}; actual acknowledged ShowFile calls={expected}.', flush=True)

    def basic(self, build):
        self.actual(build, '1', ['actual-basic'],
                    [('one.txt', 'true'), ('two.txt', 'false'), ('two.txt', 'true')])

    def test_miller_uses_canonical_provider(self):
        self.basic('build-miller')

    def test_canonical_uses_canonical_provider(self):
        self.basic('build-preview-canonical')

    def test_miller_tab_ownership(self):
        self.actual('build-miller', 'owner', ['tab-switch', 'tab-close'], [('one.txt', 'true')] * 3)

    def test_canonical_tab_ownership(self):
        self.actual('build-preview-canonical', 'owner', ['tab-switch', 'tab-close'], [('one.txt', 'true')] * 3)

    def test_miller_missing_provider(self):
        self.actual('build-miller', 'missing', ['missing-provider'], [])

    def test_canonical_missing_provider(self):
        self.actual('build-preview-canonical', 'missing', ['missing-provider'], [])

    def test_miller_restart(self):
        self.actual('build-miller', 'restart', ['restart', 'restart-latest-selection'],
                    [('one.txt', 'true')] * 3 + [('two.txt', 'false')])

    def test_canonical_restart(self):
        self.actual('build-preview-canonical', 'restart', ['restart', 'restart-latest-selection'],
                    [('one.txt', 'true')] * 3 + [('two.txt', 'false')])

    def test_miller_pending_owner(self):
        self.actual('build-miller', 'pending', ['pending-entry', 'pending-tab-switch', 'pending-tab-close',
                    'pending-deselection', 'pending-removal'], [('one.txt', 'true')] * 9)

    def test_canonical_pending_owner(self):
        self.actual('build-preview-canonical', 'pending', ['pending-entry', 'pending-tab-switch', 'pending-tab-close',
                    'pending-deselection', 'pending-removal'], [('one.txt', 'true')] * 9)

    def test_miller_shared_provider(self):
        self.actual('build-miller', 'foreign', ['foreign-selection', 'foreign-close'],
                    [('one.txt', 'true'), ('one.txt', 'false')] * 2)

    def test_canonical_shared_provider(self):
        self.actual('build-preview-canonical', 'foreign', ['foreign-selection', 'foreign-close'],
                    [('one.txt', 'true'), ('one.txt', 'false')] * 2)


if __name__ == '__main__':
    unittest.main()
