#!/usr/bin/env python3
"""Check scrollbar-colors.html at its initial, unscrolled 960 x 648 viewport.

Requires Pillow. Samples opaque 3 x 3 patches inside every styled track, thumb and
corner. The default page origin is (80, 154) in a full Tiger screenshot. Each RGB
channel of every sampled pixel must be within --tolerance (default 24, maximum 32).
Exit status: 0 pass, 1 color mismatch, 2 invalid/missing screenshot or arguments.
"""

import argparse
import sys

from PIL import Image


PAGE_SIZE = (960, 648)
ROOT_COLORS = {
    "author": ((160, 20, 180), (248, 220, 120)),
    "alternate": ((200, 40, 30), (200, 220, 250)),
}


def page_origin(value):
    try:
        x, y = (int(part) for part in value.split(","))
        if x < 0 or y < 0:
            raise ValueError
        return x, y
    except ValueError:
        raise argparse.ArgumentTypeError("page origin must be two nonnegative integers: X,Y") from None


def tolerance(value):
    try:
        result = int(value)
        if not 0 <= result <= 32:
            raise ValueError
        return result
    except ValueError:
        raise argparse.ArgumentTypeError("tolerance must be an integer from 0 to 32") from None


def samples(root_mode):
    root_thumb, root_track = ROOT_COLORS[root_mode]
    for name, thumb, track, points in (
        ("root", root_thumb, root_track,
            ((952, 10), (952, 610), (10, 640), (920, 640), (952, 640))),
        ("overflow", (22, 68, 200), (255, 204, 160),
            ((252, 90), (252, 230), (30, 252), (230, 252), (252, 252))),
        ("thin", (0, 128, 32), (205, 250, 200),
            ((754, 90), (754, 230), (590, 254), (740, 254), (754, 254))),
        ("iframe", (200, 40, 30), (200, 220, 250),
            ((252, 330), (252, 470), (30, 492), (230, 492), (252, 492))),
    ):
        for part, point, color in zip(
            ("vertical-thumb", "vertical-track", "horizontal-thumb", "horizontal-track", "corner"),
            points, (thumb, track, thumb, track, track),
        ):
            yield name + "/" + part, point, color


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("screenshot")
    parser.add_argument("--page-origin", type=page_origin, default=(80, 154), metavar="X,Y")
    parser.add_argument("--tolerance", type=tolerance, default=24, metavar="0..32")
    parser.add_argument("--root-mode", choices=ROOT_COLORS, default="author",
                        help="initial author palette, or the Other colors button's palette")
    args = parser.parse_args(argv)

    try:
        with Image.open(args.screenshot) as source:
            image = source.convert("RGBA")
    except (OSError, ValueError, Image.DecompressionBombError) as error:
        print("scrollbar-colors: cannot read screenshot: " + str(error), file=sys.stderr)
        return 2

    origin_x, origin_y = args.page_origin
    if image.width < origin_x + PAGE_SIZE[0] or image.height < origin_y + PAGE_SIZE[1]:
        print("scrollbar-colors: screenshot %dx%d does not contain the 960x648 page at %d,%d"
              % (image.width, image.height, origin_x, origin_y), file=sys.stderr)
        return 2

    failures = 0
    count = 0
    for name, (page_x, page_y), expected in samples(args.root_mode):
        count += 1
        x, y = origin_x + page_x, origin_y + page_y
        pixels = list(image.crop((x - 1, y - 1, x + 2, y + 2)).getdata())
        max_error = max(abs(pixel[channel] - expected[channel]) for pixel in pixels for channel in range(3))
        minimum_alpha = min(pixel[3] for pixel in pixels)
        if max_error <= args.tolerance and minimum_alpha == 255:
            continue
        failures += 1
        low = tuple(min(pixel[channel] for pixel in pixels) for channel in range(3))
        high = tuple(max(pixel[channel] for pixel in pixels) for channel in range(3))
        print("FAIL %s page=(%d,%d) screenshot=(%d,%d): expected RGB=%s, center RGB=%s, "
              "patch RGB range=%s..%s, max error=%d (allowed %d), min alpha=%d"
              % (name, page_x, page_y, x, y, expected, image.getpixel((x, y))[:3],
                 low, high, max_error, args.tolerance, minimum_alpha))

    print("scrollbar-colors: %s %d/%d patches; origin=%d,%d tolerance=%d root=%s"
          % ("FAIL" if failures else "PASS", count - failures, count,
             origin_x, origin_y, args.tolerance, args.root_mode))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
