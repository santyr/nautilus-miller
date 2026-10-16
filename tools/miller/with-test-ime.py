#!/usr/bin/python3
"""Run an Anthy test in the private compositor session, with a masked home.

Requires with-test-session.py. Neither HOME nor the desktop's input method is
changed. The daemon and its engines see an empty writable mount at the user's
home path; other host files are read-only. Only the owned XDG tree is writable.
"""
import argparse
import configparser
import os
from pathlib import Path
import pwd
import stat
import subprocess
import sys
import time


def owned(path, mode=None):
    info = path.lstat()
    if path.resolve() != path or info.st_uid != os.getuid() or stat.S_ISLNK(info.st_mode):
        raise RuntimeError('IME requires owned canonical paths')
    if mode is not None and stat.S_IMODE(info.st_mode) != mode:
        raise RuntimeError('IME requires private directory/file permissions')


def private_root():
    marker_name = os.environ.get('MILLER_TEST_SESSION')
    if not marker_name:
        raise RuntimeError('IME requires the owned private session')
    runtime = Path(os.environ.get('XDG_RUNTIME_DIR', '/'))
    root = runtime.parent
    if runtime.name != 'runtime' or not root.name.startswith('nautilus-miller-tests-'):
        raise RuntimeError('IME requires the owned private session')
    owned(root, 0o700)
    owned(runtime, 0o700)
    marker = Path(marker_name)
    if marker != runtime / 'miller-session.ini':
        raise RuntimeError('IME session metadata differs')
    owned(marker, 0o600)
    config = configparser.ConfigParser()
    config.read(marker)
    session = config['session']
    for name, value in [('runtime', str(runtime)), ('display', os.environ.get('WAYLAND_DISPLAY')),
                        ('bus-address', os.environ.get('DBUS_SESSION_BUS_ADDRESS'))]:
        if session.get(name) != value:
            raise RuntimeError('IME session metadata differs')
    display = Path(session['display'])
    if display != runtime / 'miller-test' or not stat.S_ISSOCK(display.stat().st_mode):
        raise RuntimeError('IME requires the private compositor socket')
    owned(display)
    owner = subprocess.check_output(['gdbus', 'call', '--session', '--dest', 'org.freedesktop.DBus',
        '--object-path', '/org/freedesktop/DBus', '--method',
        'org.freedesktop.DBus.GetConnectionUnixProcessID', 'org.gnome.Mutter.RemoteDesktop'], text=True, timeout=5)
    if owner.strip() != f"(uint32 {int(session['compositor-pid'])},)":
        raise RuntimeError('IME session compositor owner differs')
    for key, name in [('XDG_CONFIG_HOME', 'config'), ('XDG_DATA_HOME', 'data'),
                      ('XDG_CACHE_HOME', 'cache'), ('XDG_STATE_HOME', 'state'), ('TMPDIR', 'tmp')]:
        path = Path(os.environ.get(key, '/'))
        if path != root / name:
            raise RuntimeError('IME requires the private XDG tree')
        owned(path, 0o700)
    return root, runtime


def run(command):
    root, runtime = private_root()
    home = Path(os.environ['HOME'])
    if home != Path(pwd.getpwuid(os.getuid()).pw_dir) or home.resolve() != home:
        raise RuntimeError('IME requires the current user canonical home path')
    masked = root / 'ime-home'
    masked.mkdir(mode=0o700)
    address = 'unix:path=' + str(runtime / 'ibus-test-bus')
    # Drop inherited input-method discovery before setting the private endpoint.
    env = {k: v for k, v in os.environ.items() if not k.startswith('IBUS_')}
    env.update(IBUS_ADDRESS=address, IBUS_ADDRESS_FILE=str(runtime / 'ibus-address'),
               GTK_IM_MODULE='ibus', MILLER_TEST_IME='anthy', MILLER_TEST_IME_HOME=str(masked))
    prefix = ['bwrap', '--ro-bind', '/', '/', '--bind', str(masked), str(home),
        '--bind', str(root), str(root), '--dev', '/dev', '--proc', '/proc',
        '--die-with-parent', '--chdir', '/']
    # Prove the mount identity and write boundary before running any engine.
    proof = ('import pathlib,os; p=pathlib.Path.home(); '
             'assert p.stat().st_ino==int(os.environ["MILLER_HOME_INODE"]); '
             'assert p.stat().st_dev==int(os.environ["MILLER_HOME_DEVICE"]); '
             'assert os.statvfs("/").f_flag & os.ST_RDONLY; '
             '(p/"mount-proof").write_text("owned")')
    subprocess.run(prefix + [sys.executable, '-c', proof], check=True,
        env={**env, 'MILLER_HOME_INODE': str(masked.stat().st_ino),
             'MILLER_HOME_DEVICE': str(masked.stat().st_dev)}, timeout=10)
    # Keep the host PID namespace: IBus checks parent PIDs and authenticates
    # external clients. The mount namespace protects all legacy home paths.
    logpath = root / 'ibus.log'
    bus = None
    with logpath.open('w') as log:
        daemon = subprocess.Popen(prefix + ['ibus-daemon', '--single', '--panel=disable',
            '--emoji-extension=disable', '--config=disable', '--cache=none', '--address=' + address],
            env=env, stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 15
            while not (runtime / 'ibus-address').exists():
                if daemon.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError('Private IBus did not become ready: ' + logpath.read_text()[-2000:])
                time.sleep(0.05)
            # Import after private endpoint configuration, never connect to the
            # desktop's IBus. This only controls the owned test daemon.
            for key in list(os.environ):
                if key.startswith('IBUS_'):
                    del os.environ[key]
            os.environ.update({k: v for k, v in env.items() if k.startswith('IBUS_')})
            import gi
            gi.require_version('IBus', '1.0')
            from gi.repository import IBus, GLib
            IBus.init()
            bus = IBus.Bus.new()
            if not bus.is_connected() or not bus.set_global_engine('anthy'):
                raise RuntimeError('Private Anthy engine unavailable')
            engine = bus.get_global_engine()
            if engine is None or engine.get_name() != 'anthy':
                raise RuntimeError('Private Anthy engine was not selected')
            print('Private Anthy ready; home mount verified.', file=sys.stderr, flush=True)
            return subprocess.call(command, env=env)
        finally:
            if bus is not None and bus.is_connected():
                try:
                    bus.exit(False)
                except GLib.Error:
                    # A test may have disconnected the owned provider already.
                    pass
            try:
                daemon.wait(timeout=5)
            except subprocess.TimeoutExpired:
                daemon.terminate()
                try:
                    daemon.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    daemon.kill()
                    daemon.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command
    if command and command[0] == '--':
        command = command[1:]
    if not command:
        parser.error('provide a test command after --')
    return run(command)


if __name__ == '__main__':
    raise SystemExit(main())
