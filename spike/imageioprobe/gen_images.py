#!/usr/bin/env python3
"""Generate test images for spike/imageioprobe.c. Run once: python3 gen_images.py"""
import os
from PIL import Image, ImageCms
import struct

OUT = os.path.join(os.path.dirname(__file__), "images")
os.makedirs(OUT, exist_ok=True)

def grad(w, h, mode):
    im = Image.new(mode, (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            r = int(255 * x / (w - 1))
            g = int(255 * y / (h - 1))
            b = 128
            a = int(255 * ((x + y) / (w + h - 2)))
            if mode == "RGBA":
                px[x, y] = (r, g, b, a)
            elif mode == "RGB":
                px[x, y] = (r, g, b)
            elif mode == "L":
                px[x, y] = (r + g) // 2
    return im

W, H = 32, 32

# 8-bit RGBA PNG
grad(W, H, "RGBA").save(os.path.join(OUT, "rgba8.png"))

# Interlaced (Adam7) PNG
grad(W, H, "RGB").save(os.path.join(OUT, "interlaced.png"), interlace=True)

# 16-bit PNG (grayscale 16, Pillow mode I;16)
im16 = Image.new("I", (W, H))
px = im16.load()
for y in range(H):
    for x in range(W):
        px[x, y] = int(65535 * x / (W - 1))
im16.convert("I;16").save(os.path.join(OUT, "gray16.png"))

# Paletted PNG with tRNS
pal = Image.new("P", (W, H))
palette = []
for i in range(256):
    palette += [i, (i * 3) % 256, (i * 7) % 256]
pal.putpalette(palette)
px = pal.load()
for y in range(H):
    for x in range(W):
        px[x, y] = (x * 8 + y) % 256
# set some transparency
transparency_table = bytes([255 if i != 0 else 0 for i in range(256)])
pal.save(os.path.join(OUT, "palette_trns.png"), transparency=transparency_table)

# Grayscale PNG (8-bit)
grad(W, H, "L").save(os.path.join(OUT, "gray8.png"))

# Baseline JPEG
grad(W, H, "RGB").save(os.path.join(OUT, "baseline.jpg"), quality=85, progressive=False)

# Progressive JPEG
grad(W, H, "RGB").save(os.path.join(OUT, "progressive.jpg"), quality=85, progressive=True)

# CMYK JPEG
rgb = grad(W, H, "RGB")
cmyk = rgb.convert("CMYK")
cmyk.save(os.path.join(OUT, "cmyk.jpg"))

# JPEG with EXIF orientation 6 (minimal hand-built TIFF/EXIF blob, one IFD entry)
def build_minimal_exif_orientation(value):
    # TIFF header (little-endian) + IFD with 1 entry: Orientation (tag 0x0112, SHORT, count 1)
    tiff = b"II*\x00" + struct.pack("<I", 8)
    ifd = struct.pack("<H", 1)  # 1 entry
    ifd += struct.pack("<HHI", 0x0112, 3, 1) + struct.pack("<H", value) + b"\x00\x00"
    ifd += struct.pack("<I", 0)  # next IFD offset
    return tiff + ifd

im = grad(W, H, "RGB")
exif_bytes = build_minimal_exif_orientation(6)
im.save(os.path.join(OUT, "exif_orient6.jpg"), exif=exif_bytes)

# Animated GIF: 3 frames, per-frame delays, one frame with disposal (background)
frames = []
for i, color in enumerate([(255, 0, 0), (0, 255, 0), (0, 0, 255)]):
    f = Image.new("RGB", (W, H), color)
    frames.append(f.convert("P", palette=Image.ADAPTIVE))
frames[0].save(
    os.path.join(OUT, "anim3.gif"),
    save_all=True,
    append_images=frames[1:],
    duration=[100, 250, 400],
    loop=0,
    disposal=[2, 1, 0],  # frame0: restore to background, frame1: leave in place
)

# GIF with transparency
gt = Image.new("P", (W, H))
palette2 = []
for i in range(256):
    palette2 += [i, i, i]
gt.putpalette(palette2)
px = gt.load()
for y in range(H):
    for x in range(W):
        px[x, y] = 0 if (x < W // 2) else 128
gt.save(os.path.join(OUT, "trns.gif"), transparency=0)

# BMP
grad(W, H, "RGB").save(os.path.join(OUT, "test.bmp"))

# ICO with two sizes (16 and 32)
icon_imgs = [grad(16, 16, "RGBA"), grad(32, 32, "RGBA")]
icon_imgs[-1].save(os.path.join(OUT, "test.ico"), sizes=[(16, 16), (32, 32)])

# TIFF
grad(W, H, "RGB").save(os.path.join(OUT, "test.tiff"))

# PNG with embedded ICC profile (Adobe RGB, use Pillow's builtin if available via ImageCms)
im_icc = grad(W, H, "RGB")
try:
    # Use sRGB profile bytes from a known small ICC via ImageCms.createProfile
    profile = ImageCms.createProfile("sRGB")
    icc_bytes = ImageCms.ImageCmsProfile(profile).tobytes()
    im_icc.save(os.path.join(OUT, "icc_srgb.png"), icc_profile=icc_bytes)
except Exception as e:
    im_icc.save(os.path.join(OUT, "icc_srgb.png"))
    print("WARNING: could not embed ICC profile:", e)

print("Generated images in", OUT)
for f in sorted(os.listdir(OUT)):
    print(" ", f)
