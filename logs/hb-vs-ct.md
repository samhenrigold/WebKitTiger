# HarfBuzz layout against Tiger CoreText rasterisation

Does the candidate split survive contact with real fonts? Layout and shaping in a 64-bit
process with HarfBuzz, rasterisation in a 32-bit process with Tiger's CoreText through
ctcompat's adapters. That only works if the two agree on **which glyphs** and **how far
apart**.

**Verdict: yes, to within 0.03 pt on run width and 0.008 pt per glyph, for every font
tested, at 12, 16 and 24 pt.** One font-specific caveat and one Tiger incapability are
below; neither forces per-font routing.

Harness `spike/hbvsct.c`, HarfBuzz 14.5.0 (i386), run on the 10.4.11 box. Both sides load
**identical bytes**: HarfBuzz from the file, CoreText from the same buffer through
`CTFontManagerCreateFontDescriptorFromData`, which is ctcompat's ATS activation. Naming a
font instead of supplying it would have compared two font files as well as two
implementations.

## Fonts

| font | shaping tables | why it is here |
|---|---|---|
| DejaVu Sans | GSUB GPOS kern | OpenType, and the only font here with OpenType Arabic |
| Helvetica (Tiger) | morx kern feat | AAT, plus an Apple-format `kern` table |
| Lucida Grande (Tiger) | morx feat | AAT, the system font |
| Geeza Pro (Tiger) | morx feat | AAT Arabic, the joining case |
| Hiragino Kaku Gothic Pro | GSUB GPOS CFF | OpenType CJK |

Helvetica and Lucida Grande were lifted out of their `.dfont` suitcases; a resource
container cannot be handed to either side.

## Results

69 sample-and-size comparisons over plain Latin, the `fi` and `ffl` ligatures, the kerned
pairs `AV` and `To`, Arabic and CJK.

| font | samples | worst per-glyph | worst run total | glyph mismatches |
|---|---|---|---|---|
| DejaVu Sans | 18 | 0.0156 pt | 0.0234 pt | arabic |
| Helvetica | 15 | 1.3320 pt | 0.0312 pt | none |
| Lucida Grande | 15 | 0.0078 pt | 0.0312 pt | none |
| Geeza Pro | 3 | 0.0078 pt | 0.0117 pt | none |
| Hiragino Kaku Gothic Pro | 18 | 0.0091 pt | 0.0124 pt | none |

Across the 66 samples where the glyph IDs agree:

| measure | worst |
|---|---|
| run total advance | **0.0312 pt** |
| per-glyph advance, excluding kerned pairs | **0.0079 pt** |
| per-glyph advance, kerned pairs only | 1.3320 pt |

Metrics agree too. At 16 pt, ascent and descent match to about 0.008 pt on every font, and
line gap matches exactly, including Hiragino's 8.000.

**HarfBuzz shapes AAT correctly.** Geeza Pro's Arabic, which needs `morx` joining, agrees
with Tiger to 0.008 pt with identical glyph IDs. So does Lucida Grande's Latin. There is
no separate "aat" shaper in HarfBuzz: `morx` and `kerx` are handled inside the `ot`
shaper, which prefers them when the font has them, and the available shapers on this build
are `ot` and `fallback`.

## The kerned-pair caveat, and why it does not matter

Helvetica's per-glyph figure is the only real divergence, and it is a **distribution**
difference rather than a disagreement. For `AV` at 16 pt the font's Apple-format `kern`
table holds -151 units, which at 2048 per em is -1.1797 pt:

| | first glyph | second glyph | total |
|---|---|---|---|
| CoreText | 9.4922 | 10.6719 | 20.1641 |
| HarfBuzz | 10.0781 | 10.0938 | 20.1719 |

**CoreText puts the whole kern on the leading glyph; HarfBuzz splits it across the pair.**
CoreText's per-glyph value is the one that matches the table exactly. The run is the same
width either way, to 0.008 pt.

This is invisible in the proposed architecture, because HarfBuzz owns positioning and
CoreText only rasterises a glyph at a position it is given, so CoreText's own advance
distribution never runs. It would matter only if something compared per-glyph advances
across the process boundary. Lucida Grande and Geeza Pro have no `kern` table and show no
such difference; DejaVu's OpenType `kern` agrees to 0.008 pt, so this is specific to
Apple-format `kern`.

## The one genuine incapability

DejaVu Sans Arabic is the only glyph mismatch: HarfBuzz produces the joined forms 5269 and
5256, Tiger produces the isolated 1369 and 1365. This is the limit already documented in
`compat/CT-SURVEY.md`: Tiger's ATSUI reads AAT `morx` and DejaVu does its joining through
OpenType `GSUB`, so Tiger cannot join it. Geeza Pro, which joins through `morx`, agrees
exactly.

**This is an argument for the architecture rather than against it.** Moving shaping to
HarfBuzz fixes the one thing Tiger genuinely cannot do, and the rasteriser does not need
to understand why it was handed those glyph IDs.

## What would force per-font routing

Nothing found. The two candidates were AAT shaping diverging, which it does not, and CJK
diverging, which it does not either: Hiragino's CJK agrees to 0.0000 pt.

The only per-font behaviour worth carrying forward is that **a font whose complex-script
shaping is OpenType-only must be shaped by HarfBuzz, never by Tiger**. Under this
architecture that is automatic, since Tiger never shapes anything.

## Reproducing

```
toolchain/bin/tiger-clang++ -O1 -g -x c++ spike/hbvsct.c -o build/hbvsct \
  -I toolchain/sysroot-i386/usr/include/harfbuzz \
  -nostdinc++ -isystem toolchain/sysroot-i386/usr/include/c++/v1 -stdlib=libc++ \
  -Fcompat/sdk-overlay \
  -F sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks \
  -lharfbuzz -lc++ -lc++abi -lunwind -ltigercompat \
  -framework CoreFoundation -framework ApplicationServices \
  -Wl,build/builtins-i386/libclang_rt.builtins-i386.a
```

Fonts are in `spike/ctprobe-data/` and `refs/tigerfonts/`. A sample the font does not
cover is skipped rather than compared: `CTLine` silently falls back to another font where
`hb_shape` only ever uses the font it was given, so comparing there would measure font
fallback rather than shaping.
