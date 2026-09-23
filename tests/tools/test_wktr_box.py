"""Local-only regression checks for the SSH WKTR bridge and artifact gate."""
import hashlib
import io
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import types
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]


def load_orchestrator():
    text = (ROOT / 'tools/run-layout-tests-box.sh').read_text()
    source = text.split("<<'PY'\n", 1)[1].rsplit('\nPY\n', 1)[0]
    # Load the same functions without installing signal handlers or running main.
    source = source.split('signal.signal(signal.SIGTERM, interrupted)')[0]
    namespace = {}
    with mock.patch.object(sys, 'argv', ['runner', str(ROOT)]):
        exec(compile(source, 'run-layout-tests-box.sh', 'exec'), namespace)
    return namespace


class WKTRBoxTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='wktr-test-')
        self.addCleanup(temp.cleanup)
        self.temp = Path(temp.name)
        self.log = self.temp / 'ssh.json'
        ssh = self.temp / 'ssh'
        ssh.write_text('#!' + sys.executable + '\n'
                       'import json, os, sys\n'
                       'with open(os.environ["FAKE_SSH_LOG"], "w") as stream:\n'
                       '    json.dump(sys.argv[1:], stream)\n'
                       'sys.stdout.buffer.write(sys.stdin.buffer.read())\n'
                       'sys.exit(int(os.environ.get("FAKE_SSH_STATUS", "0")))\n')
        ssh.chmod(0o755)
        self.environment = {key: value for key, value in os.environ.items()
                            if not key.startswith(('WKTR_', 'WEBKIT_WKTR_', 'TIGER_', 'JSC_'))}
        self.environment.update(PATH=str(self.temp) + os.pathsep + os.environ['PATH'],
                                FAKE_SSH_LOG=str(self.log),
                                WEBKIT_WKTR_STAGE_DIR='/Users/shg/wktr/runs/test-stage')

    def driver(self, args=('-',), environment=None, payload=b'first-test\nsecond-test\n'):
        return subprocess.run([str(ROOT / 'tools/wktr-box.sh')] + list(args),
                              input=payload, capture_output=True,
                              env=dict(self.environment, **(environment or {})), timeout=10)

    def command(self):
        argv = json.loads(self.log.read_text())
        return argv, shlex.split(argv[-1])

    def test_direct_line_protocol_and_quoted_arguments_are_unchanged(self):
        arguments = ['--no-timeout', "file:///tmp/a'b space.html", '-']
        result = self.driver(arguments)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, b'first-test\nsecond-test\n')
        argv, command = self.command()
        self.assertEqual(command[-3:], arguments)
        self.assertIn('ExitOnForwardFailure=yes', argv)
        first_home = next(value for value in command if value.startswith('HOME='))
        self.assertTrue(first_home.startswith('HOME=/Users/shg/wktr/runs/test-stage/home/driver-'))
        self.driver()
        self.assertNotIn(first_home, self.command()[1])

    def test_environment_values_are_literal_and_gpu_is_off(self):
        result = self.driver(environment={
            'TIGER_GPU': '1',
            'WEBKIT_WKTR_BOX_ENV': 'TEST_TEXT="apostrophe \' and $(literal)" JSC_useJIT=false',
        })
        self.assertEqual(result.returncode, 0, result.stderr)
        command = self.command()[1]
        self.assertIn('TEST_TEXT=apostrophe \' and $(literal)', command)
        self.assertIn('JSC_useJIT=false', command)
        self.assertIn('TIGER_GPU=0', command)
        self.assertNotIn('TIGER_GPU=1', command)

    def test_unsafe_environment_is_rejected_before_ssh(self):
        for extra in ('HOME=/Users/shg', 'TIGER_GPU=1', 'nice -n 1', 'A=unterminated"'):
            with self.subTest(extra=extra):
                result = self.driver(environment={'WEBKIT_WKTR_BOX_ENV': extra})
                self.assertEqual(result.returncode, 1)
                self.assertFalse(self.log.exists())

    def test_ssh_failure_is_not_reported_as_success(self):
        result = self.driver(environment={'FAKE_SSH_STATUS': '255'})
        self.assertEqual(result.returncode, 255)

    def test_remote_preflight_requires_complete_matching_stage(self):
        self.driver()
        preflight = self.command()[1][2]
        stage = self.temp / 'stage'
        names = ['READY', 'bin/wktr-guard.pl', 'share/tiger-fonts.json', 'share/cacert.pem',
                 'Frameworks/QuartzCore.framework/QuartzCore', 'bin/WebKitTestRunner',
                 'bin/TigerWebProcess', 'bin/TigerNetworkProcess']
        names += ['evidence/' + process + '/' + name for process in ('UI', 'WEB')
                  for name in ('build-manifest.json', 'wire-messages.txt', 'wire-serializers.txt')]
        for name in names:
            path = stage / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('fixture\n')
            path.chmod(0o755)

        def run():
            return subprocess.run(['/bin/sh', '-c', preflight, 'wktr-box', str(stage),
                                   str(self.temp / 'home'), '/bin/cat'],
                                  input=b'protocol\n', capture_output=True, timeout=10)

        self.assertEqual(run().stdout, b'protocol\n')
        for name in ('READY', 'bin/TigerNetworkProcess', 'share/cacert.pem', 'evidence/UI/wire-messages.txt'):
            with self.subTest(missing=name):
                path = stage / name
                path.unlink()
                result = run()
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, b'')
                path.write_text('fixture\n')
                path.chmod(0o755)
        (stage / 'evidence/WEB/wire-serializers.txt').write_text('different\n')
        self.assertNotEqual(run().returncode, 0)

    def test_missing_manifest_fails_before_remote_contact(self):
        source = self.temp / 'source'
        (source / 'LayoutTests/js/dom').mkdir(parents=True)
        result = subprocess.run([str(ROOT / 'tools/run-layout-tests-box.sh'), 'js/dom'],
                                env=dict(self.environment, WKTR_SOURCE=str(source),
                                         WKTR_UIDIR=str(self.temp / 'missing-build')),
                                capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 1)
        self.assertIn(b'build-manifest.json', result.stderr)
        self.assertFalse(self.log.exists())


class WKTRArtifactTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='wktr-artifacts-test-')
        self.addCleanup(temp.cleanup)
        self.temp = Path(temp.name)
        self.functions = load_orchestrator()
        self.ui = self.temp / 'UI'
        self.web = self.temp / 'WEB'
        for directory, process, names in ((self.ui, 'UI', ('WebKitTestRunner',)),
                                          (self.web, 'WEB', ('wktr/TigerWebProcess', 'TigerNetworkProcess'))):
            binaries, wire = {}, {}
            for name in names:
                path = directory / 'bin' / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'executable fixture')
                path.chmod(0o755)
                binaries[name] = hashlib.sha256(path.read_bytes()).hexdigest()
            for kind in ('messages', 'serializers'):
                path = directory / ('wire-' + kind + '.txt')
                path.write_text(kind + '\n')
                wire[kind + '_sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
            data = {'schema': 1, 'process': process, 'binaries': binaries, 'wire': wire,
                    'source': {'head': 'a' * 40, 'tree': 'b' * 40,
                               'dirty_sha256': hashlib.sha256(b'').hexdigest()}}
            (directory / 'build-manifest.json').write_text(json.dumps(data))

    def verify(self):
        return self.functions['manifests'](self.ui, self.web)

    def test_matching_injected_pair_is_accepted(self):
        self.assertEqual(len(self.verify()), 2)

    def test_stale_source_wire_or_binary_is_rejected(self):
        manifest = self.web / 'build-manifest.json'
        original = manifest.read_text()
        for change in ('source', 'wire', 'binary', 'injected-helper'):
            with self.subTest(change=change):
                data = json.loads(original)
                if change == 'source':
                    data['source']['head'] = 'c' * 40
                elif change == 'wire':
                    path = self.web / 'wire-messages.txt'
                    path.write_text('different\n')
                    data['wire']['messages_sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
                elif change == 'binary':
                    data['binaries']['TigerNetworkProcess'] = '0' * 64
                else:
                    del data['binaries']['wktr/TigerWebProcess']
                manifest.write_text(json.dumps(data))
                with self.assertRaises(ValueError):
                    self.verify()
                (self.web / 'wire-messages.txt').write_text('messages\n')
                manifest.write_text(original)

    def test_harness_failure_survives_log_capture(self):
        log = self.temp / 'harness.log'
        console = types.SimpleNamespace(buffer=io.BytesIO())
        remote = types.SimpleNamespace(check_lease=lambda: None)
        with mock.patch.object(sys, 'stdout', console):
            status = self.functions['run_harness'](
                [sys.executable, '-c', 'print("harness output"); raise SystemExit(7)'],
                os.environ, log, remote)
        self.assertEqual(status, 7)
        self.assertEqual(log.read_bytes(), b'harness output\n')
        self.assertEqual(console.buffer.getvalue(), log.read_bytes())


if __name__ == '__main__':
    unittest.main()
