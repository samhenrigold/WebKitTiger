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

**7 cases, 3 pixel-identical, 3 needed fallback, 0 drew a `.notdef`.**
Worst per-glyph position disagreement over the wire: **0.1406 pt**.

| case | face the shaper chose | differing px | max channel | worst shift |
|---|---|---|---|---|
| system font, Lucida Grande 13 | LucidaGrande | **0** | **0** | 0.0103 pt |
| Arabic requested as Helvetica | GeezaPro-Bold *(fell back)* | **0** | **0** | 0.0000 pt |
| CJK requested as Helvetica | LiGothicMed *(fell back)* | **0** | **0** | 0.0000 pt |
| web font from bytes, Arabic | GeezaPro | 28 | 74 | 0.0156 pt |
| Latin with kerned pairs | Helvetica | 181 | 86 | 0.1250 pt |
| bold request | Helvetica-Bold | 165 | 86 | 0.1406 pt |
| Latin rejected by the shaper | Helvetica *(fell back)* | 110 | 86 | 0.0781 pt |

The three non-zero rows are all Apple-format `kern` faces, and the cause is the one
`logs/hb-raster.md` quantifies: CoreText puts the whole kern on the leading glyph,
HarfBuzz splits it across the pair. Nothing new appeared over the wire.

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
