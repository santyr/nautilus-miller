#!/usr/bin/python3
"""Run a private-session navigation test with a mounted disposable home.

HOME stays unchanged. A fresh private compositor runs inside the mount/user
namespace so clients can verify its /proc identity. The source checkout is readable at its normal path;
the remaining home contents are test fixtures. Only that home and the private
XDG tree are writable. Run an already built test executable, not a build tool.
"""
import argparse
import importlib.util
import os
from pathlib import Path
import pwd
import subprocess
import sys
import tempfile


def run(command):
    # Reuse the maintained guard without mounting/starting an archive backend.
    path = Path(__file__).with_name('with-delayed-backend.py')
    spec = importlib.util.spec_from_file_location('miller_private_session', path)
    guard = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(guard)
    root, runtime, address = guard.private_session()
    home = Path(os.environ['HOME'])
    repo = Path(__file__).resolve().parents[2]
    if home != Path(pwd.getpwuid(os.getuid()).pw_dir) or home.resolve() != home:
        raise RuntimeError('Navigation requires the current canonical home path')
    if not repo.is_relative_to(home) or not (repo / 'src/nautilus-miller-view.c').is_file():
        raise RuntimeError('Navigation requires its source checkout beneath home')
    with tempfile.TemporaryDirectory(prefix='navigation-home-', dir=root) as directory:
        masked = Path(directory)
        fixture = masked / 'Home fixture'
        fixture.mkdir()
        (fixture / 'owned.txt').write_text('disposable home')
        # Hold the original directory before masking home. The bind cannot
        # accidentally resolve the source inside the newly mounted empty home.
        repo_fd = os.open(repo, os.O_PATH | os.O_DIRECTORY)
        try:
            env = {key: value for key, value in os.environ.items() if not key.startswith('IBUS_')}
            env.update(MILLER_TEST_HOME=str(masked), MILLER_TEST_REPOSITORY=str(repo), GTK_IM_MODULE='simple')
            prefix = ['bwrap', '--ro-bind', '/', '/', '--tmpfs', '/tmp', '--bind', str(masked), str(home),
                '--bind', str(root), str(root), '--ro-bind-fd', str(repo_fd), str(repo),
                '--dev', '/dev', '--dev-bind-try', '/dev/dri', '/dev/dri',
                '--proc', '/proc', '--die-with-parent', '--chdir', str(repo)]
            proof = '''import os,pathlib,pwd
home=pathlib.Path.home();masked=pathlib.Path(os.environ['MILLER_TEST_HOME'])
assert str(home)==pwd.getpwuid(os.getuid()).pw_dir==os.environ['HOME']
assert (home.stat().st_dev,home.stat().st_ino)==(masked.stat().st_dev,masked.stat().st_ino)
assert os.statvfs('/').f_flag & os.ST_RDONLY
assert os.statvfs(os.environ['MILLER_TEST_REPOSITORY']).f_flag & os.ST_RDONLY
assert (home/'Home fixture'/'owned.txt').read_text()=='disposable home'
'''
            subprocess.run(prefix + [sys.executable, '-c', proof], env=env,
                           pass_fds=(repo_fd,), check=True, timeout=10)
            # The parent namespace's /proc/<mutter>/exe is ptrace-protected
            # from this unprivileged user namespace. Start a fresh owned bus
            # and compositor alongside the client; keep every input check.
            session = str(Path(__file__).with_name('with-test-session.py'))
            return subprocess.call(prefix + [sys.executable, session, '--', *command],
                                   env=env, pass_fds=(repo_fd,))
        finally:
            os.close(repo_fd)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        parser.error('provide a built test command after --')
    return run(command)


if __name__ == '__main__':
    raise SystemExit(main())
