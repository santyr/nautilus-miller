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

    def test_inside_refuses_unowned_runtime(self):
        result = subprocess.run([sys.executable, str(SCRIPT), '--inside', '--', 'true'],
            env={**os.environ, 'XDG_RUNTIME_DIR': f'/run/user/{os.getuid()}'},
            capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('owned private runtime', result.stderr)

    def test_private_input_marker_and_service_owner(self):
        code = '''import configparser,os,pathlib,stat,subprocess,json
path=pathlib.Path(os.environ['MILLER_TEST_SESSION'])
marker=configparser.ConfigParser()
marker.read(path)
data=marker['session']
assert path.parent == pathlib.Path(os.environ['XDG_RUNTIME_DIR'])
assert stat.S_IMODE(path.stat().st_mode) == 0o600
assert data['runtime'] == os.environ['XDG_RUNTIME_DIR']
assert data['display'] == os.environ['WAYLAND_DISPLAY']
assert data['bus-address'] == os.environ['DBUS_SESSION_BUS_ADDRESS']
result=subprocess.check_output(['gdbus','call','--session','--dest','org.freedesktop.DBus',
'--object-path','/org/freedesktop/DBus','--method',
'org.freedesktop.DBus.GetConnectionUnixProcessID','org.gnome.Mutter.RemoteDesktop'],text=True)
assert result.strip() == '(uint32 '+data['compositor-pid']+',)'
print(json.dumps({'marker':str(path),'pid':int(data['compositor-pid'])}))
'''
        result=subprocess.run([sys.executable,str(SCRIPT),'--',sys.executable,'-c',code],
                              capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        state=json.loads(result.stdout)
        self.assertGreater(state['pid'],0)
        self.assertFalse(Path(state['marker']).exists())

    def test_forged_home_marker_cannot_change_cache(self):
        env={**os.environ,'MILLER_TEST_HOME':'/tmp/navigation-home-forged',
             'MILLER_TEST_REPOSITORY':str(SCRIPT.parents[2])}
        result=subprocess.run([sys.executable,str(SCRIPT),'--','true'],env=env,
                              capture_output=True,text=True,timeout=20)
        self.assertNotEqual(result.returncode,0)
        self.assertIn('mounted disposable home',result.stderr)

if __name__ == '__main__':
    unittest.main()
