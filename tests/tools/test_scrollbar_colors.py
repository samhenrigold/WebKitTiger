import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("scrollbar_colors", ROOT / "tools/check-scrollbar-colors.py")
CHECKER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECKER)


def screenshot(origin=(80, 154), alternate=False):
    """Paint independent scrollbar rectangles, rather than copying the checker's samples."""
    image = Image.new("RGBA", (origin[0] + 960, origin[1] + 648), "white")
    draw = ImageDraw.Draw(image)

    def rectangle(box, color):
        x1, y1, x2, y2 = box
        draw.rectangle((origin[0] + x1, origin[1] + y1, origin[0] + x2, origin[1] + y2), fill=color)

    def bars(x, y, width, height, thickness, thumb_width, thumb_height, thumb, track):
        rectangle((x + width - thickness, y, x + width - 1, y + height - 1), track)
        rectangle((x, y + height - thickness, x + width - 1, y + height - 1), track)
        rectangle((x + width - thickness, y, x + width - 1, y + thumb_height - 1), thumb)
        rectangle((x, y + height - thickness, x + thumb_width - 1, y + height - 1), thumb)

    thumb, track = ("#c8281e", "#c8dcfa") if alternate else ("#a014b4", "#f8dc78")
    bars(0, 0, 960, 648, 15, 558, 223, thumb, track)
    bars(20, 80, 240, 180, 15, 105, 43, "#1644c8", "#ffcca0")
    bars(580, 80, 180, 180, 11, 60, 45, "#008020", "#cdfac8")
    bars(20, 320, 240, 180, 15, 105, 43, "#c8281e", "#c8dcfa")
    return image


class ScrollbarColorTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / "shot.png"

    def run_check(self, image=None, *arguments):
        if image is not None:
            image.save(self.path)
        stdout, stderr = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            result = CHECKER.main([str(self.path), *arguments])
        return result, stdout.getvalue() + stderr.getvalue()

    def test_initial_palette_and_all_patches(self):
        result, output = self.run_check(screenshot())
        self.assertEqual(result, 0, output)
        self.assertIn("PASS 20/20", output)

    def test_explicit_origin_supports_page_only_screenshot(self):
        result, output = self.run_check(screenshot((0, 0)), "--page-origin", "0,0")
        self.assertEqual(result, 0, output)
        self.assertEqual(self.run_check()[0], 2)

    def test_tolerance_boundary_checks_every_pixel(self):
        image = screenshot()
        point = (80 + 951, 154 + 9)  # One corner of the root thumb's 3x3 sample.
        image.putpixel(point, (184, 20, 180, 255))
        self.assertEqual(self.run_check(image)[0], 0)
        image.putpixel(point, (185, 20, 180, 255))
        result, output = self.run_check(image)
        self.assertEqual(result, 1)
        self.assertIn("root/vertical-thumb", output)
        self.assertIn("max error=25 (allowed 24)", output)
        self.assertIn("center RGB=(160, 20, 180)", output)

    def test_each_pane_and_corner_are_required(self):
        for name, point in (
            ("root/corner", (952, 640)),
            ("root/horizontal-thumb", (10, 640)),
            ("overflow/vertical-thumb", (252, 90)),
            ("thin/horizontal-track", (740, 254)),
            ("iframe/vertical-track", (252, 470)),
        ):
            with self.subTest(name=name):
                image = screenshot()
                image.putpixel((80 + point[0], 154 + point[1]), (128, 128, 128, 255))
                result, output = self.run_check(image)
                self.assertEqual(result, 1)
                self.assertIn("FAIL " + name, output)
                self.assertIn("center RGB=(128, 128, 128)", output)

    def test_gray_and_white_blank_screenshots_fail(self):
        for color in ("gray", "white"):
            with self.subTest(color=color):
                result, output = self.run_check(Image.new("RGB", (1440, 900), color))
                self.assertEqual(result, 1, output)

    def test_transparent_expected_colors_do_not_pass(self):
        image = screenshot()
        image.putalpha(0)
        result, output = self.run_check(image)
        self.assertEqual(result, 1)
        self.assertIn("min alpha=0", output)

    def test_missing_and_invalid_images_are_errors(self):
        self.assertEqual(self.run_check()[0], 2)
        self.path.write_text("not an image")
        self.assertEqual(self.run_check()[0], 2)

    def test_cropped_screenshot_is_an_error(self):
        self.assertEqual(self.run_check(screenshot().crop((0, 0, 1040, 801)))[0], 2)

    def test_alternate_palette_must_be_selected(self):
        image = screenshot(alternate=True)
        self.assertEqual(self.run_check(image)[0], 1)
        result, output = self.run_check(image, "--root-mode", "alternate")
        self.assertEqual(result, 0, output)

    def test_invalid_origin_and_overbroad_tolerance_are_rejected(self):
        for option, value in (("--page-origin", "-1,0"), ("--page-origin", "a,b"),
                              ("--tolerance", "-1"), ("--tolerance", "33")):
            with self.subTest(option=option, value=value):
                with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as result:
                    CHECKER.main([str(self.path), option + "=" + value])
                self.assertEqual(result.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
