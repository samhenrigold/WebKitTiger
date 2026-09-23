import importlib.util
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location('paint', Path(__file__).resolve().parents[2] / 'tools/paint-metrics.py')
PAINT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PAINT)


class PaintMetricsTests(unittest.TestCase):
    def test_separates_operations_and_excludes_warmup_and_end(self):
        lines = [
            ' 1.000 TIGER ui: drawRect 99.0 ms rect=100x100+0+0 unpainted=0\n',
            ' 4.000 TIGER gpu: frame 100x100+0+0 damage=video render 5.0 ms bitmap 0.1 ms readback 8.0 ms reverse 1.0 ms\n',
            ' 4.010 TIGER ui: incorporate 2.0 ms bounds=100x100+0+0\n',
            ' 4.020 TIGER ui: drawRect 1.0 ms rect=100x100+0+0 unpainted=0\n',
            ' 4.060 TIGER ui: drawRect 3.0 ms rect=100x100+0+0 unpainted=0\n',
            ' 5.000 TIGER ui: drawRect 99.0 ms rect=100x100+0+0 unpainted=0\n',
        ]
        result = PAINT.summarize(lines, 4, 5)['operations']
        self.assertEqual(result['gpu_render']['count'], 1)
        self.assertEqual(result['gpu_readback']['cost_p95_ms'], 8)
        self.assertEqual(result['ui_incorporate']['count'], 1)
        self.assertEqual(result['ui_draw']['per_second'], 2)
        self.assertAlmostEqual(result['ui_draw']['gap_max_ms'], 40)
        self.assertEqual(result['ui_draw']['cost_p95_ms'], 3)

    def test_skipped_gpu_frame_is_not_rendered(self):
        result = PAINT.summarize(['4.200 TIGER gpu: frame skipped, nothing damaged\n'], 4, 5)
        self.assertEqual(result['operations']['gpu_render']['count'], 0)

    def test_unstamped_log_does_not_invent_rate(self):
        for end in (None, 10):
            result = PAINT.summarize(['TIGER ui: drawRect 1.0 ms\n'], end=end)
            self.assertIsNone(result['operations']['ui_draw']['per_second'])

    def test_elapsed_window_includes_idle_tail(self):
        result = PAINT.summarize(['4.100 TIGER ui: drawRect 1.0 ms\n', '6.000 page idle\n'])
        self.assertEqual(result['operations']['ui_draw']['per_second'], 0.5)


if __name__ == '__main__':
    unittest.main()
