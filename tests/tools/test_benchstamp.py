import os
from pathlib import Path
import subprocess
import tempfile
import unittest


WRAPPER = Path(__file__).resolve().parents[2] / 'spike/bench/benchstamp.pl'


class BenchstampTests(unittest.TestCase):
    def run_wrapper(self, script, env=None):
        return subprocess.run(['perl', str(WRAPPER), '/bin/sh', '-c', script],
                              capture_output=True, text=True, timeout=5,
                              env=dict(os.environ, BENCH_PS_AT='0.02', **(env or {})))

    def test_preserves_app_failure_and_output(self):
        result = self.run_wrapper('echo app-output; exit 7')
        self.assertEqual(result.returncode, 7)
        self.assertRegex(result.stdout, r'\d+\.\d{3} app-output')

    def test_reports_signal_exit(self):
        result = self.run_wrapper('kill -TERM $$')
        self.assertEqual(result.returncode, 143)

    def test_untruncated_tiger_process_table(self):
        with tempfile.TemporaryDirectory() as directory:
            ps = Path(directory) / 'ps'
            ps.write_text('#!/bin/sh\n[ "$1" = -axww ] || exit 1\n'
                          'echo "123 1 2048 4096 30.0 0:00.10 /Users/shg/wk2/runs/long-unique-run/bin/TigerWebProcess"\n')
            ps.chmod(0o755)
            result = self.run_wrapper('sleep 0.1; exit 0', {'PATH': directory + ':' + os.environ['PATH']})
        self.assertEqual(result.returncode, 0)
        self.assertIn('BENCH-PS 0.02: 123 1 2048 4096 30.0 0:00.10', result.stdout)


if __name__ == '__main__':
    unittest.main()
