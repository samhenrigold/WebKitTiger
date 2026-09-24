"""Exercise only the native-fixture row block with local images and a mocked run()."""
import importlib.util
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = (ROOT / "tools/regress.sh").read_text()
ROWS = ("hostedclip", "fhostedclip", "scrollcolors", "fscrollcolors", "scrollcolorsalt", "fscrollcolorsalt")


def load_fixture(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tests/tools" / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.screenshot


hosted_image = load_fixture("test_hosted_iframe")
color_image = load_fixture("test_scrollbar_colors")


def function(name):
    start = SCRIPT.index(name + "() {")
    return SCRIPT[start:SCRIPT.index("\n}", start) + 2]


class NativeFixtureRowTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.fixtures, self.out = self.root / "fixtures", self.root / "out"
        self.fixtures.mkdir()
        self.out.mkdir()
        for row in ROWS:
            if "hostedclip" in row:
                image = hosted_image()
                title = "TIGER title: hosted clipping over=0 hidden=1 frameY=0 hits=1,1,1 values=//frame/\n"
            else:
                alternate = row.endswith("alt")
                image = color_image(alternate=alternate)
                title = "TIGER title: colors mode=auto root=0,0\n" if alternate else ""
                title += "TIGER title: colors mode=" + ("alternate" if alternate else "author")
                title += " root=0,0 over=0,0 auto=0,0 thin=0,0 frame=0,0\n"
            image.save(self.fixtures / (row + ".png"))
            (self.fixtures / (row + ".log")).write_text(title)

    def run_rows(self, wanted=ROWS):
        # Do not execute the harness's setup, verifier, stage helper, servers or other rows.
        block = SCRIPT.split("# Fully clipped native controls", 1)[1].split("# Ring lifecycle workload", 1)[0]
        shell = "\n".join([
            "set -u", "WKT=" + shlex.quote(str(ROOT)), "OUT=" + shlex.quote(str(self.out)),
            "FIXTURES=" + shlex.quote(str(self.fixtures)), "SHARE=/unused", "WANTED=" + shlex.quote(" ".join(wanted)),
            function("fixture_shot"), function("title_seen"),
            'wants() { case " $WANTED " in *" $1 "*) return 0;; *) return 1;; esac; }',
            'run() { printf "%s\\t%s\\t%s\\n" "$1" "$4" "${5:-}" >> "$OUT/calls"; '
            'cp "$FIXTURES/$1.log" "$OUT/$1.log"; '
            'if [ -f "$FIXTURES/$1.png" ]; then cp "$FIXTURES/$1.png" "$OUT/$1.png"; fi; }',
            'check() { case "$2" in *FAILED*) printf "FAIL %s\\n" "$1";; *) printf "PASS %s\\n" "$1";; esac; }',
            "# Fully clipped native controls" + block,
        ])
        return subprocess.check_output(["/bin/sh", "-c", shell], text=True).splitlines()

    def test_shell_syntax_and_optional_row_registration(self):
        subprocess.run(["/bin/sh", "-n", str(ROOT / "tools/regress.sh")], check=True)
        listed = subprocess.check_output(["/bin/sh", str(ROOT / "tools/regress.sh"), "--list"], text=True).split()
        self.assertTrue(set(ROWS) <= set(listed))
        default = SCRIPT.split('DEFAULT="', 1)[1].split('"', 1)[0].split()
        self.assertEqual(default, "example scroll controls boxtest textarea xcom video youtube fexample fscroll fcontrols cookies ghost fghost".split())

    def test_correct_images_titles_and_palette_transitions_pass(self):
        self.assertEqual(self.run_rows(), ["PASS " + row for row in ROWS])
        calls = (self.out / "calls").read_text().splitlines()
        for call in calls:
            row, actions, environment = call.split("\t")
            self.assertEqual("TIGER_FAITHFUL=1" in environment, row.startswith("f"))
            if row.endswith("alt"):
                self.assertIn("click 200,34; wait 1; click 330,34", actions)

    def test_matching_titles_do_not_allow_blank_screenshots(self):
        for row in ROWS:
            Image.new("RGB", (1440, 900), "white").save(self.fixtures / (row + ".png"))
        self.assertEqual(self.run_rows(), ["FAIL " + row for row in ROWS])

    def test_alternate_rows_require_alternate_pixels(self):
        rows = ("scrollcolorsalt", "fscrollcolorsalt")
        for row in rows:
            color_image().save(self.fixtures / (row + ".png"))
        self.assertEqual(self.run_rows(rows), ["FAIL " + row for row in rows])

    def test_matching_pixels_do_not_allow_missing_title(self):
        for row in ROWS:
            (self.fixtures / (row + ".log")).write_text("page painted\n")
        self.assertEqual(self.run_rows(), ["FAIL " + row for row in ROWS])

    def test_checker_input_error_fails_row(self):
        (self.fixtures / "hostedclip.png").unlink()
        self.assertEqual(self.run_rows(("hostedclip",)), ["FAIL hostedclip"])

    def test_multiline_failure_stays_in_one_summary_cell(self):
        Image.new("RGB", (1440, 900), "white").save(self.out / "hostedclip.png")
        shell = "\n".join([
            "WKT=" + shlex.quote(str(ROOT)), "OUT=" + shlex.quote(str(self.out)),
            function("fixture_shot"), "fixture_shot check-hosted-iframe hostedclip",
        ])
        output = subprocess.check_output(["/bin/sh", "-c", shell], text=True)
        self.assertEqual(len(output.splitlines()), 1)
        self.assertTrue(output.startswith("FAILED hosted-iframe: FAIL"))
        self.assertIn("need at least 48 dark pixels", output)

    def test_filetracks_selects_two_distinct_rendering_modes(self):
        block = SCRIPT.split('for tracks_case in filetracks ffiletracks;', 1)[1].split('\n# 12. Cookies:', 1)[0]
        shell = '\n'.join([
            'MEDIA_HOST=unused',
            'wants() { return 0; }',
            'run() { printf "%s:%s\\n" "$1" "$5"; }',
            'check() { :; }', 'title_seen() { :; }',
            'for tracks_case in filetracks ffiletracks;' + block,
        ])
        output = subprocess.check_output(['/bin/sh', '-c', shell], text=True).splitlines()
        self.assertEqual(output, ['filetracks:TIGER_CONSOLE=1',
                                 'ffiletracks:TIGER_CONSOLE=1 TIGER_FAITHFUL=1'])


if __name__ == "__main__":
    unittest.main()
