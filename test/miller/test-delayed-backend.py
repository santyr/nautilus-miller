#!/usr/bin/python3
"""A real, owned GVfs enumeration can be paused without signaling host services."""
import os
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
SESSION = ROOT / 'tools/miller/with-test-session.py'
BACKEND = ROOT / 'tools/miller/with-delayed-backend.py'
IMPORT = '''import importlib.util,sys
spec=importlib.util.spec_from_file_location('delayed_backend',sys.argv[1])
backend=importlib.util.module_from_spec(spec)
spec.loader.exec_module(backend)
'''


class DelayedBackend(unittest.TestCase):
    def private(self, code):
        return subprocess.run([sys.executable, str(SESSION), '--', sys.executable,
            '-c', code, str(BACKEND)], capture_output=True, text=True, timeout=60)

    def test_refuse_host_session(self):
        env = dict(os.environ)
        env.pop('MILLER_TEST_SESSION', None)
        result = subprocess.run([sys.executable, str(BACKEND), '--', 'true'],
            env=env, capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('owned private session', result.stderr)

    def test_refuse_forged_session_before_mount(self):
        code = IMPORT + '''import configparser,os,pathlib,subprocess
marker=pathlib.Path(os.environ['MILLER_TEST_SESSION'])
original=marker.read_text()
config=configparser.ConfigParser();config.read_string(original)
for field,value in [('bus-address','unix:path=/not-our-bus'),('compositor-pid',str(os.getpid()))]:
    config.read_string(original);config['session'][field]=value
    with marker.open('w') as stream: config.write(stream)
    try:
        backend.private_session()
    except RuntimeError:
        pass
    else:
        raise AssertionError('Forged metadata authorized a backend fixture')
    assert not list(marker.parent.parent.glob('delayed-backend-*'))
marker.write_text(original)
'''
        result = self.private(code)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_refuse_wrong_pid_executable_and_bus(self):
        code = IMPORT + '''import os
root,runtime,address=backend.private_session()
for pid in [os.getpid(),999999999]:
    try:
        backend.open_verified_backend(pid,address)
    except RuntimeError:
        pass
    else:
        raise AssertionError('A non-backend process was authorized')
with backend.BackendFixture() as fixture:
    try:
        backend.open_verified_backend(fixture.process.pid,'unix:path=/not-our-bus')
    except RuntimeError:
        pass
    else:
        raise AssertionError('A different bus was authorized')
    fixture.process.pause()
    fixture.process.resume()
'''
        result = self.private(code)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_actual_enumeration_waits_and_resumes(self):
        code = IMPORT + '''import subprocess,time
with backend.BackendFixture() as fixture:
    fixture.process.pause()
    pending=subprocess.Popen(['gio','list',fixture.uri+'A/'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    try:
        time.sleep(.25)
        assert pending.poll() is None, 'The real backend was not delayed'
    finally:
        fixture.process.resume()
    out,err=pending.communicate(timeout=10)
    assert pending.returncode==0 and out.strip()=='a.txt',(out,err)
    result=subprocess.run(['gio','list',fixture.uri+'B/'],capture_output=True,text=True,timeout=10)
    assert result.returncode==0 and result.stdout.strip()=='b.txt',result.stderr
'''
        result = self.private(code)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_child_failure_resumes_and_unmounts(self):
        code = r'''import os,pathlib,subprocess,sys
child="""import configparser,os,socket
config=configparser.ConfigParser();config.read(os.environ['MILLER_TEST_BACKEND'])
state=config['backend']
with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as connection:
    connection.connect(state['control'])
    connection.sendall(b'pause\\n')
    assert connection.recv(100)==b'ok\\n'
print(state['pid'],flush=True)
raise SystemExit(23)
"""
result=subprocess.run([sys.executable,sys.argv[1],'--',sys.executable,'-c',child],capture_output=True,text=True,timeout=25)
assert result.returncode==23,result.stderr
pid=int(result.stdout.strip())
status=pathlib.Path('/proc')/str(pid)/'status'
if status.exists():
    state=next(line for line in status.read_text().splitlines() if line.startswith('State:'))
    assert 'T (stopped)' not in state,state
runtime=pathlib.Path(os.environ['XDG_RUNTIME_DIR'])
assert not (runtime/'miller-backend.ini').exists()
assert not list(runtime.parent.glob('delayed-backend-*'))
'''
        result = self.private(code)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_control_accepts_fragmented_request_and_survives_rejection(self):
        child = '''import configparser,os,socket,time
config=configparser.ConfigParser();config.read(os.environ['MILLER_TEST_BACKEND'])
control=config['backend']['control']
def request(parts):
    with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as connection:
        connection.settimeout(3);connection.connect(control)
        for part in parts:
            connection.sendall(part);time.sleep(.05)
        return connection.recv(100)
assert request([b'unknown\\n'])==b'error\\n'
assert request([b'pa',b'use\\n'])==b'ok\\n'
assert request([b'resume\\n'])==b'ok\\n'
'''
        code = f'''import subprocess,sys
result=subprocess.run([sys.executable,sys.argv[1],'--',sys.executable,'-c',{child!r}],capture_output=True,text=True,timeout=25)
assert result.returncode==0,result.stderr
'''
        result = self.private(code)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_cleanup_resumes_after_child_corrupts_metadata(self):
        child = '''import configparser,os,pathlib,socket
config=configparser.ConfigParser();config.read(os.environ['MILLER_TEST_BACKEND'])
state=config['backend']
with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as connection:
    connection.settimeout(3);connection.connect(state['control']);connection.sendall(b'pause\\n')
    assert connection.recv(100)==b'ok\\n'
marker=pathlib.Path(os.environ['MILLER_TEST_SESSION'])
session=configparser.ConfigParser();session.read(marker)
session['session']['compositor-pid']='0'
with marker.open('w') as stream:session.write(stream)
print(state['pid'],flush=True)
raise SystemExit(23)
'''
        code = f'''import os,pathlib,subprocess,sys
marker=pathlib.Path(os.environ['MILLER_TEST_SESSION']);original=marker.read_text()
try:
    result=subprocess.run([sys.executable,sys.argv[1],'--',sys.executable,'-c',{child!r}],capture_output=True,text=True,timeout=25)
finally:
    marker.write_text(original)
assert result.returncode==23,result.stderr
status=pathlib.Path('/proc')/result.stdout.strip()/'status'
if status.exists():
    state=next(line for line in status.read_text().splitlines() if line.startswith('State:'))
    assert 'T (stopped)' not in state,state
'''
        result = self.private(code)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_pause_deadline_resumes_backend_and_fails_the_run(self):
        child = '''import configparser,os,socket,subprocess,time
config=configparser.ConfigParser(interpolation=None);config.read(os.environ['MILLER_TEST_BACKEND'])
state=config['backend']
with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as connection:
    connection.settimeout(3);connection.connect(state['control']);connection.sendall(b'pause\\n')
    assert connection.recv(100)==b'ok\\n'
time.sleep(6)
result=subprocess.run(['gio','list',state['root-uri']+'A/'],capture_output=True,text=True,timeout=2)
assert result.returncode==0 and result.stdout.strip()=='a.txt',result.stderr
print('Backend resumed before the child exited',flush=True)
'''
        code = f'''import subprocess,sys
result=subprocess.run([sys.executable,sys.argv[1],'--',sys.executable,'-c',{child!r}],capture_output=True,text=True,timeout=20)
assert result.returncode!=0
assert 'Backend resumed before the child exited' in result.stdout,result.stderr
assert 'Maximum private backend pause exceeded' in result.stderr,result.stderr
'''
        result = self.private(code)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_termination_resumes_backend_and_reaps_child(self):
        child = '''import configparser,json,os,pathlib,socket,time
config=configparser.ConfigParser(interpolation=None);config.read(os.environ['MILLER_TEST_BACKEND'])
state=config['backend']
with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as connection:
    connection.settimeout(3);connection.connect(state['control']);connection.sendall(b'pause\\n')
    assert connection.recv(100)==b'ok\\n'
pathlib.Path(os.environ['XDG_RUNTIME_DIR'],'pause-ready.json').write_text(json.dumps({'backend':int(state['pid']),'child':os.getpid()}))
time.sleep(30)
'''
        code = f'''import json,os,pathlib,signal,subprocess,sys,time
runtime=pathlib.Path(os.environ['XDG_RUNTIME_DIR']);ready=runtime/'pause-ready.json'
helper=subprocess.Popen([sys.executable,sys.argv[1],'--',sys.executable,'-c',{child!r}],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
state=None;fds={{}}
try:
    end=time.monotonic()+10
    while not ready.exists():
        assert helper.poll() is None
        assert time.monotonic()<end
        time.sleep(.02)
    state=json.loads(ready.read_text())
    for name in ('backend','child'):
        pid=state[name]
        fds[name]=os.pidfd_open(pid)
        proc=pathlib.Path('/proc',str(pid))
        env=dict(item.split(b'=',1) for item in (proc/'environ').read_bytes().split(b'\\0') if b'=' in item)
        assert env.get(b'DBUS_SESSION_BUS_ADDRESS')==os.fsencode(os.environ['DBUS_SESSION_BUS_ADDRESS'])
        assert proc.stat().st_uid==os.getuid()
    helper.terminate()
    out,err=helper.communicate(timeout=10)
    assert not (runtime/'miller-backend.ini').exists(),err
    assert not pathlib.Path('/proc',str(state['child'])).exists(),'Fixture child was left running'
    status=pathlib.Path('/proc',str(state['backend']),'status')
    if status.exists():
        assert 'T (stopped)' not in status.read_text()
finally:
    if helper.poll() is None:
        helper.kill();helper.wait()
    # Retained process handles cannot retarget cleanup after PID reuse.
    for name,sig in [('backend',signal.SIGCONT),('child',signal.SIGKILL)]:
        if name in fds:
            try:signal.pidfd_send_signal(fds[name],sig)
            except ProcessLookupError:pass
            finally:os.close(fds[name])
'''
        result = self.private(code)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
