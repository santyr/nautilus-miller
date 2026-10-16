#!/usr/bin/python3
"""Tracked Fedora deployment with verified rollback and explicit recovery phases."""
import argparse
from contextlib import contextmanager
import fcntl
import hashlib
import json
import os
import pathlib
import platform
import re
import subprocess
import sys
import tempfile
import time
import tomllib
import uuid

SCHEMA = 'org.gnome.nautilus.preferences'
KEY = 'default-folder-viewer'
HANDLER = 'org.gnome.Nautilus.desktop'
# DNF5's documented system-state directory; unknown/corrupt states fail closed.
# https://dnf5.readthedocs.io/en/stable/commands/offline.8.html
OFFLINE_STATE = pathlib.Path('/usr/lib/sysimage/libdnf5/offline/offline-transaction-state.toml')
NAMES = ('nautilus', 'nautilus-extensions', 'nautilus-devel')
REPOS = ('--repo=fedora', '--repo=updates')


def run(*args, capture=False):
    result = subprocess.run(args, check=True, text=True,
                            stdout=subprocess.PIPE if capture else None,
                            stderr=subprocess.PIPE if capture else None)
    return result.stdout.strip() if capture else None


def state_dir():
    return pathlib.Path(os.environ.get('XDG_STATE_HOME', pathlib.Path.home() / '.local/state')) / 'nautilus-miller'


def atomic_json(path, data):
    with tempfile.NamedTemporaryFile(mode='w', dir=path.parent, delete=False) as output:
        temporary = pathlib.Path(output.name)
        try:
            json.dump(data, output, indent=2)
            output.write('\n')
            output.flush()
            os.fsync(output.fileno())
            output.close()
            temporary.replace(path)
        finally:
            temporary.unlink(missing_ok=True)


def read_json(path):
    if path.stat().st_size > 1024 * 1024:
        raise ValueError(f'Deployment state is too large: {path}')
    return json.loads(path.read_text())


@contextmanager
def locked_state(directory):
    directory.mkdir(parents=True, exist_ok=True, mode=0o700)
    with (directory / 'deployment.lock').open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise RuntimeError('Another Miller deployment is running') from error
        yield


class Attempt:
    def __init__(self, directory, operation):
        self.directory = directory
        self.data = {'attempt': uuid.uuid4().hex, 'operation': operation,
                     'phase': 'preflight', 'status': 'running'}

    def __enter__(self):
        previous = self.directory / 'installed.json'
        if previous.exists():
            # Keep the last verified record, but never present it as a new success.
            previous.replace(self.directory / 'last-success.json')
        self.phase('preflight')
        return self

    def phase(self, name):
        self.data['phase'] = name
        atomic_json(self.directory / 'deployment.json', self.data)

    def complete(self):
        self.data['status'] = 'complete'
        atomic_json(self.directory / 'deployment.json', self.data)

    def __exit__(self, kind, error, traceback):
        if error is not None:
            error.miller_phase_reported = True
            self.data.update(status='failed', error=str(error))
            try:
                atomic_json(self.directory / 'deployment.json', self.data)
            except OSError:
                pass
            print(f"Deployment failed during {self.data['phase']}: {error}", file=sys.stderr)
            print('Original rollback data was preserved. If packages changed, use '
                  'tools/miller/restore-stock.sh; otherwise reopen the existing Nautilus. '
                  f'Details: {self.directory / "deployment.json"}', file=sys.stderr)
        return False


def offline_pending():
    if not OFFLINE_STATE.exists():
        return False
    data = tomllib.loads(OFFLINE_STATE.read_text())
    status = data.get('offline-transaction-state', {}).get('status')
    # Presence of a prepared or unknown state requires explicit consent. A stale
    # state can be examined/cleaned by the operator, never silently consumed here.
    return status not in ('none', '')


def check_offline(allow_reset):
    if offline_pending() and not allow_reset:
        raise RuntimeError('A DNF offline transaction is prepared or its state is unknown. '
                           'Preserve it, or explicitly authorize replacement with --allow-offline-reset.')


def has_application():
    return run('gdbus', 'call', '--session', '--dest', 'org.freedesktop.DBus',
               '--object-path', '/org/freedesktop/DBus', '--method',
               'org.freedesktop.DBus.NameHasOwner', 'org.gnome.Nautilus', capture=True) == '(true,)'


def close_application():
    if not has_application():
        return
    run('gdbus', 'call', '--session', '--dest', 'org.gnome.Nautilus',
        '--object-path', '/org/gnome/Nautilus', '--method', 'org.gtk.Actions.Activate',
        'quit', '[]', '{}')
    deadline = time.monotonic() + 25
    while has_application():
        if time.monotonic() >= deadline:
            raise RuntimeError('Nautilus still has background work or open requests. '
                               'Finish those and retry; it was not force-killed.')
        time.sleep(0.25)


def installed_packages():
    names = list(NAMES[:2])
    try:
        run('rpm', '-q', 'nautilus-devel', capture=True)
        names.append('nautilus-devel')
    except subprocess.CalledProcessError:
        pass
    return names


def validate_host():
    release = platform.freedesktop_os_release()
    version = release.get('VERSION_ID', '')
    if release.get('ID') != 'fedora' or not version.isdigit():
        raise RuntimeError('This RPM deployment tool requires a stable Fedora host')
    return '.fc' + version


def package_info(path, replacement=False, require_signature=False):
    if not path.is_file() or path.is_symlink():
        raise ValueError(f'Package does not exist or is a symlink: {path}')
    fields = run('rpm', '-qp', '--qf', '%{NAME}\n%{VERSION}-%{RELEASE}\n%{ARCH}',
                 str(path), capture=True).splitlines()
    if len(fields) != 3 or fields[0] not in NAMES:
        raise ValueError(f'Unexpected Nautilus package metadata: {path}')
    if replacement and '.miller' not in fields[1]:
        raise ValueError('Expected a Nautilus Miller replacement RPM')
    run('rpm', '--checksig', '--nosignature', str(path), capture=True)
    if require_signature:
        # rpmkeys inherits the host verification policy, which can accept unsigned
        # packages. Require signatures and trust only this Fedora release's key,
        # not every third-party key installed in the system RPM database.
        release = validate_host().removeprefix('.fc')
        key = pathlib.Path(f'/etc/pki/rpm-gpg/RPM-GPG-KEY-fedora-{release}-primary')
        if not key.is_file():
            raise ValueError(f'Fedora rollback signing key is unavailable: {key}')
        with tempfile.TemporaryDirectory(prefix='miller-rollback-keyring-') as keyring:
            options = ('--dbpath', keyring, '--define', '_keyring rpmdb',
                       '--define', '_pkgverify_level all', '--define', '_pkgverify_flags 0x0')
            run('rpmkeys', *options, '--import', str(key), capture=True)
            run('rpmkeys', *options, '--checksig', str(path), capture=True)
    return {'name': fields[0], 'version': fields[1], 'arch': fields[2],
            'nevra': f'{fields[0]}-{fields[1]}.{fields[2]}', 'path': str(path)}


def replacement_set(package, names):
    main = package_info(package, replacement=True)
    if not main['version'].endswith(validate_host()):
        raise ValueError('Replacement RPM Fedora release does not match this host')
    if main['name'] != 'nautilus':
        raise ValueError('Expected the main Nautilus Miller replacement RPM')
    if main['arch'] != run('rpm', '--eval', '%{_arch}', capture=True):
        raise ValueError('Replacement package architecture does not match this host')
    artifacts = []
    for name in names:
        path = package.parent / f'{name}-{main["version"]}.{main["arch"]}.rpm'
        info = package_info(path, replacement=True)
        if (info['name'], info['version'], info['arch']) != (name, main['version'], main['arch']):
            raise ValueError(f'Mismatched split package: {path}')
        artifacts.append(info)
    return artifacts


def validate_snapshot(state):
    if not isinstance(state, dict):
        raise ValueError('Invalid saved deployment state')
    packages = state.get('packages')
    if not isinstance(packages, list) or not 2 <= len(packages) <= 3:
        raise ValueError('Invalid saved package set')
    found = set()
    versions = set()
    for package in packages:
        if not isinstance(package, str) or not re.fullmatch(r'[A-Za-z0-9_.+~^:-]+', package):
            raise ValueError('Invalid saved package identity')
        name = next((name for name in reversed(NAMES) if package.startswith(name + '-')), None)
        if name is None or name in found or '.miller' in package:
            raise ValueError('Invalid original Fedora package set')
        found.add(name)
        versions.add(package[len(name) + 1:])
    if not set(NAMES[:2]).issubset(found):
        raise ValueError('Incomplete original Fedora package set')
    if len(versions) != 1:
        raise ValueError('Original Fedora split packages have mismatched versions or architectures')
    if state.get('folder_view') not in ("'list-view'", "'icon-view'", "'columns-view'"):
        raise ValueError('Invalid saved folder view')
    handler = state.get('directory_handler')
    if not isinstance(handler, str) or '/' in handler or any(ord(char) < 32 for char in handler):
        raise ValueError('Invalid saved directory handler')
    if not handler:
        raise ValueError('An explicit original directory handler is required for a verifiable rollback')
    return state


def original_state(directory, names):
    snapshot = directory / 'before-install.json'
    if snapshot.exists():
        return validate_snapshot(read_json(snapshot))
    packages = run('rpm', '-q', *names, capture=True).splitlines()
    if any('.miller' in package for package in packages):
        raise RuntimeError('A Miller package is installed but the original deployment record is missing.')
    state = validate_snapshot({'packages': packages,
        'directory_handler': run('xdg-mime', 'query', 'default', 'inode/directory', capture=True),
        'folder_view': run('gsettings', 'get', SCHEMA, KEY, capture=True)})
    atomic_json(snapshot, state)
    return state


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def rollback_packages(state, names):
    packages = list(state['packages'])
    if 'nautilus-devel' in names and not any(item.startswith('nautilus-devel-') for item in packages):
        main = next(item for item in packages if item.startswith('nautilus-') and
                    not item.startswith(('nautilus-extensions-', 'nautilus-devel-')))
        packages.append('nautilus-devel-' + main[len('nautilus-'):])
    return packages


def verify_rollback(directory, state, names, download):
    stock = directory / 'stock-rpms'
    stock.mkdir(exist_ok=True)
    packages = rollback_packages(state, names)
    manifest_path = directory / 'rollback-checksums.json'
    checksums = read_json(manifest_path) if manifest_path.exists() else {}
    if not isinstance(checksums, dict) or any(not isinstance(value, str) or
        not re.fullmatch('[0-9a-f]{64}', value) for value in checksums.values()):
        raise ValueError('Invalid rollback checksum manifest')
    artifacts = []
    for nevra in packages:
        artifact = stock / f'{nevra}.rpm'
        if not artifact.is_file():
            if not download:
                raise ValueError(f'Saved stock package does not exist: {artifact}')
            run('dnf', *REPOS, 'download', f'--destdir={stock}', nevra)
        info = package_info(artifact, require_signature=True)
        if info['nevra'] != nevra:
            raise ValueError(f'Saved stock RPM does not match its recorded identity: {artifact}')
        digest = sha256(artifact)
        if nevra in checksums and checksums[nevra] != digest:
            raise ValueError(f'Saved stock RPM checksum changed: {artifact}')
        checksums[nevra] = digest
        artifacts.append(info)
    atomic_json(manifest_path, checksums)
    return artifacts


def start_application(directory):
    with (directory / 'nautilus.log').open('a') as log:
        return subprocess.Popen(['/usr/bin/nautilus', '--new-window', str(pathlib.Path.home())],
                                stdout=subprocess.DEVNULL, stderr=log, start_new_session=True)


def service_pid(name):
    response = run('gdbus', 'call', '--session', '--dest', 'org.freedesktop.DBus',
                   '--object-path', '/org/freedesktop/DBus', '--method',
                   'org.freedesktop.DBus.GetConnectionUnixProcessID', name, capture=True)
    match = re.fullmatch(r'\(uint32 ([0-9]+),\)', response)
    if match is None:
        raise RuntimeError(f'No valid process owner for {name}')
    return int(match[1])


def verify_running(packages, handler, folder_view):
    deadline = time.monotonic() + 20
    last_error = None
    while time.monotonic() < deadline:
        try:
            pid = service_pid('org.gnome.Nautilus')
            if pid != service_pid('org.freedesktop.FileManager1'):
                raise RuntimeError('Canonical D-Bus services have different process owners')
            executable = pathlib.Path('/proc') / str(pid) / 'exe'
            installed = pathlib.Path('/usr/bin/nautilus')
            if executable.resolve() != installed or executable.stat().st_ino != installed.stat().st_ino or executable.stat().st_dev != installed.stat().st_dev:
                raise RuntimeError('Running Nautilus is not the installed executable inode')
            actual = run('rpm', '-q', *[item['name'] for item in packages], capture=True).splitlines()
            if set(actual) != {item['nevra'] for item in packages}:
                raise RuntimeError('Installed Nautilus split packages do not match the requested set')
            run('rpm', '-V', *[item['name'] for item in packages], capture=True)
            if run('xdg-mime', 'query', 'default', 'inode/directory', capture=True) != handler:
                raise RuntimeError('Directory handler verification failed')
            if run('gsettings', 'get', SCHEMA, KEY, capture=True) != folder_view:
                raise RuntimeError('Folder preference verification failed')
            return {'pid': pid, 'executable': str(installed)}
        except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
            last_error = error
            time.sleep(0.2)
    raise RuntimeError(f'Installed application failed verification: {last_error}')


def install(package, allow_offline_reset=False, yes=False):
    package = pathlib.Path(package).absolute()
    if not package.is_file():
        raise ValueError(f'Package does not exist: {package}')
    names = installed_packages()
    artifacts = replacement_set(package, names)
    check_offline(allow_offline_reset)
    directory = state_dir()
    with locked_state(directory), Attempt(directory, 'install') as attempt:
        state = original_state(directory, names)
        attempt.data['artifacts'] = [{**item, 'sha256': sha256(pathlib.Path(item['path']))} for item in artifacts]
        attempt.phase('rollback')
        verify_rollback(directory, state, names, download=True)
        attempt.phase('authorize')
        run('sudo', '-v')
        attempt.phase('drain')
        close_application()
        check_offline(allow_offline_reset)
        for item in attempt.data['artifacts']:
            if sha256(pathlib.Path(item['path'])) != item['sha256']:
                raise ValueError('A staged replacement RPM changed during preflight; rebuild/review and retry')
        attempt.phase('transaction')
        run('sudo', 'dnf', *REPOS, 'install', *(['-y'] if yes else []), *[item['path'] for item in artifacts])
        attempt.phase('settings')
        run('xdg-mime', 'default', HANDLER, 'inode/directory')
        run('gsettings', 'set', SCHEMA, KEY, 'columns-view')
        attempt.phase('launch')
        process = start_application(directory)
        if process.poll() not in (None, 0):
            raise RuntimeError('Nautilus launch process exited with an error')
        attempt.phase('verify')
        runtime = verify_running(artifacts, HANDLER, "'columns-view'")
        atomic_json(directory / 'installed.json', {**attempt.data, 'verified': True,
            'status': 'complete', 'package': artifacts[0]['version'], 'runtime': runtime})
        attempt.complete()
    print(f'Installed and verified Nautilus {artifacts[0]["version"]}. Rollback state: {directory}')


def restore(allow_offline_reset=False, yes=False):
    directory = state_dir()
    snapshot = directory / 'before-install.json'
    if not snapshot.is_file():
        raise ValueError(f'No saved deployment state: {snapshot}')
    check_offline(allow_offline_reset)
    with locked_state(directory), Attempt(directory, 'restore') as attempt:
        state = validate_snapshot(read_json(snapshot))
        attempt.phase('rollback')
        artifacts = verify_rollback(directory, state, installed_packages(), download=False)
        if any(not item['version'].endswith(validate_host()) for item in artifacts):
            raise ValueError('Saved rollback RPM Fedora release does not match this host; review rollback after OS upgrades')
        attempt.phase('authorize')
        run('sudo', '-v')
        attempt.phase('drain')
        close_application()
        check_offline(allow_offline_reset)
        # Restore a stock-compatible enum before replacing the Miller schema.
        attempt.phase('settings-before-transaction')
        run('gsettings', 'set', SCHEMA, KEY, state['folder_view'])
        attempt.phase('transaction')
        run('sudo', 'dnf', *REPOS, 'downgrade', *(['-y'] if yes else []), *[item['path'] for item in artifacts])
        attempt.phase('settings')
        if state['directory_handler']:
            run('xdg-mime', 'default', state['directory_handler'], 'inode/directory')
        attempt.phase('launch')
        process = start_application(directory)
        if process.poll() not in (None, 0):
            raise RuntimeError('Restored Nautilus launch process exited with an error')
        attempt.phase('verify')
        runtime = verify_running(artifacts, state['directory_handler'], state['folder_view'])
        atomic_json(directory / 'restored.json', {**attempt.data, 'status':'complete', 'verified':True, 'runtime':runtime})
        attempt.complete()
    print('Restored and verified saved Fedora packages and original settings.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    installer = commands.add_parser('install')
    installer.add_argument('package', help='explicit path to the built main Nautilus RPM')
    restorer = commands.add_parser('restore')
    for command in (installer, restorer):
        command.add_argument('--allow-offline-reset', action='store_true', help='explicitly permit invalidating a prepared DNF offline transaction')
        command.add_argument('--yes', action='store_true', help='accept the reviewed DNF transaction without a second terminal prompt')
    args = parser.parse_args()
    try:
        if os.geteuid() == 0:
            raise RuntimeError('Run as your desktop user, without sudo; the package transaction requests sudo itself.')
        if args.command == 'install':
            install(args.package, args.allow_offline_reset, args.yes)
        else:
            restore(args.allow_offline_reset, args.yes)
    except (ValueError, RuntimeError, OSError, subprocess.CalledProcessError) as error:
        if not getattr(error, 'miller_phase_reported', False):
            print(f'Deployment failed during preflight: {error}', file=sys.stderr)
            print('No package transaction was started. Correct the inputs or offline-state decision before retrying.', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
