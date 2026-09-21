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

## Reading it

**The plain, unmodified setting wins, and wins by a lot.** Gray antialiasing, hinting off,
no gamma, no darkening, no emboldening: 0.987 of Quartz's ink, and every per-line ratio
between 0.95 and 1.04. Nothing in the tuning matrix improves on doing nothing.

**Every knob makes it worse, and the good ones make it worse the least.** Gamma at 0.85
costs 4% more error and overshoots the weight by 5%; at 0.55 the text is visibly fat.
Hinting costs monotonically: slight +13% error, full +39%. FreeType's stem darkening is a
no-op on the seven TrueType `.dfont` faces and only reaches the CFF Hiragino line, where it
adds 24% ink the reference does not have (see the last per-line column of
`gray-none-darken`). Emboldening is 26% too heavy and is the worst variant in the matrix.
The direction to tune *in* would have been lighter, not heavier, and there is no knob for
that.

**Do not round glyph x to whole pixels.** `gray-none-intpos` is the same rendering with
integer pen positions and it costs **+43% error** (2.665 → 3.814). cairo honours fractional
glyph origins on an image surface, CoreText positions at fractional x, and the two agree.
Any layer that quantises positions on the way to `cairo_show_glyphs` throws away a third of
the fidelity this spike measured.

**Subpixel antialiasing is strictly a regression here** (+15% error at every hint style),
because the reference has no colour in it at all. It is not a look to be tuned toward; it
is a look Quartz is not producing.

## What the residual is

28.56/255 mean error over inked pixels — about 11% — with stroke weight matched to 1.3%.
That is not weight, position or hinting; it is the rasterisers' antialiasing kernels
disagreeing about how to share coverage between adjacent pixels on a curve. Quartz's
outline scan-converter and FreeType's are different code computing the same integral, and
the difference lands entirely on edge pixels. No cairo or FreeType option addresses it, and
the only thing that would is rasterising with Quartz, which is what the other mode is for.

## Recommendation

See `NOTES.md`, "Fast mode's font options".
