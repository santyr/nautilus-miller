#!/usr/bin/python3
"""Install the Fedora replacement or restore its saved stock packages/settings."""
import argparse
import json
import os
import pathlib
import subprocess
import sys
import time

SCHEMA = 'org.gnome.nautilus.preferences'
KEY = 'default-folder-viewer'


def run(*args, capture=False):
    result = subprocess.run(args, check=True, text=True, stdout=subprocess.PIPE if capture else None)
    return result.stdout.strip() if capture else None


def state_dir():
    return pathlib.Path(os.environ.get('XDG_STATE_HOME', pathlib.Path.home() / '.local/state')) / 'nautilus-miller'


def has_application():
    result = run('gdbus', 'call', '--session', '--dest', 'org.freedesktop.DBus',
                 '--object-path', '/org/freedesktop/DBus', '--method',
                 'org.freedesktop.DBus.NameHasOwner', 'org.gnome.Nautilus', capture=True)
    return result == '(true,)'


def close_application():
    if not has_application():
        return
    # app.quit closes windows and lets held file operations finish. CLI --quit
    # activates app.kill and must not be used while replacing a file manager.
    run('gdbus', 'call', '--session', '--dest', 'org.gnome.Nautilus',
        '--object-path', '/org/gnome/Nautilus', '--method', 'org.gtk.Actions.Activate',
        'quit', '[]', '{}')
    deadline = time.monotonic() + 25
    while has_application():
        if time.monotonic() >= deadline:
            raise RuntimeError('Nautilus still has background work or open requests. Finish those and retry; it was not force-killed.')
        time.sleep(0.25)


def installed_packages():
    packages = ['nautilus', 'nautilus-extensions']
    if subprocess.run(['rpm', '-q', 'nautilus-devel'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0:
        packages.append('nautilus-devel')
    return packages


def install(package):
    package = pathlib.Path(package).resolve()
    if not package.is_file():
        raise ValueError(f'Package does not exist: {package}')
    fields = run('rpm', '-qp', '--qf', '%{NAME}\n%{VERSION}-%{RELEASE}\n%{ARCH}', str(package), capture=True).splitlines()
    if len(fields) != 3 or fields[0] != 'nautilus' or '.miller' not in fields[1]:
        raise ValueError('Expected a Nautilus Miller replacement RPM')
    names = installed_packages()
    artifacts = [package.parent / f'{name}-{fields[1]}.{fields[2]}.rpm' for name in names]
    for artifact in artifacts:
        if not artifact.is_file():
            raise ValueError(f'Matching package does not exist: {artifact}')
    directory = state_dir()
    directory.mkdir(parents=True, exist_ok=True, mode=0o700)
    snapshot = directory / 'before-install.json'
    if not snapshot.exists():
        state = {
            'packages': run('rpm', '-q', *names, capture=True).splitlines(),
            'directory_handler': run('xdg-mime', 'query', 'default', 'inode/directory', capture=True),
            'folder_view': run('gsettings', 'get', SCHEMA, KEY, capture=True),
        }
        if any('.miller' in name for name in state['packages']):
            raise RuntimeError('A Miller package is already installed but the original deployment record is missing.')
        temporary = snapshot.with_suffix('.tmp')
        temporary.write_text(json.dumps(state, indent=2) + '\n')
        temporary.replace(snapshot)
    state = json.loads(snapshot.read_text())
    stock = directory / 'stock-rpms'
    stock.mkdir(exist_ok=True)
    run('dnf', '--repo=fedora', '--repo=updates', 'download', f'--destdir={stock}', *state['packages'])
    # These explicit system changes are the installation requested by the user.
    run('sudo', 'dnf', '--repo=fedora', '--repo=updates', 'install', '-y', *map(str, artifacts))
    close_application()
    run('xdg-mime', 'default', 'org.gnome.Nautilus.desktop', 'inode/directory')
    run('gsettings', 'set', SCHEMA, KEY, 'columns-view')
    (directory / 'installed.json').write_text(json.dumps({'package': fields[1], 'artifacts': list(map(str, artifacts))}, indent=2) + '\n')
    subprocess.Popen(['/usr/bin/nautilus', '--new-window', str(pathlib.Path.home())],
                     stdout=subprocess.DEVNULL, stderr=open(directory / 'nautilus.log', 'a'), start_new_session=True)
    print(f'Installed Nautilus {fields[1]} as the default file manager. Rollback state: {directory}')


def restore():
    directory = state_dir()
    snapshot = directory / 'before-install.json'
    if not snapshot.is_file():
        raise ValueError(f'No saved deployment state: {snapshot}')
    state = json.loads(snapshot.read_text())
    artifacts = [directory / 'stock-rpms' / f'{name}.rpm' for name in state['packages']]
    for artifact in artifacts:
        if not artifact.is_file():
            raise ValueError(f'Saved stock package does not exist: {artifact}')
    close_application()
    # Restore a stock-compatible enum before replacing its schema.
    run('gsettings', 'set', SCHEMA, KEY, state['folder_view'])
    run('sudo', 'dnf', '--repo=fedora', '--repo=updates', 'downgrade', '-y', *map(str, artifacts))
    if state['directory_handler']:
        run('xdg-mime', 'default', state['directory_handler'], 'inode/directory')
    print('Restored the saved Fedora packages, directory handler, and folder view.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    installer = commands.add_parser('install')
    installer.add_argument('package', help='explicit path to the built nautilus RPM')
    commands.add_parser('restore')
    args = parser.parse_args()
    try:
        install(args.package) if args.command == 'install' else restore()
    except (ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
