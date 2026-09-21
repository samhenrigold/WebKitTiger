# What Quartz's glyph rasteriser actually does (10.4.11, i386, offscreen bitmap)

Method: black-box probes on the box (`spike/quartzraster/`), scored against Quartz's own
pixels. Everything below is stated as **proven** (a probe measures it directly) or
**inferred** (best explanation of the measurements, not independently confirmed).

The short version: **there is no exotic sampling, no gamma and no LUT.** CoreGraphics
computes exact analytic area coverage — the same integral FreeType's smooth rasteriser
computes — and the entire "Quartz look" that fast mode was missing is (a) *where* the glyph
is placed on a size-dependent subpixel grid and (b) the font's TrueType hinting, applied at
integer ppem only. With (a) modelled and (b) left out, an unhinted FreeType outline through
FreeType's own rasteriser reproduces Quartz's glyph bitmaps to a **median 0.5–0.8 of 255 per
inked pixel (0.2–0.3%)**, against the 7% that `spike/fasttext` measured through cairo.

## 1. The scan converter computes exact area. PROVEN.

`spike/quartzraster/pathprobe32.c` fills synthetic `CGPath`s with fractional edges into a
`CGBitmapContext` and reads the coverage back.

| probe | result |
|---|---|
| rect `x=[20, 20+w)`, w swept in 1/64 px | coverage byte = `255 - (255*floor(256w) >> 8)` — **linear in w over all 64 steps** |
| rect slid across a pixel boundary, w=1 | the two pixels' coverages sum to 1.0000 at every offset (255/256 rounding aside) |
| same two probes on the y axis | identical numbers — no axis is special |
| square of side t at a pixel corner | coverage = t², not t (so it is 2-D area, not a separable filter) |
| triangle with slope 1/2 | every edge row is exactly `0.2510 / 0.7529` = 64/255, 192/255 = areas 1/4 and 3/4 |

An N×M supersampler cannot produce 64 distinct levels from a 1/64-px sweep; a coverage LUT
or gamma would bend the line. Neither happens. Coverage is accumulated at 1/256 and the
composite is `dst = 255 - ((255*a) >> 8)`, which is why full black reads 255 but 1/64 of a
pixel reads 3 rather than 4.

This also settles the open question from `spike/fasttext`: the ~7% residual there was never
an "antialiasing kernel disagreement". Both rasterisers compute the same integral.

## 2. Horizontal subpixel positioning: N phases per pixel, N chosen by device pixel size. PROVEN.

`spike/quartzraster/ctgrid32.c` draws one glyph at 1/48-px offsets and hashes the bitmap;
offsets that land in one quantisation cell are byte-identical, so the runs are the grid.

| device pixel size | x phases | cell starts (1/48 px) |
|---|---|---|
| ≤ 8.33 | 5 | 0 10 20 29 39 |
| 8.375 … 11.11 | 4 | 0 12 24 36 |
| 11.125 … 16.67 | 3 | 0 16 33 |
| 16.75 … 33.33 | 2 | 0 24 |
| > 33.33 | 1 | 0 |

Thresholds measured to 1/8 pt and identical for Helvetica, Times and Lucida Grande, so this
is a CoreGraphics rule, not font data. They fit

```
Nx = min(5, 1 + floor((100/3) / devicePixelSize))
```

with the four measured thresholds pinning the constant to (33.25, 33.375] — 100/3 is the
obvious member of that interval (**inferred**; any constant in that range fits).

The snap is `floor(x * N) / N` applied to the pen's device x (**proven**: cell boundaries sit
at k/N, and the float noise in the boundary position moves with the pen's integer part, not
with the glyph, so it is the pen that is quantised).

**It is the device pixel size that selects N, not the point size**: with `CGContextScaleCTM(2,2)`
an 8 pt font gets the 16 px row of the table, and at 0.5 a 24 pt font gets the 12 px row.
So page zoom changes the grid.

Vertical: `Ny = min(5, 1 + floor((25/3) / devicePixelSize))`, which is **1 for everything
above 8.33 px** — real text has no vertical subpixel positioning at all. The snap is
`floor(y)` in CG's bottom-up device space (equivalently `ceil` in a top-down raster), which
confirms `spike/fasttext`'s finding and extends it: below 8.33 px there really are 2, 3, 4
vertical phases, which nothing in the earlier probe could have seen.

## 3. Glyph rasterisation is that same exact-area fill of the unhinted outline. PROVEN at non-integer ppem.

`spike/quartzraster/ctglyph32.c` dumps Quartz's own glyph bitmaps (48×48 coverage patches,
one glyph, swept over 48 subpixel offsets on each axis); `model64.c` renders the same glyph
from the manifest font file with FreeType (`FT_LOAD_NO_HINTING`, `FT_Outline_Get_Bitmap`)
placed on the grid of §2, and diffs them.

Mean |difference| per inked pixel, 'H' at 85 sizes from 9 to 30 pt:

| face | fractional ppem (median / max) | integer ppem (median / max) |
|---|---|---|
| Helvetica | **0.68** / 2.17 | 0.72 / 21.2 |
| Helvetica-Bold | **0.52** / 0.93 | 0.66 / 19.0 |
| Lucida Grande | **0.65** / 1.29 | 0.76 / 28.5 |
| Times Roman | **2.62** / 3.65 | 2.66 / 30.6 |

0.5–0.8 of 255 is the quantisation floor of two independent implementations of the same
integral (FreeType works in 26.6 fixed point; CG in float). Times carries a constant extra
~2.6 at every size, integer or not — unexplained, and worth one more look; it is not
hinting, because it is there at fractional ppem too.

## 4. At integer ppem — and only integer ppem — Quartz grid-fits vertically. PROVEN; the mechanism is INFERRED.

The max column above is the whole story: at integer point sizes the error jumps to 20–30 for
TrueType faces, and it is entirely a y effect. Measuring the sub-pixel position of the cap
top of 'H' (`model64 … edge`), Helvetica:

```
size   Quartz   unhinted   FT v35/v40 bytecode
 12.0   27.000   27.388    27.000     <- Quartz == hinted
 13.0   26.682   26.678    26.000     <- Quartz == unhinted
 16.0   24.000   24.514    24.000     <- Quartz == hinted
 17.0   24.000   23.812    24.000     <- Quartz == hinted
 19.0   22.373   22.373    22.000     <- Quartz == unhinted
 24.0   18.784   18.780    18.000     <- Quartz == unhinted
```

* Horizontal geometry is untouched at those sizes — the stem columns of 'H' and 'n' match
  the unhinted model byte for byte (73/82, 96/43), only the top row changes. So the grid
  fitting is **y only**.
* It never happens at a fractional ppem. 63 fractional sizes per font, zero cases.
* It never happens for **Hiragino Kaku Gothic Pro (CFF)** — integer ppem matches the
  unhinted model to 0.55–1.3, i.e. as well as anything measured here.

TrueType faces hint at integer ppem, a CFF face does not, and the hinted positions coincide
exactly with what FreeType's bytecode interpreter produces at the sizes where Quartz moves
the edge. The inference is that **CoreGraphics runs the font's own TrueType bytecode, with
horizontal movement suppressed, and only when the ppem is integral** (hinting is defined per
integer ppem, so a fractional ppem has no hinted form to use). Why the bytecode leaves the
cap alone at 13, 19, 20, 24 while FreeType's interpreter rounds it is an interpreter
difference; Apple's scaler is not FreeType (see §6) and its CVT handling differs.

Fitting a single vertical scale to the model explains part of it (16 pt: 16.21 → 1.41 at
yscale 1.0415, which is exactly "cap height 11.486 px → 12"), but not all of it (12 pt:
21.2 → 5.6), so it is per-feature grid fitting, not one uniform y scale. Reproducing it
exactly needs Apple's interpreter; FreeType's v35 and v40 get some sizes right and others
wrong, and **on average both are worse than not hinting at all** (measured: v40 19.5, v35
20.8, autohint-light 16.8, unhinted 8.6 over the 864-patch set).

## 5. No tone curve, no smoothing table, offscreen. PROVEN (here and in spike/fasttext).

`CGContextSetShouldSmoothFonts(true)` and `false` give byte-identical bitmaps in a
`CGBitmapContext` and neither produces a coloured pixel; `spike/fasttext` additionally showed
the coverage-vs-coverage conditional mean tracks the identity line and that an oracle LUT
makes things worse. §1's linear sweep is the direct version of the same statement.

## 6. Disassembly, such as it is.

Hopper's MCP bridge was down for this session, so this is `nm`/`otool`/`strings` on
`refs/tiger-cg/CoreGraphics` (copied off the box; `refs/*` is already gitignored).

* Tiger's CoreGraphics contains **no FreeType** — no `FT_` symbols, no version strings, and
  `Resources/` has no `libCGFreetype`. (Leopard's does; Tiger's scaler is Apple's own.)
* It does not link ATS either (`otool -L`: libz, libbsm, IOKit, CoreServices, CF, libSystem).
  The font scaler and the scan converter are static inside the 9.9 MB binary with local
  symbols stripped, so there is no `aa_*`/`rip_*` to read; `libRIP.A.dylib` is the
  window-server side, not this path.
* The scan converter's public face is the `CGCoverage*` family —
  `CGCoverageCreateFillCoverageWithPath`, `…WithRect`, `CGCoverageConvertToMask`,
  `CGContextDrawCoverage` — i.e. CG builds an 8-bit *coverage* object and composites it.
  That is exactly the shape §1 measures.
* The glyph path is `CGFontCreateGlyphBitmap` / `CGFontCreateGlyphBitmaps` /
  `CGGlyphBitmapCreate` (all exported), with `CGFontGetGlyphPath` alongside — a per-glyph
  bitmap cache, consistent with the finite phase table of §2.

## 7. The model, and what it scores

`spike/quartzraster/model64.c` (x86_64, FreeType only, no cairo):

1. outline from the manifest font file, `FT_LOAD_NO_HINTING`, CFF stem darkening off;
2. pen device x snapped `floor(x·N)/N` with N from §2's table; pen device y snapped
   `floor(y)` in CG's bottom-up space;
3. `FT_Outline_Translate` to that exact position (no re-quantisation) and
   `FT_Outline_Get_Bitmap`, whose exact-area coverage is the same integral as §1.

On the `spike/fasttext` sample (the fractional-baseline one: 8 runs, 400 glyphs, CoreText's
own layout, scored against `ref-smooth.bin` with fast64's metric):

```
                                             luma   inkluma    ink
fast mode today (cairo, no grid rules)       3.929    41.22    0.987
best cairo variant (yceil + x3floor)         1.617    17.95    0.987
model64 (exact area + measured grid)         1.194    13.41    0.987   <- this spike
model64 with the grid rules off              4.264    44.10    0.987
```

**-26% against the best cairo configuration and -67% against what fast mode does today.**
Note the sample is all integer sizes (11, 13, 16), so every line pays §4's hinting gap;
at the fractional sizes real content also produces, the model is at 0.2–0.3% (§3).

What prevents zero, in order of size:

1. **§4, the TrueType hinting at integer ppem.** Needs Apple's interpreter to reproduce;
   FreeType's is worse than nothing. This is the whole of the remaining 13.41.
2. **Times' constant ~2.6/255**, cause unknown.
3. **~0.5–0.8/255 quantisation floor** — FreeType's 26.6 outline coordinates and 1/256
   coverage against CG's float. Irreducible without reimplementing the rasteriser in float.
