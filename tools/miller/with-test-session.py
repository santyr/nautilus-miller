#!/usr/bin/python3
"""Run a command in disposable XDG directories, D-Bus and a headless Mutter compositor."""
import argparse
import configparser
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time


def inside(command):
    runtime = Path(os.environ['XDG_RUNTIME_DIR'])
    if (runtime.name != 'runtime' or not runtime.parent.name.startswith('nautilus-miller-tests-')
            or runtime.resolve() != runtime or runtime.stat().st_uid != os.getuid()
            or runtime.stat().st_mode & 0o077):
        raise RuntimeError('Input harness requires its owned private runtime')
    log = runtime.parent / 'mutter.log'
    with log.open('w') as output:
        compositor = subprocess.Popen(['mutter', '--headless', '--no-x11',
            '--virtual-monitor=1280x900', '--wayland-display=miller-test'], stdout=output, stderr=output)
        try:
            deadline = time.monotonic() + 15
            socket = runtime / 'miller-test'
            while True:
                if compositor.poll() is not None or time.monotonic() >= deadline:
                    print(log.read_text(), file=sys.stderr)
                    raise RuntimeError('Disposable compositor did not become ready')
                if socket.exists():
                    try:
                        owner = subprocess.run(['gdbus', 'call', '--session',
                            '--dest', 'org.freedesktop.DBus', '--object-path', '/org/freedesktop/DBus',
                            '--method', 'org.freedesktop.DBus.GetConnectionUnixProcessID',
                            'org.gnome.Mutter.RemoteDesktop'], capture_output=True, text=True, timeout=2)
                        if owner.returncode == 0 and owner.stdout.strip() == f'(uint32 {compositor.pid},)':
                            break
                    except subprocess.TimeoutExpired:
                        pass
                time.sleep(0.05)
            # An absolute socket survives a child harness changing XDG_RUNTIME_DIR.
            env = {**os.environ, 'WAYLAND_DISPLAY': str(socket), 'GDK_BACKEND': 'wayland'}
            marker = runtime / 'miller-session.ini'
            config = configparser.ConfigParser()
            config['session'] = {'runtime': str(runtime), 'display': str(socket),
                'bus-address': os.environ['DBUS_SESSION_BUS_ADDRESS'], 'compositor-pid': str(compositor.pid)}
            with os.fdopen(os.open(marker, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), 'w') as output:
                config.write(output)
            env['MILLER_TEST_SESSION'] = str(marker)
            return subprocess.call(command, env=env)
        finally:
            compositor.terminate()
            try:
                compositor.wait(timeout=10)
            except subprocess.TimeoutExpired:
                # This is our disposable compositor, never the host file manager.
                compositor.kill()
                compositor.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inside', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command
    if command and command[0] == '--':
        command = command[1:]
    if not command:
        parser.error('provide a test command after --')
    if args.inside:
        return inside(command)
    with tempfile.TemporaryDirectory(prefix='nautilus-miller-tests-') as directory:
        root = Path(directory)
        env = dict(os.environ)
        for key, name in [('XDG_RUNTIME_DIR','runtime'), ('XDG_CONFIG_HOME','config'),
                          ('XDG_DATA_HOME','data'), ('XDG_CACHE_HOME','cache'), ('XDG_STATE_HOME','state')]:
            path = root / name
            path.mkdir(mode=0o700)
            env[key] = str(path)
        (root / 'tmp').mkdir(mode=0o700)
        env['TMPDIR'] = str(root / 'tmp')
        env.pop('DISPLAY', None)
        env.pop('WAYLAND_DISPLAY', None)
        env.pop('MILLER_TEST_SESSION', None)
        env['GVFS_DISABLE_FUSE'] = '1'
        env.setdefault('GSETTINGS_BACKEND', 'memory')
        process = subprocess.Popen(['dbus-run-session', '--', sys.executable,
            str(Path(__file__).resolve()), '--inside', '--', *command], env=env, start_new_session=True)
        try:
            return process.wait()
        finally:
            # Clean up only this invocation's process group and private services.
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            # D-Bus-activated portals may leave their private FUSE mount after
            # bus shutdown. Detach only mountpoints inside our owned runtime.
            for name in ('doc', 'gvfs'):
                mount = root / 'runtime' / name
                if not mount.is_symlink():
                    subprocess.run(['fusermount3', '-uz', str(mount)],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


if __name__ == '__main__':
    raise SystemExit(main())
