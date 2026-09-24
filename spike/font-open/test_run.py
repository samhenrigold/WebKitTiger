#!/usr/bin/env python3
"""Fail-closed checks of the font-open result protocol; no device needed."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('font_open_runner', Path(__file__).with_name('run-probe.py'))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


def log(negative=False):
    lines = ['VERSION\t2.13.3']
    names = sorted(runner.NAMES)
    for label, index in sorted(runner.LABELS):
        courier = label in ('courier-normal', 'courier-fork')
        if negative and courier:
            values = [label, index, 0, 2, '', 0, 0, 0, -1, -1, -1, -1, 0, 0, 0, 0, 0, '00000000']
        else:
            name = names[index] if courier else ('Monaco' if label == 'monaco' else 'Courier')
            values = [label, index, 1, 0, name, 4 if courier else 1, 2048, 54, 0, 0, 0, 0, 845, 11, 15, 100, 15000, '12345678']
        lines.append('CASE\t' + '\t'.join(map(str, values)))
    lines.append('SUMMARY\t9\t0\t0\t0' if negative else 'SUMMARY\t0\t15\t15\t1')
    return '\n'.join(lines)


class FontOpenChecks(unittest.TestCase):
    def test_complete_rebuilt_and_expected_negative_pass(self):
        self.assertTrue(runner.check(log(), 0)['passed'])
        self.assertTrue(runner.check(log(True), 1, negative=True)['passed'])

    def test_exit_codes_are_required(self):
        for text, code, negative in ((log(), 1, False), (log(True), 0, True), (log(True), -14, True)):
            with self.subTest(code=code, negative=negative), self.assertRaises(ValueError):
                runner.check(text, code, negative)

    def test_partial_and_duplicate_logs_reject(self):
        for text in ('', log().split('SUMMARY')[0], '\n'.join(log().splitlines()[2:]),
                     log() + '\n' + log().splitlines()[1]):
            with self.subTest(text=text), self.assertRaises(ValueError):
                runner.check(text, 0)

    def test_negative_control_cannot_fail_system_fonts(self):
        text = log(True).replace('CASE\tmonaco\t0\t1\t0', 'CASE\tmonaco\t0\t0\t2')
        with self.assertRaises(ValueError):
            runner.check(text, 1, negative=True)

    def test_alias_pixel_mismatch_and_duplicate_face_reject(self):
        text = log().replace('courier-fork\t0', 'courier-fork\t0', 1)
        lines = text.splitlines()
        for index, line in enumerate(lines):
            if line.startswith('CASE\tcourier-fork\t0\t'):
                lines[index] = line.replace('12345678', 'abcdef00')
        with self.assertRaises(ValueError):
            runner.check('\n'.join(lines), 0)
        names = sorted(runner.NAMES)
        with self.assertRaises(ValueError):
            runner.check(log().replace(names[0] + '\t', names[1] + '\t'), 0)

    def test_pass_flag_cannot_hide_missing_metrics_or_api_errors(self):
        for damaged in (log().replace('\t845\t11\t15', '\t0\t11\t15', 1),
                        log().replace('\t0\t0\t0\t0\t845', '\t0\t0\t1\t0\t845', 1),
                        log().replace('\t100\t15000\t', '\t1000\t15000\t', 1)):
            with self.subTest(log=damaged), self.assertRaises(ValueError):
                runner.check(damaged, 0)


if __name__ == '__main__':
    unittest.main()
