import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('live_sites', Path(__file__).resolve().parents[2] / 'tools/live-site-evidence.py')
LIVE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(LIVE)


class LiveSiteEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.metric = {'url': 'https://www.youtube.com/watch?v=expected', 'stage_exit_status': 0,
                       'titles': [[1.2, 'Expected video - YouTube']], 'fvnl': 1.3, 'crashes': [],
                       'paint_operations': {'duration_s': 88, 'operations': {'ui_draw': {'count': 2600, 'per_second': 29.5}}},
                       'input': {'tti_moves': 58, 'hover_n': 15, 'wheel_n': 40, 'hover': [0.01], 'wheel': [0.02], 'tti': 2},
                       'media': []}
        (self.root / 'youtube.log').write_text('4.100 TIGER ui: drawRect 1.0 ms rect=100x100+0+0\n')

    def run_report(self):
        return LIVE.build_report(self.root, {'youtube': self.metric})

    def test_absent_targets_are_explicit(self):
        report = self.run_report()
        self.assertEqual(set(report['targets']), set(LIVE.TARGETS))
        for name in ('X', 'New York Times', 'The Verge'):
            self.assertEqual(report['targets'][name], {'assessment': 'not-recorded', 'runs': []})

    def test_paints_titles_and_input_never_prove_functionality(self):
        run = self.run_report()['targets']['YouTube']['runs'][0]
        self.assertEqual(run['observation'], 'window-paint-activity-observed')
        self.assertEqual(run['input_activity']['logged_moves'], 73)
        self.assertEqual(run['input_activity']['scroll_frame_response_pairs'], 1)
        self.assertEqual(run['functional_acceptance'], 'unverified')
        self.assertEqual(set(run['checks'].values()), {'unverified'})
        self.assertEqual(run['screenshot']['status'], 'not-recorded')

    def test_decoder_fps_is_not_window_fps_or_target_streaming(self):
        self.metric['media'] = [{'t': 12.0, 'fps': 30.0, 'dropped': 0, 'pts': 10.0}]
        run = self.run_report()['targets']['YouTube']['runs'][0]
        self.assertEqual(run['decoder_telemetry']['status'], 'recorded')
        self.assertEqual(run['video_window_paints']['status'], 'not-recorded')
        self.assertEqual(run['checks']['streaming'], 'unverified')
        self.assertEqual(run['checks']['displayed_frame_rate'], 'unverified')
        self.assertEqual(run['checks']['audio_output'], 'unverified')

    def test_window_telemetry_with_dimensions_does_not_certify_video_identity(self):
        (self.root / 'youtube.video-paints.json').write_text(json.dumps({
            'measurement': 'window-paint-not-scanout', 'status': 'complete', 'seconds': 30,
            'expected_pixels': [1280, 720], 'expected_display': [1280, 720],
            'streams': [{'ring': '/tmp/ad', 'fps': 30, 'unique_frames': 900,
                         'actual_pixel_sizes': {'1280x720': 900}, 'actual_display_sizes': {'1280x720': 900}}]}))
        video = self.run_report()['targets']['YouTube']['runs'][0]['video_window_paints']
        self.assertEqual(video['status'], 'recorded')
        self.assertEqual(video['streams'][0]['fps'], 30)
        self.assertEqual(video['frame_rate_acceptance'], 'unverified')
        self.assertEqual(video['target_video_identity'], 'unverified')

    def test_load_failures_separate_cancellation_and_ignore_symbol_samples(self):
        diagnostics = LIVE.load_diagnostics([
            '1.000 TIGER-SAMPLE crashinfo frames: __error didFailLoadingSymbol\n',
            '2.000 TIGER paint: cairo status after paint: no error has occurred\n',
            '3.000 NetworkResourceLoader::didFailLoading: (isTimeout=1, isCancellation=0, errorCode=-1001)\n',
            '4.000 NetworkResourceLoader::didFailLoading: (isCancellation=1, errorCode=-999)\n'])
        self.assertEqual(diagnostics['failure_markers'], 1)
        self.assertEqual(diagnostics['cancellation_markers'], 1)
        self.assertEqual(diagnostics['examples'][0]['line'], 3)
        self.assertEqual(diagnostics['examples'][0]['seconds'], 3)

    def test_failed_run_and_crash_markers_are_visible_despite_paint_activity(self):
        self.metric['stage_exit_status'] = 1
        run = self.run_report()['targets']['YouTube']['runs'][0]
        self.assertEqual(run['observation'], 'run-failed')
        self.metric['stage_exit_status'] = 0
        self.metric['crashes'] = [[8.0, 'TIGER-CRASH signal 11']]
        run = self.run_report()['targets']['YouTube']['runs'][0]
        self.assertEqual(run['observation'], 'crash-markers-observed')
        self.assertEqual(run['crash_markers']['count'], 1)

    def test_missing_log_and_invalid_video_artifact_are_not_silent_passes(self):
        (self.root / 'youtube.log').unlink()
        (self.root / 'youtube.video-paints.json').write_text('{invalid')
        run = self.run_report()['targets']['YouTube']['runs'][0]
        self.assertEqual(run['observation'], 'log-missing')
        self.assertIsNone(run['load_diagnostics'])
        self.assertEqual(run['video_window_paints']['status'], 'invalid-artifact')

    def test_hostname_matching_does_not_accept_lookalikes(self):
        for url in ('https://youtube.com.example.org/watch', 'https://notx.com/', 'https://x.com@other.org/'):
            self.assertIsNone(LIVE.target_for_url(url))
        self.assertEqual(LIVE.target_for_url('https://mobile.twitter.com/'), 'X')

    def test_report_writes_both_artifacts_and_escapes_titles(self):
        self.metric['titles'] = [[2, 'Video | hello\nworld']]
        text = LIVE.write_report(self.root, {'youtube': self.metric})
        self.assertIn('Video \\| hello world', text)
        self.assertIn('**Unverified for every target:**', text)
        self.assertEqual((self.root / 'live-sites.md').read_text(), text)
        report = json.loads((self.root / 'live-sites.json').read_text())
        self.assertEqual(report['schema'], 1)
        self.assertTrue(report['targets']['YouTube']['runs'][0]['artifacts']['log_sha256'])

    def test_bench_report_hook_preserves_explicit_absent_targets(self):
        script = Path(__file__).resolve().parents[2] / 'tools/bench-report.py'
        result = subprocess.run([sys.executable, str(script), str(self.root)], capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('Live-site acceptance evidence', (self.root / 'tables.md').read_text())
        report = json.loads((self.root / 'live-sites.json').read_text())
        self.assertEqual({row['assessment'] for row in report['targets'].values()}, {'not-recorded'})


if __name__ == '__main__':
    unittest.main()
