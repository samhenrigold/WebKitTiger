import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('video_bench', Path(__file__).resolve().parents[2] / 'tools/tiger-video-bench.py')
gate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gate)


class VideoBenchTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.repo = self.root / 'repo'
        self.repo.mkdir()
        self.snapshot = self.root / 'snapshot'
        self.candidate = self.root / 'candidate.json'
        self.log = self.root / 'native.log'
        self.log.write_text('a fixture log\n')
        self.output = self.root / 'native.video-paints.json'
        self.git('init', '-q')

    def git(self, *args):
        return subprocess.check_output(['git', '-C', str(self.repo), *args], stderr=subprocess.PIPE).decode().strip()

    def commit_parser(self, mode='fast', returncode=0):
        report = {'measurement': 'window-paint-not-scanout', 'status': 'complete', 'streams': [
            {'ring': '/tmp/test-ring', 'mode': mode, 'status': 'certified-window-paints', 'fps': 30,
             'max_gap_ms_including_window_edges': 34, 'uncertified_attempts_by_reason': {}}]}
        path = self.repo / gate.PARSER_PATH
        path.parent.mkdir(parents=True, exist_ok=True)
        body = 'import json,sys\nprint(json.dumps(' + repr(report) + '))\nsys.exit(' + str(returncode) + ')\n'
        path.write_text(body)
        self.git('add', gate.PARSER_PATH)
        self.git('-c', 'user.name=Test', '-c', 'user.email=test@example.invalid', 'commit', '-qm', 'parser fixture')
        source = {'head': self.git('rev-parse', 'HEAD'), 'tree': self.git('rev-parse', 'HEAD^{tree}'),
                  'dirty_sha256': hashlib.sha256(b'').hexdigest()}
        self.candidate.write_text(json.dumps({'source': source}))
        return path, body

    def prepare(self):
        return gate.prepare(self.candidate, self.repo, self.snapshot)

    def check(self, mode='fast'):
        with contextlib.redirect_stdout(io.StringIO()):
            return gate.check(self.snapshot, self.log, self.output, mode)

    def test_extracts_committed_parser_not_mutable_worktree(self):
        path, committed = self.commit_parser()
        path.write_text('raise RuntimeError("mutable working copy was executed")\n')
        metadata = self.prepare()
        self.assertEqual((self.snapshot / gate.PARSER_NAME).read_text(), committed)
        self.assertEqual(metadata['parser_sha256'], hashlib.sha256(committed.encode()).hexdigest())
        self.assertTrue(self.check())

    def test_tree_mismatch_rejected(self):
        self.commit_parser()
        candidate = json.loads(self.candidate.read_text())
        candidate['source']['tree'] = '0' * 40
        self.candidate.write_text(json.dumps(candidate))
        with self.assertRaisesRegex(ValueError, 'tree does not match'):
            self.prepare()

    def test_dirty_candidate_rejected(self):
        self.commit_parser()
        candidate = json.loads(self.candidate.read_text())
        candidate['source']['dirty_sha256'] = '0' * 64
        self.candidate.write_text(json.dumps(candidate))
        with self.assertRaisesRegex(ValueError, 'must be committed'):
            self.prepare()

    def test_snapshot_tampering_fails_and_writes_result(self):
        self.commit_parser()
        self.prepare()
        (self.snapshot / gate.PARSER_NAME).write_text('raise Exception("tampered")\n')
        self.assertFalse(self.check())
        self.assertEqual(json.loads(self.output.read_text())['status'], 'gate-error')

    def test_parser_failure_is_propagated(self):
        self.commit_parser(returncode=1)
        self.prepare()
        self.assertFalse(self.check())
        self.assertEqual(json.loads(self.output.read_text())['benchmark_gate']['parser_returncode'], 1)

    def test_wrong_mode_cannot_pass_faithful_case(self):
        self.commit_parser(mode='fast')
        self.prepare()
        self.assertFalse(self.check(mode='faithful'))

    def test_missing_log_produces_explicit_failure(self):
        self.commit_parser()
        self.prepare()
        self.log.unlink()
        self.assertFalse(self.check())
        self.assertIn('log is missing', json.loads(self.output.read_text())['error'])


if __name__ == '__main__':
    unittest.main()
