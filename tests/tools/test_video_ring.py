"""Host coverage of the actual Tiger video-ring header, not a Python model.

Run after the baseline build freeze is lifted:
    python3 -m unittest discover -s tests/tools -p 'test_video_ring.py' -v
Before the shared header is replaced, set
    VIDEO_RING_HEADER=build/handoff/tigervideoring-v2.h
Set VIDEO_RING_HEADER to select a header; VIDEO_RING_SANITIZE=address,undefined
or thread enables the compiler's sanitizer. This compiles only a host test helper.
"""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class VideoRingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="tiger-video-ring-test-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binary = Path(cls.temp.name) / "video-ring-stress"
        cls.header = Path(os.environ.get("VIDEO_RING_HEADER", ROOT / "spike/media/tigervideoring.h")).resolve()
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c11", "-O2", "-g", "-Wall", "-Wextra", "-Werror", "-pthread",
            "-D_DEFAULT_SOURCE", f'-DTVR_TEST_HEADER="{cls.header}"',
            str(ROOT / "tests/tools/video_ring_stress.c"), "-o", str(cls.binary),
        ]
        if sanitizer := os.environ.get("VIDEO_RING_SANITIZE"):
            command.extend([f"-fsanitize={sanitizer}", "-fno-omit-frame-pointer"])
        subprocess.run(command, check=True, capture_output=True, text=True, timeout=60)

    def run_case(self, case):
        result = subprocess.run([str(self.binary), case], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_bounds_and_truncated_headers(self):
        self.run_case("bounds")

    def test_pinned_slots_wrap_and_bounded_starvation(self):
        self.run_case("ownership")

    def test_concurrent_writer_and_four_readers(self):
        self.run_case("concurrent")

    def test_resize_unlink_and_snapshot_lifetime(self):
        self.run_case("resize")

    def test_reader_process_deaths_and_replacement(self):
        self.run_case("crashed-readers")

    def test_cpp_header_layout(self):
        command = shlex.split(os.environ.get("CXX", "c++")) + ["-std=c++17", "-x", "c++", "-fsyntax-only", "-"]
        source = f'#include "{self.header}"\n' + """
static_assert(sizeof(TigerVideoRing) == 64);
static_assert(offsetof(TigerVideoRing, slotGeneration) == 32);
static_assert(offsetof(TigerVideoRing, slotState) == 48);
"""
        result = subprocess.run(command, input=source, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
