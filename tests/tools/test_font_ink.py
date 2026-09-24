import importlib.util
from pathlib import Path
import unittest

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('font_ink', ROOT / 'tools/check-font-ink.py')
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class FontInkTests(unittest.TestCase):
    def image(self):
        image = Image.new('RGB', (1440, 900), '#eeeeee')
        draw = ImageDraw.Draw(image)
        font = ImageFont.load_default(size=20)
        for row in range(12):
            draw.text((322, 211 + row * 45), 'Sphinx 012345', fill='#111111', font=font)
        return image

    def test_all_samples_require_glyph_ink(self):
        self.assertTrue(all(row[1] for row in checker.check(self.image())))

    def test_one_missing_sample_fails_independently(self):
        image = self.image()
        ImageDraw.Draw(image).rectangle((320, 342, 680, 377), fill='#eeeeee')
        results = checker.check(image)
        self.assertEqual([row[0] for row in results if not row[1]], ['cursive'])

    def test_solid_rectangle_is_not_text(self):
        image = self.image()
        ImageDraw.Draw(image).rectangle((320, 207, 679, 242), fill='black')
        self.assertFalse(checker.check(image)[0][1])

    def test_invalid_screenshot_is_rejected(self):
        with self.assertRaises(ValueError):
            checker.check(Image.new('RGB', (940, 620)))


if __name__ == '__main__':
    unittest.main()
