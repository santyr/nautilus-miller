#!/usr/bin/python3
"""The real-session harness isolates settings and propagates test failures."""
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest

SCRIPT = Path(__file__).resolve().parents[2] / 'tools/miller/with-test-session.py'

class Session(unittest.TestCase):
    def test_private_directories_and_absolute_socket(self):
        code = ('import json,os,pathlib; '
                'print(json.dumps({key:os.environ[key] for key in '
                '("XDG_RUNTIME_DIR","XDG_CONFIG_HOME","XDG_DATA_HOME","WAYLAND_DISPLAY")}))')
        result = subprocess.run([sys.executable, str(SCRIPT), '--', sys.executable, '-c', code],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        state = json.loads(result.stdout)
        self.assertNotEqual(state['XDG_RUNTIME_DIR'], os.environ.get('XDG_RUNTIME_DIR'))
        self.assertTrue(Path(state['WAYLAND_DISPLAY']).is_absolute())
        root = Path(state['XDG_RUNTIME_DIR']).parent
        self.assertFalse(root.exists(), 'Owned session and FUSE mounts must be cleaned up')
        self.assertFalse(Path(state['XDG_DATA_HOME']).exists())

    def test_command_failure_propagates(self):
        result = subprocess.run([sys.executable, str(SCRIPT), '--', sys.executable, '-c', 'raise SystemExit(23)'],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 23, result.stderr)

if __name__ == '__main__':
    unittest.main()
