#!/usr/bin/env python3
"""fast64's metric (luma / inkluma / ink, per-line ink) for pagedriver PNGs against a ctref32
reference, plus the contact sheet. Each rendered line is aligned to the reference by an
integer dy (WebKit rounds baselines to whole pixels, ctref32int draws at 17 + 30 i), reported.

usage: score.py <ref-smooth.bin> <line0> <leading> <name=png> ... [--sheet out.png] [--zoom out.png]
"""
import struct, sys
import numpy as np
from PIL import Image

W, H = 720, 264
LUMA = np.array([0.2126, 0.7152, 0.0722])

def load_ref(path):
    b = open(path, 'rb').read()
    assert b[:4] == b'FTX1'
    w, h = struct.unpack('<ii', b[4:12])
    return np.frombuffer(b[12:12 + w * h * 4], np.uint8).reshape(h, w, 4)[:, :, :3].astype(float)

def luma(rgb): return rgb @ LUMA

def bands(line0, leading):
    ys = np.arange(H)
    return np.floor((ys - (line0 - leading * 0.7)) / leading).astype(int)

def align(ref, img, line0, leading, nlines, search=6):
    """Per line, pick the integer dy that minimises |diff| within the band; return aligned img."""
    out = np.full_like(img, 255.0)
    band = bands(line0, leading)
    dys = []
    for i in range(nlines):
        rows = np.where(band == i)[0]
        best = None
        for dy in range(-search, search + 1):
            src = rows + dy
            ok = (src >= 0) & (src < H)
            cand = np.full((len(rows), W), 255.0)
            cand[ok] = img[src[ok]]
            e = np.abs(cand - ref[rows]).sum()
            if best is None or e < best[0]: best = (e, dy, cand)
        out[rows] = best[2]
        dys.append(best[1])
    return out, dys

def score(refL, imgL, line0, leading, nlines):
    d = np.abs(imgL - refL)
    ink = (imgL < 253) | (refL < 253)
    band = bands(line0, leading)
    per = []
    for i in range(nlines):
        rows = band == i
        r = (255 - refL[rows]).sum(); v = (255 - imgL[rows]).sum()
        per.append(v / r if r > 0 else 0)
    return d.mean(), d[ink].mean(), (255 - imgL).sum() / (255 - refL).sum(), per

def main():
    args = sys.argv[1:]
    sheet = zoom = None
    if '--sheet' in args: i = args.index('--sheet'); sheet = args[i + 1]; del args[i:i + 2]
    if '--zoom' in args: i = args.index('--zoom'); zoom = args[i + 1]; del args[i:i + 2]
    ref = load_ref(args[0]); line0 = float(args[1]); leading = float(args[2]); nlines = 8
    refL = luma(ref)
    print('%-28s %8s %8s %8s  per-line ink                              dy per line' % ('variant', 'luma', 'inkluma', 'ink'))
    rows = [('quartz (ctref32int)', ref)]
    for spec in args[3:]:
        name, path = spec.split('=', 1)
        img = np.asarray(Image.open(path).convert('RGB'), float)[:H, :W]
        imgL, dys = align(refL, luma(img), line0, leading, nlines)
        l, il, ink, per = score(refL, imgL, line0, leading, nlines)
        print('%-28s %8.3f %8.2f %8.3f  %s  %s' % (name, l, il, ink, ' '.join('%.2f' % p for p in per), ' '.join('%+d' % d for d in dys)))
        rows.append((name, np.repeat(imgL[:, :, None], 3, 2)))
    if sheet:
        from PIL import ImageDraw
        im = Image.new('RGB', (W, (H + 14) * len(rows)), 'white'); dr = ImageDraw.Draw(im)
        for k, (name, px) in enumerate(rows):
            y0 = k * (H + 14)
            dr.rectangle([0, y0, W, y0 + 13], fill=(32, 64, 128)); dr.text((4, y0 + 1), name, fill='white')
            im.paste(Image.fromarray(np.clip(px, 0, 255).astype(np.uint8)), (0, y0 + 14))
        im.save(sheet); print('sheet', sheet)
    if zoom:
        # 6x crops of the same three windows in every band: "jigs" (Helvetica Bold), "quartz" (Times), Lucida 13.
        crops = [(12, 8, 152, 24), (300, 128, 440, 144), (140, 158, 280, 174)]
        Z = 6; cw = sum(c[2] - c[0] for c in crops) * Z + 8 * (len(crops) - 1); ch = 16 * Z
        im = Image.new('RGB', (cw, (ch + 14) * len(rows)), 'white')
        from PIL import ImageDraw; dr = ImageDraw.Draw(im)
        for k, (name, px) in enumerate(rows):
            y0 = k * (ch + 14); x0 = 0
            dr.rectangle([0, y0, cw, y0 + 13], fill=(32, 64, 128)); dr.text((4, y0 + 1), name, fill='white')
            src = Image.fromarray(np.clip(px, 0, 255).astype(np.uint8))
            for c in crops:
                im.paste(src.crop(c).resize(((c[2] - c[0]) * Z, (c[3] - c[1]) * Z), Image.NEAREST), (x0, y0 + 14))
                x0 += (c[2] - c[0]) * Z + 8
        im.save(zoom); print('zoom', zoom)

main()
