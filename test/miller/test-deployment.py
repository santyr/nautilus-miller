#!/usr/bin/python3
"""Deployment scripts reject incomplete inputs before invoking system tools."""
import pathlib
import subprocess
import unittest
import importlib.util
import json
import tempfile
import shutil
from types import SimpleNamespace
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[2]

class DeploymentSignatures(unittest.TestCase):
    @unittest.skipUnless(shutil.which('rpmbuild'), 'requires the Fedora RPM build tools')
    def test_unsigned_stock_identity_is_not_trusted(self):
        """A valid unsigned RPM must not become the original rollback baseline."""
        spec = importlib.util.spec_from_file_location('signature_deploy', ROOT / 'tools/miller/deploy.py')
        deploy = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(deploy)
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            source = root / 'unsigned.spec'
            source.write_text('''Name: nautilus
Version: 50.4
Release: 1.fc44
Summary: Disposable unsigned rollback validation fixture
License: GPL-3.0-or-later
BuildArch: noarch
%description
An empty package used only to check signature enforcement.
%files
''')
            subprocess.run(['rpmbuild', '--define', f'_topdir {root}', '-bb', str(source)],
                           check=True, capture_output=True, text=True)
            package = next((root/'RPMS').rglob('*.rpm'))
            self.assertEqual(deploy.package_info(package)['name'], 'nautilus')
            with self.assertRaises((ValueError, subprocess.CalledProcessError)):
                deploy.package_info(package, require_signature=True)

class DeploymentRuntimeChecks(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('runtime_deploy', ROOT / 'tools/miller/deploy.py')
        self.deploy = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.deploy)

    def test_held_operation_stops_drain_without_force_kill(self):
        with mock.patch.object(self.deploy, 'has_application', return_value=True), \
             mock.patch.object(self.deploy, 'run') as command, \
             mock.patch.object(self.deploy.time, 'monotonic', side_effect=[0, 26]):
            with self.assertRaisesRegex(RuntimeError, 'not force-killed'):
                self.deploy.close_application()
        self.assertEqual(command.call_count, 1)
        self.assertIn('quit', command.call_args.args)

    def check_activation(self, *, other_owner=False, stale_inode=False,
                         wrong_view=False, wrong_packages=False):
        packages = [{'name': 'nautilus', 'nevra': 'nautilus-50.4-1.miller2.fc44.x86_64'}]
        def command(*args, capture=False):
            if args[0] == 'rpm':
                return 'wrong-package' if wrong_packages else packages[0]['nevra']
            if args[0] == 'xdg-mime':
                return self.deploy.HANDLER
            return "'grid-view'" if wrong_view else "'miller-view'"
        def stat(path):
            return SimpleNamespace(st_dev=1, st_ino=2 if stale_inode and str(path).startswith('/proc/') else 1)
        with mock.patch.object(self.deploy, 'service_pid', side_effect=[123, 456 if other_owner else 123]), \
             mock.patch.object(self.deploy, 'run', side_effect=command), \
             mock.patch.object(self.deploy.time, 'monotonic', side_effect=[0, 0, 21]), \
             mock.patch.object(self.deploy.time, 'sleep'), \
             mock.patch.object(pathlib.Path, 'resolve', return_value=pathlib.Path('/usr/bin/nautilus')), \
             mock.patch.object(pathlib.Path, 'stat', stat):
            return self.deploy.verify_running(packages, self.deploy.HANDLER, "'miller-view'")

    def test_matching_activation_is_accepted(self):
        self.assertEqual(self.check_activation()['pid'], 123)

    def test_different_service_owners_are_rejected(self):
        with self.assertRaisesRegex(RuntimeError, 'different process owners'):
            self.check_activation(other_owner=True)

    def test_old_executable_inode_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, 'installed executable inode'):
            self.check_activation(stale_inode=True)

    def test_wrong_folder_setting_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, 'Folder preference'):
            self.check_activation(wrong_view=True)

    def test_wrong_installed_package_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, 'split packages'):
            self.check_activation(wrong_packages=True)

class DeploymentInputs(unittest.TestCase):
    def test_missing_package_is_rejected(self):
        result = subprocess.run([str(ROOT / 'tools/miller/install-default.sh'), '/tmp/miller-no-such-package.rpm'], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Package does not exist', result.stderr)

    def test_missing_argument_shows_usage(self):
        result = subprocess.run([str(ROOT / 'tools/miller/install-default.sh')], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('usage:', result.stderr)

    def test_restore_requires_saved_state(self):
        import tempfile
        import os
        with tempfile.TemporaryDirectory() as state:
            result = subprocess.run([str(ROOT / 'tools/miller/restore-stock.sh')], env={**os.environ, 'XDG_STATE_HOME': state}, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('No saved deployment state', result.stderr)

class DeploymentPhases(unittest.TestCase):
    VERSION = '50.4-1.miller2.fc44'
    STOCK = ['nautilus-50.4-1.fc44.x86_64', 'nautilus-extensions-50.4-1.fc44.x86_64']

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        self.state = self.root / 'state'
        self.state.mkdir()
        self.main = self.root / f'nautilus-{self.VERSION}.x86_64.rpm'
        self.extension = self.root / f'nautilus-extensions-{self.VERSION}.x86_64.rpm'
        self.main.write_bytes(b'fixture-main')
        self.extension.write_bytes(b'fixture-extension')
        spec = importlib.util.spec_from_file_location('miller_deploy_under_test', ROOT / 'tools/miller/deploy.py')
        self.deploy = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.deploy)
        self.offline_reader = self.deploy.offline_pending
        self.events = []
        self.failure = None
        self.current = list(self.STOCK)
        self.names = ['nautilus', 'nautilus-extensions']
        for name, replacement in {
            'run': self.run_command,
            'state_dir': lambda: self.state,
            'installed_packages': lambda: self.names,
            'close_application': self.drain,
            'start_application': self.launch,
            'verify_running': self.verify,
            'offline_pending': lambda: False,
            'validate_host': lambda: '.fc44',
        }.items():
            patch = mock.patch.object(self.deploy, name, replacement, create=True)
            patch.start()
            self.addCleanup(patch.stop)
        patch = mock.patch.object(self.deploy.subprocess, 'Popen', side_effect=self.launch)
        patch.start()
        self.addCleanup(patch.stop)

    def run_command(self, *args, capture=False):
        self.events.append(args)
        if args[0] == 'rpm' and '-qp' in args:
            artifact = pathlib.Path(args[-1])
            name = ('nautilus-devel' if artifact.name.startswith('nautilus-devel-') else
                    'nautilus-extensions' if artifact.name.startswith('nautilus-extensions-') else 'nautilus')
            version = self.VERSION if '.miller' in artifact.name else '50.4-1.fc44'
            if self.failure == 'wrong-split' and name == 'nautilus-extensions' and '.miller' in artifact.name:
                version = '50.4-1.miller999.fc44'
            arch = 'aarch64' if self.failure == 'wrong-arch' and name == 'nautilus-extensions' else 'x86_64'
            return f'{name}\n{version}\n{arch}'
        if args[0] == 'rpm' and '-q' in args:
            return '\n'.join(self.current)
        if args[0] == 'rpm' and '--eval' in args:
            return 'x86_64'
        if args[0] == 'xdg-mime' and 'query' in args:
            return 'org.gnome.Nautilus.desktop'
        if args[0] == 'gsettings' and 'get' in args:
            return "'list-view'"
        if args[0] == 'dnf' and 'download' in args:
            if self.failure == 'download':
                raise subprocess.CalledProcessError(1, args)
            destination = pathlib.Path(next(arg.split('=',1)[1] for arg in args if arg.startswith('--destdir=')))
            destination.mkdir(parents=True, exist_ok=True)
            for nevra in [args[-1]]:
                (destination / f'{nevra}.rpm').write_bytes(b'fixture-stock')
        if args[0] == 'sudo' and 'dnf' in args:
            self.events.append('transaction')
            if self.failure == 'transaction':
                raise subprocess.CalledProcessError(1, args)
        return '' if capture else None

    def drain(self):
        self.events.append('drain')
        if self.failure == 'drain':
            raise RuntimeError('still has background work')

    def launch(self, *args, **kwargs):
        self.events.append('launch')
        if self.failure == 'launch':
            raise OSError('injected failed launch')
        return mock.Mock(poll=lambda: None)

    def verify(self, *args, **kwargs):
        self.events.append('verify')
        if self.failure == 'verify':
            raise RuntimeError('wrong service process')
        return {'pid': 1234, 'executable': '/usr/bin/nautilus'}

    def original(self):
        snapshot = {'packages':self.STOCK, 'directory_handler':'org.gnome.Nautilus.desktop', 'folder_view':"'list-view'"}
        path = self.state / 'before-install.json'
        path.write_text(json.dumps(snapshot))
        stock = self.state / 'stock-rpms'
        stock.mkdir()
        for nevra in self.STOCK:
            (stock / f'{nevra}.rpm').write_bytes(b'fixture-stock')
        return path.read_bytes()

    def test_drain_precedes_transaction(self):
        self.deploy.install(self.main)
        self.assertLess(self.events.index('drain'), self.events.index('transaction'))
        self.assertLess(self.events.index('launch'), self.events.index('verify'))
        state = json.loads((self.state/'installed.json').read_text())
        self.assertTrue(state['verified'])

    def test_drain_refusal_prevents_transaction(self):
        self.failure = 'drain'
        with self.assertRaises(RuntimeError): self.deploy.install(self.main)
        self.assertNotIn('transaction',self.events)

    def test_wrong_split_package(self):
        self.failure = 'wrong-split'
        with self.assertRaises((RuntimeError,ValueError)): self.deploy.install(self.main)
        self.assertNotIn('drain',self.events)
        self.assertNotIn('transaction',self.events)

    def test_download_failure(self):
        self.failure = 'download'
        with self.assertRaises(Exception): self.deploy.install(self.main)
        self.assertNotIn('drain',self.events)
        self.assertNotIn('transaction',self.events)

    def test_transaction_failure(self):
        self.failure = 'transaction'
        with self.assertRaises(Exception): self.deploy.install(self.main)
        self.assertNotIn('launch',self.events)
        self.assertFalse((self.state/'installed.json').exists())
        journal = json.loads((self.state/'deployment.json').read_text())
        self.assertEqual(journal['phase'],'transaction')
        self.assertEqual(journal['status'],'failed')

    def test_launch_failure_no_success_record(self):
        self.failure = 'launch'
        with self.assertRaises(OSError): self.deploy.install(self.main)
        self.assertFalse((self.state/'installed.json').exists())

    def test_failed_verification_no_success_record(self):
        self.failure = 'verify'
        with self.assertRaises(RuntimeError): self.deploy.install(self.main)
        self.assertFalse((self.state/'installed.json').exists())

    def test_repeat_install_preserves_original_state(self):
        original = self.original()
        self.deploy.install(self.main)
        self.deploy.install(self.main)
        self.assertEqual((self.state/'before-install.json').read_bytes(),original)

    def test_cached_rollback_used(self):
        self.original()
        self.deploy.install(self.main)
        self.assertFalse(any(isinstance(event,tuple) and event[0]=='dnf' and 'download' in event for event in self.events))

    def test_corrupt_state(self):
        (self.state/'before-install.json').write_text('{bad-json')
        with self.assertRaises(ValueError): self.deploy.install(self.main)
        self.assertNotIn('drain',self.events)

    def test_restore_ordering(self):
        original = self.original()
        self.deploy.restore()
        self.assertLess(self.events.index('drain'),self.events.index('transaction'))
        self.assertIn('verify',self.events)
        self.assertEqual((self.state/'before-install.json').read_bytes(),original)

    def test_offline_opt_in_required(self):
        with mock.patch.object(self.deploy,'offline_pending',return_value=True):
            with self.assertRaises(RuntimeError): self.deploy.install(self.main)
        self.assertNotIn('drain',self.events)
        self.assertNotIn('transaction',self.events)

    def test_corrupt_cached_rollback_stops_install(self):
        self.original()
        self.deploy.install(self.main)
        (self.state/'stock-rpms'/f'{self.STOCK[0]}.rpm').write_bytes(b'corrupt-cache')
        self.events.clear()
        with self.assertRaises((RuntimeError,ValueError)): self.deploy.install(self.main)
        self.assertNotIn('transaction',self.events)

    def test_update_from_miller_preserves_stock_snapshot(self):
        original = self.original()
        self.current = ['nautilus-50.4-1.miller1.fc44.x86_64',
                        'nautilus-extensions-50.4-1.miller1.fc44.x86_64']
        self.deploy.install(self.main)
        self.assertEqual((self.state/'before-install.json').read_bytes(),original)

    def test_miller_without_original_record_is_refused(self):
        self.current = ['nautilus-50.4-1.miller1.fc44.x86_64',
                        'nautilus-extensions-50.4-1.miller1.fc44.x86_64']
        with self.assertRaises(RuntimeError): self.deploy.install(self.main)
        self.assertNotIn('drain',self.events)
        self.assertNotIn('transaction',self.events)

    def test_malformed_snapshot_field_is_rejected(self):
        self.original()
        path = self.state/'before-install.json'
        state=json.loads(path.read_text())
        state['folder_view']=123
        path.write_text(json.dumps(state))
        with self.assertRaises(ValueError): self.deploy.install(self.main)
        self.assertNotIn('transaction',self.events)

    def test_payload_changed_while_draining_is_refused(self):
        def changed():
            self.events.append('drain')
            self.main.write_bytes(b'replaced-during-drain')
        with mock.patch.object(self.deploy,'close_application',side_effect=changed):
            with self.assertRaises(ValueError): self.deploy.install(self.main)
        self.assertNotIn('transaction',self.events)

    def test_failed_child_process_cannot_record_success(self):
        with mock.patch.object(self.deploy,'start_application',return_value=mock.Mock(poll=lambda: 1)):
            with self.assertRaises(RuntimeError): self.deploy.install(self.main)
        self.assertFalse((self.state/'installed.json').exists())

    def test_offline_can_appear_after_preflight(self):
        with mock.patch.object(self.deploy,'offline_pending',side_effect=[False, True]):
            with self.assertRaises(RuntimeError): self.deploy.install(self.main)
        self.assertNotIn('transaction',self.events)

    def test_explicit_offline_reset_flag_is_honored(self):
        with mock.patch.object(self.deploy,'offline_pending',return_value=True):
            self.deploy.install(self.main,allow_offline_reset=True)
        self.assertIn('transaction',self.events)

    def test_failed_new_attempt_archives_previous_success(self):
        self.original()
        self.deploy.install(self.main)
        previous=(self.state/'installed.json').read_bytes()
        self.failure='launch'
        with self.assertRaises(OSError): self.deploy.install(self.main)
        self.assertFalse((self.state/'installed.json').exists())
        self.assertEqual((self.state/'last-success.json').read_bytes(),previous)

    def test_original_split_version_mismatch_is_rejected(self):
        self.original()
        path=self.state/'before-install.json'
        state=json.loads(path.read_text())
        state['packages'][1]='nautilus-extensions-49.0-1.fc44.x86_64'
        path.write_text(json.dumps(state))
        with self.assertRaises(ValueError): self.deploy.install(self.main)
        self.assertNotIn('drain',self.events)

    def test_concurrent_deployment_is_refused(self):
        with self.deploy.locked_state(self.state):
            with self.assertRaises(RuntimeError): self.deploy.install(self.main)
        self.assertNotIn('transaction',self.events)

    def test_offline_state_reader_is_conservative(self):
        path=self.root/'offline-state.toml'
        with mock.patch.object(self.deploy,'OFFLINE_STATE',path):
            self.assertFalse(self.offline_reader())
            path.write_text('[offline-transaction-state]\\nstatus="download-complete"\\n'.replace('\\n','\n'))
            self.assertTrue(self.offline_reader())
            path.write_text('[offline-transaction-state]\\nstatus="unknown-new-state"\\n'.replace('\\n','\n'))
            self.assertTrue(self.offline_reader())

    def test_optional_devel_matches_install_and_restore(self):
        original=self.original()
        self.names.append('nautilus-devel')
        (self.root/f'nautilus-devel-{self.VERSION}.x86_64.rpm').write_bytes(b'fixture-devel')
        self.deploy.install(self.main)
        self.assertTrue((self.state/'stock-rpms/nautilus-devel-50.4-1.fc44.x86_64.rpm').is_file())
        self.deploy.restore()
        self.assertEqual((self.state/'before-install.json').read_bytes(),original)

    def test_mismatched_split_architecture_is_rejected(self):
        self.failure='wrong-arch'
        with self.assertRaises(ValueError): self.deploy.install(self.main)
        self.assertNotIn('drain',self.events)

    def test_wrong_fedora_release_is_rejected(self):
        with mock.patch.object(self.deploy,'validate_host',return_value='.fc45'):
            with self.assertRaises(ValueError): self.deploy.install(self.main)
        self.assertNotIn('drain',self.events)

if __name__ == '__main__':
    unittest.main()
