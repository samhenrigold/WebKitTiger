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

## The correction that mattered most: a half-quantum grid offset

The table above says "nothing to tune", and at the level of cairo font options that is true.
It is also misleading, because the largest disagreement was not an option at all. Sweeping a
global horizontal shift over the whole sample finds a clean V with a single minimum at
**dx = -0.125 px**, which cuts the error by 30%:

```
shift-dx-0.250   2.267      shift-dx+0.000   2.665   <- as drawn
shift-dx-0.188   2.050      shift-dx+0.062   3.146
shift-dx-0.125   1.865  <-  shift-dx+0.125   3.784
shift-dx-0.062   2.179      shift-dx+0.250   5.193
```

Two probes identify it exactly. Quantising our glyph x to the nearest 1/4 px is
**byte-identical to not quantising at all** (2.665 either way), so cairo is already snapping
glyph origins to a 1/4-pixel grid. Quantising by *flooring* to 1/4 px instead scores 1.865 —
identical to the -0.125 shift, as it must be, since floor is round-to-nearest of x - 1/8.

So both rasterisers place glyphs on the same 1/4-pixel horizontal grid, and they disagree
about the rule: **Quartz floors to the grid, cairo rounds to nearest.** Our glyphs sit half a
quantum — 1/8 px — to the right of Quartz's, uniformly, on every face and size in the sample.
Flooring glyph x to 1/4 px before `cairo_show_glyphs` removes it, is free, and is orthogonal
to every other setting: it takes 30% off hinting none, 26% off slight and 18% off full.

It does not go to zero (1.865 remains), so the two grids agree on average rather than
per glyph. But it is the single biggest available win and it is not a font option.

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

**Do not round glyph x to whole pixels.** `gray-none-intpos` is the same rendering with
integer pen positions and it costs **+43% error** (2.665 → 3.814). Quarter-pixel positioning
is real and both rasterisers do it; whole-pixel rounding throws it away. Floor to 1/4 px,
never to 1.

**Subpixel antialiasing is strictly a regression here** (+15% error at every hint style),
because the reference has no colour in it at all. It is not a look to be tuned toward; it
is a look Quartz is not producing.

## What the residual is

After the grid fix, 20.37/255 mean error over inked pixels — about 8% — with stroke weight
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
