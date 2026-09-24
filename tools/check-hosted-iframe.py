#!/usr/bin/env python3
"""Check visible text in hosted-clipping.html's offset iframe field after typing frame.

Pair this screenshot check with the fixture's title/value assertion. This checks
glyph-shaped ink, not OCR. The page must be unscrolled at its default 960 x 648
viewport. --page-origin defaults to 80,154 in a full Tiger screenshot.
Exit status: 0 visible text, 1 missing/implausible text, 2 invalid screenshot/input.
"""

import argparse
import sys

from PIL import Image


PAGE_SIZE = (960, 648)
# Four pixels inside the known 186x28 field, excluding its bezel and focus ring.
INTERIOR = (356, 236, 534, 256)
DARK_CHANNEL_LIMIT = 110
MINIMUM_DARK_PIXELS = 48
MINIMUM_INK_COLUMNS = 16


def page_origin(value):
    try:
        x, y = (int(part) for part in value.split(","))
        if x < 0 or y < 0:
            raise ValueError
        return x, y
    except ValueError:
        raise argparse.ArgumentTypeError("page origin must be two nonnegative integers: X,Y") from None


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("screenshot")
    parser.add_argument("--page-origin", type=page_origin, default=(80, 154), metavar="X,Y")
    args = parser.parse_args(argv)

    try:
        with Image.open(args.screenshot) as source:
            image = source.convert("RGBA")
    except (OSError, ValueError, Image.DecompressionBombError) as error:
        print("hosted-iframe: cannot read screenshot: " + str(error), file=sys.stderr)
        return 2
    origin_x, origin_y = args.page_origin
    if image.width < origin_x + PAGE_SIZE[0] or image.height < origin_y + PAGE_SIZE[1]:
        print("hosted-iframe: screenshot %dx%d does not contain the 960x648 page at %d,%d"
              % (image.width, image.height, origin_x, origin_y), file=sys.stderr)
        return 2

    left, top, right, bottom = INTERIOR
    region = image.crop((origin_x + left, origin_y + top, origin_x + right, origin_y + bottom))
    ink = []
    opaque = True
    for y in range(region.height):
        for x in range(region.width):
            red, green, blue, alpha = region.getpixel((x, y))
            opaque = opaque and alpha == 255
            if alpha == 255 and max(red, green, blue) <= DARK_CHANNEL_LIMIT:
                ink.append((x, y))
    width = height = columns = 0
    bounds = None
    if ink:
        xs, ys = zip(*ink)
        width, height = max(xs) - min(xs) + 1, max(ys) - min(ys) + 1
        bounds = (left + min(xs), top + min(ys), width, height)
        columns = len(set(xs))
    fraction = len(ink) / (region.width * region.height)
    density = len(ink) / (width * height) if width and height else 0
    failures = []
    if not opaque:
        failures.append("transparent field interior")
    if len(ink) < MINIMUM_DARK_PIXELS:
        failures.append("need at least 48 dark pixels")
    if columns < MINIMUM_INK_COLUMNS or not 20 <= width <= 96 or height < 6:
        failures.append("ink must span at least 16 columns, 20..96 pixels wide and 6 pixels high")
    if fraction > 0.30 or density > 0.75:
        failures.append("solid/dense fill is not visible glyphs")
    print("hosted-iframe: %s dark=%d columns=%d page-ink-bounds=%s fraction=%.4f density=%.4f "
          "page-interior=%s origin=%d,%d"
          % ("FAIL" if failures else "PASS", len(ink), columns, bounds, fraction, density,
             INTERIOR, origin_x, origin_y))
    for failure in failures:
        print("  " + failure)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
