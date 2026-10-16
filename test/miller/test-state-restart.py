#!/usr/bin/python3
"""Persist native UI settings, exit, then inspect a fresh application process."""
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SESSION = ROOT / 'tools/miller/with-test-session.py'


def inside(build):
    runtime = Path(os.environ['XDG_RUNTIME_DIR'])
    assert runtime.name == 'runtime' and runtime.parent.name.startswith('nautilus-miller-tests-')
    assert os.environ.get('MILLER_TEST_SESSION') == str(runtime / 'miller-session.ini')
    env = {**os.environ, 'GTK_A11Y': 'test', 'GTK_IM_MODULE': 'simple',
           'GSETTINGS_SCHEMA_DIR': str(build / 'data'), 'GSETTINGS_BACKEND': 'dconf'}
    executable = build / 'test/automated/display/test-miller-typeahead-ui'
    pids = []
    for phase in ('write', 'read'):
        result = subprocess.run([str(executable), '-p', '/miller/typeahead-ui/process-state'],
            env={**env, 'MILLER_STATE_PHASE': phase}, capture_output=True, text=True, timeout=30)
        if result.returncode != 0:
            print(result.stdout, file=sys.stderr)
            print(result.stderr, file=sys.stderr)
            return result.returncode
        match = re.search(r'Persistence ' + phase + r' process (\d+)', result.stdout)
        assert match, result.stdout
        pids.append(int(match[1]))
        print(f'{phase}: process {pids[-1]} passed', flush=True)
    assert pids[0] != pids[1]
    assert (Path(env['XDG_CONFIG_HOME']) / 'dconf/user').is_file()
    print('Native dconf state and realized Columns controls survived application restart.', flush=True)
    return 0


if __name__ == '__main__':
    build = Path(sys.argv[-1]).resolve() if len(sys.argv) > 1 else ROOT / 'build-miller'
    if '--inside' in sys.argv:
        raise SystemExit(inside(build))
    raise SystemExit(subprocess.call([sys.executable, str(SESSION), '--', sys.executable,
        str(Path(__file__).resolve()), '--inside', str(build)]))
