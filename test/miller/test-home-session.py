#!/usr/bin/python3
"""Home navigation must use a mounted disposable home, never the operator's files."""
import os
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
SESSION = ROOT / 'tools/miller/with-test-session.py'
HOME_SESSION = ROOT / 'tools/miller/with-test-home.py'


class HomeSession(unittest.TestCase):
    def test_refuses_host_session(self):
        env = dict(os.environ)
        env.pop('MILLER_TEST_SESSION', None)
        result = subprocess.run([sys.executable, str(HOME_SESSION), '--', 'true'],
                                env=env, capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('owned private session', result.stderr)

    def test_real_mount_masks_home_preserves_identity_and_repository(self):
        code = '''import os,pathlib,pwd,stat
home=pathlib.Path.home();masked=pathlib.Path(os.environ['MILLER_TEST_HOME'])
assert str(home)==os.environ['MILLER_ORIGINAL_HOME']==pwd.getpwuid(os.getuid()).pw_dir
assert (home.stat().st_dev,home.stat().st_ino)==(masked.stat().st_dev,masked.stat().st_ino)
assert home.stat().st_ino!=int(os.environ['MILLER_ORIGINAL_HOME_INODE'])
assert (home/'Home fixture'/'owned.txt').read_text()=='disposable home'
assert os.statvfs('/').f_flag & os.ST_RDONLY
repo=pathlib.Path(os.environ['MILLER_TEST_REPOSITORY'])
assert (repo/'src/nautilus-miller-view.c').is_file()
assert os.statvfs(repo).f_flag & os.ST_RDONLY
(home/'owned-write').write_text('private')
assert (masked/'owned-write').read_text()=='private'
for key in ['XDG_RUNTIME_DIR','XDG_CONFIG_HOME','XDG_DATA_HOME','XDG_STATE_HOME','XDG_CACHE_HOME','TMPDIR']:
    path=pathlib.Path(os.environ[key]);assert path.is_dir()
    assert not (os.statvfs(path).f_flag & os.ST_RDONLY)
'''
        env = {**os.environ, 'MILLER_ORIGINAL_HOME': os.environ['HOME'],
               'MILLER_ORIGINAL_HOME_INODE': str(Path.home().stat().st_ino)}
        result = subprocess.run([sys.executable, str(SESSION), '--', sys.executable,
            str(HOME_SESSION), '--', sys.executable, '-c', code],
            env=env, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_failed_child_status_and_cleanup(self):
        code = '''import os,pathlib,subprocess,sys
root=pathlib.Path(os.environ['XDG_RUNTIME_DIR']).parent
result=subprocess.run([sys.executable,sys.argv[1],'--',sys.executable,'-c','raise SystemExit(23)'],capture_output=True,text=True,timeout=15)
assert result.returncode==23,result.stderr
assert not list(root.glob('navigation-home-*'))
'''
        result = subprocess.run([sys.executable, str(SESSION), '--', sys.executable,
            '-c', code, str(HOME_SESSION)], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_compositor_identity_remains_verifiable(self):
        code = '''import configparser,os,pathlib
config=configparser.ConfigParser();config.read(os.environ['MILLER_TEST_SESSION'])
pid=int(config['session']['compositor-pid']);proc=pathlib.Path('/proc',str(pid))
assert proc.stat().st_uid==os.geteuid()
assert (proc/'exe').readlink().name=='mutter'
command=(proc/'cmdline').read_bytes().split(b'\\0')
assert b'--headless' in command and b'--wayland-display=miller-test' in command
'''
        result = subprocess.run([sys.executable, str(SESSION), '--', sys.executable,
            str(HOME_SESSION), '--', sys.executable, '-c', code],
            capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_metadata_mismatch_prevents_mount(self):
        code = '''import os,pathlib,subprocess,sys
root=pathlib.Path(os.environ['XDG_RUNTIME_DIR']).parent
result=subprocess.run([sys.executable,sys.argv[1],'--','true'],env={**os.environ,'DBUS_SESSION_BUS_ADDRESS':'unix:path=/not-our-bus'},capture_output=True,text=True,timeout=10)
assert result.returncode!=0 and 'session metadata' in result.stderr,result.stderr
assert not list(root.glob('navigation-home-*'))
'''
        result = subprocess.run([sys.executable, str(SESSION), '--', sys.executable,
            '-c', code, str(HOME_SESSION)], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
