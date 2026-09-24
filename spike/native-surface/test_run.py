#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import tempfile
import unittest
from PIL import Image, ImageDraw

spec = importlib.util.spec_from_file_location('native_surface_runner', Path(__file__).with_name('run.py'))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


def log(order='below'):
    value = -1 if order == 'below' else 1
    return '\n'.join([
        'SURFACE foreground-process=0', 'SURFACE front-process=0',
        'SURFACE attach-other-process-surface=0', 'SURFACE public-surface-order=0',
        'SURFACE read-surface-order=0', 'SURFACE detach-surface=0', 'SURFACE remove-owned-surface=0',
        f'NATIVE order requested={value} actual={value}',
        'NATIVE editing field-editor=1 text-match=1 selected=0,6 key=1 clipped-viewport=240x72',
        'NATIVE geometry origin=0,0 size=1280,720 field=40,72,360,28',
        'SURFACE RESULT mode=gl uploads=180 pixels=1280x720 elapsed=6.0',
    ])


def screenshot(native):
    image = Image.new('RGB', (1280, 720))
    draw = ImageDraw.Draw(image)
    for y in range(0, 720, 32):
        for x in range(0, 1280, 32):
            draw.rectangle((x, y, x + 31, y + 31), fill=(80, 200 if y // 32 % 2 else 64, 224 if x // 32 % 2 else 48))
    if native:
        draw.rectangle((430, 70, 609, 149), fill='magenta')
        draw.rectangle((40, 72, 399, 99), fill='white')
        draw.rectangle((44, 78, 104, 95), fill=(60, 120, 220))
        draw.text((110, 78), 'Cocoa text', fill='black')
        draw.rectangle((200, 170, 279, 197), fill='white')
        draw.text((204, 174), 'CLIPPED', fill='black')
        draw.rectangle((1240, 60, 1255, 559), fill=(220, 220, 220))
    return image


class NativeSurfaceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / 'shot.png'

    def test_api_parser_requires_real_order_edit_and_cleanup(self):
        self.assertTrue(runner.parse_result(log(), 'below')['native_editing_api'])
        for damaged in (log('above'), log().replace('text-match=1', 'text-match=0'),
                        log().replace('SURFACE detach-surface=0', ''),
                        log() + '\nSURFACE shape=1000', log().replace('uploads=180', 'uploads=179')):
            with self.subTest(log=damaged), self.assertRaises(ValueError):
                runner.parse_result(damaged, 'below')

    def test_native_controls_and_clipping_pass(self):
        screenshot(True).save(self.path)
        self.assertTrue(runner.check_screenshot(self.path, (0, 0), 'below')['passed'])

    def test_above_order_is_a_real_negative_control(self):
        screenshot(False).save(self.path)
        self.assertTrue(runner.check_screenshot(self.path, (0, 0), 'above')['passed'])
        self.assertFalse(runner.check_screenshot(self.path, (0, 0), 'below')['passed'])

    def test_leaked_clipped_field_rejects(self):
        image = screenshot(True)
        ImageDraw.Draw(image).rectangle((280, 170, 439, 197), fill='white')
        image.save(self.path)
        self.assertFalse(runner.check_screenshot(self.path, (0, 0), 'below')['passed'])

    def test_blank_images_reject(self):
        for color in ('white', 'black', (80, 80, 80)):
            Image.new('RGB', (1280, 720), color).save(self.path)
            self.assertFalse(runner.check_screenshot(self.path, (0, 0), 'below')['passed'])

    def test_missing_and_cropped_images_reject(self):
        with self.assertRaises(FileNotFoundError):
            runner.check_screenshot(self.path, (0, 0), 'below')
        Image.new('RGB', (100, 100)).save(self.path)
        with self.assertRaises(ValueError):
            runner.check_screenshot(self.path, (0, 0), 'below')


if __name__ == '__main__':
    unittest.main()
