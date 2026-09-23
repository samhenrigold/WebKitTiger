#!/usr/bin/env python3
"""Compare a box screenshot against a golden, over the page area only.

  tools/regress-compare.py shot.png golden.png [--crop L,T,R,B] [--tol N] [--max-frac F]
  tools/regress-compare.py shot.png --nonblank [--crop ...]
  tools/regress-compare.py shot.png --dark-text L,T,R,B   (region has dark pixels)

The screenshot is the whole 1440x900 screen; only the TigerBrowser2 window's content
rect is compared, because the clock in the menu bar and the Dock change between runs.
The default crop is that rect. Colour management makes exact equality useless, so a
pixel counts as different when any channel differs by more than --tol, and the image
passes when the fraction of differing pixels is at most --max-frac. The fraction is
always printed: it is the number to look at when a check fails.

Exit status 0 = pass, 1 = fail, 2 = could not compare.
"""
import sys
from PIL import Image, ImageChops, ImageStat

DEFAULT_CROP = (80, 152, 1040, 812)   # TigerBrowser2 window content rect at 1440x900


def parse_crop(spec):
    left, top, right, bottom = (int(v) for v in spec.split(','))
    return left, top, right, bottom


def load(path, crop):
    image = Image.open(path).convert('RGB')
    # A screenshot smaller than the crop means the capture failed; say so rather than
    # silently comparing a corner of it.
    if image.width < crop[2] or image.height < crop[3]:
        raise SystemExit('compare: %s is %dx%d, smaller than the crop %r' % (
            path, image.width, image.height, crop))
    return image.crop(crop)


def differing_fraction(a, b, tol):
    """A pixel differs when ANY channel is off by more than tol. Done with PIL's C
    primitives rather than a Python loop: 600k pixels a compare, nine compares a run."""
    if a.size != b.size:
        raise SystemExit('compare: sizes differ %r vs %r' % (a.size, b.size))
    delta = ImageChops.difference(a, b)
    mask = None
    for channel in delta.split():
        over = channel.point(lambda v: 255 if v > tol else 0)
        mask = over if mask is None else ImageChops.lighter(mask, over)
    histogram = mask.histogram()
    return histogram[255] / float(a.size[0] * a.size[1])


def nonblank_stddev(image):
    """A blank page is one flat colour over the whole content rect, so its grey-level
    standard deviation is near zero. Any real page is far above it."""
    return ImageStat.Stat(image.convert('L')).stddev[0]


def dark_fraction(image):
    grey = image.convert('L').point(lambda v: 255 if v < 110 else 0)
    return grey.histogram()[255] / float(image.size[0] * image.size[1])


def main(argv):
    if len(argv) < 2:
        raise SystemExit(__doc__)
    shot = argv[1]
    golden, crop, tol, max_frac = None, DEFAULT_CROP, 24, 0.02
    mode, region = 'golden', None
    i = 2
    while i < len(argv):
        arg = argv[i]
        if arg == '--crop':
            i += 1; crop = parse_crop(argv[i])
        elif arg == '--tol':
            i += 1; tol = int(argv[i])
        elif arg == '--max-frac':
            i += 1; max_frac = float(argv[i])
        elif arg == '--nonblank':
            mode = 'nonblank'
        elif arg == '--dark-text':
            i += 1; mode, region = 'dark', parse_crop(argv[i])
        elif not arg.startswith('--'):
            golden = arg
        else:
            raise SystemExit('compare: unknown option %s' % arg)
        i += 1

    if mode == 'nonblank':
        stddev = nonblank_stddev(load(shot, crop))
        print('nonblank: grey stddev %.2f over the page area (need > 3)' % stddev)
        return 0 if stddev > 3 else 1

    if mode == 'dark':
        fraction = dark_fraction(load(shot, region))
        print('dark-text: %.4f of %r is dark (need > 0.005)' % (fraction, region))
        return 0 if fraction > 0.005 else 1

    if not golden:
        raise SystemExit('compare: no golden given')
    fraction = differing_fraction(load(shot, crop), load(golden, crop), tol)
    print('diff: %.4f of the page area differs by more than %d (allowed %.4f)' % (
        fraction, tol, max_frac))
    return 0 if fraction <= max_frac else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
