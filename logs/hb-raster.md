# HarfBuzz positions, rasterised by Tiger CoreText, against CoreText end to end

`logs/hb-vs-ct.md` showed the two engines agree on glyph ids and advances. This asks the
question that decides the architecture: draw HarfBuzz's glyphs at HarfBuzz's positions
with Tiger's CoreText, and compare the **pixels** against what CoreText produces on its
own.

**Verdict: near-identical, with one real and bounded difference.** Fonts without an
Apple-format `kern` table come out **pixel-identical**, zero differing pixels and zero
channel delta. Fonts with one differ, because HarfBuzz and CoreText distribute AAT kerning
differently; the worst glyph displacement measured is 0.99 pt. That affects 66 of the 176
installed faces and has a cheap fix.

Harness `spike/hbraster.c`, 640x64 bitmaps, antialiased, font smoothing off, identical
context state on both sides.

| case | differing px | % of ink | max channel | worst glyph shift |
|---|---|---|---|---|
| Lucida Grande 13, paragraph | **0** | 0.0% | **0** | 0.0117 pt |
| Lucida Grande 13, plain | **0** | 0.0% | **0** | 0.0166 pt |
| Hiragino Kaku Gothic 16, CJK | **0** | 0.0% | **0** | 0.0000 pt |
| Geeza Pro 16, Arabic RTL | 28 | 25.2% | 74 | 0.0156 pt |
| Helvetica 16, plain | 156 | 19.5% | 86 | 0.0859 pt |
| Helvetica 16, paragraph with kerned pairs | 606 | 42.6% | 86 | **0.9922 pt** |

Three panels per case in `/tmp/hbraster-*.ppm`: HarfBuzz-positioned, CoreText, difference.

## Reading the numbers

The system font renders **identically**. Lucida Grande at 13 pt, the size Tiger's UI uses,
produces the same 1063 inked pixels with the same values either way. So does CJK.

**A percentage of differing pixels is not a measure of how wrong something looks.** Geeza
Pro's Arabic differs on 25% of its inked pixels from a glyph displacement of 0.0156 pt,
which is a sixtieth of a point: antialiasing coverage changes as soon as a glyph moves at
all, so a sub-pixel shift lights up a quarter of the ink while being invisible. The
displacement column is the one to read, and the pixel column only says whether the
displacement was exactly zero.

## The one real difference

Helvetica's paragraph displaces a glyph by 0.99 pt, which is visible. The cause is the one
`logs/hb-vs-ct.md` already identified: for an Apple-format `kern` table, **CoreText puts
the whole kern on the leading glyph and HarfBuzz splits it across the pair**. The run ends
the same width, so line breaking is unaffected, but the second glyph of each kerned pair
sits about half a kern to the right, and the positions re-converge at the glyph after.

CoreText's distribution is the one that matches the table: for `AV` at 16 pt the table
holds -151 units, which is -1.1797 pt, and that is exactly what CoreText applies.

This is specific to Apple-format `kern`. HarfBuzz's OpenType kerning agrees with CoreText
to 0.008 pt, measured on DejaVu Sans, and fonts with no `kern` table at all are
pixel-identical.

**Blast radius: 66 of 176 installed faces** carry an Apple-format `kern` table, 42 carry a
Microsoft-format one, and 68 carry none. Helvetica and Courier are in the first group;
Lucida Grande, the system font, is in the third.

**The fix is cheap and belongs in the web process.** For a face whose `kern` table is
Apple-format, shape with `-kern` and apply the table's value to the leading glyph, which
is what CoreText does and what the table means. Nothing in the render process changes.

## Two things this test got wrong first, both of which looked like success

Worth recording, because both are the failure mode this port keeps producing.

**Tiger's `CTLineDraw` takes its colour from the attributed string, not from the context.**
It defaults to black and ignores `CGContextSetRGBFillColor` entirely. The first run of this
test drew the CoreText side black on a black bitmap and reported *zero* inked pixels for
every case, which reads as a catastrophic failure rather than a test bug.
`kCTForegroundColorAttributeName` fixes it. `CTFontDrawGlyphs` has no such behaviour and
honours the context, so the two sides need setting up differently to be compared fairly.

**That same behaviour was making a committed check pass for the wrong reason.** The
`CTLineDraw adapter draws the whole line` case in `spike/cttest.c` set the colour on the
context, so it too was drawing black on black, and it passed only because it reused a
bitmap still owned by a live context from the preceding case, whose ink it was reading.
Two independent faults cancelling into a green check. Fixed: its own buffer, an explicit
foreground colour, and it now fails if either regresses.

## Verdict for the architecture

Layout in a 64-bit process with HarfBuzz and rasterisation in a 32-bit process with Tiger
CoreText produces **the same pixels** for the system font, for CJK, and for any face
without an Apple-format `kern` table. For the 38% of faces that have one, glyphs within a
kerned pair sit up to 1 pt from where CoreText would put them until the web process
applies AAT kerning the way the table means it. No other difference was found.
