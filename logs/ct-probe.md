# CoreText behavioural probe: Tiger 10.4.11 against modern macOS

The static screens say these functions link and take the right arguments. This asks the
different question: given identical inputs and identical font bytes, does Tiger's
implementation **answer** the same as modern CoreText?

Scope is the **47 CoreText functions WebCore calls that Tiger exports directly**, with no
ctcompat adapter — `used-CT.txt` intersected with `tiger-CT.txt`, minus the twelve
same-name-different-function cases in `compat/CT-SURVEY.md`. Adapters are ctcompat's own
live-oracle work and are not re-tested here.

Result on the first pass: **163 values match, 71 diverge, 26 appear only on the modern
side.** Of the divergences, one was a real bug for the port, two are behaviour WebCore must
be told about, and the rest are either expected absences or an artifact of comparing run
indices across two different run splits.

**That bug is now fixed** (ctcompat, `c2f7f57`) and re-verified here: matches went 163 to
**189** and the cap-height and x-height error went from 5.8% to 0.033%. The committed dumps
are the post-fix run; the tables below keep the pre-fix numbers, because they are what the
fix was derived from.

## How it was run

`spike/ctprobe.c` builds from one source twice and prints a canonical `key value` dump.
`spike/ctprobe-diff.py` compares the dumps: floats within **1/64 pt**, integers exactly.
A value is treated as integral when neither side wrote a decimal point, so glyph ids,
counts and string ranges are held to exact equality while metrics get the tolerance.

Values outside 1/64 pt but within **0.05% relative** are reported in a separate `near`
bucket rather than counted as failures. A systematic difference shows up as a constant
relative error, which an absolute tolerance flags only at large sizes; the bucket keeps
those visible without crying wolf. Nothing is silently passed.

```bash
cc -O1 -o build/ctprobe-mac spike/ctprobe.c -framework CoreText \
   -framework CoreFoundation -framework CoreGraphics
toolchain/bin/tiger-clang -O1 -o build/ctprobe-tiger spike/ctprobe.c \
   -Icompat/include -Fcompat/sdk-overlay \
   -Fsdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks \
   -framework ApplicationServices -ltigercompat
./build/ctprobe-mac  spike/ctprobe-data/DejaVuSans.ttf > logs/ctprobe-mac.txt
scp -O build/ctprobe-tiger spike/ctprobe-data/DejaVuSans.ttf tiger:/tmp/ctprobe/
ssh tiger 'cd /tmp/ctprobe && ./ctprobe-tiger DejaVuSans.ttf' > logs/ctprobe-tiger.txt
python3 spike/ctprobe-diff.py logs/ctprobe-mac.txt logs/ctprobe-tiger.txt > logs/ctprobe-diff.txt
```

The font ships with the test at `spike/ctprobe-data/DejaVuSans.ttf`, md5
`49c0f03ec2fa354df7002bcb6331e106`, verified identical on both machines. DejaVu Sans was
chosen over Arial because it is freely redistributable, so it can live in the repository;
its license note sits beside it. It carries Latin, Greek, Cyrillic and Arabic, and **no
CJK**, which turned out to be useful rather than a limitation.

Loading goes through `CTFontManagerCreateFontDescriptorFromData` on both sides: native
here, ctcompat's ATS activation on Tiger. That is a helper, not a subject.

Inputs held fixed: sizes 12, 16 and 24 plus a sweep of 9, 10, 11, 13, 17, 19, 31 and 100;
Latin `"AVA fi Wave"`, Arabic `عربية`, CJK `一二三` and a mixed Latin/Cyrillic/Greek/
Arabic/Han string; descriptor attribute round trips; truncation at three widths with and
without a token.

## The one real bug: cap height and x-height are quantised on Tiger

**`CTFontGetCapHeight` and `CTFontGetXHeight` do not scale linearly on Tiger.** Every other
metric does: ascent, descent, leading, underline position and underline thickness match
modern CoreText to six decimal places at every size tested. These two do not.

| Size | capHeight (modern) | capHeight (Tiger) | xHeight (modern) | xHeight (Tiger) |
|---|---|---|---|---|
| 9 | 6.618164 | **7.000000** | 4.979004 | 5.000000 |
| 10 | 7.353516 | **7.000000** | 5.532227 | 5.500000 |
| 11 | 8.088867 | 8.000000 | 6.085449 | 6.000000 |
| 12 | 8.824219 | **9.000000** | 6.638672 | **7.000000** |
| 13 | 9.559570 | 9.500000 | 7.191895 | 7.000000 |
| 16 | 11.765625 | **12.000000** | 8.851562 | 9.000000 |
| 17 | 12.500977 | 12.500000 | 9.404785 | 9.500000 |
| 19 | 13.971680 | 14.000000 | 10.511230 | 10.500000 |
| 24 | 17.648438 | **18.000000** | 13.277344 | **13.000000** |
| 31 | 22.795898 | 23.000000 | 17.149902 | 17.000000 |
| 100 | 73.535156 | 73.500000 | 55.322266 | 55.500000 |

Tiger's answers always land on a half-point step. The worst deviation seen is **0.382 pt**
(cap height at 9 pt) and the worst relative error is **5.8%**, both at small sizes, which is
exactly the range web text lives in. At 12 pt the x-height is out by 0.361 pt, 5.4%.

It is not a rounding of the correct value. At 9 pt the linear cap height is 6.618 and
rounding to the nearest half would give 6.5, but Tiger returns 7.0. The pattern is
consistent with Tiger measuring a **grid-fitted** outline at each pixel size rather than
scaling a single design value; DejaVu Sans is fully hinted, and the deviation is largest
where grid fitting moves an edge furthest. That is a plausible mechanism, not a confirmed
one. What is confirmed is the divergence and its size.

**Why it matters.** `FontCoreText.cpp` reads both directly into `FontMetrics`:

```
FontCoreText.cpp:109   CGFloat capHeight = pointSize ? CTFontGetCapHeight(ctFont.get()) : 0;
FontCoreText.cpp:227   xHeight = CTFontGetXHeight(ctFont);
```

x-height is the CSS `ex` unit and drives `vertical-align: middle`; cap height feeds the
`cap` unit and leading trim. A 5% error in `ex` is a visible layout difference on any page
that sizes with `ex`, and it is silent.

**Fixed, and the mechanism turned out to be the midpoint after all.** ctcompat made both
adapters in `c2f7f57`. Re-running this probe against them: 0.033% for cap height and 0.044%
for x-height at every size, against 5.8% before, and at 12, 16 and 24 pt the absolute error
is 0.003 to 0.006 pt, well inside the 1/64 pt tolerance.

The residual is the evidence I was missing. Measuring flat `H` and `x` alone leaves a
constant 0.86% and 1.15% at every one of the ten sizes, and 1506/1493 and 1133/1120 are
those same ratios. A constant relative residual across ten independent sizes is a
mechanism, not a coincidence of one font, so averaging the flat form with the round one is
right: half the overshoot counts. A font whose round glyphs do not overshoot gets the same
answer either way, so the rule cannot make anything worse.

Only the 100 pt stress size now exceeds the absolute tolerance, at the same 0.033%, which
is why the differ grew a relative-tolerance bucket. The original recommendation follows.

**Recommendation for ctcompat:** these two need adapters after all, which is a change to
the survey's direct-use classification.

The right source needs care, because modern CoreText's numbers are not a straight read of
any font table. This font's `OS/2` is version 1, only 86 bytes, so it has no `sCapHeight`
or `sxHeight` at all. Modern's values work out to 1506 and 1133 font units, and the glyph
bounding boxes are 1493 for the flat capitals (`H B D E I T`) and 1520 for the round ones
(`O Q S`), 1120 for flat x-height (`x v w z`) and 1147 for round (`o e a c n m s`). 1506
and 1133 are the midpoints of those pairs, to the unit. That is too exact to be
coincidence, but it is an observation about one font, not a rule I have confirmed.

So the practical advice is a two-step fallback rather than a formula:

1. When `OS/2` is version 2 or later and the fields are non-zero, scale `sCapHeight` and
   `sxHeight` by `size / CTFontGetUnitsPerEm`. That is what the specification says those
   fields are for, and most fonts supply them.
2. Otherwise take the bounding box of `H` and of `x`. On this font that gives 6.561 at
   9 pt against modern's 6.618, an error of **0.9%** where Tiger's current answer is out by
   **5.8%**.

Either way the fix should be checked by re-running this probe rather than reasoned about,
which is the same rule the survey already draws from `CGFontGetGlyphAdvancesForStyle`.

## Two behaviours to know about

**`CTFontCopyAttribute(font, kCTFontSizeAttribute)` returns NULL on Tiger** at all three
sizes, where modern returns the size as a `CFNumber`. `CTFontGetSize` is correct on both,
so the value is available; only the attribute lookup is missing. Anything reading a size
off a font through the attribute interface gets nothing.

**`CTFontGetSymbolicTraits` reports a font class Tiger fills in and modern leaves empty.**
Tiger returns `0x80000000` for DejaVu Sans where modern returns `0x0`. The low trait bits,
which are the ones WebCore actually tests, are zero on both — bold, italic and monospace
agree. `0x80000000` is class 8, sans-serif, in the class field at bits 28-31, read from the
font's `OS/2` family class. The risk is narrow but real: code comparing whole trait words
for equality, or testing `traits == 0`, behaves differently. Code masking with
`kCTFontTraitClassMask` or testing individual bits is fine.

**`CTLineCreateTruncatedLine` needs a non-NULL token on Tiger.** With a NULL
`truncationToken` Tiger returns NULL at every width tried, including widths far wider than
the line; modern returns a line whenever the text fits. Given a real ellipsis token line
the two agree exactly, including both returning NULL when the width is too small for the
token. Impact here is nil: the single call site,
`rendering/AttachmentLayout.mm:349`, passes a real ellipsis line, and it sits behind
`ENABLE(ATTACHMENT_ELEMENT)` which the survey already says should be off. Worth recording
so nobody adds a NULL-token call later.

## Expected absences, confirmed rather than assumed

**Arabic is not shaped on Tiger.** This is the largest behavioural gap and it is exactly
what the survey predicted. For `عربية` at 16 pt:

| | glyphs in the shaped line |
|---|---|
| modern | 5259 5355 5256 5285 5314 |
| Tiger | 1366 1394 1365 1374 1382 |

Tiger's five are precisely the codepoints' own `cmap` glyphs, which both sides agree on
(`CTFontGetGlyphsForCharacters` matched exactly), merely reversed into visual order. Modern
CoreText substituted five different glyphs, the initial, medial and final contextual forms.
DejaVu Sans does its Arabic joining through OpenType `GSUB`; Tiger's ATSUI reads AAT `morx`
and `mort`, which this font does not have, so nothing is substituted. Tiger does get the
direction right: it reverses the run and sets the right-to-left status bit.

**`CTFontCopyFeatures` returns 3 feature types on Tiger against 7 on modern**, and Tiger's
dictionaries lack `CTFeatureTypeExclusive` and `CTFeatureOpenTypeTag`. Same cause: modern
synthesises feature descriptions from `GSUB`, Tiger reports only what the AAT `feat` table
declares. The four extra modern entries are the 26 "only on the modern side" lines.

**CJK falls back to a different font.** Neither side finds `一二三` in DejaVu —
`CTFontGetGlyphsForCharacters` returns false with zero glyphs on both, which is the
agreement that matters. At line level each machine substituted its own system fallback, so
the glyph ids differ. That is a difference in installed fonts, not in CoreText.

**Descriptors are thin, as documented.** `CTFontDescriptorCopyAttribute` for family, style
and traits returns NULL on Tiger for the descriptor that came from font data, because
ctcompat builds it as name-and-size only. `CTFontCopyFontDescriptor` returns three
attributes on Tiger against two on modern, Tiger adding `NSCTFontTraitsAttribute`. Both
already appear in the survey.

## Two artifacts of the comparison, not findings

**The `Ptr` accessors return NULL on modern and real data on Tiger**, which is the opposite
of the direction one expects and accounts for 16 of the 71 divergences. Modern CoreText
does not guarantee direct access and declines it here; Tiger hands back its internal
arrays. This is not a Tiger defect, and it has an operational consequence worth passing on:
on modern hardware `ComplexTextControllerCoreText.mm` always takes its copying fallback,
while on Tiger it always takes the `Ptr` path. The two platforms therefore exercise
different code, and on Tiger the copying variants are ctcompat adapters over Tiger's six
empty stubs. Whichever path WebKit is tested on, the other is the untested one.

**Run segmentation differs, which misaligns the per-run comparison.** For the mixed-script
string modern produced 5 runs and Tiger 3: Tiger kept Latin, Cyrillic and Greek together in
one run, modern split each script into its own. Since the differ aligns by run index,
several lines report a divergence that is really the same glyph appearing at a different
run number. Reading through it, the glyph ids agree wherever the same font was used
(Cyrillic 932 and Greek 837 on both, Arabic 1365 on both); only the CJK glyph genuinely
differs, for the fallback reason above. Run *counts* differing is itself a real Tiger
behaviour, just not a wrong one.

## What this changes

- **ctcompat should add adapters for `CTFontGetCapHeight` and `CTFontGetXHeight`.** They
  are currently classified as direct-use and they are not safe as such. Messaged separately.
- The survey's direct-use tier is otherwise sound: 45 of the 47 functions either match or
  differ only for a documented structural reason.
- Complex-text routing is confirmed as the priority the survey says it is. Arabic through
  `CTLine` on Tiger yields unshaped isolated forms for an OpenType-only font, so any font
  without AAT tables gets no joining.

## Files

| Path | What |
|---|---|
| `spike/ctprobe.c` | the probe, one source for both targets |
| `spike/ctprobe-diff.py` | tolerance-aware differ, 1/64 pt on floats, exact on integers |
| `spike/ctprobe-data/DejaVuSans.ttf` | the bundled font, plus its license note |
| `logs/ctprobe-mac.txt` | modern macOS dump, the oracle |
| `logs/ctprobe-tiger.txt` | Tiger 10.4.11 dump, post-fix |
| `logs/ctprobe-diff.txt` | the full divergence list |

## Not covered

Bounded pass, so these are open rather than done: the twelve adapter functions, which are
ctcompat's; `CTFrameDraw` and `CTRunDraw`; and vertical writing. The two items that were
open here, AAT shaping and a pixel comparison of glyph drawing, are now done and appear
below.

---

# Follow-up 1: shaping with AAT fonts

The first pass showed Tiger applying no Arabic shaping to DejaVu Sans and left the
question of whether Tiger's shaper works at all or only its OpenType path is missing. It
is the second, and the boundary is sharper than "AAT yes, OpenType no".

**Tiger shapes AAT fonts identically to modern CoreText. It also applies OpenType `liga`
ligatures identically. What it does not do is OpenType complex-script joining.**

`spike/ctshape.c` prints, per font and per sample, the raw `cmap` glyphs beside the glyphs
the line actually produced, so substitution is visible rather than inferred.

## Which of Tiger's fonts can be shaped at all

`spike/ctprobe-fonttables.py` reads sfnt table directories, including inside `.dfont`
resource containers, and runs under the box's Python 2.3 as well as 3.x. Over the box's
`/System/Library/Fonts` and `/Library/Fonts`:

| | files |
|---|---|
| scanned | 49 |
| with AAT shaping (`morx`/`mort`) | **41** |
| with OpenType `GSUB` | 8 |
| with both | 2 |
| with neither | 2 |

So the overwhelming majority of what Tiger ships is AAT, and shapes correctly. The eight
`GSUB` fonts are the six Hiragino CJK faces plus AquaKana Regular and Bold, and those two
are the only ones carrying both. The two with neither are Apple Symbols and AppleCasual.
The per-file listing is in `logs/ctprobe-tigerfonts.txt`.

Notably **none of Tiger's six Hiragino CJK faces has `morx`**; they are `GSUB`/`GPOS` only.
That matters less than it looks, because CJK is mostly a one-to-one mapping, but vertical
forms and ruby do need substitution and will not get it.

## The measurements

Three fonts, same bytes on both machines. Helvetica had to be lifted out of its `.dfont`
suitcase first, since a resource container cannot be handed to
`CTFontManagerCreateFontDescriptorFromData`; the extracted sfnt is byte-identical to the
one Tiger loads.

**Arabic through an AAT font (Geeza Pro, `morx`) is identical.** Not close, identical:

```
                        mac                          tiger
cmapGlyphs              222 214 205 234 206          222 214 205 234 206
line                    runs=1 glyphs=5              runs=1 glyphs=5
run0.glyphs             117 109 4 130 14             117 109 4 130 14
run0.advances           7.102 6.539 6.539 ...        7.102 6.539 6.539 ...
run0.status             0x1                          0x1
shaped                  yes                          yes
```

Five characters in, five contextual forms out, none of them the `cmap` glyph, the same
five on both machines, with the same advances and the same right-to-left status bit. The
second Arabic sample behaves the same way.

**Latin ligatures through an AAT font (Helvetica, `morx`+`kern`) are identical.** `fi`
becomes one glyph, 192, on both; `ffl` becomes two, 73 and 193, on both. `AVATar` gets no
substitution on either and the advances agree to the last digit, so the `kern` table is
being applied the same way.

**OpenType `liga` also works, and is identical.** This was the surprise. DejaVu Sans has no
`morx`, yet on both machines `fi` becomes glyph 5039 with advance 15.117 and `ffl` becomes
glyph 5042 with advance 23.203. Tiger read the `GSUB` ligature lookup and applied it.

**OpenType Arabic joining is the one real gap.** Same font, same bytes:

```
                        mac                          tiger
cmapGlyphs              1382 1374 1365 1394 1366     1382 1374 1365 1394 1366
run0.glyphs             5259 5355 5256 5285 5314     1366 1394 1365 1374 1382
shaped                  yes                          no - raw cmap glyphs only
```

Tiger returns the `cmap` glyphs in reverse, which is the right visual order and the right
direction bit but no joining at all. Modern substitutes five contextual forms.

The distinction that falls out: a `GSUB` lookup the font can apply on its own, like a
ligature, works on Tiger. A `GSUB` feature that only a script-aware shaper can select,
like the initial/medial/final forms Arabic needs, does not, because selecting it requires
Arabic joining logic in the shaper. With AAT the joining is a state machine inside the
font, which is why `morx` Arabic works.

## What this means for the port

**HarfBuzz is needed only for complex scripts in fonts without AAT tables.** For Tiger's
own fonts, 41 of 49, the system shaper is correct and matches modern CoreText exactly. For
web fonts, which are almost always OpenType-only, Latin and ligatures are fine and Arabic,
Hebrew with marks, and the Indic scripts are not.

## Everything else in the shaping diff is font fallback

Of 31 divergences in `logs/ctshape-diff.txt`, 27 are one machine picking a different
fallback font, which is a difference in what is installed rather than in CoreText. Asking
Helvetica for Arabic falls back to this Mac's Geeza Pro, 2212 units per em and 1705 glyphs,
against Tiger's, 2048 and 343. CJK falls back to PingFang here and Hiragino there; kana to
Hiragino Sans here and AquaKana there. Printing each run font's units per em and glyph
count is what made this legible, and it is worth keeping: pointer identity is useless here,
because an equivalent font is not the same object and a same-named font from another source
is exactly the hazard.

---

# Follow-up 2: pixel comparison of glyph drawing

**No offset error and no scale error. With antialiasing off the two machines produce
identical rasters.**

`spike/ctdraw.c` draws the same string from the bundled DejaVu Sans into an 8-bit grey
bitmap at 16 and 24 pt, by two paths: `CTLineDraw`, which is ctcompat's adapter on Tiger,
and `CGContextShowGlyphsWithAdvances`. It then reduces each canvas to the inked bounding
box, total coverage, centroid and pixel count, which survive a different antialiaser where
exact pixels would not.

| measurement | modern | Tiger |
|---|---|---|
| 16 pt, no AA, `CTLineDraw` | bbox 21,44,175,58 cov 381.00 centroid 94.864,50.551 | **identical** |
| 16 pt, no AA, glyph path | bbox 21,44,175,58 cov 381.00 centroid 94.921,50.551 | **identical** |
| 24 pt, no AA, `CTLineDraw` | bbox 22,38,253,60 cov 1094.00 centroid 132.274,48.424 | **identical** |
| 24 pt, no AA, glyph path | bbox 22,38,254,60 cov 1094.00 centroid 132.400,48.424 | **identical** |
| 16 pt, AA, `CTLineDraw` | cov 518.03 centroid 95.254,50.659 | cov 517.62 centroid 95.269,50.659 |
| 24 pt, AA, `CTLineDraw` | cov 1134.04 centroid 133.065,48.364 | cov 1132.77 centroid 133.089,48.364 |

With antialiasing off every number matches exactly, including the pixel counts, so glyph
scaling and pen placement are the same on both machines. With antialiasing on the bounding
boxes and pixel counts still match and only the coverage differs, by **0.08% at 16 pt and
0.11% at 24 pt**, with the centroid moving at most 0.024 px. That is the antialiasing
filter, not geometry.

The advance width agrees exactly at both sizes, 157.6562 and 236.4844.

One thing the comparison shows that is *not* a Tiger difference: the glyph path draws one
pixel wider than the `CTLineDraw` path, 176 against 175 and 254 against 253. That appears
identically on both machines, so it is a property of the two CoreText paths rather than
anything the port introduced.

**ctcompat's `CTLineDraw` adapter is correct.** Tiger's own `CTLineDraw` takes an extra
`CFRange` and draws nothing when it receives stack junk; the adapter presents the modern
two-argument form, and the ink it produces is pixel-identical to modern CoreText.

## Files from the follow-ups

| Path | What |
|---|---|
| `spike/ctshape.c` | shaping probe, raw cmap beside shaped output |
| `spike/ctdraw.c` | rasterises and reduces to bbox, coverage, centroid |
| `spike/ctprobe-fonttables.py` | sfnt and `.dfont` table reader, runs on Python 2.3 |
| `logs/ctshape-{mac,tiger,diff}.txt` | shaping dumps and their diff |
| `logs/ctdraw-{mac,tiger,diff}.txt` | drawing dumps and their diff |
| `logs/ctprobe-tigerfonts.txt` | every font file on the box and its tables |

The three fonts used are Apple's and one is extracted from a system suitcase, so they are
kept in `refs/tigerfonts/`, which is outside version control. `spike/ctprobe-fonttables.py`
regenerates the survey and the extraction recipe is in this file's history.

## Still not covered

`CTFrameDraw` and `CTRunDraw`; vertical writing; and Indic or Hebrew shaping, which the
AAT-versus-OpenType boundary above predicts will behave like Arabic but which was not
measured.


---

# Postscript: why the Leopard oracle passed cap height

ctcompat's live oracle runs 9A241's CoreText on the box and diffs it against Tiger's. It
passed cap height, which was 5.8% out against modern, and the stated reason was that both
were wrong in the same direction. That is exactly right, and it is worth having measured
rather than reasoned, because it marks the boundary of what that oracle can see.

`spike/ct9metrics.c` asks all three for the same metric on the same font. DejaVu is
activated through ctcompat's `CTFontManagerCreateFontDescriptorFromData`, which ATS-registers
it process-wide, so 9A241's CoreText resolves it by PostScript name: the same trick the
web-font path depends on.

| size | cap (9A241) | cap (Tiger, pre-fix) | cap (modern) | x-height (9A241) | x-height (Tiger, pre-fix) | x-height (modern) |
|---|---|---|---|---|---|---|
| 9 | 7.000000 | 7.000000 | 6.618164 | 5.000000 | 5.000000 | 4.979004 |
| 12 | 9.000000 | 9.000000 | 8.824219 | 7.000000 | 7.000000 | 6.638672 |
| 16 | 12.000000 | 12.000000 | 11.765625 | 9.000000 | 9.000000 | 8.851562 |
| 24 | 18.000000 | 18.000000 | 17.648438 | 13.000000 | 13.000000 | 13.277344 |
| 100 | 73.500000 | 73.500000 | 55.500000 | 55.500000 | 55.500000 | 55.322266 |

9A241 agrees with pre-fix Tiger to the last digit at every size, on both metrics. Ascent
agrees with all three, so this is specific to the two quantised metrics rather than a
general disagreement.

So the two runtime checks are not interchangeable. A Leopard-era oracle inherits Leopard-era
behaviour, which makes it excellent for questions of the form "does our shim match what
Apple's code did" and blind to anything Apple changed after that binary shipped. Cap height
was settled somewhere between 9A241 and now, so both implementations were on the same side
of the change and the diff was clean. Only a comparison against a *modern* implementation
could see it.

Stated as a rule: the Leopard oracle answers "is this faithful to the era", this probe
answers "is this right today", and a divergence that predates Leopard is invisible to the
first and visible to the second. Both are worth running.


---

# Re-verified after Security Update 2009-005

The box was updated on 2026-09-20 with Security Update 2009-005, QuickTime 7.6.4, ImageIO
and Safari 4.1.3, then again with Java 9 and iPhoto, rebooting at 21:57. Everything below
was re-run at 22:03, after that final reboot, against software the user has now frozen.
Five of the libraries these probes exercise were replaced:

| | before | after |
|---|---|---|
| CoreFoundation | 368.31.0 | 368.35.0 |
| ATS | 184.13.1 | 184.17.0 |
| CoreGraphics | 258.77.0 | 258.85.0 |
| Foundation | 567.36.0 | 567.42.0 |
| libSystem | 88.3.9 | 88.3.11 |

CoreText's binary was rewritten at the same timestamp but keeps version 1.0.0 and the same
889,256 bytes.

**Nothing changed.** All three probes were rebuilt and re-run, and every dump is
byte-identical to the committed pre-update run: `ctprobe` still 189 matched, 2 near, 43
differing; `ctshape` identical on all three fonts; `ctdraw` identical at both sizes and both
paths. The font table survey over the box is unchanged, and no font file was touched, only
the `/Library/Fonts` directory mtime.

**The 9A241 rig survives, which was the open risk.** Its CoreFoundation bridge bootstrap
depends on CF internals and CF is one of the libraries that moved. The framework still
loads, the sentinel is still `0xa0813d20` at the same address, and `ct9metrics` returns the
same numbers. The bootstrap reads that sentinel at run time rather than assuming it, which
is what made it survive; a hardcoded value would have been a coin flip.

**The `sysroot/` mirror is now stale in content but not in API surface.** Comparing exports
between the box's new binaries and the mirror:

| library | exports before | after | added | removed |
|---|---|---|---|---|
| CoreFoundation | 2484 | 2484 | 0 | 0 |
| ATS | 419 | 419 | 0 | 0 |
| CoreGraphics | 3568 | 3568 | 0 | 0 |
| CoreText | 243 | 243 | 0 | 0 |
| Foundation | 1516 | 1517 | **1** | 0 |
| libSystem | 3413 | 3413 | 0 | 0 |

The single addition is `_NSHTTPCookieHTTPOnly`, which is the HttpOnly cookie support the
security update brought. Nothing else in the linkable surface moved, so symbol resolution
and the `logs/api/tiger-*.txt` export lists remain correct. Refreshing the mirror is
therefore optional rather than urgent, and only matters to anyone who wants that one
Foundation constant.
