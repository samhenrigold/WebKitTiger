"""Reject incomplete or wrongly attributed probe timing results."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('surface_run', Path(__file__).with_name('run.py'))
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)
REMOTE = '/Users/shg/wk2/runs/test-surface'
LOG = '''SURFACE mode=ca
SURFACE frame_storage=malloc
SURFACE quartzcore_path=/Users/shg/wk2/runs/test-surface/bin/../Frameworks/QuartzCore.framework/Versions/A/QuartzCore
SURFACE overlay=opaque-magenta rect=40,40,200,72 above=fresh-video-image
SURFACE RESULT mode=ca uploads=240 pixels=1280x720 elapsed=7.98 rate=30.08 work_mean_ms=9.1 work_p95_ms=10.2 work_max_ms=40.3 slowest_frame=0 copy_mean_ms=1.1 render_mean_ms=6.0 flush_mean_ms=2.0 completed_at=8.1 deadline=8.5 (flushes, not scanout)
SURFACE detach-surface=0
SURFACE remove-owned-surface=0
'''

LOG += ''.join(f'SURFACE FRAME id={i} allocation_ms=0.100 memcpy_ms=0.900 image_ms=0.100 transaction_ms=1.000 render_ms=5.000 flush_ms=2.000 total_ms=9.100\n' for i in range(240))


class SurfaceResultTests(unittest.TestCase):
    def test_completed_ca_result_keeps_work_components(self):
        result = probe.parse_result(LOG, 'ca', REMOTE)
        self.assertEqual(result['uploads'], 240)
        self.assertEqual(result['copy_mean_ms'], 1.1)
        self.assertEqual(result['render_mean_ms'], 6.0)
        self.assertEqual(result['flush_mean_ms'], 2.0)
        self.assertIn('not decoded video delivery', result['measurement'])

    def test_default_gl_does_not_require_ca_framework(self):
        log = '\n'.join(line for line in LOG.splitlines() if 'quartzcore_path' not in line and 'overlay=' not in line)
        self.assertEqual(probe.parse_result(log.replace('mode=ca', 'mode=gl'), 'gl', REMOTE)['mode'], 'gl')

    def test_incomplete_or_late_result_fails(self):
        for old, new in [('uploads=240', 'uploads=239'), ('completed_at=8.1', 'completed_at=10.1'), ('deadline=8.5', 'deadline=11')]:
            with self.subTest(new=new), self.assertRaises(ValueError):
                probe.parse_result(LOG.replace(old, new), 'ca', REMOTE)

    def test_wrong_mode_or_other_framework_fails(self):
        with self.assertRaises(ValueError):
            probe.parse_result(LOG, 'gl', REMOTE)
        with self.assertRaises(ValueError):
            probe.parse_result(LOG.replace(REMOTE + '/bin/..', '/System/Library'), 'ca', REMOTE)

    def test_missing_cleanup_or_overlay_fails(self):
        for line in ['SURFACE detach-surface=0', 'SURFACE remove-owned-surface=0', 'SURFACE overlay=']:
            with self.subTest(line=line), self.assertRaises(ValueError):
                probe.parse_result('\n'.join(item for item in LOG.splitlines() if not item.startswith(line)), 'ca', REMOTE)

    def test_detailed_stages_include_explicit_warmup_window(self):
        details = probe.parse_result(LOG, 'ca', REMOTE)['frame_timing']
        self.assertEqual(details['all']['count'], 240)
        self.assertEqual(details['after_first_30_frames']['count'], 210)
        self.assertEqual(details['all']['mean_ms']['memcpy'], .9)

    def test_missing_or_inconsistent_frame_details_fail(self):
        for changed in [LOG.replace('SURFACE FRAME id=8 ', 'SURFACE FRAME id=7 '),
                        LOG.replace('total_ms=9.100', 'total_ms=1.000')]:
            with self.assertRaises(ValueError):
                probe.parse_result(changed, 'ca', REMOTE)

    def test_pool_reuse_requires_consistent_counters(self):
        pooled = LOG.replace('frame_storage=malloc', 'frame_storage=pool')
        pooled += 'SURFACE POOL allocations=3 reuses=237 fallbacks=0 in_use=1 peak_in_use=3 slots=3\n'
        result = probe.parse_result(pooled, 'ca', REMOTE, 'pool')
        self.assertEqual(result['pool']['reuses'], 237)
        with self.assertRaises(ValueError):
            probe.parse_result(pooled.replace('reuses=237', 'reuses=236'), 'ca', REMOTE, 'pool')

    def test_duplicate_or_missing_result_fails(self):
        with self.assertRaises(ValueError):
            probe.parse_result(LOG + LOG, 'ca', REMOTE)
        with self.assertRaises(ValueError):
            probe.parse_result('SURFACE renderer=NVIDIA', 'ca', REMOTE)


if __name__ == '__main__':
    unittest.main()
