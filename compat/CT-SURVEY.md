# CoreText on Mac OS X 10.4.11 — what WebCore needs and what Tiger has

Source data: `logs/api/{missing,tiger,used}-CT.txt`. WebCore calls 120 CT functions;
Tiger's private CoreText (ApplicationServices/Frameworks/CoreText, 243 exports) covers 54.
The 66 in `missing-CT.txt` are classified below and implemented in `compat/ctcompat.c`
(declarations in `compat/include/TigerCompat/CTCompat.h`).

Three Tiger facts drive most of the work:

- Tiger's CT is the **pre-1.0 CoreText** that shipped with 10.4.7. Most modern entry
  points exist under an older name (`CTFontDescriptorCopyWithAttributes` rather than
  `...CreateCopyWithAttributes`, `CTFontCopyDefaultCascadeList` rather than
  `...ForLanguages`).
- **Tiger's CoreText passes every by-value scalar as a `double`, not a `CGFloat`.**
  See the next section; this is the single most dangerous thing on the box.
- Tiger's **CoreGraphics exports `CGFontGetGlyphPath` and `CGFontGetUnitsPerEm`** even
  though the 10.4u SDK's `CGFont.h` declares neither. Those, plus
  `ATSFontActivateFromMemory` (properly declared in the 10.4u SDK), are what make web
  fonts and glyph paths possible at all. `CGFontCreateWithDataProvider` is exported too
  but is **non-functional**: the leopard track established that it returns NULL for
  `.ttf` and `.dfont`, as does `CGFontCreateWithName`, so ATS is the only route to a
  `CGFontRef` from bytes. Nothing here depends on it.

## The part that will bite the port: same name, different function

Everything below was read out of the disassembly of the box's own
`ApplicationServices/Frameworks/CoreText` and then confirmed by running against it.
None of it is guesswork, and none of it is visible from any SDK.

**Doubles, not CGFloat.** Tiger's CoreText predates the CGFloat unification and its
C++ core (`TFont::GetSize`, `CTFont::CTFont(CFStringRef, double, const CGAffineTransform*)`)
is written in `double`. Every prototype the 10.5 SDK spells `CGFloat` is `movsd` on this
box. A caller that passes a 4-byte float misaligns every argument after it, silently:
`CTFontCreateWithName(CFSTR("Helvetica"), 16.0f, NULL)` returns a font whose size is 0,
and every metric derived from it is 0 or NaN. Anything reached through a pointer, or
inside a `CGSize`/`CGRect`, is still a 4-byte float: `CTLineGetTypographicBounds` stores
its ascent with `movss`. **The 10.5 SDK's CoreText headers are therefore wrong for this
binary and must not be used to compile against it.** `CTCompat.h` carries correct
prototypes for everything the compat layer touches.

**Seven exports do nothing.** Six have a body of exactly `xor eax, eax; ret`:
`CTFontCreateUIFontForLocale`, `CTFontCreateWithQuickdrawNameAndStyle`, `CTRunGetGlyphs`,
`CTRunGetAdvances`, `CTRunGetStringIndices` and `CTRunDraw`. Only the `Ptr` variants
(`CTRunGetGlyphsPtr`, `CTRunGetAdvancesPtr`, `CTRunGetStringIndicesPtr`) return real
data, which is lucky, because `ComplexTextControllerCoreText.mm` already prefers them.
The seventh is `CTLineGetImageBounds`, which never looks at its line: it copies a fixed
global rect into the struct return and comes back. Anything wanting ink bounds on Tiger
has to compute them from glyph bounding rects itself.

**Five take different arguments** from the modern API of the same name:

| Function | Tiger's real signature |
| --- | --- |
| `CTFontCopyTable` | `(CTFontRef, CFStringRef tableName)` — the four-character tag as a **CFString**, and no options argument. Tiger's own `kCTFontTableGSUB` is the string `"GSUB"`. |
| `CTFontGetAdvancesForGlyphs` | `CGSize (CTFontRef, const CGGlyph[], CGSize[], CFIndex)` — **no orientation**, and the summed advance comes back as a CGSize rather than a double. |
| `CTFontGetBoundingRectsForGlyphs` | `CGRect (CTFontRef, const CGGlyph[], CGRect[], CFIndex)` — no orientation. |
| `CTLineGetTypographicBounds` | `double (CTLineRef, CFRange, CGFloat*, CGFloat*, CGFloat*)` — takes a **CFRange** the modern four-argument form does not. The range is only checked against the glyph count, so `{0, 0}` is always safe; calling it the modern way puts the ascent pointer where the range goes and the call quietly returns 0. |
| `CTLineDraw` | `void (CTLineRef, CGContextRef, CFRange)` — also takes a **CFRange**. It compares `location + length` against the line's glyph count with an integer `cmpl` and draws nothing when the sum is larger, so a modern two-argument call passes stack junk as the range and draws the line only when that junk happens to be `{0, 0}` or `{0, count}`. Five call sites in tree. |

These eleven are **not** in `missing-CT.txt`, because they are exported under exactly the
name WebCore calls. They are the worst kind of problem: they link, they run, and they
return zeroes.

**This is now handled, and WebCore needs no edits.** `ctcompat.c` carries a
`TigerCT...`-prefixed adapter for each of the eleven, presenting the modern signature and
reaching Tiger's real behaviour underneath, and the SDK overlay's `<CoreText/*.h>` binds
the public name to the adapter with an `__asm__` label. WebCore writes `CTFontCopyTable`
and the call lands on `_TigerCTFontCopyTable`. The same mechanism covers the by-value
`double` parameters, except that those need no adapter at all: declaring the prototype
with `double` is enough, because C converts the caller's `CGFloat` at the call site.

**Descriptors are thinner than they look.** `CTFontCopyFontDescriptor` returns a
descriptor holding exactly three attributes: `NSFontNameAttribute` (the PostScript name),
`NSFontSizeAttribute` and `NSCTFontTraitsAttribute`. There is no family name on it, so
anything that reads `kCTFontFamilyNameAttribute` off a descriptor gets NULL and has to
realise the descriptor into a font and ask that instead.

## Which tier each function landed in

Per the standing rule: **tier 1** means Tiger already exports something that does the
job, private or older-named; **tier 2** means it is implemented the way Apple implements
it, read out of the 10.5 binary in `refs/leopard/CoreText.i386`; **tier 3** means neither
was reachable and this is the best version that could be written on what Tiger has.

Everything in bucket (b) is **tier 1** by definition: each one forwards to a Tiger export
under its older name.

Bucket (c), by tier:

| Tier | Functions |
| --- | --- |
| 1 | `CTFontDescriptorCreateCopyWithSymbolicTraits` (Tiger's `CTFontCreateVariantWithMatchingSymbolicTraits`), `CTFontCopyAvailableTables` (ATS's `ATSFontGetTableDirectory`, the same directory Leopard reads), `CTFontHasTable` (`CTFontCopyTable`), `CTFontGetGlyphsForCharacterRange` and `CTFontGetVerticalGlyphsForCharacters` (`CTFontGetGlyphsForCharacters`), `CTFontGetPhysicalSymbolicTraits`, `CTFontCopyPhysicalFont`, `CTFontCreateForCharactersWithLanguageAndOption` and its two siblings (`CTFontCreateForString`), `CTFontCreatePathForGlyph` (`CGFontGetGlyphPath`), `CTFontDescriptorCreateLastResort`, all four `CTFontManager*` font-loading entry points (`ATSFontActivateFromMemory`) |
| 2 | `CTFontCreateUIFontForLanguage`, `CTFontDescriptorCreateForUIType`, `CTFontDescriptorCreateWithTextStyle`, `CTFontDescriptorGetTextStyleSize`, `CTFontDescriptorCreateForCSSFamily`, `CTFontIsSystemUIFont`, `CTFontDescriptorIsSystemUIFont`, `CTFontGetUIFontType` — Leopard's `CTFontCreateUIFontForLanguage` is `CTFontDescriptorCreateForUIType` plus `CTFontCreateWithFontDescriptor`, and Leopard's `CTFontDescriptorCreateForUIType` builds its descriptor from a function-local static table of name and size with `CFStringHasPrefix` on the language. That is the same shape as the table here; only the values differ, and on 10.4 they are Lucida Grande's. Also `CTFontDrawGlyphs`, which is the documented set-font, set-size, show-glyphs sequence. |
| 3 | `CTFontGetVerticalTranslationsForGlyphs`, `CTLineGetTrailingWhitespaceWidth`, `CTLineGetBoundsWithOptions`, `CTFrameGetLineOrigins`, `CTFramesetterSuggestFrameSizeWithConstraints`, `CTRunGetBaseAdvancesAndOrigins` — reasons below. |

Three tier-3 cases are worth stating, because tiers 1 and 2 really were checked and
really were closed:

- `CTFontGetVerticalTranslationsForGlyphs`: Leopard's delegates to
  `TFont::GetVerticalTranslationsForGlyphs`, which calls `CGGetGlyphDeviceMetrics`,
  a CoreGraphics private Tiger's CoreGraphics does not export. So this reads the font's
  own `VORG` table, which is where a CJK font records per-glyph vertical origins, and
  falls back to the ascent only when there is no `VORG`. The horizontal half is half the
  advance either way.
- `CTLineGetTrailingWhitespaceWidth`: 10.5 does export this one, and Leopard's
  implementation calls `TLine::CountTrailingWhitespaceChars`. Tiger's CoreText contains
  that exact method, but as a **local** symbol, so it cannot be linked against, and
  `CTLine` never hands back its string. This walks the runs backwards instead, summing the advances of glyphs equal to
  the run font's space glyph. Verified on the box against a line with two trailing spaces.
- `CTLineGetBoundsWithOptions`: the obvious tier-1 source for the glyph-path and optical
  options is `CTLineGetImageBounds`, which on Tiger returns a constant. So the rect is
  built from `CTLineGetTypographicBounds` for every option, and the ink-bounds options are
  not honoured.

Bucket (d) is tier 3 where anything is returned at all, and the entry below says in each
case why tiers 1 and 2 are closed.

## (a) SPI or newer-macOS gates the port turns off — 18, nothing implemented

No live call site in this checkout (declared in `PAL/pal/spi/cf/CoreTextSPI.h`, named in
`Configurations/AllowedSPI-legacy.toml`, or mentioned only in a comment):

`CTFontCopyPhysicalFont`, `CTFontCreateForCSS`,
`CTFontCreatePhysicalFontDescriptorForCharactersWithLanguage`,
`CTFontGetUnsummedAdvancesForGlyphsAndStyle`, `CTFontManagerCreateFontDescriptorsFromData`,
`CTFontManagerEnableAllUserFonts`, `CTFontCreateForCharacters`,
`CTFontCreateForCharactersWithLanguage`, `CTFontGetLigatureCaretPositions` (comment only),
`CTFontTransformGlyphs` (comment only).

Behind a `HAVE()`/`ENABLE()` that must be `0` on this port:

| Function | Gate to turn off |
| --- | --- |
| `CTFontHasComplexColorFormatForGlyph` | `HAVE(CORE_TEXT_GLYPHHASCOMPLEXCOLOR_FUNCTION)` (already soft-linked) |
| `CTFontGetSbixImageSizeForGlyphAndContentsScale` | `HAVE(CORE_TEXT_SBIX_IMAGE_SIZE_FUNCTIONS)` |
| `CTFontManagerCreateMemorySafeFontDescriptorFromData` | `HAVE(CTFONTMANAGER_CREATEMEMORYSAFEFONTDESCRIPTORFROMDATA)` |
| `CTFontDrawImageFromAdaptiveImageProviderAtPoint` | `ENABLE(MULTI_REPRESENTATION_HEIC)` |
| `CTFontGetTypographicBoundsForAdaptiveImageProvider` | `ENABLE(MULTI_REPRESENTATION_HEIC)` |
| `CTFontDescriptorCreateWithTextStyleAndAttributes` | `ENABLE(ATTACHMENT_ELEMENT)` (`rendering/AttachmentLayout.mm` only) |
| `CTFontGetAccessibilityBoldWeightOfWeight` | accessibility bold-text, no Tiger equivalent |
| `CTFontDescriptorCreateForCSSFamily` | `SystemFontDatabaseCoreText` CSS generic-family path |

Every one of these still gets a trivial definition in `ctcompat.c` anyway, so the port
links whether or not the gate is flipped. Flipping the gate is strictly better, because
the stub is a lie (`false`, `0`, `CGRectZero`); the stubs are the safety net, not the plan.

**Three names in `missing-CT.txt` are false positives.** `CTRunGetGlyphsSpan`,
`CTRunGetAdvancesSpan` and `CTRunGetStringIndicesPtrSpan` are `static` helpers defined
inside `platform/graphics/coretext/ComplexTextControllerCoreText.mm`, not CoreText API.
They wrap `CTRunGetGlyphsPtr`/`CTRunGetAdvancesPtr`/`CTRunGetStringIndicesPtr`, all of
which Tiger exports. Nothing to do.

## (b) Public API with a direct Tiger equivalent under an older name — 11 thin wrappers

| WebCore calls | Tiger export | Note |
| --- | --- | --- |
| `CTFontCreateWithFontDescriptorAndOptions` | `CTFontCreateWithFontDescriptor` | options dropped |
| `CTFontDescriptorCreateWithAttributesAndOptions` | `CTFontDescriptorCreateWithAttributes` | options dropped |
| `CTFontDescriptorCreateCopyWithAttributes` | `CTFontDescriptorCopyWithAttributes` | null attributes = retain |
| `CTFontDescriptorCreateCopyWithFeature` | `CTFontDescriptorCopyWithFeature` | |
| `CTFontCopyDefaultCascadeListForLanguages` | `CTFontCopyDefaultCascadeList` | language list ignored |
| `CTFontDescriptorCreateMatchingFontDescriptors` | `CTFontDescriptorCopyMatchingFontDescriptors` | |
| `CTFontDescriptorCreateMatchingFontDescriptor` | same, take element 0 | |
| `CTFontGetGlyphCount` | `CTFontGetNumberOfGlyphs` | |
| `CTFontCopyFullName` | `CTFontCopyName(f, kCTFullNameKey)` | |
| `CTFontCopyGraphicsFont` | `CTFontGetGraphicsFont` + `CGFontRetain` | get vs copy |
| `CTFontManagerCopyAvailableFontFamilyNames` | `_CTFontDescriptorCopyAvailableFontFamilyNames` | leading underscore is Tiger's |

`CTFontDescriptorGetOptions` returns 0: Tiger has no descriptor options, and
`FontPlatformData` round-trips whatever it gets back through the wrapper above.

## (c) Implementable on Tiger CT + ATS + CG — 24 with real code

**Descriptors and the system font.** Tiger's `CTFontCreateUIFontForLocale` is an empty
stub, so there is no UI font API to forward to and no way to ask CoreText what the system
font is. `CTFontCreateUIFontForLanguage` and `CTFontDescriptorCreateForUIType` are built
from Apple's own table instead, decoded out of 10.5.8: a 32-entry array at `__DATA+0x460`
of records `{ int uiType; CFStringRef psName; float size; CFStringRef cssName; }`,
terminated by `-1`. Leopard's `CTFontCreateUIFontForLanguage` is a four-line wrapper over
a lookup in it, and its `CTFontDescriptorCreateForUIType` never reads the language
argument, so neither does this. All 27 rows plus the five 1000-series rows are
transcribed into `ctcompat.c`, and the test checks thirteen of them against the box.

Two things fell out of getting this right, both of which had been wrong:

- The table stores **PostScript** names, not family names. Using them means the bold rows
  need no symbolic-trait matching at all: `LucidaGrande-Bold` resolves directly, and the
  `CTFontCreateVariantWithMatchingSymbolicTraits` round-trip disappears from this path.
- **`kCTFontUIFontMenuItem` is 12 and `kCTFontUIFontLabel` is 10.** This header had them
  as 10 and 20, which is not a table problem but a constant problem: WebCore asks for
  `kCTFontUIFontMenuItem` in two places, and would have been handed the label font.
  The full 0..26 enum is now spelled out in both `CTCompat.h` and the overlay.

Outside the accepted ranges the reference returns NULL and so does this, with one
deliberate exception: types 27, 102, 103 and 104 are the italic and thin/light/ultralight
system faces, SPI that postdates the reference entirely. Tiger has no such faces, and
`SystemFontDatabaseCoreText` does reach them, so they fall back to the regular system
font rather than to nothing.
`CTFontDescriptorCreateLastResort` is `CTFontDescriptorCreateWithNameAndSize("LastResort", 0)`;
Tiger ships LastResort.
`CTFontDescriptorCreateCopyWithSymbolicTraits` realises the descriptor into a font, calls
Tiger's own `CTFontCreateVariantWithMatchingSymbolicTraits`, and takes the result's
descriptor. Merging the symbolic trait into `kCTFontTraitsAttribute` and copying, which is
the obvious implementation, does not work: Tiger's matcher ignores the trait and falls
back to the system font, so asking Helvetica for bold hands back Lucida Grande. Going
through the variant API gives Helvetica-Bold. When the family has no such face Tiger
returns NULL, where CoreText would too, but WebCore reads NULL as "descriptor unusable",
so the wrapper hands back the original and lets WebCore synthesise.
`CTFontDescriptorCreateWithTextStyle` / `CTFontDescriptorGetTextStyleSize` map the
`kCTUIFontTextStyle*` names onto a static macOS point-size table (Title0 26 … Caption2 10,
Headline semibold at 0.3, which is what the macOS metrics document; it was 0.4 here, which
is Bold). These are 10.9+ API with no reference implementation to match. The `lineSpacing`
out-parameter returns `size * 1.2`, an invented constant where real CoreText returns the
style's designed leading; no caller in this checkout reads it and return a system-font descriptor at that size; Tiger has no Dynamic Type
and no content-size category, so the size category argument is ignored.
`CTFontDescriptorCreateForCSSFamily` maps the six `kCTFontCSSFamily*` keys onto Times /
Helvetica / Courier / Apple Chancery / Papyrus / system font.
`CTFontIsSystemUIFont` and `CTFontDescriptorIsSystemUIFont` compare the family name against
the system font's, cached. `CTFontGetUIFontType` returns `kCTFontUIFontSystem` for those and
`kCTFontNoFontType` otherwise.

**Glyphs and metrics.** `CTFontGetGlyphsForCharacterRange` fills a UniChar buffer for the
range and calls `CTFontGetGlyphsForCharacters` in 256-character chunks; BMP only, it
returns false for a range crossing U+FFFF (glyph pages never do).
`CTFontGetVerticalGlyphsForCharacters` forwards to the horizontal call, because Tiger has no
vertical substitution. `CTFontGetVerticalTranslationsForGlyphs` returns
`(-advance.width / 2, -verticalOriginY)`, taking the origin from the font's `VORG` table
and falling back to the ascent when there is none.
`CTFontGetPhysicalSymbolicTraits` is `CTFontGetSymbolicTraits` (Tiger has no font
composition, so the physical font is the font).
`CTFontHasTable` is `CTFontCopyTable() != NULL`, with the integer tag converted to the
four-character CFString Tiger wants.
`CTFontCopyAvailableTables` reads the font's real sfnt table directory:
`CTFontGetPlatformFont` gives the `ATSFontRef` and `ATSFontGetTableDirectory` fills in the
offset table and its 16-byte records, which is the same directory Leopard's CoreText
parses. On Helvetica that returns 19 tables, including `fpgm`, `hdmx`, `mora`, `prop` and
`synh`, none of which a fixed probe list would have thought to ask for.
`CTFontCreatePathForGlyph` uses the undeclared-but-exported `CGFontGetGlyphPath`.
`CTFontDrawGlyphs` sets the graphics font and size on the context and issues one
`CGContextShowGlyphsAtPoint` per glyph, since Tiger has no `CGContextShowGlyphsAtPositions`.

**Web fonts.** `CTFontManagerCreateFontDescriptorFromData` activates the bytes with
`ATSFontActivateFromMemory(kATSFontContextLocal)`, finds the container's fonts with
`ATSFontFindFromContainer`, takes the PostScript name with `ATSFontGetPostScriptName` and
returns `CTFontDescriptorCreateWithNameAndSize(psName, 0)`. That is the whole trick: once
ATS knows the font, **real Tiger CoreText resolves the descriptor by name**, so every
descriptor operation WebCore performs afterwards, and `CTFontCreateWithFontDescriptor`
itself, works unmodified with no side table and no shimming of Tiger's own entry points.
`...FromURL`, `...FromData` (plural) and `CTFontManagerRegisterFontsForURL` are the same
path. The activated `CFData` is retained forever, since ATS reads the caller's memory for
the lifetime of the container; a page that loads many web fonts leaks their bytes.
`CTFontManagerCreateMemorySafeFontDescriptorFromData` is aliased to the ordinary one —
there is no hardened font parser on Tiger, which is exactly why the `HAVE()` should be off.

**Lines and frames.** `CTLineGetBoundsWithOptions` builds the rect from `CTLineGetTypographicBounds`, dropping
leading when `kCTLineBoundsExcludeTypographicLeading` is set. The ink-bounds options,
`UseGlyphPathBounds` and `UseOpticalBounds`, route to the `CTLineGetImageBounds` adapter,
which computes real ink bounds from glyph bounding rects; Tiger's own
`CTLineGetImageBounds` returns a constant and is no use. `ExcludeTypographicShifts` and
`UseHangingPunctuation` are still ignored, and WebCore passes neither.
`CTLineGetTrailingWhitespaceWidth` walks the runs backwards summing the advances of
glyphs that match the run font's space glyph.
`CTFrameGetLineOrigins` walks `CTFrameGetLines`, starting at the top of
`CGPathGetBoundingBox(CTFrameGetPath(frame))` and stepping down by ascent, then
descent + leading.
`CTFramesetterSuggestFrameSizeWithConstraints` lays the range out in a frame of the
constraint size (clamped, `CGFLOAT_MAX` in a `CGPath` upsets Tiger CT), sums line heights
and takes the widest line.
`CTRunGetBaseAdvancesAndOrigins` copies out of `CTRunGetAdvancesPtr` (`CTRunGetAdvances`
itself is one of the stubs) and zeroes the origins. That is safe rather than merely
convenient: Tiger's `CTRunGetStatus` sets bit 0 for right-to-left and bit 1 for
non-monotonic, and **never sets `kCTRunStatusHasOrigins`**, so WebCore's origins branch
can never be taken. `CTRunGetInitialAdvance` is `CGSizeZero` and
`kCTRunStatusHasOrigins` is never set.
`CTTypesetterCreateWithUniCharProviderAndOptions` drops the options dictionary onto
`CTTypesetterCreateWithUniCharProvider`. The one option WebCore passes is
`kCTTypesetterOptionForcedEmbeddingLevel`, so **RTL runs lose their forced direction** and
fall back to the Bidi algorithm's own guess for the substring.
`CTParagraphStyleSetCompositionLanguage` is a no-op; Tiger's paragraph style is immutable
and has no composition-language slot.

**Font fallback.** `CTFontCreateForCharactersWithLanguageAndOption` wraps Tiger's
`CTFontCreateForString`, then computes `coveredLength` by walking the characters through
`CTFontGetGlyphsForCharacters` on the result. The language and the fallback-option filter
(system vs user-installed) are ignored.

## (d) Genuinely infeasible — 6

| Function | Why | Consequence |
| --- | --- | --- |
| `CTFontShapeGlyphs` | The modern shaper does not exist in any form on Tiger. Its 10.4 shaping engine is ATSUI, reachable only through `CTTypesetter`/`CTLine`/`CTRun`. | **Called unconditionally** from `FontCoreText.cpp:665`, so the stub fills advances from `CTFontGetAdvancesForGlyphs`, zeroes the origins and returns `CGSizeZero`: correct for simple Latin, no ligatures/kerning/marks. Complex text must be routed through `ComplexTextController`, which uses `CTTypesetter` and does work on Tiger. This is the single biggest thing to gate. |
| `CTFontCopyGlyphCoverageForFeature` | Tier 1 is closed: Tiger CT has no such query. Tier 2 is closed too, and not by accident — Leopard's CoreText does not export this function at all, so there is no Apple implementation to match. A correct tier 3 means parsing `morx`/`GSUB` lookups and collecting their substitution outputs, which is the same shaper work as `CTFontShapeGlyphs` below. | Returns NULL, so synthesised small-caps coverage is empty and `font-variant: small-caps` falls back to scaled capitals. |
| `CTFontCopyColorGlyphCoverage`, `CTFontIsAppleColorEmoji` | Tiger has no color font formats at all: no sbix, no COLR, no CBDT, and no Apple Color Emoji. | Honest constants (NULL / false), not stubs. Emoji render as monochrome or as missing glyphs. |
| `CTFontGetSbixImageSizeForGlyphAndContentsScale`, `CTFontHasComplexColorFormatForGlyph` | Same. | 0 / false. |
| `CTFontDrawImageFromAdaptiveImageProviderAtPoint`, `CTFontGetTypographicBoundsForAdaptiveImageProvider` | Multi-representation HEIC is a 2023 feature over an image provider protocol that does not exist. | No-op / `CGRectZero`, behind `ENABLE(MULTI_REPRESENTATION_HEIC)`. |
| Variation axes | Tiger CT has `CTFontCopyVariationAxes`/`CTFontCopyVariation` but its ATS only understands TrueType GX variations, not OpenType 1.8 `fvar`/`STAT`. | `FontInterrogation` will report `TrueTypeGX` at best; variable web fonts render at their default instance. |

## Also missing: data symbols

`comm` on `logs/api/used-kCT.txt` against Tiger's exports lists 150 `kCT*` names, but most
are compile-time enumerators (`kCTFontTraitBold`, `kCTFontTableMATH`, `kCTFontUIFontSystem`,
`kCTLineBoundsExcludeTypographicLeading` …) that only need a declaration.
About 40 are real `CFStringRef`/`CGFloat` globals Tiger's CoreText does not export;
`ctcompat.c` defines them. Two are aliases rather than inventions —
`kCTFontURLAttribute` is Tiger's `kCTFontFileURLAttribute`, and `kCTFontVariationAxesAttribute`
is its `kCTFontVariationAttribute`. The rest are keys Tiger's CT has never heard of, so
their string values only have to be unique; CT ignores unknown attribute keys.
The `kCTFontWeight*`/`kCTFontWidth*` CGFloat constants use Apple's documented -1…1 scale
(Regular 0.0, Bold 0.4, …); the width values are the less certain of the two.

## Three CoreGraphics names that landed here

`CGFontCopyFamilyName`, `CGFontGetGlyphsForUnichars` and `CGFontGetGlyphAdvancesForStyle`
are CoreGraphics by name but absent from Tiger's CoreGraphics under those names, so they
are implemented in `ctcompat.c` rather than `cgcompat.c`, which then needs no CoreText.
WebCore declares all three in `PAL/pal/spi/cg/CoreGraphicsSPI.h` and calls none of them
in this checkout, so like the rest of bucket (a) they exist to keep the port linking if
that changes.

`CGFontCopyFamilyName` and `CGFontGetGlyphsForUnichars` are tier 1 through CoreText: wrap
the `CGFontRef` with `CTFontCreateWithGraphicsFont` and ask.

`CGFontGetGlyphAdvancesForStyle` is tier 1 through CoreGraphics itself, and is worth
reading as a warning about trusting a signature match. The leopard track identified
Tiger's `CGFontGetGlyphTransformedAdvances` as the older name for it, and the
disassembly agrees completely: six dword arguments at 0x8 through 0x1c, in the same
order, returning through `movzbl`. It looks like a pure rename. **It is not.** On the
box it dispatches through a slot in the font object and returns false for rendering
style 0, the unhinted style, which is the one WebCore asks for when it wants linear
advances. Asked for a hinted style it does answer, but with pixel-rounded numbers: 11.0
where the linear advance is 10.67. The implementation therefore calls it first, so a
caller asking for a hinted style gets genuinely hinted metrics, and falls back to
`CGFontGetGlyphAdvances`, which returns unscaled integer advances in font units and
always works, applying the transform locally. Only the linear part of the matrix
applies, since a size carries no translation. Verified on the box against
`CTFontGetAdvancesForGlyphs` for the same glyph at the same size.

This is the second time a name match has hidden a behaviour difference on this box, after
the ten in the section above. The rule that keeps falling out: on Tiger, check the
signature by disassembly and then check the behaviour by running it.

## Two habits that keep paying

**Write the whole buffer.** The three copying `CTRunGet*` adapters used to leave the
caller's buffer untouched when the pointer variant returned NULL, which is the *only*
case WebCore calls them in: `ComplexTextControllerCoreText.mm` takes the span and falls
back to the copying form precisely when its data is null, over a `Vector::grow` that does
not zero POD elements. So the early return handed back uninitialized heap, and for string
indices those are values used to index into the character buffer. Of the three, only
indices can really come back NULL — Tiger's glyph and advance accessors are plain pointer
arithmetic, while `GetStringIndices` dispatches through a virtual — but all three now
write every element. Indices are rebuilt from `CTRunGetStringRange`, ascending for LTR and
descending for RTL, and only when the run is monotonic, which Tiger does report.

**Return the error value the caller tests for.**
`CTFontGetBoundingRectsForGlyphs` returned `CGRectZero` on failure where real CoreText
returns `CGRectNull`. Callers tell them apart with `CGRectIsNull`, and `CGRectZero` is a
perfectly valid empty rect at the origin.

Two placement approximations are documented in the source rather than fixed, because
tier 1 has nothing better and neither is reachable today. `CTRunDraw` accumulates from the
context's text position, where real CoreText reads the run's own positions and ignores the
text position entirely; Tiger exports no `CTRunGetPositions` and its `TRun::GetPositions`
is a local symbol, so the adapter needs the text position set **per run**. And
`CTLineGetImageBounds` accumulates its pen across runs, which assumes visual
left-to-right layout and misplaces the ink of an RTL run.

## Do not trust the count files

`logs/api/used-CT.txt` and `used-kCT.txt` count **identifier occurrences, not calls**, and
they are wrong in both directions. A function declared once in
`PAL/pal/spi/cf/CoreTextSPI.h` and called nowhere still shows a count of 1, which is why
so much of bucket (a) looked live; and cgcompat found the same files under-report too,
with `CGGradientCreateWithColorComponents` having a real call site while appearing in
`used-CG.txt` not at all. Anyone working off those lists should re-derive from call sites
first.

This survey was re-verified that way rather than from the counts. Walking every `.cpp`,
`.mm`, `.m` and `.h` under `WebKit/Source` except `CoreTextSPI.h`, taking every
`CT[A-Z]...(` as a call site and every `kCT...` as a reference, and subtracting Tiger's
exports and what the compat layer provides:

- **Functions: no gap.** The 66 in `missing-CT.txt` are the complete set. The only three
  names left over are `CTFont` (a C++ type), `CTM` (a local in `DrawGlyphsRecorder.cpp`)
  and `CTZ` (count-trailing-zeros in JavaScriptCore's ARM64 disassembler), none of which
  is CoreText.
- **Constants: 14 were missing**, all real references, none of them in `missing-CT.txt`
  because they are compile-time enumerators rather than exported symbols, so they would
  have surfaced as compile errors deep into the WebCore build rather than as link
  failures. They are the older `kCTFontBoldTrait`/`kCTFontItalicTrait`/
  `kCTFontMonoSpaceTrait`/`kCTFontColorGlyphsTrait` spellings of the symbolic traits, the
  two SPI trait bits `kCTFontTraitEmphasized` and `kCTFontTraitTightLeading`,
  `kCTFontPaletteLight`/`Dark`, two `CTFontTextStylePlatform` members, the
  `CTLineBreakMode` truncation modes, `kCTLineTruncationMiddle` and
  `kCTParagraphStyleSpecifierLineBreakMode`. All are now in `CTCompat.h`.

The paragraph-style specifier numbering was checked rather than assumed: Tiger's and
Leopard's `CTParagraphStyleGetValueForSpecifier` both bound the specifier at 13, which is
`kCTParagraphStyleSpecifierBaseWritingDirection`, the last value the 10.5 header defines.
So Tiger uses the documented 10.5 numbering and `...LineBreakMode` is 6. Re-running the
audit now reports zero unresolved names of either kind.

## The SDK overlay

`compat/sdk-overlay/CoreText.framework/Headers/` is the CoreText header set, seventeen
files, all ours, because Tiger ships none. Layout and per-file notes are in
`compat/sdk-overlay/README.md`; `CTDefines.h` is the one to read first.

Only six Tiger exports actually need the `double` treatment, and they are all the font
`size` parameter: `CTFontCreateWithName`, `CTFontCreateWithFontDescriptor`,
`CTFontCreateWithGraphicsFont`, `CTFontCreateCopyWithAttributes`,
`CTFontCreateWithPlatformFont` and `CTFontDescriptorCreateWithNameAndSize`. Sixteen
exports take a by-value double in total, but for the other ten the modern header already
says `double` — `CTTypesetterSuggestLineBreak`, `CTLineCreateTruncatedLine`,
`CTTextTabCreate` and friends — so Tiger and Apple agree and nothing needs doing.
Scalar *returns* never need adapting either: on i386 they come back in `ST(0)`, so a
`CGFloat` return reads correctly whether the callee computed a float or a double.

**The overlay declares no SPI, by design.** WebCore declares CoreText SPI for itself in
`PAL/pal/spi/cf/CoreTextSPI.h`. The two are meant to be included together, so the overlay
must not define anything that header's non-internal-SDK branch defines. Checked
mechanically: they now share **no type and no enumerator**. The six types that would have
collided are `CTCompositionLanguage`, `CTFontDescriptorOptions`, `CTFontFallbackOption`,
`CTFontShapeOptions`, `CTFontTextStylePlatform` and `CTFontTransformOptions`, along with
27 enumerators including `kCTFontTraitEmphasized`, `kCTRunStatusHasOrigins` and the
`kCTFontTextStylePlatform*` set. All of them are SPI and all now live only in
`CoreTextSPI.h`. Duplicate *function* and *constant* declarations are legal when the
signatures match, so those overlap harmlessly.

### For the wkcmake track

Nothing in `CoreTextSPI.h` needs a `PLATFORM(TIGER)` gate to avoid conflicting with the
overlay. What it does need:

- `USE(APPLE_INTERNAL_SDK)` must be **off**, so its `#else` branch is the one that
  compiles. That branch is what supplies the SPI types the overlay deliberately omits.
- The `HAVE()` and `ENABLE()` gates listed in section (a) above should be **0**. They are
  what turn off sbix, color-glyph, memory-safe-parser and multi-representation-HEIC code
  paths whose shims are honest stubs rather than implementations.
- `CTFontShapeGlyphs` is the one SPI declaration that matters most: it is called
  unconditionally and its shim does no shaping. Complex text has to route through
  `ComplexTextController`.
- The overlay needs `-F compat/sdk-overlay` ahead of the SDK, and the ApplicationServices
  sub-framework directory also on `-F` so `<ATS/SFNTLayoutTypes.h>` resolves from
  `<CoreText/SFNTLayoutTypes.h>`. `compat/Makefile` shows both.

## Leopard's CoreText cannot be used directly

Worth recording so nobody tries it: the leopard track confirmed that Leopard's CoreText
binary will not run on Tiger, because it has CoreFoundation 476's object layout inlined
into it. So the seventeen functions it adds over Tiger's are genuinely absent from this
machine and have to be written, not borrowed. `refs/leopard/keep/CoreText-10.5.8.i386`
stays useful as a disassembly reference for Apple's algorithms, which is how the tier-2
implementations here were derived, but it is a reference and not a shippable library.

## Open items for the port

- ~~Nothing stages CoreText headers.~~ Done: see the SDK overlay section above. Note that
  the 10.5 SDK's CoreText headers were **not** used, and must not be: they spell the font
  size `CGFloat`, which is wrong for this binary.
- `FontCustomPlatformDataCoreText.cpp` reaches `CTFontManagerCreateFontDescriptorFromData`
  only after `FPFontCreateFontsFromData` / `FPFontCopySFNTData`. Those are the `fparse`
  font-parser SPI, a different library, and are not in `missing-CT.txt` or in scope here.
  Something has to shim `FPFont*` as well, or that path has to be rewritten to hand the
  raw `SharedBuffer` straight to `CTFontManagerCreateFontDescriptorFromData`.
- Link flag: the CoreText binary lives inside ApplicationServices, so linking wants
  `-F sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks
  -framework CoreText`, or just `-framework ApplicationServices` plus the sub-framework path.
