/* TIGER: CoreText entry points Mac OS X 10.4.11 lacks.
 *
 * This is the IMPLEMENTATION-SIDE header. It declares Tiger's CoreText the way
 * the 10.4.11 binary really is: by-value doubles, CTFontCopyTable keyed on a
 * CFString, CTLineGetTypographicBounds taking a CFRange. compat/ctcompat.c is
 * built against it and nothing else.
 *
 * **Do not include this together with <CoreText/CoreText.h>.** The SDK overlay
 * at compat/sdk-overlay/CoreText.framework is the CONSUMER-SIDE header: it
 * declares the same functions with their modern prototypes and asm-labels the
 * mismatched ones onto the adapters in ctcompat.c. The two headers contradict
 * each other on purpose, which is the entire mechanism. WebCore gets the
 * overlay; only the compat layer gets this file.
 *
 * Classification of all 66 missing names, the disassembly the ABI came from,
 * and the tier each function landed in are in compat/CT-SURVEY.md.
 */

#ifndef TIGERCOMPAT_CTCOMPAT_H
#define TIGERCOMPAT_CTCOMPAT_H

#if defined(__CORETEXT__) || defined(__CTFONT__)
#error "TigerCompat/CTCompat.h declares Tiger's real CoreText ABI and cannot be combined with <CoreText/CoreText.h>. See compat/CT-SURVEY.md."
#endif

#include <CoreFoundation/CoreFoundation.h>
#include <ApplicationServices/ApplicationServices.h>

/* CGFloat arrived in 10.5; the 10.4u SDK has no such type and every
 * CoreGraphics prototype in it says `float`, which on i386 is right.
 *
 * TigerCompat/CGCompat.h defines this too, behind the same guard, so including
 * both is harmless. This header does not include that one: it pulls in
 * <ImageIO/CGImageSource.h> and drags ImageIO into every CoreText unit. */
#ifndef CGFLOAT_DEFINED
#include <float.h>
typedef float CGFloat;
#define CGFLOAT_DEFINED 1
#define CGFLOAT_IS_DOUBLE 0
#define CGFLOAT_MIN FLT_MIN
#define CGFLOAT_MAX FLT_MAX
#endif

/* CFError is 10.5+; Tiger's CoreFoundation has neither the type nor the API. */
#ifndef __COREFOUNDATION_CFERROR__
typedef struct __CFError* CFErrorRef;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- types ------------------------------------------------------------- */


typedef const struct __CTFont* CTFontRef;
typedef const struct __CTFontDescriptor* CTFontDescriptorRef;
typedef const struct __CTFontCollection* CTFontCollectionRef;
typedef const struct __CTLine* CTLineRef;
typedef const struct __CTRun* CTRunRef;
typedef const struct __CTFrame* CTFrameRef;
typedef const struct __CTFramesetter* CTFramesetterRef;
typedef const struct __CTTypesetter* CTTypesetterRef;
typedef const struct __CTParagraphStyle* CTParagraphStyleRef;

typedef uint32_t CTFontSymbolicTraits;
typedef uint32_t CTFontUIFontType;
typedef uint32_t CTFontTableTag;
typedef uint32_t CTFontTableOptions;
typedef uint32_t CTFontOptions;
typedef uint32_t CTFontOrientation;
typedef CFOptionFlags CTLineBoundsOptions;
typedef CFIndex CTRunStatus;

enum {
    kCTFontTableOptionNoOptions = 0,
    kCTFontTableOptionExcludeSynthetic = (1 << 0)
};

enum {
    kCTFontNoFontType             = (uint32_t)-1,
    kCTFontUIFontUser             = 0,
    kCTFontUIFontUserFixedPitch   = 1,
    kCTFontUIFontSystem           = 2,
    kCTFontUIFontEmphasizedSystem = 3,
    kCTFontUIFontSmallSystem      = 4,
    kCTFontUIFontSmallEmphasizedSystem = 5,
    kCTFontUIFontMiniSystem       = 6,
    kCTFontUIFontMiniEmphasizedSystem = 7,
    kCTFontUIFontViews            = 8,
    kCTFontUIFontApplication      = 9,
    kCTFontUIFontLabel            = 10,
    kCTFontUIFontMenuTitle        = 11,
    kCTFontUIFontMenuItem         = 12,
    kCTFontUIFontMenuItemMark     = 13,
    kCTFontUIFontMenuItemCmdKey   = 14,
    kCTFontUIFontWindowTitle      = 15,
    kCTFontUIFontPushButton       = 16,
    kCTFontUIFontUtilityWindowTitle = 17,
    kCTFontUIFontAlertHeader      = 18,
    kCTFontUIFontSystemDetail     = 19,
    kCTFontUIFontEmphasizedSystemDetail = 20,
    kCTFontUIFontToolbar          = 21,
    kCTFontUIFontSmallToolbar     = 22,
    kCTFontUIFontMessage          = 23,
    kCTFontUIFontPalette          = 24,
    kCTFontUIFontToolTip          = 25,
    kCTFontUIFontControlContent   = 26
};

enum {
    kCTFontTraitItalic = (1 << 0),
    kCTFontTraitBold = (1 << 1),
    kCTFontTraitExpanded = (1 << 5),
    kCTFontTraitCondensed = (1 << 6),
    kCTFontTraitMonoSpace = (1 << 10),
    kCTFontTraitVertical = (1 << 11),
    kCTFontTraitUIOptimized = (1 << 12),
    kCTFontTraitColorGlyphs = (1 << 13),
    kCTFontTraitComposite = (1 << 14)
};

enum {
    kCTLineBoundsExcludeTypographicLeading = (1 << 0),
    kCTLineBoundsExcludeTypographicShifts = (1 << 1),
    kCTLineBoundsUseHangingPunctuation = (1 << 2),
    kCTLineBoundsUseGlyphPathBounds = (1 << 3),
    kCTLineBoundsUseOpticalBounds = (1 << 4)
};

enum {
    kCTFontOrientationDefault = 0,
    kCTFontOrientationHorizontal = 1,
    kCTFontOrientationVertical = 2
};

enum { kCTRunStatusRightToLeft = (1 << 0) };

/* Tiger CoreText exports that ctcompat.c and its callers rely on.
 *
 * ** Every scalar passed BY VALUE to Tiger's CoreText is a `double`, not a
 *    CGFloat. ** Read that again before adding a prototype here. Tiger's CT
 *    predates the CGFloat unification and its own C++ core (TFont::GetSize and
 *    friends) is written in double; the disassembly shows `movsd 0xc(%ebp)`
 *    wherever the 10.5 headers say CGFloat. A caller that passes a 4-byte float
 *    misaligns every argument after it, so `CTFontCreateWithName(name, 16.0f,
 *    NULL)` silently produces a font of size 0. Anything reached through a
 *    POINTER, or inside a struct such as CGSize or CGRect, is a float:
 *    CTLineGetTypographicBounds writes its ascent with `movss`.
 *
 *    This is why the 10.5 SDK's CoreText headers must not be used to build
 *    against Tiger's CoreText. They declare CGFloat and are wrong on this box.
 *
 * Names are the Tiger ones, which pre-date the Create/Copy rename. */
CTFontRef CTFontCreateWithName(CFStringRef, double size, const CGAffineTransform*);
CTFontRef CTFontCreateWithFontDescriptor(CTFontDescriptorRef, double size, const CGAffineTransform*);
CTFontRef CTFontCreateWithGraphicsFont(CGFontRef, double size, const CGAffineTransform*, CTFontDescriptorRef);
CTFontRef CTFontCreateCopyWithAttributes(CTFontRef, double size, const CGAffineTransform*, CTFontDescriptorRef);
CTFontRef CTFontCreateForString(CTFontRef, CFStringRef, CFRange);
/* Tiger's own symbolic-trait matcher, and the only thing on the box that can
 * actually resolve "the bold face of this family". */
CTFontRef CTFontCreateVariantWithMatchingSymbolicTraits(CTFontRef, double size,
    const CGAffineTransform*, CTFontSymbolicTraits value, CTFontSymbolicTraits mask);
CGFontRef CTFontGetGraphicsFont(CTFontRef, CTFontDescriptorRef*);
ATSFontRef CTFontGetPlatformFont(CTFontRef, CTFontDescriptorRef*);
CFStringRef CTFontCopyFamilyName(CTFontRef);
CFStringRef CTFontCopyPostScriptName(CTFontRef);
CFStringRef CTFontCopyName(CTFontRef, CFStringRef);
CFTypeRef CTFontCopyAttribute(CTFontRef, CFStringRef);
CTFontDescriptorRef CTFontCopyFontDescriptor(CTFontRef);
/* Tiger takes the table's four-character name as a CFString and has no options
 * argument: TFont::CopyTable(CFStringRef). The modern three-argument form with
 * an integer CTFontTableTag does NOT exist on this box. */
CFDataRef CTFontCopyTable(CTFontRef, CFStringRef tableName);
CFArrayRef CTFontCopyDefaultCascadeList(CTFontRef);
CTFontSymbolicTraits CTFontGetSymbolicTraits(CTFontRef);
double CTFontGetSize(CTFontRef);
double CTFontGetAscent(CTFontRef);
double CTFontGetDescent(CTFontRef);
double CTFontGetLeading(CTFontRef);
double CTFontGetCapHeight(CTFontRef);
double CTFontGetXHeight(CTFontRef);
double CTFontGetUnderlinePosition(CTFontRef);
double CTFontGetUnderlineThickness(CTFontRef);
double CTFontGetSlantAngle(CTFontRef);
CGAffineTransform CTFontGetMatrix(CTFontRef);
CFIndex CTFontGetNumberOfGlyphs(CTFontRef);
unsigned CTFontGetUnitsPerEm(CTFontRef);
bool CTFontGetGlyphsForCharacters(CTFontRef, const UniChar[], CGGlyph[], CFIndex);
/* Tiger takes no orientation and returns the summed advance as a CGSize. */
CGSize CTFontGetAdvancesForGlyphs(CTFontRef, const CGGlyph[], CGSize[], CFIndex);
/* Tiger takes no orientation; Apple's 9A241 TRANSITIONAL list flags this one. */
void CTFontGetSideBearingsForGlyphs(CTFontRef, const CGGlyph[], CGFloat[], CFIndex);
CGRect CTFontGetBoundingRectsForGlyphs(CTFontRef, const CGGlyph[], CGRect[], CFIndex);

CTFontDescriptorRef CTFontDescriptorCreateWithAttributes(CFDictionaryRef);
CTFontDescriptorRef CTFontDescriptorCreateWithNameAndSize(CFStringRef, double size);
CTFontDescriptorRef CTFontDescriptorCopyWithAttributes(CTFontDescriptorRef, CFDictionaryRef);
CTFontDescriptorRef CTFontDescriptorCopyWithFeature(CTFontDescriptorRef, CFNumberRef, CFNumberRef);
/* Tiger's own per-language CSS generic-family lookup, reading the table in
 * CoreText.framework/Resources/DefaultFontFallbacks.plist. Argument order
 * confirmed on the box: language first, CSS key second. */
CTFontDescriptorRef CTFontDescriptorCreatePerLanguageAndCSSKey(CFStringRef language, CFStringRef cssKey);
extern const CFStringRef kCTFontDescriptorSerifFamilyKey;
extern const CFStringRef kCTFontDescriptorSanSerifFamilyKey;
extern const CFStringRef kCTFontDescriptorMonospaceFamilyKey;
extern const CFStringRef kCTFontDescriptorCursiveFamilyKey;
extern const CFStringRef kCTFontDescriptorFantasyFamilyKey;
extern const CFStringRef kCTFontDescriptorDefaultKey;
CFArrayRef CTFontDescriptorCopyMatchingFontDescriptors(CTFontDescriptorRef, CFSetRef);
CFTypeRef CTFontDescriptorCopyAttribute(CTFontDescriptorRef, CFStringRef);
CFDictionaryRef CTFontDescriptorCopyAttributes(CTFontDescriptorRef);

CTLineRef CTLineCreateWithAttributedString(CFAttributedStringRef);
CFArrayRef CTLineGetGlyphRuns(CTLineRef);
CFIndex CTLineGetGlyphCount(CTLineRef);
/* The settings array is CTParagraphStyleSetting[]; spelled void* because
 * nothing in the compat layer builds one. */
CTParagraphStyleRef CTParagraphStyleCreate(const void* settings, CFIndex settingCount);
/* Tiger takes a CFRange the modern four-argument form does not. The range is
 * only validated against the glyph count (the metrics returned are the whole
 * line's either way), so passing {0, 0} is always safe; passing the modern
 * argument list makes the ascent pointer land in the range and the call
 * silently returns 0. */
double CTLineGetTypographicBounds(CTLineRef, CFRange, CGFloat* ascent, CGFloat* descent, CGFloat* leading);
CFIndex CTRunGetGlyphCount(CTRunRef);
/* CTRunGetGlyphs, CTRunGetAdvances, CTRunGetStringIndices, CTRunDraw and
 * CTFontCreateUIFontForLocale are exported by Tiger's CoreText but their bodies
 * are `xor eax, eax; ret`. Only the Ptr variants below return real data. */
const CGGlyph* CTRunGetGlyphsPtr(CTRunRef);
const CGSize* CTRunGetAdvancesPtr(CTRunRef);
const CFIndex* CTRunGetStringIndicesPtr(CTRunRef);
/* Tiger takes a CFRange the modern two-argument form does not. */
void CTLineDraw(CTLineRef, CGContextRef, CFRange);
CTRunStatus CTRunGetStatus(CTRunRef);
CFRange CTRunGetStringRange(CTRunRef);
CFDictionaryRef CTRunGetAttributes(CTRunRef);

CTFramesetterRef CTFramesetterCreateWithAttributedString(CFAttributedStringRef);
CTFrameRef CTFramesetterCreateFrame(CTFramesetterRef, CFRange, CGPathRef, CFDictionaryRef);
CFArrayRef CTFrameGetLines(CTFrameRef);
CGPathRef CTFrameGetPath(CTFrameRef);
CFRange CTFrameGetVisibleStringRange(CTFrameRef);

CTTypesetterRef CTTypesetterCreateWithUniCharProvider(const UniChar* (*)(CFIndex, CFIndex*, CFDictionaryRef*, void*),
    void (*)(const UniChar*, void*), void*);

extern const CFStringRef kCTFontFamilyNameAttribute;
extern const CFStringRef kCTFontNameAttribute;
extern const CFStringRef kCTFontSizeAttribute;
extern const CFStringRef kCTFontTraitsAttribute;
extern const CFStringRef kCTFontSymbolicTrait;
extern const CFStringRef kCTFontFileURLAttribute;
extern const CFStringRef kCTFontVariationAttribute;
extern const CFStringRef kCTFontAttributeName;
extern const CFStringRef kCTFullNameKey;


/* Types WebCore's CoreTextSPI.h defines for itself; repeated here because
 * ctcompat.c is compiled without WebCore's headers. Guarded so that including
 * both is harmless in C, where a repeated typedef of the same type is legal. */

typedef CFOptionFlags CTFontShapeOptions;
typedef uint32_t CTFontTransformOptions;
typedef uint32_t CTFontDescriptorOptions;
typedef CFOptionFlags CTFontFallbackOption;
typedef uint8_t CTCompositionLanguage;
typedef uint32_t CTFontTextStylePlatform;
typedef int CTFontManagerScope;

enum {
    kCTFontShapeWithKerning = (1 << 0),
    kCTFontShapeWithClusterComposition = (1 << 1),
    kCTFontShapeRightToLeft = (1 << 2)
};
enum {
    kCTFontTransformApplyShaping = (1 << 0),
    kCTFontTransformApplyPositioning = (1 << 1)
};
enum {
    kCTFontDescriptorOptionSystemUIFont = (1 << 1),
    kCTFontOptionsPreferSystemFont = (1 << 2),
    kCTFontDescriptorOptionPreferAppleSystemFont = kCTFontOptionsPreferSystemFont,
    kCTFontOptionsSystemUIFont = (1 << 1)
};
enum {
    kCTFontFallbackOptionNone = 0,
    kCTFontFallbackOptionSystem = (1 << 0),
    kCTFontFallbackOptionUserInstalled = (1 << 1),
    kCTFontFallbackOptionDefault = kCTFontFallbackOptionSystem | kCTFontFallbackOptionUserInstalled
};
enum {
    kCTCompositionLanguageUnset = 0,
    kCTCompositionLanguageNone,
    kCTCompositionLanguageJapanese,
    kCTCompositionLanguageSimplifiedChinese,
    kCTCompositionLanguageTraditionalChinese
};
enum { kCTRunStatusHasOrigins = (1 << 4) };
enum { kCTFontManagerScopeProcess = 1, kCTFontManagerScopeUser = 3 };
enum {
    kCTFontTextStylePlatformDefault = (uint32_t)-1,
    kCTFontTextStylePlatformMac = 3
};
enum { kCTFontDescriptorMatchingOptionIncludeHiddenFonts = (1 << 16) };

/* The older `kCTFontXTrait` spelling of the symbolic trait bits, which WebCore
 * still uses in FontCoreText.cpp and FontPlatformDataCoreText.cpp, plus the two
 * SPI trait bits. Same values as the kCTFontTraitX names above. */
enum {
    kCTFontItalicTrait = kCTFontTraitItalic,
    kCTFontBoldTrait = kCTFontTraitBold,
    kCTFontExpandedTrait = kCTFontTraitExpanded,
    kCTFontCondensedTrait = kCTFontTraitCondensed,
    kCTFontMonoSpaceTrait = kCTFontTraitMonoSpace,
    kCTFontVerticalTrait = kCTFontTraitVertical,
    kCTFontUIOptimizedTrait = kCTFontTraitUIOptimized,
    kCTFontColorGlyphsTrait = kCTFontTraitColorGlyphs,
    kCTFontCompositeTrait = kCTFontTraitComposite,
    kCTFontTraitEmphasized = kCTFontTraitBold,
    kCTFontTraitTightLeading = (1 << 15)
};

enum { kCTFontPaletteLight = -1, kCTFontPaletteDark = -2 };

/* CTFontTextStylePlatform; only Default and Mac mean anything on this port. */
enum {
    kCTFontTextStylePlatformPhone = 0,
    kCTFontTextStylePlatformWatch = 1,
    kCTFontTextStylePlatformTV = 2,
    kCTFontTextStylePlatformMacTouchBar = 4,
    kCTFontTextStylePlatformVision = 5,
    kCTFontTextStylePlatformVisionLegacy = 6
};

/* CTLineBreakMode and CTLineTruncationType. Tiger's CTParagraphStyle numbering
 * is the documented 10.5 numbering: both Tiger's and Leopard's
 * CTParagraphStyleGetValueForSpecifier bound the specifier at 13, which is
 * kCTParagraphStyleSpecifierBaseWritingDirection, the last one 10.5 defines. */
enum {
    kCTLineBreakByWordWrapping = 0,
    kCTLineBreakByCharWrapping = 1,
    kCTLineBreakByClipping = 2,
    kCTLineBreakByTruncatingHead = 3,
    kCTLineBreakByTruncatingTail = 4,
    kCTLineBreakByTruncatingMiddle = 5
};

enum {
    kCTLineTruncationStart = 0,
    kCTLineTruncationEnd = 1,
    kCTLineTruncationMiddle = 2
};

enum {
    kCTParagraphStyleSpecifierAlignment = 0,
    kCTParagraphStyleSpecifierFirstLineHeadIndent = 1,
    kCTParagraphStyleSpecifierHeadIndent = 2,
    kCTParagraphStyleSpecifierTailIndent = 3,
    kCTParagraphStyleSpecifierTabStops = 4,
    kCTParagraphStyleSpecifierDefaultTabInterval = 5,
    kCTParagraphStyleSpecifierLineBreakMode = 6,
    kCTParagraphStyleSpecifierLineHeightMultiple = 7,
    kCTParagraphStyleSpecifierMaximumLineHeight = 8,
    kCTParagraphStyleSpecifierMinimumLineHeight = 9,
    kCTParagraphStyleSpecifierLineSpacing = 10,
    kCTParagraphStyleSpecifierParagraphSpacing = 11,
    kCTParagraphStyleSpecifierParagraphSpacingBefore = 12,
    kCTParagraphStyleSpecifierBaseWritingDirection = 13,
    kCTParagraphStyleSpecifierCount = 14
};

/* sfnt tags WebCore switches on that Tiger's CT does not name. */
#ifndef kCTFontTableMATH
enum {
    kCTFontTableMATH = 'MATH',
    kCTFontTableSTAT = 'STAT',
    kCTFontTableSVG  = 'SVG ',
    kCTFontTableSbix = 'sbix',
    kCTFontTableTrak = 'trak',
    kCTFontTableFvar = 'fvar',
    kCTFontTableHead = 'head',
    kCTFontTableOS2  = 'OS/2',
    kCTFontTableVORG = 'VORG',
    kCTFontTableVhea = 'vhea',
    kCTFontTableCOLR = 'COLR',
    kCTFontTableCPAL = 'CPAL',
    kCTFontTableCBDT = 'CBDT'
};
#endif

/* ---- (b) thin wrappers over Tiger's older names ------------------------ */

CTFontRef CTFontCreateWithFontDescriptorAndOptions(CTFontDescriptorRef, CGFloat size, const CGAffineTransform*, CTFontOptions);
CTFontRef CTFontCreateUIFontForLanguage(CTFontUIFontType, CGFloat size, CFStringRef language);
CTFontDescriptorRef CTFontDescriptorCreateWithAttributesAndOptions(CFDictionaryRef, CTFontDescriptorOptions);
CTFontDescriptorRef CTFontDescriptorCreateCopyWithAttributes(CTFontDescriptorRef, CFDictionaryRef);
CTFontDescriptorRef CTFontDescriptorCreateCopyWithFeature(CTFontDescriptorRef, CFNumberRef type, CFNumberRef selector);
CTFontDescriptorOptions CTFontDescriptorGetOptions(CTFontDescriptorRef);
CFArrayRef CTFontDescriptorCreateMatchingFontDescriptors(CTFontDescriptorRef, CFSetRef mandatoryAttributes);
CTFontDescriptorRef CTFontDescriptorCreateMatchingFontDescriptor(CTFontDescriptorRef, CFSetRef mandatoryAttributes);
CFArrayRef CTFontCopyDefaultCascadeListForLanguages(CTFontRef, CFArrayRef languages);
CFIndex CTFontGetGlyphCount(CTFontRef);
CFStringRef CTFontCopyFullName(CTFontRef);
CGFontRef CTFontCopyGraphicsFont(CTFontRef, CTFontDescriptorRef*);
CFArrayRef CTFontManagerCopyAvailableFontFamilyNames(void);

/* ---- (c) implemented on Tiger CT + ATS + CG ---------------------------- */

CTFontDescriptorRef CTFontDescriptorCreateForUIType(CTFontUIFontType, CGFloat size, CFStringRef language);
CTFontDescriptorRef CTFontDescriptorCreateLastResort(void);
CTFontDescriptorRef CTFontDescriptorCreateCopyWithSymbolicTraits(CTFontDescriptorRef, CTFontSymbolicTraits value, CTFontSymbolicTraits mask);
CTFontDescriptorRef CTFontDescriptorCreateWithTextStyle(CFStringRef style, CFStringRef sizeCategory, CFStringRef language);
CTFontDescriptorRef CTFontDescriptorCreateWithTextStyleAndAttributes(CFStringRef style, CFStringRef sizeCategory, CFDictionaryRef);
CGFloat CTFontDescriptorGetTextStyleSize(CFStringRef style, CFTypeRef sizeCategory, CTFontTextStylePlatform, CGFloat* weight, CGFloat* lineSpacing);
CTFontDescriptorRef CTFontDescriptorCreateForCSSFamily(CFStringRef cssFamily, CFStringRef language);
bool CTFontDescriptorIsSystemUIFont(CTFontDescriptorRef);
bool CTFontIsSystemUIFont(CTFontRef);
CTFontUIFontType CTFontGetUIFontType(CTFontRef);
CTFontSymbolicTraits CTFontGetPhysicalSymbolicTraits(CTFontRef);
CTFontRef CTFontCopyPhysicalFont(CTFontRef);
bool CTFontHasTable(CTFontRef, CTFontTableTag);
CFArrayRef CTFontCopyAvailableTables(CTFontRef, CTFontTableOptions);
bool CTFontGetGlyphsForCharacterRange(CTFontRef, CGGlyph glyphs[], CFRange);
bool CTFontGetVerticalGlyphsForCharacters(CTFontRef, const UniChar characters[], CGGlyph glyphs[], CFIndex count);
void CTFontGetVerticalTranslationsForGlyphs(CTFontRef, const CGGlyph glyphs[], CGSize translations[], CFIndex count);
CGPathRef CTFontCreatePathForGlyph(CTFontRef, CGGlyph, const CGAffineTransform*);
void CTFontDrawGlyphs(CTFontRef, const CGGlyph glyphs[], const CGPoint positions[], size_t count, CGContextRef);
bool CTFontTransformGlyphs(CTFontRef, CGGlyph glyphs[], CGSize advances[], CFIndex count, CTFontTransformOptions);
CTFontRef CTFontCreateForCharacters(CTFontRef, const UniChar characters[], CFIndex length, CFIndex* coveredLength);
CTFontRef CTFontCreateForCharactersWithLanguage(CTFontRef, const UniChar characters[], CFIndex length, CFStringRef language, CFIndex* coveredLength);
CTFontRef CTFontCreateForCharactersWithLanguageAndOption(CTFontRef, const UniChar characters[], CFIndex length, CFStringRef language, CTFontFallbackOption, CFIndex* coveredLength);
CTFontDescriptorRef CTFontCreatePhysicalFontDescriptorForCharactersWithLanguage(CTFontRef, const UniChar characters[], CFIndex length, CFStringRef language, CFIndex* coveredLength);
CTFontRef CTFontCreateForCSS(CFStringRef name, uint16_t weight, CTFontSymbolicTraits, CGFloat size);

CTFontDescriptorRef CTFontManagerCreateFontDescriptorFromData(CFDataRef);
CTFontDescriptorRef CTFontManagerCreateMemorySafeFontDescriptorFromData(CFDataRef);
CFArrayRef CTFontManagerCreateFontDescriptorsFromData(CFDataRef);
CFArrayRef CTFontManagerCreateFontDescriptorsFromURL(CFURLRef);
bool CTFontManagerRegisterFontsForURL(CFURLRef, CTFontManagerScope, CFErrorRef*);
bool CTFontManagerEnableAllUserFonts(bool postFontChangeNotification);

CGRect CTLineGetBoundsWithOptions(CTLineRef, CTLineBoundsOptions);
double CTLineGetTrailingWhitespaceWidth(CTLineRef);
CGSize CTRunGetInitialAdvance(CTRunRef);
void CTRunGetBaseAdvancesAndOrigins(CTRunRef, CFRange, CGSize baseAdvances[], CGPoint origins[]);
CTTypesetterRef CTTypesetterCreateWithUniCharProviderAndOptions(const UniChar* (*provider)(CFIndex, CFIndex*, CFDictionaryRef*, void*),
    void (*dispose)(const UniChar*, void*), void* refCon, CFDictionaryRef options);
void CTParagraphStyleSetCompositionLanguage(CTParagraphStyleRef, CTCompositionLanguage);
void CTFrameGetLineOrigins(CTFrameRef, CFRange, CGPoint origins[]);
CGSize CTFramesetterSuggestFrameSizeWithConstraints(CTFramesetterRef, CFRange, CFDictionaryRef, CGSize constraints, CFRange* fitRange);

/* ---- (d) infeasible on Tiger: honest degradations ---------------------- */

CGSize CTFontShapeGlyphs(CTFontRef, CGGlyph glyphs[], CGSize advances[], CGPoint origins[], CFIndex indexes[],
    const UniChar chars[], CFIndex count, CTFontShapeOptions, CFStringRef language,
    void (^handler)(CFRange, CGGlyph**, CGSize**, CGPoint**, CFIndex**));
CFBitVectorRef CTFontCopyGlyphCoverageForFeature(CTFontRef, CFDictionaryRef feature);
CFBitVectorRef CTFontCopyColorGlyphCoverage(CTFontRef);
bool CTFontIsAppleColorEmoji(CTFontRef);
bool CTFontHasComplexColorFormatForGlyph(CTFontRef, CGGlyph);
CGFloat CTFontGetSbixImageSizeForGlyphAndContentsScale(CTFontRef, CGGlyph, CGFloat contentsScale);
CGRect CTFontGetTypographicBoundsForAdaptiveImageProvider(CTFontRef, CFTypeRef);
void CTFontDrawImageFromAdaptiveImageProviderAtPoint(CTFontRef, CFTypeRef, CGPoint, CGContextRef);
void CTFontGetUnsummedAdvancesForGlyphsAndStyle(CTFontRef, CTFontOrientation, uint32_t renderingStyle, const CGGlyph[], CGSize advances[], CFIndex count);
CGFloat CTFontGetAccessibilityBoldWeightOfWeight(CGFloat);

/* ---- Tiger-ABI adapters ------------------------------------------------
 *
 * The modern signature for the ten Tiger exports that share a name with the
 * modern API but not its behaviour. The SDK overlay's CoreText headers bind
 * each public name to these with an asm label. Nothing in the compat layer
 * calls them; they exist for WebCore. */

CFDataRef TigerCTFontCopyTable(CTFontRef, CTFontTableTag, CTFontTableOptions);
double TigerCTFontGetAdvancesForGlyphs(CTFontRef, CTFontOrientation, const CGGlyph[], CGSize[], CFIndex);
CGRect TigerCTFontGetBoundingRectsForGlyphs(CTFontRef, CTFontOrientation, const CGGlyph[], CGRect[], CFIndex);
double TigerCTLineGetTypographicBounds(CTLineRef, CGFloat* ascent, CGFloat* descent, CGFloat* leading);
void TigerCTRunGetGlyphs(CTRunRef, CFRange, CGGlyph[]);
void TigerCTRunGetAdvances(CTRunRef, CFRange, CGSize[]);
void TigerCTRunGetStringIndices(CTRunRef, CFRange, CFIndex[]);
void TigerCTRunDraw(CTRunRef, CGContextRef, CFRange);
CGRect TigerCTLineGetImageBounds(CTLineRef, CGContextRef);
void TigerCTFontGetSideBearingsForGlyphs(CTFontRef, CTFontOrientation, const CGGlyph[], CGFloat[], CFIndex);
void TigerCTLineDraw(CTLineRef, CGContextRef);
CTFontRef TigerCTFontCreateUIFontForLocale(CTFontUIFontType, CGFloat size, CFStringRef locale);

/* ---- CoreGraphics font SPI whose only route on Tiger is CoreText -------
 *
 * These three are CoreGraphics by name but there is no CoreGraphics on Tiger
 * that answers them, so they live here rather than in cgcompat.c: each one is a
 * CTFontCreateWithGraphicsFont away from a CoreText call that works. WebCore
 * declares all three in PAL/pal/spi/cg/CoreGraphicsSPI.h and calls none of them
 * in this checkout; they are here so the port links if that ever changes. */

#ifndef CGFONTRENDERINGSTYLE_DEFINED
#define CGFONTRENDERINGSTYLE_DEFINED 1
typedef uint32_t CGFontRenderingStyle;
#endif

CFStringRef CGFontCopyFamilyName(CGFontRef);
void CGFontGetGlyphsForUnichars(CGFontRef, const UniChar[], CGGlyph[], size_t count);
bool CGFontGetGlyphAdvancesForStyle(CGFontRef, const CGAffineTransform*, CGFontRenderingStyle,
    const CGGlyph[], size_t count, CGSize advances[]);

/* ---- data symbols Tiger's CoreText does not export --------------------- */

extern const CFStringRef kCTFontReferenceURLAttribute;
extern const CFStringRef kCTFontPostScriptNameAttribute;
extern const CFStringRef kCTFontOpticalSizeAttribute;
extern const CFStringRef kCTFontUserInstalledAttribute;
extern const CFStringRef kCTFontEnabledAttribute;
extern const CFStringRef kCTFontFallbackOptionAttribute;
extern const CFStringRef kCTFontDescriptorLanguageAttribute;
extern const CFStringRef kCTFontDescriptorTextStyleAttribute;
extern const CFStringRef kCTFontDescriptorTextStyleEmphasized;
extern const CFStringRef kCTFontCSSWeightAttribute;
extern const CFStringRef kCTFontCSSWidthAttribute;
extern const CFStringRef kCTFontSizeCategoryAttribute;
extern const CFStringRef kCTFontTrackAttribute;
extern const CFStringRef kCTFontUnscaledTrackingAttribute;
extern const CFStringRef kCTFontIgnoreLegibilityWeightAttribute;
extern const CFStringRef kCTFontOrientationAttribute;
extern const CFStringRef kCTFontPaletteAttribute;
extern const CFStringRef kCTFontPaletteColorsAttribute;
extern const CFStringRef kCTFontGradeTrait;
extern const CFStringRef kCTFontUIFontDesignTrait;
extern const CFStringRef kCTFontUIFontDesignDefault;
extern const CFStringRef kCTFontUIFontDesignSerif;
extern const CFStringRef kCTFontUIFontDesignMonospaced;
extern const CFStringRef kCTFontUIFontDesignRounded;
extern const CFStringRef kCTFontOpenTypeFeatureTag;
extern const CFStringRef kCTFontOpenTypeFeatureValue;
extern const CFStringRef kCTFontURLAttribute;
extern const CFStringRef kCTFontVariationAxesAttribute;
extern const CFStringRef kCTFontCSSFamilySerif;
extern const CFStringRef kCTFontCSSFamilySansSerif;
extern const CFStringRef kCTFontCSSFamilyCursive;
extern const CFStringRef kCTFontCSSFamilyFantasy;
extern const CFStringRef kCTFontCSSFamilyMonospace;
extern const CFStringRef kCTFontCSSFamilySystemUI;
extern const CFStringRef kCTFontContentSizeCategoryL;
extern const CFStringRef kCTFontContentSizeCategoryXXXL;
extern const CFStringRef kCTFontManagerRegisteredFontsChangedNotification;
extern const CFStringRef kCTLanguageAttributeName;
extern const CFStringRef kCTStrokeColorAttributeName;
extern const CFStringRef kCTStrokeWidthAttributeName;
extern const CFStringRef kCTVerticalFormsAttributeName;
extern const CFStringRef kCTFrameMaximumNumberOfLinesAttributeName;
extern const CFStringRef kCTTypesetterOptionForcedEmbeddingLevel;

extern const CFStringRef kCTUIFontTextStyleTitle0;
extern const CFStringRef kCTUIFontTextStyleTitle1;
extern const CFStringRef kCTUIFontTextStyleTitle2;
extern const CFStringRef kCTUIFontTextStyleTitle3;
extern const CFStringRef kCTUIFontTextStyleTitle4;
extern const CFStringRef kCTUIFontTextStyleHeadline;
extern const CFStringRef kCTUIFontTextStyleBody;
extern const CFStringRef kCTUIFontTextStyleSubhead;
extern const CFStringRef kCTUIFontTextStyleFootnote;
extern const CFStringRef kCTUIFontTextStyleCaption1;
extern const CFStringRef kCTUIFontTextStyleCaption2;
extern const CFStringRef kCTUIFontTextStyleShortHeadline;
extern const CFStringRef kCTUIFontTextStyleShortBody;
extern const CFStringRef kCTUIFontTextStyleShortSubhead;
extern const CFStringRef kCTUIFontTextStyleShortFootnote;
extern const CFStringRef kCTUIFontTextStyleShortCaption1;
extern const CFStringRef kCTUIFontTextStyleTallBody;

/* Apple's -1..1 weight and width scale. */
extern const CGFloat kCTFontWeightUltraLight;
extern const CGFloat kCTFontWeightThin;
extern const CGFloat kCTFontWeightLight;
extern const CGFloat kCTFontWeightRegular;
extern const CGFloat kCTFontWeightMedium;
extern const CGFloat kCTFontWeightSemibold;
extern const CGFloat kCTFontWeightBold;
extern const CGFloat kCTFontWeightHeavy;
extern const CGFloat kCTFontWeightBlack;
extern const CGFloat kCTFontWidthUltraCompressed;
extern const CGFloat kCTFontWidthExtraCompressed;
extern const CGFloat kCTFontWidthCompressed;
extern const CGFloat kCTFontWidthExtraCondensed;
extern const CGFloat kCTFontWidthCondensed;
extern const CGFloat kCTFontWidthSemiCondensed;
extern const CGFloat kCTFontWidthStandard;
extern const CGFloat kCTFontWidthSemiExpanded;
extern const CGFloat kCTFontWidthExpanded;
extern const CGFloat kCTFontWidthExtraExpanded;

#ifdef __cplusplus
}
#endif

#endif /* TIGERCOMPAT_CTCOMPAT_H */
