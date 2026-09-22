#!/usr/bin/env python3
"""Zoomed side-by-side crops for looking at, not scoring: the CoreText reference on top,
then each named PNG under it, per-line aligned by the same integer dy score.py picks.

usage: crops.py [--lines line0 leading] <ref-smooth.bin> <outdir> <name=png> ...
Writes <outdir>/crop-<line>.png (8x, ref over each variant) and a per-line score table.
"""
import struct, sys
import numpy as np
from PIL import Image, ImageDraw

W, H, Z = 720, 264, 8
LUMA = np.array([0.2126, 0.7152, 0.0722])
LINES = ['LG13', 'LGB13', 'LG11', 'Helv16', 'HelvB16', 'Times16', 'TimesIt16', 'Hira16']
# (x0, x1) windows worth looking at on each line; full band height around the baseline.
WINDOWS = {3: (12, 262), 4: (12, 262), 5: (12, 262), 6: (12, 262), 0: (12, 262), 7: (12, 262)}

def load_ref(path):
    b = open(path, 'rb').read()
    w, h = struct.unpack('<ii', b[4:12])
    return np.frombuffer(b[12:12 + w * h * 4], np.uint8).reshape(h, w, 4)[:, :, :3]

def best_dy(refL, imgL, rows):
    best = None
    for dy in range(-8, 9):
        a = np.roll(imgL, dy, 0)[rows]; r = refL[rows]
        ink = (a < 253) | (r < 253)
        e = np.abs(a - r)[ink].mean() if ink.any() else 0
        if best is None or e < best[0]: best = (e, dy)
    return best

def main():
    line0, leading = 17.0, 30.0
    if sys.argv[1] == '--lines':
        line0, leading = float(sys.argv[2]), float(sys.argv[3]); del sys.argv[1:4]
    ref = load_ref(sys.argv[1]); outdir = sys.argv[2]
    refL = ref.astype(float) @ LUMA
    variants = []
    for spec in sys.argv[3:]:
        name, path = spec.split('=', 1)
        im = Image.open(path).convert('RGB')
        variants.append((name, im, np.asarray(im, float)[:H, :W] @ LUMA))
    print('%-10s ' % 'line' + ' '.join('%12s' % v[0] for v in variants))
    for i, lname in enumerate(LINES):
        base = int(round(line0 + leading * i)); rows = slice(base - 16, base + 6)
        scores = [best_dy(refL, v[2], rows) for v in variants]
        print('%-10s ' % lname + ' '.join('%8.1f(%+d)' % s for s in scores))
        if i not in WINDOWS: continue
        x0, x1 = WINDOWS[i]; y0, y1 = base - 16, base + 6
        cw, ch = (x1 - x0) * Z, (y1 - y0) * Z
        out = Image.new('RGB', (cw, (ch + 14) * (1 + len(variants))), 'white'); dr = ImageDraw.Draw(out)
        panels = [('quartz (CoreText, 10.4)', Image.fromarray(ref).crop((x0, y0, x1, y1)))]
        for (name, im, _), (e, dy) in zip(variants, scores):
            panels.append(('%s  inkluma %.1f' % (name, e), im.crop((x0, y0 - dy, x1, y1 - dy))))
        for k, (label, crop) in enumerate(panels):
            y = k * (ch + 14)
            dr.rectangle([0, y, cw, y + 13], fill=(32, 64, 128)); dr.text((4, y + 1), label, fill='white')
            out.paste(crop.resize((cw, ch), Image.NEAREST), (0, y + 14))
        out.save('%s/crop-%s.png' % (outdir, lname))

main()
