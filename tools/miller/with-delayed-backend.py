#!/usr/bin/python3
"""Run a test with a controllably delayed, private, read-only GVfs archive.

Requires with-test-session.py and Linux PIDfds. The child can request pause or
resume over an owned Unix socket; it cannot select which process is signaled.
"""
import argparse
import configparser
import os
from pathlib import Path
import signal
import socket
import stat
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse
import zipfile


ARCHIVE_BACKEND = Path('/usr/libexec/gvfsd-archive')


def owned(path, mode=None):
    info = path.lstat()
    if path.resolve() != path or info.st_uid != os.getuid() or stat.S_ISLNK(info.st_mode):
        raise RuntimeError('Backend requires owned canonical paths')
    if mode is not None and stat.S_IMODE(info.st_mode) != mode:
        raise RuntimeError('Backend requires private permissions')
    return info


def private_session():
    if not os.environ.get('MILLER_TEST_SESSION'):
        raise RuntimeError('Backend requires the owned private session')
    runtime = Path(os.environ.get('XDG_RUNTIME_DIR', '/'))
    root = runtime.parent
    if runtime.name != 'runtime' or not root.name.startswith('nautilus-miller-tests-'):
        raise RuntimeError('Backend requires the owned private session')
    owned(root, 0o700)
    owned(runtime, 0o700)
    marker = Path(os.environ['MILLER_TEST_SESSION'])
    if marker != runtime / 'miller-session.ini':
        raise RuntimeError('Backend session metadata differs')
    owned(marker, 0o600)
    config = configparser.ConfigParser(interpolation=None)
    config.read(marker)
    session = config['session']
    address = os.environ.get('DBUS_SESSION_BUS_ADDRESS')
    for name, value in [('runtime', str(runtime)), ('display', os.environ.get('WAYLAND_DISPLAY')),
                        ('bus-address', address)]:
        if session.get(name) != value:
            raise RuntimeError('Backend session metadata differs')
    display = Path(session['display'])
    if display != runtime / 'miller-test' or not stat.S_ISSOCK(owned(display).st_mode):
        raise RuntimeError('Backend requires the private compositor socket')
    pid = int(session['compositor-pid'])
    owner = subprocess.check_output(['gdbus', 'call', '--session', '--dest', 'org.freedesktop.DBus',
        '--object-path', '/org/freedesktop/DBus', '--method',
        'org.freedesktop.DBus.GetConnectionUnixProcessID', 'org.gnome.Mutter.RemoteDesktop'],
        text=True, timeout=5)
    proc = Path('/proc') / str(pid)
    command = (proc / 'cmdline').read_bytes().split(b'\0')
    if (owner.strip() != f'(uint32 {pid},)' or proc.stat().st_uid != os.getuid()
            or (proc / 'exe').resolve().name != 'mutter' or b'--headless' not in command
            or b'--wayland-display=miller-test' not in command):
        raise RuntimeError('Backend session compositor owner differs')
    for key, name in [('XDG_CONFIG_HOME', 'config'), ('XDG_DATA_HOME', 'data'),
                      ('XDG_CACHE_HOME', 'cache'), ('XDG_STATE_HOME', 'state'), ('TMPDIR', 'tmp')]:
        path = Path(os.environ.get(key, '/'))
        if path != root / name:
            raise RuntimeError('Backend requires the private XDG tree')
        owned(path, 0o700)
    return root, runtime, address


def verify_process(pid, address):
    proc = Path('/proc') / str(pid)
    try:
        if (pid <= 0 or proc.stat().st_uid != os.getuid()
                or (proc / 'exe').resolve() != ARCHIVE_BACKEND.resolve()):
            raise RuntimeError('Refusing a process other than the owned archive backend')
        env = dict(item.split(b'=', 1) for item in (proc / 'environ').read_bytes().split(b'\0')
                   if b'=' in item)
        if env.get(b'DBUS_SESSION_BUS_ADDRESS') != os.fsencode(address):
            raise RuntimeError('Archive backend bus differs')
    except OSError as error:
        raise RuntimeError('Archive backend process is unavailable') from error


class BackendProcess:
    def __init__(self, pid, address, fd, pause_seconds=5):
        self.pid, self.address, self.fd = pid, address, fd
        if type(pause_seconds) is not int or not 1 <= pause_seconds <= 10:
            raise ValueError('Private pause limit must be an integer from 1 to 10 seconds')
        self.pause_seconds = pause_seconds
        self.lock = threading.Lock()
        self.timer = None
        self.timed_out = False

    def _expire(self):
        with self.lock:
            if self.fd is not None:
                self.timed_out = True
                try:
                    signal.pidfd_send_signal(self.fd, signal.SIGCONT)
                except ProcessLookupError:
                    pass

    def pause(self):
        _, _, address = private_session()
        if address != self.address:
            raise RuntimeError('Archive backend session changed')
        verify_process(self.pid, self.address)
        with self.lock:
            if self.timer is not None:
                raise RuntimeError('Owned archive backend is already paused')
            signal.pidfd_send_signal(self.fd, signal.SIGSTOP)
            self.timer = threading.Timer(self.pause_seconds, self._expire)
            self.timer.daemon = True
            self.timer.start()
        deadline = time.monotonic() + 2
        while '\nState:\tT ' not in (Path('/proc') / str(self.pid) / 'status').read_text():
            if time.monotonic() >= deadline:
                raise RuntimeError('Owned archive backend did not pause')
            time.sleep(.01)

    def resume(self):
        # This FD was authorized before it was retained. Cleanup must resume
        # that exact process even if the child corrupts its session metadata.
        with self.lock:
            if self.timer is not None:
                self.timer.cancel()
                self.timer = None
            if self.fd is not None:
                try:
                    signal.pidfd_send_signal(self.fd, signal.SIGCONT)
                except ProcessLookupError:
                    pass

    def close(self):
        try:
            self.resume()
        finally:
            with self.lock:
                if self.fd is not None:
                    os.close(self.fd)
                    self.fd = None


def open_verified_backend(pid, address, pause_seconds=5):
    _, _, actual = private_session()
    if address != actual:
        raise RuntimeError('Archive backend bus differs')
    try:
        fd = os.pidfd_open(pid)
    except (OSError, OverflowError) as error:
        raise RuntimeError('Archive backend process is unavailable') from error
    try:
        verify_process(pid, address)
        return BackendProcess(pid, address, fd, pause_seconds)
    except BaseException:
        os.close(fd)
        raise


class BackendFixture:
    def __init__(self, pause_seconds=5):
        self.process = self.directory = self.uri = None
        if type(pause_seconds) is not int or not 1 <= pause_seconds <= 10:
            raise ValueError('Private pause limit must be an integer from 1 to 10 seconds')
        self.pause_seconds = pause_seconds

    def __enter__(self):
        root, self.runtime, address = private_session()
        if not hasattr(os, 'pidfd_open') or not hasattr(signal, 'pidfd_send_signal'):
            raise RuntimeError('Delayed backend requires Linux PIDfd support')
        if (self.runtime / 'miller-backend.ini').exists():
            raise RuntimeError('A delayed backend fixture already owns this session')
        self.directory = tempfile.TemporaryDirectory(prefix='delayed-backend-', dir=root)
        try:
            archive = Path(self.directory.name) / 'branches.zip'
            with zipfile.ZipFile(archive, 'w') as output:
                output.writestr('A/', '')
                output.writestr('A/a.txt', 'a')
                output.writestr('B/', '')
                output.writestr('B/b.txt', 'b')
            import gi
            gi.require_version('Gio', '2.0')
            from gi.repository import Gio, GLib
            monitor = Gio.VolumeMonitor.get()
            before = {mount.get_root().get_uri() for mount in monitor.get_mounts()}
            requested = 'archive://' + urllib.parse.quote(archive.as_uri(), safe='') + '/'
            subprocess.run(['gio', 'mount', requested], check=True, capture_output=True, timeout=10)
            deadline = time.monotonic() + 10
            while self.uri is None:
                while GLib.MainContext.default().iteration(False):
                    pass
                roots = [mount.get_root().get_uri() for mount in monitor.get_mounts()
                         if mount.get_root().get_uri() not in before
                         and mount.get_name() == archive.name
                         and mount.get_root().has_uri_scheme('archive')]
                if len(roots) == 1:
                    # GVfs canonical escaping differs from the requested URI.
                    self.uri = roots[0]
                elif len(roots) > 1 or time.monotonic() >= deadline:
                    raise RuntimeError('Could not identify the owned canonical archive mount')
                else:
                    time.sleep(.02)
            candidates = []
            for proc in Path('/proc').iterdir():
                if not proc.name.isdecimal():
                    continue
                try:
                    if (proc / 'comm').read_text().strip() != 'gvfsd-archive':
                        continue
                    verify_process(int(proc.name), address)
                    candidates.append(int(proc.name))
                except (OSError, RuntimeError):
                    continue
            if len(candidates) != 1:
                raise RuntimeError('Could not identify exactly one private archive backend')
            self.process = open_verified_backend(candidates[0], address, self.pause_seconds)
            return self
        except BaseException:
            self.close()
            raise

    def __exit__(self, kind, value, traceback):
        self.close()

    def close(self):
        try:
            if self.process is not None:
                self.process.close()
            if self.uri is not None:
                subprocess.run(['gio', 'mount', '-u', self.uri], check=True,
                               capture_output=True, timeout=10)
                self.uri = None
        finally:
            if self.directory is not None:
                self.directory.cleanup()
                self.directory = None
        if self.process is not None and self.process.timed_out:
            raise RuntimeError('Maximum private backend pause exceeded; the test is not a pass')

    def run(self, command):
        control = self.runtime / 'miller-backend.sock'
        marker = self.runtime / 'miller-backend.ini'
        stopping = threading.Event()
        server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        server.bind(str(control))
        os.chmod(control, 0o600)
        server.listen(1)
        server.settimeout(.1)
        config = configparser.ConfigParser(interpolation=None)
        config['backend'] = {'root-uri': self.uri, 'control': str(control), 'pid': str(self.process.pid),
                             'pause-seconds': str(self.pause_seconds)}
        with os.fdopen(os.open(marker, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), 'w') as output:
            config.write(output)

        def serve():
            while not stopping.is_set():
                try:
                    connection, _ = server.accept()
                except socket.timeout:
                    continue
                with connection:
                    connection.settimeout(2)
                    try:
                        request = b''
                        while not request.endswith(b'\n') and len(request) < 32:
                            part = connection.recv(32 - len(request))
                            if not part:
                                break
                            request += part
                        if request == b'pause\n':
                            self.process.pause()
                        elif request == b'resume\n':
                            self.process.resume()
                        else:
                            raise RuntimeError('Unknown backend control request')
                        connection.sendall(b'ok\n')
                    except (OSError, RuntimeError):
                        try:
                            connection.sendall(b'error\n')
                        except OSError:
                            pass

        worker = threading.Thread(target=serve, daemon=True)
        worker.start()
        child = None
        try:
            child = subprocess.Popen(command, env={**os.environ, 'MILLER_TEST_BACKEND': str(marker)})
            return child.wait()
        finally:
            stopping.set()
            worker.join(timeout=3)
            server.close()
            self.process.resume()
            marker.unlink(missing_ok=True)
            control.unlink(missing_ok=True)
            if child is not None and child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    # Only the test command spawned above, never a host app.
                    child.kill()
                    child.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pause-seconds', type=int, choices=range(1, 11), default=5,
                        help='failing watchdog limit (default: 5; maximum: 10 seconds)')
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        parser.error('provide a test command after --')
    def terminate(signum, frame):
        raise SystemExit(128 + signum)
    signal.signal(signal.SIGTERM, terminate)
    with BackendFixture(args.pause_seconds) as fixture:
        return fixture.run(command)


if __name__ == '__main__':
    raise SystemExit(main())
