#!/usr/bin/env python3
"""Check glyph ink in all 12 samples in font-ink.html, paired with its layout gate.

This is a missing-text check, not OCR or a proof of typeface/style identity.
The native manifest probe separately verifies the selected font's table identity.
"""
import argparse
from pathlib import Path
import sys

from PIL import Image

NAMES = ['serif', 'sans-serif', 'monospace', 'cursive', 'fantasy', 'Monaco',
         'Courier', 'Courier New', 'Lucida Grande', 'Helvetica', 'static pre', 'dynamic pre']


def check(image, origin=(80, 154)):
    if image.width < origin[0] + 940 or image.height < origin[1] + 620:
        raise ValueError('screenshot does not contain the 940x620 fixture')
    image = image.convert('RGBA')
    results = []
    for index, name in enumerate(NAMES):
        # Only the sample, excluding its label, row boundary and adjacent rows.
        x, y = origin[0] + 240, origin[1] + 50 + index * 45 + 3
        crop = image.crop((x, y, x + 360, y + 36))
        ink = [(px, py) for py in range(crop.height) for px in range(crop.width)
               if max(crop.getpixel((px, py))[:3]) <= 110 and crop.getpixel((px, py))[3] == 255]
        columns = len({p[0] for p in ink})
        rows = len({p[1] for p in ink})
        density = 1
        if ink:
            width = max(p[0] for p in ink) - min(p[0] for p in ink) + 1
            height = max(p[1] for p in ink) - min(p[1] for p in ink) + 1
            density = len(ink) / (width * height)
        passed = len(ink) >= 80 and columns >= 35 and rows >= 6 and density < .75
        results.append((name, passed, len(ink), columns, rows))
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('screenshot', type=Path)
    args = parser.parse_args()
    try:
        with Image.open(args.screenshot) as image:
            results = check(image)
    except (OSError, ValueError, Image.DecompressionBombError) as error:
        print('font-ink: ' + str(error), file=sys.stderr)
        return 2
    for name, passed, pixels, columns, rows in results:
        print('font-ink: %s %s pixels=%d columns=%d rows=%d' %
              ('PASS' if passed else 'FAIL', name, pixels, columns, rows))
    return int(not all(item[1] for item in results))


if __name__ == '__main__':
    raise SystemExit(main())
