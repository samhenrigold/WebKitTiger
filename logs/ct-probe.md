# CoreText behavioural probe: Tiger 10.4.11 against modern macOS

The static screens say these functions link and take the right arguments. This asks the
different question: given identical inputs and identical font bytes, does Tiger's
implementation **answer** the same as modern CoreText?

Scope is the **47 CoreText functions WebCore calls that Tiger exports directly**, with no
ctcompat adapter — `used-CT.txt` intersected with `tiger-CT.txt`, minus the twelve
same-name-different-function cases in `compat/CT-SURVEY.md`. Adapters are ctcompat's own
live-oracle work and are not re-tested here.

Result: **163 values match, 71 diverge, 26 appear only on the modern side.** Of the
divergences, one is a real bug for the port, two are behaviour WebCore must be told about,
and the rest are either expected absences or an artifact of comparing run indices across
two different run splits.

## How it was run

`spike/ctprobe.c` builds from one source twice and prints a canonical `key value` dump.
`spike/ctprobe-diff.py` compares the dumps: floats within **1/64 pt**, integers exactly.
A value is treated as integral when neither side wrote a decimal point, so glyph ids,
counts and string ranges are held to exact equality while metrics get the tolerance.

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
| `logs/ctprobe-mac.txt` | modern macOS dump, 322 lines |
| `logs/ctprobe-tiger.txt` | Tiger 10.4.11 dump, 296 lines |
| `logs/ctprobe-diff.txt` | the full divergence list |

## Not covered

Bounded pass, so these are open rather than done: the twelve adapter functions, which are
ctcompat's; drawing calls (`CTLineDraw`, `CTFrameDraw`, `CTRunDraw`), which need a bitmap
context and a pixel comparison rather than a text dump; vertical writing; and a second font
with AAT tables, which would separate "Tiger cannot shape" from "Tiger cannot shape
OpenType". A Tiger-installed font with `morx`, run through the same probe, would settle
that and is the obvious next step.
