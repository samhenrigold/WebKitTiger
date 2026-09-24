import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("hosted_iframe", ROOT / "tools/check-hosted-iframe.py")
CHECKER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECKER)


def screenshot(origin=(80, 154), text="frame"):
    image = Image.new("RGBA", (origin[0] + 960, origin[1] + 648), "white")
    draw = ImageDraw.Draw(image)
    draw.rectangle((origin[0] + 352, origin[1] + 232, origin[0] + 537, origin[1] + 259), outline="#888")
    if text:
        draw.text((origin[0] + 357, origin[1] + 237), text, font=ImageFont.load_default(), fill="black")
    return image


class HostedIframeTests(unittest.TestCase):
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

    def test_visible_frame_text(self):
        result, output = self.run_check(screenshot())
        self.assertEqual(result, 0, output)

    def test_page_only_origin(self):
        result, output = self.run_check(screenshot((0, 0)), "--page-origin", "0,0")
        self.assertEqual(result, 0, output)
        self.assertEqual(self.run_check()[0], 2)

    def test_blank_field_and_gray_placeholder_fail(self):
        self.assertEqual(self.run_check(screenshot(text=""))[0], 1)
        for shade in ("#eee", "#888", "#444"):
            with self.subTest(shade=shade):
                image = screenshot(text="")
                ImageDraw.Draw(image).rectangle((432, 386, 617, 413), fill=shade)
                self.assertEqual(self.run_check(image)[0], 1)

    def test_caret_focus_ring_and_bezel_cannot_pass(self):
        image = screenshot(text="")
        draw = ImageDraw.Draw(image)
        draw.rectangle((430, 384, 619, 415), outline="#369", width=2)
        draw.line((438, 391, 438, 407), fill="black", width=2)
        result, output = self.run_check(image)
        self.assertEqual(result, 1, output)

    def test_horizontal_line_and_solid_block_cannot_pass(self):
        for shape in ("line", "block"):
            with self.subTest(shape=shape):
                image = screenshot(text="")
                draw = ImageDraw.Draw(image)
                if shape == "line":
                    draw.line((440, 398, 480, 398), fill="black", width=2)
                else:
                    draw.rectangle((440, 391, 470, 401), fill="black")
                self.assertEqual(self.run_check(image)[0], 1)

    def test_transparent_text_screenshot_fails(self):
        image = screenshot()
        image.putalpha(0)
        self.assertEqual(self.run_check(image)[0], 1)

    def test_missing_invalid_and_cropped_images_are_errors(self):
        self.assertEqual(self.run_check()[0], 2)
        self.path.write_text("not an image")
        self.assertEqual(self.run_check()[0], 2)
        self.assertEqual(self.run_check(screenshot().crop((0, 0, 1040, 801)))[0], 2)


if __name__ == "__main__":
    unittest.main()
