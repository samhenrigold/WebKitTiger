# How close can fast-mode text get to Tiger's own?

Fast mode rasterises text in the 64-bit web process with cairo + FreeType out of the
manifest font files, and gives up the Quartz text look to do it. This spike measures how
much look there actually is to give up.

```
make                      # ctref32 (i386) + fast64 (x86_64)
scp ctref32 fast64 ../../logs/tiger-fonts.json tiger:~/fasttext/
ssh tiger 'cd ~/fasttext && ./ctref32 . smooth && ./fast64 tiger-fonts.json . ref-smooth.bin'
```

`ctref32` lays out `sample.h` with CoreText, draws it into a 720×260 bitmap and writes the
pixels plus the glyph ids and pen positions it used. `fast64` replays *that exact layout*
through cairo + FreeType under every option variant and scores each against the CoreText
bitmap. Layout is taken from the reference on purpose: `spike/textpixel` already proved the
64-bit shaper agrees with CoreText to 0.0039 pt, so this spike is only about pixels.

Sample: Lucida Grande 13/13 bold/11, Helvetica 16 and 16 bold, Times 16 and 16 italic, and
a Hiragino Kaku Gothic Pro line — eight runs, 400 glyphs.

## Two findings that decided the answer before the matrix ran

**Quartz does not do subpixel antialiasing into an offscreen bitmap on 10.4.** With
`AppleFontSmoothing` unset (Tiger's default, which is LCD "medium" on this MacBook Pro's
panel), `ctref32` run with `CGContextSetShouldSmoothFonts(ctx, true)` and with it `false`
produce **byte-identical** bitmaps, and neither contains a single pixel where R, G and B
differ. CoreGraphics only does subpixel into a window backing store, where it knows the
panel geometry. Every pixel fast mode has to match is therefore grayscale-antialiased, and
the "Quartz look" fast mode gives up is narrower than it sounds.

**Our FreeType has no LCD filter.** `deps/build-c-deps.sh` builds FreeType 2.13.3 with
stock `ftoption.h`, where `FT_CONFIG_OPTION_SUBPIXEL_RENDERING` is commented out. That is
Harmony mode: LCD rendering works, but `FT_Library_SetLcdFilter` returns error 7
(`Unimplemented_Feature`), which `fast64` prints on every run. cairo's lcd_filter option
and fontconfig's `FC_LCD_FILTER` are dead properties on this build — the `fir5` and `light`
rows below are byte-identical to the unfiltered row, and that is why.

## The matrix

`luma` is mean |luma difference| against the CoreText bitmap over all 187,200 pixels;
`inkluma` is the same over only the 22k pixels where either image has ink, which is the
honest number; `ink` is total ink of the variant over total ink of the reference, so 1.00
means the strokes carry exactly Quartz's weight. `clr` marks variants that emitted
subpixel-coloured pixels — every one of which is a divergence, since the reference has none.

```
variant                              luma  inkluma      ink clr  per-line ink
gray-none-g1.00                     2.665    28.56    0.987   -  1.00 1.03 1.01 0.96 0.98 0.95 1.04 1.00
gray-none-g0.85                     2.795    29.78    1.048   -  1.07 1.08 1.09 1.02 1.01 1.02 1.13 1.08
gray-none-g0.70                     3.106    32.88    1.122   -  1.15 1.14 1.18 1.09 1.06 1.11 1.23 1.17
gray-none-g0.55                     3.692    39.09    1.213   -  1.24 1.21 1.30 1.18 1.12 1.22 1.37 1.28
gray-slight-g1.00                   2.981    32.15    1.002   -  0.98 0.99 1.01 1.00 1.00 1.00 1.02 1.03
gray-slight-g0.85                   3.090    33.19    1.056   -  1.04 1.04 1.08 1.06 1.03 1.07 1.10 1.09
gray-slight-g0.70                   3.342    35.77    1.122   -  1.11 1.10 1.16 1.13 1.07 1.15 1.20 1.17
gray-slight-g0.55                   3.832    41.02    1.202   -  1.20 1.16 1.27 1.21 1.12 1.25 1.32 1.26
gray-full-g1.00                     3.717    39.74    1.005   -  1.02 0.97 1.05 0.93 1.00 1.03 1.16 1.03
gray-full-g0.85                     3.813    40.59    1.056   -  1.08 1.01 1.12 0.98 1.03 1.09 1.24 1.09
gray-full-g0.70                     4.020    42.66    1.118   -  1.15 1.07 1.20 1.04 1.07 1.16 1.33 1.17
gray-full-g0.55                     4.423    46.95    1.192   -  1.23 1.13 1.29 1.11 1.12 1.24 1.44 1.26
rgb-none-g1.00                      3.069    30.53    0.987 rgb  1.00 1.03 1.01 0.96 0.98 0.94 1.04 1.00
rgb-slight-g1.00                    3.364    33.78    1.001 rgb  0.98 0.99 1.01 1.00 1.00 1.00 1.02 1.03
rgb-full-g1.00                      4.067    40.60    1.005 rgb  1.02 0.97 1.05 0.93 1.00 1.03 1.16 1.03
rgb-slight-fir5-g0.85               3.453    34.27    1.056 rgb  1.04 1.04 1.08 1.06 1.03 1.07 1.10 1.09
rgb-slight-light-g0.85              3.453    34.27    1.056 rgb  1.04 1.04 1.08 1.06 1.03 1.07 1.10 1.09
gray-slight-autohint-darken         3.587    37.61    1.089   -  1.07 1.03 1.10 1.09 1.01 1.14 1.17 1.25
gray-none-darken                    2.797    29.72    1.008   -  1.00 1.03 1.01 0.96 0.98 0.95 1.04 1.24
gray-none-intpos                    3.814    39.86    0.987   -  1.00 1.03 1.01 0.96 0.98 0.95 1.04 1.00
gray-none-embolden0.3               4.876    48.93    1.264   -  1.32 1.26 1.38 1.23 1.16 1.26 1.43 1.33
gray-slight-embolden0.3-g0.85       5.328    54.04    1.307   -  1.32 1.26 1.42 1.31 1.19 1.36 1.47 1.34
```

`out/contact.png` is the reference on top and the best three under it; `out/results.txt` is
the run verbatim. To the eye the four bands are the same text.

## The glyph position grids, measured

**Correcting an earlier version of this file.** It said Quartz floors x to a 1/4-pixel grid and
cairo rounds to nearest. The cairo half was right; the Quartz half was wrong, and it was wrong
because the probe was wrong. Sweeping a global shift only ever measures an *average* bias, and
the first grid probe drew a whole 56-glyph line at each offset — with glyphs at many different
fractional x, no quantiser ever reproduces a bitmap exactly, so every offset hashes differently
and the axis reads as "continuous" whatever the grid actually is.

Probing with **one glyph**, at 1/16 px offsets, collapses correctly: offsets inside one
quantisation cell render to identical pixels, so runs of equal hashes are the grid. (The y axis
was safe either way, because every glyph on a line shares its y.)

| axis | Quartz / CoreText, offscreen bitmap, 10.4 | cairo image surface |
|------|-------------------------------------------|---------------------|
| x    | **1/3 px**, 3 cells per pixel, floor       | 1/4 px, round to nearest |
| y    | **whole pixels**, floor in CG's bottom-up device space | 1/4 px, round to nearest |

The x cell boundaries land at 0.375 and 0.6875 rather than 1/3 and 2/3 because the probe glyph's
own fractional left bearing shifts the phase; the spacing, 0.3125 against a 1/16 sample step, is
1/3. For y only the 0 offset renders differently from 1/16..15/16, so the cell boundary sits
immediately above the integer: Quartz floors y in CG coordinates, and since cairo's y runs the
other way, the matching rule in cairo's top-down space is **ceil**.

## Fractional baselines, which is where this actually bites

Every baseline in the first version of this sample was an integer, so nothing exercised the y
grid at all. With `FT_LINE0 = 17.25` and `FT_LEADING = 30.375` the eight baselines walk the
fractional parts .25 .625 .0 .375 .75 .125 .5 .875, and the gap opens up:

```
variant             luma  inkluma      ink   soft  solid
y-asis             3.929    41.22    0.987   1.04   0.93   <- what fast mode does today
y-round            4.676    48.02    0.987   1.00   1.00
y-floor            7.745    74.08    0.987   1.00   1.00
y-ceil             2.625    28.56    0.987   1.00   1.00   <- matches Quartz's rule
yceil+q4floor      1.837    20.37    0.987   1.01   1.00
yceil+x3round      2.759    29.87    0.987   1.01   0.99
yceil+x3floor      1.617    17.95    0.987   1.01   1.00   <- both grids matched
yceil+slight       2.936    32.15    1.002   0.90   1.10
```

`y-ceil` alone takes **33%** off. Two things confirm it is the right rule rather than a lucky
constant: `y-floor` is the worst variant in the entire project (+97%), and `y-round` is *worse
than doing nothing* — only ceil matches. And `y-ceil`'s inkluma, 28.56, is exactly what the
integer-baseline sample scored, so ceil removes the fractional-baseline penalty completely
rather than merely reducing it.

Look at the `soft`/`solid` columns for `y-asis`: 1.04 soft and 0.93 solid against the reference.
That is the blur, stated numerically — cairo spreads each horizontal stroke across two rows at
quarter-pixel precision where Quartz snaps it onto one. It is the same "pixel fitting" that was
visible by eye on "jigs" and "quartz", and it is much worse at fractional baselines than the
integer-baseline sample ever showed.

Matching the x grid too — floor to 1/3 px — takes it to **1.617 / 17.95, a 59% reduction**
against what fast mode does today, and beats the best integer-baseline result this spike ever
produced. `x3floor` beating `q4floor` (1.837) and `x3round` (2.759) is what establishes the 1/3
grid: this is not the mean bias doing the work, because the earlier shift sweep put the best
uniform shift at -0.125 and showed -0.1875 as *worse*, while floor-to-1/3 carries a mean bias of
-1/6 and is better than both. A grid effect, not a bias.

## Pixel fitting, and why the mean-error metric nearly missed it

Looking at the contact sheet, the CoreText band reads as crisper than ours on some lines —
most visibly the stems of "jigs" in Helvetica Bold 16 and "quartz" in Times 16. Mean absolute
error does not capture that: a crisply grid-fitted stem in the *wrong* column scores worse
than a blurry stem in the right one, which is why `gray-full` ranks badly while looking sharp.

So: **bimodality**, the mean of |2·coverage - 1| over inked pixels. 1.0 means every inked
pixel is fully on or fully off (a stem snapped to the grid); 0 means everything is mid-grey.
Plus `hstem`, the mean horizontal coverage gradient, which is stem edge contrast.

```
line              ref bimod   none    slight   full      ref hstem   none    slight
LucidaGrande 13     0.567     0.563   0.572    0.572       0.4295   0.4324   0.4577
LG-Bold 13          0.633     0.660   0.627    0.652       0.3876   0.3735   0.3947
LG 11               0.555     0.556   0.549    0.580       0.4093   0.4179   0.4463
Helvetica 16        0.606     0.607   0.602    0.617       0.4191   0.4008   0.4265
Helv-Bold 16        0.703     0.674   0.721    0.734       0.3535   0.3481   0.3724
Times 16            0.559     0.558   0.561    0.619       0.4187   0.4072   0.4459
Times-It 16         0.539     0.545   0.522    0.577       0.4170   0.4187   0.4372
Hiragino 16         0.532     0.532   0.620    0.620       0.2850   0.2852   0.3184
```

Overall we match the reference's crispness closely (0.5978 against 0.5996). The deficit is
local and it is on the bold 16 px line: Helvetica Bold, where the reference is 0.703 and
unhinted cairo is 0.674. `slight` overshoots it to 0.721, `full` to 0.734. Times 16 is a tie
on bimodality but 3.6% short on stem edge contrast, which `slight` overshoots by 5.5%.

That is the whole of the crispness gap: one bold face, about 4%, in the direction of slightly
soft. Hinting closes it and overshoots, and costs 19% on positional fidelity to do it.

## Reading it

**Among the font options, the plain unmodified setting wins.** Gray antialiasing, hinting
off, no gamma, no darkening, no emboldening: 0.987 of Quartz's ink, and every per-line ratio
between 0.95 and 1.04. Nothing in the *option* matrix improves on doing nothing — but see the
1/4-pixel grid offset above, which is worth more than every option in this table combined.

**Every knob makes it worse, and the good ones make it worse the least.** Gamma at 0.85
costs 4% more error and overshoots the weight by 5%; at 0.55 the text is visibly fat.
Hinting costs monotonically: slight +13% error, full +39%. FreeType's stem darkening is a
no-op on the seven TrueType `.dfont` faces and only reaches the CFF Hiragino line, where it
adds 24% ink the reference does not have (see the last per-line column of
`gray-none-darken`). Emboldening is 26% too heavy and is the worst variant in the matrix.
The direction to tune *in* would have been lighter, not heavier, and there is no knob for
that.

**Do not round glyph x to whole pixels.** `gray-none-intpos` costs **+43% error**. Subpixel
positioning in x is real on both sides; whole-pixel rounding throws it away. Floor x to 1/3 px.
In y it is the opposite: Quartz has no subpixel positioning at all, so ceil to a whole pixel.

**Subpixel antialiasing is strictly a regression here** (+15% error at every hint style),
because the reference has no colour in it at all. It is not a look to be tuned toward; it
is a look Quartz is not producing.

## What the residual is

With both grids matched, 17.95/255 mean error over inked pixels — about 7% — with stroke weight
matched to 1.3%.
That is not weight, gross position or hinting; it is the rasterisers' antialiasing kernels
disagreeing about how to share coverage between adjacent pixels on a curve. Quartz's
outline scan-converter and FreeType's are different code computing the same integral, and
the difference lands entirely on edge pixels. No cairo or FreeType option addresses it, and
the only thing that would is rasterising with Quartz, which is what the other mode is for.

## Is there a Quartz smoothing parameter left to find? No.

Worth settling before anyone disassembles CoreGraphics looking for filter weights, a gamma or
a contrast setting: bin every inked pixel by *our* coverage and look at what CoreText put in
the same pixel.

```
ours     n    ref mean   ref sd        ours     n    ref mean   ref sd
0.025  2016     0.062    0.093         0.525   663     0.551    0.173
0.175   747     0.216    0.153         0.675   611     0.666    0.131
0.325   560     0.348    0.155         0.825   627     0.814    0.106
0.475   651     0.481    0.168         0.975  3118     0.973    0.059
```

The conditional mean tracks the **identity line** the whole way. There is no tone curve: Quartz
is not applying a gamma, a contrast boost or a coverage LUT that we are failing to apply. And
the spread around it is large — 0.13 to 0.17 in the mid-coverage bins, which is the size of the
entire residual. Fitting the best possible per-bin lookup table and applying it removes **-7%**
of the error, i.e. an oracle LUT is slightly *worse* than doing nothing.

So the two rasterisers already agree on how much ink a pixel should get on average, and disagree
about *which* pixels get it. That is scan-conversion geometry, not tone mapping. Nothing in a
disassembly of the smoothing path can be applied to close it; only replacing FreeType's
rasteriser could, which is a large, hot-path change to buy ~8% on edge pixels alone.

The useful lesson is the opposite one: the structural difference that *was* worth 30%, the
1/4-pixel grid rule, was found by black-box probing in minutes. That is the method that pays
here.

**Untested and worth testing:** every baseline in this sample is an integer, so nothing here
exercises fractional baselines, which real content produces constantly. cairo quantises glyph
*y* to whole pixels (dy = 0 and dy = -0.125 render identically); if Quartz places y on the same
1/4-pixel grid it uses for x, fast mode would diverge on any line box that does not land on an
integer. The existing harness tests this by moving `FT_LINE0`/`FT_LEADING` off integers.

## Recommendation

See `NOTES.md`, "Fast mode's font options".

## 2026-09-22 — run on the box through the real web process (run-box.sh)

`run-box.sh` renders `sample.html` with `pagedriver` + the `build/tiger-web-text` web process
(worktree `WebKit-fasttext`, commit 1d023af5: TigerGlyphSnap in FontCairo.cpp, TIGER64 font
options) and scores it against `ctref32int`. Two things about the reference had to change first:

* **The reference kerned and ligated; WebKit does not.** CTLine's defaults are kern on, liga on.
  WebKit with `text-rendering: auto` turns both off on every backend (its own CoreText path sets
  `kCTKernAttributeName` 0 and `kCTLigatureAttributeName` 0, SimpleFontDataCoreText.cpp). Against
  the kerned reference the Times line drifted 3 px by its end and scored 82; against the fair
  reference it is 28. `ctref32.c` now passes both attributes as 0.
* `pkill` does not exist on 10.4; `killall` does.

```
                        inkluma   per line: LG13 LGB13 LG11 Helv16 HelvB16 Times16 TimesIt16 Hira16
no snap (1d023af5^)      97.84
snap (1d023af5)          17.36     12.7  16.8  6.6   24.5   14.2    28.4    17.2      7.1
snap + TT bytecode at
  integral ppem (v40)    27.42     24.1  29.7  19.6  28.9   19.0    49.5    34.0      7.1   <- reverted
```

The web process now lands where `fast64`'s replay of CoreText's own layout predicted (17.95), i.e.
layout is no longer part of the difference. `out/pagedriver/crop-*.png` are 8x, reference over
each variant; `crops.py` makes them.

**What still differs, by eye:** at integer ppem Quartz puts a TrueType face's cap top and
x-height on whole pixel rows (Helvetica 16: cap 11.63 -> 12, x-height 8.49 -> 9; Times 16:
x-height 7.25 -> 8) and we do not, so the tops of x-height letters read one shade softer on the
16 px lines. Lucida Grande 13 and 11 and Hiragino 16 are not fitted by Quartz either and match.
Running FreeType's v40 interpreter at integral ppem does put those rows where Quartz has them
(see `crop-Helv16.png`, third band) but this font's prep runs v40 in backward-compatibility mode,
which also snaps stems in x and fattens them (ink 1.05-1.16); worse on every TrueType line, so it
is not in the build. Closing it needs y-only bytecode, which FreeType does not offer as an
option: render once hinted for y, once unhinted for x, and merge the outlines point-wise before
rasterising (~60 lines in a custom glyph path bypassing cairo's glyph cache), and even then
Apple's interpreter leaves 13/19/20/24 ppem unhinted where FreeType's does not.

## 2026-09-22 — what Quartz does vertically at integral ppem, and TigerGlyphFit

`yfit64.c` replays `ref.glyphs` through FreeType alone on the host (homebrew freetype; `make`
is not involved: `cc yfit64.c $(pkg-config --cflags --libs freetype2)`), one vertical rule per
run, scored per line by `score-pgm.py`. It reproduces the box to the decimal (`ftY` = the box's
bytecode build on every line), so hypotheses cost seconds instead of a build.

Per-glyph row profiles of the CoreText bitmap against unhinted and bytecode-hinted renders
(Helvetica 16: H, T, i, n, x, u; Times 16: T, n, x, o, l, d) say: Quartz is **not** running the
glyph programs the way FreeType does. Cap and ascender land on 12.0 as FreeType puts them, but
the x-height lands on ~8.9 where FreeType rounds to 9.0, and the H and e crossbars keep their
1.4 px thickness where FreeType's interpreter thins them to one pixel. Times 16 puts its x-height
on 8.0 exactly and its cap on 11.0. The picture that fits every glyph is a piecewise-linear
vertical map about the baseline with the x-height as its knot: stretch or squeeze below the
x-height so it lands on a pixel row, translate everything above by the same amount, leave the
descender alone. A sweep of one uniform factor per line confirms it (Helvetica 16 best 1.040,
Bold 1.020, Lucida Grande 13 1.0, Bold 0.972, Times Italic 0.964).

Which row the x-height goes to is the part we cannot compute: it comes from the font's control
values through Apple's interpreter. `round('o' top)` and FreeType's hinted 'o' agree for Helvetica,
Helvetica Bold and Lucida Grande, and then they are right; for Times they disagree (round 7,
bytecode 8, Quartz 8) and for Times Italic they disagree the other way (round 7, bytecode 8,
Quartz 7). TigerGlyphFit applies the fit only where they agree. Host model, inkluma per line:

```
rule       LG13  LGB13  LG11  Helv16  HelvB16  Times16  TimesIt16   mean
none        5.8   13.8   6.6    20.7     10.4     25.6      12.7    13.66
ftY        18.9   26.9  19.6    24.7     14.7     30.9      55.2    27.27  (bytecode y, unhinted x)
capY       26.4   33.9   6.6     6.4      8.6     13.0      28.8    17.66  (uniform, cap-based)
zones       8.5    6.4   6.6    13.5      4.1     31.8       5.5    10.92  (x-height+cap knots, round)
zonesXF     5.1    9.1   6.6     7.1      5.7     11.4      47.0    13.13  (x-height knot, bytecode target)
quartz      5.1    9.1   6.6     7.1      5.7     25.6      12.7    10.26  (shipped: agree-or-leave)
```

On the box, through the web process (`run-box.sh`, `out/pagedriver`): **17.36 -> 10.73** overall,
per line 5.1 / 9.1 / 6.6 / 7.1 / 5.7 / 28.4 / 17.2 / 7.1. The 1.125x page (`sample-zoom.html`,
reference `ctref32zoom`, `out/zoom`): **44.15 -> 10.78**; the old build did not snap under a
non-integral CTM at all, which is where most of that comes from, and the fit itself carries
Helvetica 18 to 8.0 and Bold to 4.0. Times 18 is the one line where the agree rule fires and
Quartz disagrees (32.6).

**Residual, honestly:** Times and Times Italic at integral ppem (28 / 17 on the sample page) are
left unhinted on purpose; getting them needs Apple's cvt rounding. The Helvetica exclusion list
(13, 19, 20, 24 unfitted) is still a measured table, not a rule. Lucida Grande 13 regular is
unfitted in Quartz while Bold 13 is squeezed 3%; the agree rule happens to be a near no-op for
both. Everything else the eye can pick out of the 8x crops is now edge-coverage noise.
