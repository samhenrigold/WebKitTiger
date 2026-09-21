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

**Nine exports do nothing.** Seven have a body of exactly `xor eax, eax; ret`:
`CTFontCreateUIFontForLocale`, `CTFontCreateWithQuickdrawNameAndStyle`, `CTRunGetGlyphs`,
`CTRunGetAdvances`, `CTRunGetStringIndices`, `CTRunDraw` and `CTRunGetEmbeddedObject`.
Only the `Ptr` variants (`CTRunGetGlyphsPtr`, `CTRunGetAdvancesPtr`,
`CTRunGetStringIndicesPtr`) return real data, which is lucky, because
`ComplexTextControllerCoreText.mm` already prefers them.

The other two are `CTLineGetImageBounds` and `CTRunGetImageBounds`, which never look at
their line or run: each copies a fixed global rect into its struct return and comes back.
Anything wanting ink bounds on Tiger has to compute them from glyph bounding rects
itself, which `runInkBounds` in `ctcompat.c` does for both.

`CTRunGetEmbeddedObject` is the only one of the nine with no adapter, because nothing in
WebCore calls it and there is nothing sensible to return: embedded objects are a
`CTGlyphInfo` feature the port does not use.

**Five take different arguments** from the modern API of the same name:

| Function | Tiger's real signature |
| --- | --- |
| `CTFontCopyTable` | `(CTFontRef, CFStringRef tableName)` — the four-character tag as a **CFString**, and no options argument. Tiger's own `kCTFontTableGSUB` is the string `"GSUB"`. |
| `CTFontGetAdvancesForGlyphs` | `CGSize (CTFontRef, const CGGlyph[], CGSize[], CFIndex)` — **no orientation**, and the summed advance comes back as a CGSize rather than a double. |
| `CTFontGetBoundingRectsForGlyphs` | `CGRect (CTFontRef, const CGGlyph[], CGRect[], CFIndex)` — no orientation. |
| `CTLineGetTypographicBounds` | `double (CTLineRef, CFRange, CGFloat*, CGFloat*, CGFloat*)` — takes a **CFRange** the modern four-argument form does not. The range is only checked against the glyph count, so `{0, 0}` is always safe; calling it the modern way puts the ascent pointer where the range goes and the call quietly returns 0. |
| `CTRunGetImageBounds` | Exported, and its body copies a fixed global into the struct return without reading the run. Same shape as `CTLineGetImageBounds`. No call site in tree, but the overlay would otherwise have declared it as working. |
| `CTLineDraw` | `void (CTLineRef, CGContextRef, CFRange)` — also takes a **CFRange**. It compares `location + length` against the line's glyph count with an integer `cmpl` and draws nothing when the sum is larger, so a modern two-argument call passes stack junk as the range and draws the line only when that junk happens to be `{0, 0}` or `{0, count}`. Five call sites in tree. |

These thirteen are **not** in `missing-CT.txt`, because they are exported under exactly the
name WebCore calls. They are the worst kind of problem: they link, they run, and they
return zeroes.

**This is now handled, and WebCore needs no edits.** `ctcompat.c` carries a
`TigerCT...`-prefixed adapter for each of the thirteen, presenting the modern signature and
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
it, read out of `refs/leopard/CoreText.i386` (10.5.8) or
`refs/snowleopard-10.6.3/.../CoreText` (10.6.3); **tier 3** means neither was reachable
and this is the best version that could be written on what Tiger has.

10.6.3 matters because it is the first CoreText with `CTFontManager*`, so it is the
reference for the web-font path. Checked against it, and **absent even there**:
`CTLineGetBoundsWithOptions`, `CTFontGetUnsummedAdvancesForGlyphsAndStyle`,
`CTRunGetBaseAdvancesAndOrigins`, `CTFontCopyDefaultCascadeListForLanguages`,
`CTFontManagerCreateFontDescriptorFromData` and `CTFontCopyGlyphCoverageForFeature`.
Those are tier 3 with no Apple implementation in existence to match, which is now a
checked fact rather than an assumption.

Everything in bucket (b) is **tier 1** by definition: each one forwards to a Tiger export
under its older name.

Bucket (c), by tier:

| Tier | Functions |
| --- | --- |
| 1 | `CTFontDescriptorCreateCopyWithSymbolicTraits` (Tiger's `CTFontCreateVariantWithMatchingSymbolicTraits`), `CTFontCopyAvailableTables` (ATS's `ATSFontGetTableDirectory`, the same directory Leopard reads), `CTFontHasTable` (`CTFontCopyTable`), `CTFontGetGlyphsForCharacterRange` and `CTFontGetVerticalGlyphsForCharacters` (`CTFontGetGlyphsForCharacters`), `CTFontGetPhysicalSymbolicTraits`, `CTFontCopyPhysicalFont`, `CTFontCreateForCharactersWithLanguageAndOption` and its two siblings (`CTFontCreateForString`), `CTFontCreatePathForGlyph` (`CGFontGetGlyphPath`), `CTFontDescriptorCreateLastResort`, all four `CTFontManager*` font-loading entry points (`ATSFontActivateFromMemory`) |
| 2 | `CTFontCreateUIFontForLanguage`, `CTFontDescriptorCreateForUIType`, `CTFontDescriptorCreateWithTextStyle`, `CTFontDescriptorGetTextStyleSize`, `CTFontIsSystemUIFont`, `CTFontDescriptorIsSystemUIFont`, `CTFontGetUIFontType` — Leopard's `CTFontCreateUIFontForLanguage` is `CTFontDescriptorCreateForUIType` plus `CTFontCreateWithFontDescriptor`, and Leopard's `CTFontDescriptorCreateForUIType` builds its descriptor from a function-local static table of name and size with `CFStringHasPrefix` on the language. That is the same shape as the table here; only the values differ, and on 10.4 they are Lucida Grande's. Also `CTFontDrawGlyphs`, which is the documented set-font, set-size, show-glyphs sequence. |
| 3 | `CTFontGetVerticalTranslationsForGlyphs`, `CTLineGetTrailingWhitespaceWidth`, `CTLineGetBoundsWithOptions`, `CTFrameGetLineOrigins`, `CTFramesetterSuggestFrameSizeWithConstraints`, `CTRunGetBaseAdvancesAndOrigins` — reasons below. |

### Moved up a tier after checking 10.6.3 and Tiger's own resources

- `CTFontDescriptorCreateForCSSFamily` was tier 2 and is now **tier 1**. Tiger ships
  `CoreText.framework/Resources/DefaultFontFallbacks.plist`, keyed by CSS generic family
  with per-language alternatives, and exports `CTFontDescriptorCreatePerLanguageAndCSSKey`
  to read it. Its CSS key constants are the literal strings `serif`, `sans-serif` and so
  on, which is exactly what WebCore passes, so the key goes straight through. The
  hardcoded table it replaces was wrong more often than right: Apple maps sans-serif to
  **Lucida Grande**, not Helvetica; monospace to **Monaco**, not Courier; and fantasy to
  **Zapfino**, not Papyrus. It also ignored the language, where the real table answers
  serif/ja with HiraMinPro-W3.
- `CTFontCopyDefaultCascadeListForLanguages` was tier 1 but dropped the language list on
  the floor. Tiger's own `CTFontCopyDefaultCascadeList` answers for the current locale
  only, but the per-language fallbacks are in that same plist, so the requested languages
  now go in front of the locale's list in the order asked for. With `ja` the list leads
  with AquaKana-HiraKaku. This is what the function is for in
  `SystemFontDatabaseCoreText`: CJK fallback ordering for the page's language rather than
  the user's.
- `CTFontGetVerticalTranslationsForGlyphs` stays tier 3, but now follows 10.6's
  fallback. Both Apple versions reach for a CoreGraphics private Tiger lacks (10.5
  `CGGetGlyphDeviceMetrics`, 10.6 `CGFontGetGlyphVerticalOffsets`), but what 10.6 does
  when that fails is reachable: it derives the origin from the glyph's bounding box. So
  the order is now VORG, which is authoritative where a CJK font provides it, then the
  bounding-box top via `CTFontGetBoundingRectsForGlyphs`, then the ascent. For Helvetica
  'A' that moves the origin from the font ascent to the glyph's own cap height.

Three tier-3 cases are worth stating, because tiers 1 and 2 really were checked and
really were closed:

- `CTFontGetVerticalTranslationsForGlyphs`: Leopard's delegates to
  `TFont::GetVerticalTranslationsForGlyphs`, which calls `CGGetGlyphDeviceMetrics`,
  a CoreGraphics private Tiger's CoreGraphics does not export. So this reads the font's
  own `VORG` table, which is where a CJK font records per-glyph vertical origins, and
  falls back to the ascent only when there is no `VORG`. The horizontal half is half the
  advance either way.
- `CTLineGetTrailingWhitespaceWidth`: 10.5 and 10.6 both export this one, and both
  implement it by calling `TLine::CountTrailingWhitespaceChars`. Tiger's CoreText contains
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
| `CTFontShapeGlyphs` | The modern shaper does not exist in any form on Tiger. Its 10.4 shaping engine is ATSUI, reachable only through `CTTypesetter`/`CTLine`/`CTRun`. | **Called unconditionally** from `FontCoreText.cpp:665`, so the stub fills advances from `CTFontGetAdvancesForGlyphs`, zeroes the origins and returns `CGSizeZero`: correct for simple Latin, no ligatures, kerning or marks. Routing complex text through `ComplexTextController` instead is the single biggest thing to gate, and it buys more than it looks like. See below. |
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

## Checked against a live Apple CoreText

`spike/ctoracle.c` runs the shims against a real Apple implementation **on the box**.
Leopard DP1 (9A241) CoreText loads on 10.4.11 with a patched import table and a
CF-bridge bootstrap; the recipe is in `refs/leopard-9a241/tools/`. It is a
Tiger-generation binary with the real public CGFloat ABI and it exports 13 of the 66
functions here, which makes it an oracle rather than a dependency: it is pre-release,
it puts two CoreTexts in one process, and objects cannot cross between them, so every
comparison keeps each side's objects on its own side and diffs only values.

**56 comparisons, 0 unexplained mismatches** (8 before the two test bugs below were
fixed and the rest were chased down). What it confirms:

- **All 21 UI font types match Apple exactly**, by PostScript name. That is the
  strongest possible check on the table decoded from 10.5.8, and it is now verified
  against a running implementation rather than a data dump.
- `CTLineGetTypographicBounds` through the adapter, `CTLineGetBoundsWithOptions`, and
  `CTLineGetTrailingWhitespaceWidth` all agree with Apple's numbers. The last of those
  was written from scratch, so agreement on a line with two trailing spaces is worth
  more than the rest.
- Full names, glyph counts, graphics fonts, descriptor matching, copy-with-attributes
  and `CTFontDescriptorCreateForUIType` all agree.

Two differences were chased down and are **not** defects here, so the oracle records
them as known rather than counting them:

- **Bold via symbolic traits.** 9A241 returns NULL from
  `CTFontDescriptorCreateCopyWithSymbolicTraits` for Helvetica plus bold, though the
  same descriptor resolves to plain Helvetica. Ours returns Helvetica-Bold, through
  Tiger's `CTFontCreateVariantWithMatchingSymbolicTraits`. We are ahead of the oracle.
- **Cascade list length, 7 against 6.** Tiger's font catalogue lists `AquaKana` and
  `HiraKakuPro-W3` separately where DP1 has the merged `AquaKana-HiraKaku`. The entries
  either side of that pair are identical. Ours faithfully returns Tiger's own list.

### What the oracle cannot see

A clean oracle run means the shim is faithful **to the era**, not that it is correct
today, and the two come apart whenever Apple changed a behaviour after DP1. Cap height is
the measured case: 9A241 returns the same quantised values as unfixed Tiger, not merely
the same direction but the same digits at every size, 7.0 at 9pt where modern says 6.618.
Ascent agrees across all three, so this is specific to the quantised metrics rather than a
general disagreement between the eras. The oracle compared cap height happily and found
nothing, because both implementations sat on the same side of a change that came later.

So the two runtime checks have different ranges, and neither subsumes the other.
`spike/ctoracle.c` answers whether a shim matches a real Apple CoreText of Tiger's
generation, which is the right question for anything where Tiger-era semantics are what
WebCore's fallback paths expect. `spike/ctprobe.c` answers whether it matches CoreText
today, which is the right question for anything feeding layout. A divergence introduced
after DP1 is invisible to the first and visible to the second.

Three things the oracle simply cannot answer, marked n/a: its `CTFontCreateForCharacters`
is an empty stub, so it has no CJK fallback to compare against (ours returns
HiraKakuPro-W3), and its advances entry point returns NaN for fonts created on its own
side, which is a limitation of running it outside its own CoreFoundation.

**Two of the original eight mismatches were bugs in the test, not the shims**, and both
are worth recording because they are the same trap this port keeps hitting: 9A241's
plain `CTFontGetAdvancesForGlyphs` is five-argument while Tiger's is four, and its
`CTFontCreateForCharacters` looks like a working export but is a stub. Guessing the arity
crashed the process.

### The screen, as a tool

The technique that found `CTLineDraw` is now `spike/abi-screen.py`, and it covers all
three ways a Tiger export can link cleanly and still not work: an empty body, a body that
returns a global without reading its arguments, and a signature that differs from the
modern prototype. Run it as `spike/abi-screen.py CT CG CF`.

Over the 436 functions WebCore calls that Tiger exports it reports, with no false
positives: **CoreText** has the problems catalogued above and nothing else;
**CoreGraphics** (200 compared) and **CoreFoundation** (188 compared) have **none at
all**. That negative result is worth as much as the findings. The CoreGraphics trouble on
this port has been behavioural, not structural, which is the one mode a static screen
cannot see: `CGShading` matches its modern signature and reads every argument, and still
drops the alpha component. That kind needs a runtime probe, which is what
`spike/ctoracle.c` is for on the CoreText side.

The two functions the screen flags in CoreText that are **not** findings are
`CTFontCreateWithName` and `CTFontCreateWithGraphicsFont`, which read one slot more than
the modern prototype. That is the by-value `double` size showing up, and the overlay
already declares both correctly.

### The TRANSITIONAL cross-check

9A241 exports 11 `*TRANSITIONAL` entry points, which is Apple's own record of which
functions changed shape in the double-to-CGFloat migration. Five of them are functions
this layer already adapts: `CTFontGetAdvancesForGlyphs`,
`CTFontGetBoundingRectsForGlyphs`, `CTLineGetTypographicBounds`, `CTLineDraw` and
`CTLineGetImageBounds`. That is independent confirmation from Apple that those five
needed adapting.

The list also caught one this layer had **wrong**:
`CTFontGetSideBearingsForGlyphs` is on it, and Tiger's takes no orientation, but the
overlay was declaring the modern five-argument form. It is now the twelfth adapter.
Nothing in WebCore calls it today, so it was a latent trap rather than a live bug.

Of the rest of the list, `CTFontCopyDefaultCascadeList` was re-checked and really is
one-argument on Tiger, so the direct call is correct;
`CTFontGetTransformedAdvancesForGlyphs`, `CTFontGetTransformedBoundingRectsForGlyphs`
and `CTFontCollectionCreateWithFilterCallback` have no WebCore call site and are not
declared in the overlay; and `CTFontDescriptorCreateForUIType` does not exist on Tiger
at all, so it is implemented here rather than adapted.

## Tiger's shaping is better than "no shaping"

Measured, not assumed: `spike/ctshape.c` prints the raw `cmap` glyphs beside the glyphs a
line actually produced, per font and per sample, so substitution is visible rather than
inferred. The boundary is sharper than "AAT yes, OpenType no".

**Tiger shapes AAT fonts identically to modern CoreText, and applies OpenType `liga`
ligatures identically too. What it does not do is OpenType complex-script joining.**
Arabic through Geeza Pro, an AAT font with `morx`, comes out not merely close but
identical on both machines: same five glyph ids, same advances, same right-to-left status
bit. The same text through an OpenType-only font gets the isolated forms, correctly
ordered and correctly reversed, but never the initial, medial and final variants, because
Tiger's ATSUI reads AAT `morx`/`mort` and that font has neither.

This matters for how much of the text stack has to be written off, and the answer is: much
less than the `CTFontShapeGlyphs` stub suggests. Of the 49 fonts Tiger ships, 41 carry AAT
shaping tables, 8 carry `GSUB`, 2 carry both, and 2 carry neither. The overwhelming
majority of the system's own fonts shape correctly, **provided text goes through
`CTTypesetter` rather than the shaping stub**.

Two caveats worth carrying forward. None of Tiger's six Hiragino CJK faces has `morx`;
they are `GSUB`/`GPOS` only, which matters less than it sounds because CJK is largely a
one-to-one mapping, but vertical forms and ruby do need substitution and will not get it.
And for **web fonts specifically**, an OpenType-only face gets correct Latin and correct
ligatures and no Arabic joining, which is the common case for a downloaded font.

## Tiger's metrics are quantised: cap height and x-height

The static screens say a function links and takes the right arguments; the oracle says it
returns the same values. A third question is whether a function that passes both is still
*accurate*, and `spike/ctprobe.c` answers it by running the same font bytes through Tiger
and through modern CoreText and diffing.

Across the 47 functions WebCore uses directly, ascent, descent, leading, underline
position and underline thickness agree to six decimal places at every size. **Cap height
and x-height do not.** Tiger quantises both to a half-point step: at 9pt it answers 7.0
for a cap height whose real value is 6.618, and at 12pt it answers 7.0 for an x-height
whose real value is 6.639. Worst case 5.8% out. It is not a rounding of the linear value
either, since the nearest half-point to 6.618 is 6.5; the shape is consistent with
measuring a grid-fitted outline at each pixel size. Both feed `FontMetrics`, so this was
the CSS `ex` unit and `vertical-align: middle` carrying a silent five percent error.

Both are now adapters. OS/2 version 2 and later carry `sCapHeight` and `sxHeight` for
exactly this purpose, so those get scaled by size over units per em. Older fonts have no
such fields, so the fallback measures glyphs.

**Which glyphs, and how, is the interesting part.** Measuring the flat `H` and flat `x`
alone, the conventional definition, leaves a constant 0.86% and 1.15% under modern at
every size. That constant is the tell: modern averages the flat and round extremes rather
than discarding the round ones' overshoot. On DejaVu the flat capitals measure 1493 units
and the round 1520, and modern reports 1506, their midpoint to the unit; x-height is 1120
flat and 1147 round, and modern reports 1133. Averaging `H` with `O`, and `x` with `o`,
reproduces modern's numbers **exactly at nine of ten probe sizes**, the tenth being 100pt
where it is 0.03% out. From 5.8% to 0.03%.

That is one font's worth of evidence for the mechanism, but it is ten independent sizes
agreeing to within a 64th of a point, and the definition it implies, that half the
overshoot counts, is a sensible one rather than a curve fit. A font whose round glyphs do
not overshoot gets the same answer either way, so the change cannot make one worse.

A second controlled case has since turned up: **Courier has no OS/2 table on either
Tiger or modern macOS**, so both sides have to compute the metric rather than read it.
They agree to 0.04%, 9.395 against 9.391. That is the closest thing to a direct test of
the rule, because it is the one font where modern CoreText is running the same fallback we
are.

### Why not Tiger's NSFont

It is the obvious alternative and it looks good at one size, so the measurement is worth
recording. For Helvetica at 16pt, `[NSFont capHeight]` gives 11.500 against modern's
11.477, where this heuristic gives 11.633. Nearly seven times closer.

It does not survive a size sweep, and it does not survive a second font. **Tiger's NSFont
quantises to a half-point grid**: Helvetica cap height is 7.0 at 9pt, 8.0 at both 10 and
11pt, 9.0 at 12pt, 73.0 at 100pt. The 16pt agreement is the one size where the grid lands
near the true value, and 16pt is where it was measured.

Measured properly, with **identical font bytes on both machines** so that no part of the
difference is one font file against another, four fonts at eight sizes for both metrics,
64 comparisons, error against modern CoreText:

| font | metric | adapter mean | adapter max | NSFont mean | NSFont max |
|---|---|---|---|---|---|
| Arial | cap | 0.000% | 0.000% | 1.92% | 4.70% |
| Arial | x | 0.000% | 0.000% | 3.57% | 7.14% |
| Georgia | cap | 0.000% | 0.000% | 2.02% | 4.97% |
| Georgia | x | 0.000% | 0.000% | 4.33% | 7.69% |
| Verdana | cap | 0.000% | 0.000% | 2.37% | 6.98% |
| Verdana | x | 0.000% | 0.000% | 2.06% | 6.95% |
| DejaVu Sans | cap | 0.033% | 0.034% | 2.29% | 5.77% |
| DejaVu Sans | x | 0.044% | 0.045% | 1.83% | 5.44% |

Worst case: **adapter 0.05%, NSFont 7.69%.** The adapter is exact wherever the font
declares `sCapHeight` and `sxHeight`, because it reads them; only DejaVu, whose OS/2 is
version 1, falls through to the glyph heuristic.

**Nothing in WebCore reads NSFont for either metric**, which was worth checking, because
if layout took cap height from the platform font on some paths and from CoreText on
others, the two sources disagreeing would show up as text looking subtly wrong rather
than as a failure. It does not: `FontMetrics::capHeight()` and `xHeight()` are fed from
CoreText, and `FontPlatformData` is built from a `CTFontRef` throughout. The only direct
NSFont metric reads in the tree are `[font ascender]` and `[font descender]` in
`WebKitNSStringExtras.mm`, for placing a drawn string, and those two agree exactly with
CoreText on Tiger. Everything else that touches an NSFont takes `[font pointSize]` and
bridges straight to a `CTFontRef`. So these adapters are the only source of cap height and
x-height on this port, and their accuracy is the whole story.

That also answers what NSFont reads, without needing a disassembly. Arial, Georgia and
Verdana all carry valid OS/2 version 2+ fields, which is why the adapter is exact on them
to four decimal places. **NSFont is still 2-7% out on those same fonts**, so it is not
reading the table; it is measuring a rasterised outline, which is why the values land on
a half-point grid. Reading the field directly in C, which is what these adapters already
do, is strictly better than going through AppKit and gets the AppKit dependency out of a
CoreText compat library for free. Not taken.

One methodological note, because the comparison is easy to set up wrongly. **Tiger's
Helvetica is not modern macOS's Helvetica.** Tiger's has no usable OS/2 table at all,
where modern's is version 3 with `sCapHeight` declared, and modern simply reads it:
1469/2048 x 16 is 11.477 exactly. Comparing metrics for a font *name* across the two
machines measures the difference between two font files as much as between two
implementations. That is why `spike/ctprobe.c` ships its own font and hands identical
bytes to both sides, and why Courier above is worth more than Helvetica.

### Three smaller behavioural divergences

- `CTFontCopyAttribute` with `kCTFontSizeAttribute` answered NULL on Tiger at every size,
  where `CTFontGetSize` is correct on both. Now an adapter that fills it in.
- `CTFontDescriptorCopyAttribute` answered NULL for family, style and traits, which
  WebCore reads in four places. Now an adapter that realises the descriptor and asks the
  font. It deliberately does not change how descriptors *resolve*, because the web-font
  path depends on Tiger matching them by name.
- `CTFontGetSymbolicTraits` returns `0x80000000` where modern returns `0x0`. That is the
  class field at bits 28 to 31, which Tiger fills from the OS/2 family class and modern
  leaves empty. Harmless here: every WebCore use masks for a specific bit, and a check
  confirmed there is no whole-word comparison or `traits == 0` test anywhere.
- `CTLineCreateTruncatedLine` returns NULL on Tiger for every width when the truncation
  token is NULL, where modern returns a line whenever the text fits. With a real ellipsis
  token the two agree exactly. The one call site is behind `ENABLE(ATTACHMENT_ELEMENT)`,
  which is off.

### The Ptr accessors invert between platforms

Modern CoreText returns NULL from `CTRunGetGlyphsPtr`, `CTRunGetAdvancesPtr` and
`CTRunGetStringIndicesPtr` for ordinary runs; Tiger returns real pointers. So
`ComplexTextControllerCoreText.mm` always takes its copying fallback on modern and always
takes the pointer path on Tiger. **Whichever machine WebKit gets tested on, the other path
is the untested one**, and on Tiger the copying variants are the adapters over Tiger's
empty stubs. `spike/cttest.c` exercises the copying adapters explicitly, including the
poisoned-buffer check, precisely because normal Tiger use will never reach them.

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

## The web-font path diverges from 10.6 in one visible way

10.6.3 is the reference for this path, and its
`CTFontManagerCreateFontDescriptorsFromURL` builds each descriptor straight from a
`CGFont` carrying an `is_unregistered_t` tag: the font is never registered and stays
invisible to font enumeration. Tiger has no descriptor that can wrap a `CGFont`, so ATS
activation is the only way to make a descriptor resolvable at all.

The consequence is a real behavioural difference, not just an implementation one. A web
font loaded on Tiger becomes visible process-wide: it will appear in
`CTFontManagerCopyAvailableFontFamilyNames` and can shadow an installed family of the
same name.

The same property turns out to be useful in a way it was not designed for. Loading a font
through `CTFontManagerCreateFontDescriptorFromData` makes it resolvable by PostScript name
to *any* CoreText in the process, including the 9A241 build used as an oracle, which has
no `CTFontManager` of its own at all. That is how the metric comparison above got the same
font bytes into both implementations. ATS offers no unregistered-but-resolvable mode to avoid it. Worth knowing
before debugging a page whose `@font-face` named "Arial" appears to affect unrelated text.

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
