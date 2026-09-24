import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('bench_status', ROOT / 'tools/bench-status.py')
STATUS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(STATUS)


class BenchStatusTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def write(self, suffix, value):
        (self.root / ('n720' + suffix)).write_text(str(value))

    def test_explicit_stage_success_and_gate_failure_remain_separate(self):
        self.write('.exit-status', 1)
        self.write('.stage-exit-status', 0)
        self.write('.gate-exit-status', 1)
        result = STATUS.read_status(self.root, 'n720')
        self.assertEqual((result['exit_status'], result['stage_exit_status'], result['benchmark_gate_exit_status']), (1, 0, 1))
        self.assertEqual(result['stage_status_source'], 'explicit')

    def test_legacy_nonzero_is_never_attributed_to_staging(self):
        self.write('.exit-status', 1)
        self.write('.video-paints.json', json.dumps({'benchmark_gate': {'passed': False}}))
        result = STATUS.read_status(self.root, 'n720')
        self.assertIsNone(result['stage_exit_status'])
        self.assertEqual(result['stage_status_source'], 'legacy-combined-unknown')
        self.assertEqual(result['benchmark_gate_exit_status'], 1)

    def test_legacy_success_and_missing_status_are_distinct(self):
        self.assertIsNone(STATUS.read_status(self.root, 'n720')['stage_exit_status'])
        self.write('.exit-status', 0)
        result = STATUS.read_status(self.root, 'n720')
        self.assertEqual(result['stage_exit_status'], 0)
        self.assertEqual(result['stage_status_source'], 'legacy-combined-success')

    def test_malformed_gate_artifact_does_not_manufacture_staging_status(self):
        self.write('.exit-status', 1)
        self.write('.video-paints.json', '{bad')
        result = STATUS.read_status(self.root, 'n720')
        self.assertIsNone(result['stage_exit_status'])
        self.assertIsNone(result['benchmark_gate_exit_status'])

    def run_shell_case(self, stage, gate, ready=1):
        # Execute the actual shell run() function, substituting only external
        # staging and gate commands. No verifier, server, lease or SSH is invoked.
        script = (ROOT / 'tools/bench.sh').read_text()
        function = script[script.index('run() {'):script.index('\nFAILS=0')]
        fake_stage = self.root / 'stage.sh'
        fake_stage.write_text('if [ "$PROBE_READY" = 1 ]; then echo "NATIVE720 READY" > "$LOG"; else : > "$LOG"; fi\nexit "$STAGE_RC"\n')
        tools = self.root / 'tools'
        tools.mkdir(exist_ok=True)
        (tools / 'tiger-video-bench.py').write_text('import os, sys\nsys.exit(int(os.environ["GATE_RC"]))\n')
        harness = 'set -u\nOUT=$TEST_ROOT\nWKT=$TEST_ROOT\nSTAGE=$TEST_STAGE\nSTD=std\n' + function + '\nrun n720 https://example.test/ 45 test ""\n'
        env = dict(os.environ, TEST_ROOT=str(self.root), TEST_STAGE=str(fake_stage),
                   STAGE_RC=str(stage), GATE_RC=str(gate), PROBE_READY=str(ready))
        return subprocess.run(['sh', '-c', harness], env=env, capture_output=True, text=True, timeout=15)

    def test_shell_reports_fps_gate_failure_without_false_stage_failure(self):
        result = self.run_shell_case(0, 1)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn('benchmark gate failed', result.stdout)
        self.assertNotIn('stage-app.sh exited', result.stdout)
        status = STATUS.read_status(self.root, 'n720')
        self.assertEqual(status['stage_exit_status'], 0)
        self.assertEqual(status['benchmark_gate_exit_status'], 1)

    def test_shell_preserves_actual_staging_failure(self):
        result = self.run_shell_case(7, 1)
        self.assertEqual(result.returncode, 7, result.stderr)
        self.assertIn('stage-app.sh exited 7', result.stdout)
        self.assertEqual(STATUS.read_status(self.root, 'n720')['stage_exit_status'], 7)

    def test_shell_all_success_records_both_zero(self):
        result = self.run_shell_case(0, 0)
        self.assertEqual(result.returncode, 0, result.stderr)
        status = STATUS.read_status(self.root, 'n720')
        self.assertEqual((status['exit_status'], status['stage_exit_status'], status['benchmark_gate_exit_status']), (0, 0, 0))

    def test_shell_missing_fixture_readiness_is_a_gate_failure(self):
        result = self.run_shell_case(0, 0, ready=0)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertNotIn('stage-app.sh exited', result.stdout)
        self.assertEqual(STATUS.read_status(self.root, 'n720')['benchmark_gate_exit_status'], 1)


if __name__ == '__main__':
    unittest.main()
