#!/usr/bin/python3
"""Deployment scripts reject incomplete inputs before invoking system tools."""
import pathlib
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]

class DeploymentInputs(unittest.TestCase):
    def test_missing_package_is_rejected(self):
        result = subprocess.run([str(ROOT / 'tools/miller/install-default.sh'), '/tmp/miller-no-such-package.rpm'], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Package does not exist', result.stderr)

    def test_missing_argument_shows_usage(self):
        result = subprocess.run([str(ROOT / 'tools/miller/install-default.sh')], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('usage:', result.stderr)

    def test_restore_requires_saved_state(self):
        import tempfile
        import os
        with tempfile.TemporaryDirectory() as state:
            result = subprocess.run([str(ROOT / 'tools/miller/restore-stock.sh')], env={**os.environ, 'XDG_STATE_HOME': state}, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('No saved deployment state', result.stderr)

if __name__ == '__main__':
    unittest.main()
