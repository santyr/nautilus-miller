#!/usr/bin/python3
"""External IME runs only inside the owned test session and masked home."""
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
SESSION = ROOT / 'tools/miller/with-test-session.py'
IME = ROOT / 'tools/miller/with-test-ime.py'


class IMESession(unittest.TestCase):
    def test_refuse_host_session(self):
        env = dict(os.environ)
        env.pop('MILLER_TEST_SESSION', None)
        result = subprocess.run([sys.executable, str(IME), '--', 'true'],
                                env=env, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('owned private session', result.stderr)

    def test_private_engine_home_and_exit_status(self):
        code = '''import os,pathlib,json,gi
gi.require_version('IBus','1.0')
from gi.repository import IBus
runtime=pathlib.Path(os.environ['XDG_RUNTIME_DIR'])
assert os.environ['IBUS_ADDRESS']=='unix:path='+str(runtime/'ibus-test-bus')
assert pathlib.Path(os.environ['MILLER_TEST_IME_HOME']).parent==runtime.parent
assert (pathlib.Path(os.environ['MILLER_TEST_IME_HOME'])/'mount-proof').read_text()=='owned'
assert os.environ['GTK_IM_MODULE']=='ibus'
assert os.environ['MILLER_TEST_IME']=='anthy'
assert os.environ['HOME']==os.environ['MILLER_ORIGINAL_HOME']
IBus.init(); bus=IBus.Bus.new()
assert bus.is_connected()
assert bus.get_global_engine().get_name()=='anthy'
print(json.dumps({'root':str(runtime.parent),'home':os.environ['HOME'],'engine':'anthy'}),flush=True)
raise SystemExit(23)
'''
        result = subprocess.run([sys.executable, str(SESSION), '--', sys.executable,
            str(IME), '--', sys.executable, '-c', code],
            env={**os.environ, 'MILLER_ORIGINAL_HOME': os.environ['HOME']},
            capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 23, result.stderr)
        state = json.loads(result.stdout)
        self.assertEqual(state['engine'], 'anthy')
        self.assertFalse(Path(state['root']).exists())

    def test_refuse_mismatched_bus_before_starting_engine(self):
        code = '''import os,subprocess,sys,pathlib
env={**os.environ,'DBUS_SESSION_BUS_ADDRESS':'unix:path=/not-our-bus'}
result=subprocess.run([sys.executable,sys.argv[1],'--','true'],env=env,capture_output=True,text=True)
assert result.returncode!=0 and 'session metadata' in result.stderr, result.stderr
assert not (pathlib.Path(os.environ['XDG_RUNTIME_DIR']).parent/'ime-home').exists()
'''
        result = subprocess.run([sys.executable, str(SESSION), '--', sys.executable,
            '-c', code, str(IME)], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
