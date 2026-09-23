from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class RegressionLogTests(unittest.TestCase):
    def faults(self, lines):
        script = (ROOT / 'tools/regress.sh').read_text()
        function = script.split('log_faults() {', 1)[1].split('\n# record ', 1)[0]
        with tempfile.TemporaryDirectory() as temp:
            log = Path(temp) / 'app.log'
            log.write_text('\n'.join(lines) + '\n')
            return subprocess.check_output(
                ['/bin/sh', '-c', 'log_faults() {' + function + '\nlog_faults "$1"', 'test', str(log)],
                text=True).strip()

    def test_already_playing_autoplay_transition_is_not_denial(self):
        self.assertEqual(self.faults([
            '[com.apple.WebKit:Media:-] HTMLMediaElement::setReadyState(8102CE0BE5C1C5BF) Autoplay blocked with reason: PageConsentRequired: !paused',
            'TIGER ui: incorporate 1.0 ms',
        ]), '')

    def test_real_denials_and_crashes_still_fail(self):
        for line in [
            'HTMLMediaElement::setReadyState(1234) Autoplay blocked with reason: UserGestureRequired: user gesture required',
            'HTMLMediaElement::setReadyState(1234) Autoplay blocked with reason: PageConsentRequired: !mediaSession().autoplayPermitted',
            'other function Autoplay blocked with reason: PageConsentRequired: !paused',
            'TIGER-CRASH signal 11',
            'TIGER-ABORT',
            'process unresponsive',
            'cannot connect to : server',
        ]:
            with self.subTest(line=line):
                self.assertNotEqual(self.faults([line, 'TIGER ui: incorporate 1.0 ms']), '')

    def test_paint_is_required_even_without_diagnostics(self):
        self.assertIn('nothing was painted', self.faults(['page loaded']))


if __name__ == '__main__':
    unittest.main()
