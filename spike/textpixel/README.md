# Text across the ABI split: 64-bit HarfBuzz, 32-bit CoreText

A 64-bit process picks fonts from `logs/tiger-fonts.json`, shapes with HarfBuzz and sends
(face handle, size, glyph ids, positions) over Mach to a 32-bit process, which resolves the
handle through `compat/ctfonthandle.c` and draws with `CTFontDrawGlyphs`. Each case is then
compared, pixel for pixel, against CoreText laying out the same text in the same face on
its own.

```
make            # builds raster32 (i386) and shaper64 (x86_64)
# on the box:
./raster32 ./shaper64 ./tiger-fonts.json ./GeezaPro.ttf
```

## Result

**7 cases, 7 pixel-identical, 3 needed fallback, 0 drew a `.notdef`.**
Worst per-glyph position disagreement over the wire: **0.0039 pt**, against a tolerance of
0.02 pt that the test asserts and exits non-zero on.

Every case, including Latin with kerned pairs, Arabic, CJK and a web font from bytes,
produces pixels identical to CoreText laying out the same text itself.

Two fixes got it there, and both are rules the real font code has to keep.

**Apply Apple-format `kern` to the leading glyph.** CoreText subtracts the whole pair
value from the leading glyph; HarfBuzz splits it across the pair, moving the second glyph
by up to 0.99 pt. So for a face whose `kern` table is Apple-format, `shaper64.c` shapes
with `-kern` and applies the pair values itself. 66 of the box's 176 faces are in that
group, including Helvetica and Courier; the system font is not.

**Shape at 1/1024 pt, not 26.6.** This one was hiding behind the first. The conventional
`hb_font_set_scale(font, size * 64, ...)` rounds every advance into a 1/64 pt quantum, and
the error **accumulates along the run**: 0.0078 pt per glyph on Helvetica at 16 pt, which
is 0.14 pt by the eighteenth glyph and would be near half a point across a line of body
text. CoreText computes in float and does not accumulate, so this is HarfBuzz's precision
to choose rather than a disagreement to reconcile. Nothing about it is specific to Tiger,
and it would be easy to ship without noticing, because a single glyph looks correct.

Applying the kern rule alone reached 0.10 pt. Tracing the per-glyph advances is what
showed the remainder was not kerning at all.

## What this proves that the in-process test did not

**Fallback works when it is decided in the recording process.** The brief in
`logs/n1-briefs.md` calls this the second-highest residual risk in N1: the replay side
rasterises exactly what it is told, so it cannot ask CoreText what it would have chosen.
Asking for Arabic and for CJK *as Helvetica* forces the shaper to leave the requested
family, and both came back **pixel-identical** to what CoreText produces natively with
the face the shaper picked. Zero `.notdef` across every case is the check the brief asks
for, and it is asserted rather than eyeballed: `raster32` exits non-zero if any run
contains glyph 0.

The coverage decision is HarfBuzz's `hb_font_get_nominal_glyph` per character, taken in a
process that has no CoreText, no ATS and no CoreFoundation to consult. That is the
constraint the real font cache will be under.

## The web font

Bytes travel once, in 60 KB inline chunks, and are registered on the 32-bit side by
`TigerCTFontForData`, which is **ATS activation from memory**.
`CGFontCreateWithDataProvider` is not an alternative: Tiger exports it and it returns NULL
for `.ttf` and `.dfont` alike, which `compat/CT-SURVEY.md` records.

**Cost: 105,808 bytes registered in 2.9 ms**, once per distinct blob. The bytes then stay
resident for the process lifetime, because ATS reads the caller's memory for as long as
the container lives and there is no safe point to free it. A page with many `@font-face`
rules therefore holds all of them; that is a known and deliberate leak, noted in the
survey.

Out-of-line Mach descriptors were avoided on purpose. `mach_msg_ool_descriptor_t` is the
one descriptor whose size differs across the split, 12 bytes on i386 and 16 on x86_64, and
this test should not depend on that also being right.

## The case that failed first, and why that was the point

The web-font case originally asked an Arabic-only face to render Latin. Every glyph came
back `.notdef` and the rasteriser flagged it. That is the failure mode the whole design is
exposed to, reproduced by accident: a face the shaper should have rejected reached the
rasteriser, which faithfully drew nothing. The shaper now checks coverage for a downloaded
face exactly as it does for an installed one, and the test keeps both paths, the one where
the web font covers the text and the one where it does not.

## What a 64-bit process can use here

Only libSystem, libstdc++ and libz have an x86_64 slice on Tiger, so `shaper64` links
HarfBuzz, FreeType, libpng, brotli and zlib from `toolchain/sysroot-x86_64` and nothing
from the system but libc. The manifest is parsed by hand because there is no
CFPropertyList to parse it with.
