#!/usr/bin/python3
"""Inspect real pane controls in two processes sharing private native dconf."""
import os
from pathlib import Path
import re
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
SESSION = ROOT / 'tools/miller/with-test-session.py'
HOME_SESSION = ROOT / 'tools/miller/with-test-home.py'
BINARY = ROOT / 'build-miller/test/automated/display/test-miller-preview-ui'


def inside():
    env = {**os.environ, 'GTK_A11Y': 'test', 'GSETTINGS_BACKEND': 'dconf',
           'GSETTINGS_SCHEMA_DIR': str(ROOT / 'build-miller/data')}
    pids = []
    for phase in ('write', 'read'):
        result = subprocess.run([str(BINARY), '-p', '/miller/preview-ui/process-state'],
            env={**env, 'MILLER_PREVIEW_STATE_PHASE': phase},
            capture_output=True, text=True, timeout=30)
        if result.returncode:
            print(result.stdout + result.stderr, file=sys.stderr)
            return result.returncode
        match = re.search(r'Preview persistence ' + phase + r' process (\d+)', result.stdout)
        assert match, result.stdout
        pids.append(int(match[1]))
        print(f'Preview {phase}: process {pids[-1]} passed', flush=True)
    assert pids[0] != pids[1]
    assert (Path(env['XDG_CONFIG_HOME']) / 'dconf/user').is_file()
    print('Inspector visibility, width, remote policy and actual pane sizing survived restart.', flush=True)
    return 0


class PreviewRestart(unittest.TestCase):
    @unittest.skipUnless(BINARY.is_file(), 'Build test-miller-preview-ui first')
    def test_native_dconf_restart(self):
        result = subprocess.run([sys.executable, str(SESSION), '--', sys.executable,
            str(HOME_SESSION), '--', sys.executable, str(Path(__file__).resolve()), '--inside'],
            cwd=ROOT, capture_output=True, text=True, timeout=80)
        self.assertEqual(result.returncode, 0, (result.stdout + result.stderr)[-6000:])
        self.assertIn('actual pane sizing survived restart', result.stdout)
        print(result.stdout, end='', flush=True)


if __name__ == '__main__':
    if '--inside' in sys.argv:
        raise SystemExit(inside())
    unittest.main()
