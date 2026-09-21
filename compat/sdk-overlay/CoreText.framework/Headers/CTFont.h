/* TIGER SDK OVERLAY: <CoreText/CTFont.h> — see CTDefines.h. */
#ifndef __CTFONT__
#define __CTFONT__

#include <CoreText/CTDefines.h>
#include <CoreText/CTFontDescriptor.h>
#include <CoreText/CTFontTraits.h>

typedef const struct __CTFont* CTFontRef;

typedef uint32_t CTFontUIFontType;
typedef uint32_t CTFontTableTag;
typedef uint32_t CTFontTableOptions;
typedef uint32_t CTFontOrientation;
typedef CFOptionFlags CTFontShapeOptions;
typedef uint32_t CTFontTransformOptions;
typedef CFOptionFlags CTFontFallbackOption;

enum { kCTFontTableOptionNoOptions = 0, kCTFontTableOptionExcludeSynthetic = (1 << 0) };
enum { kCTFontOrientationDefault = 0, kCTFontOrientationHorizontal = 1, kCTFontOrientationVertical = 2 };
enum { kCTFontPaletteLight = -1, kCTFontPaletteDark = -2 };

enum {
    kCTFontNoFontType             = (uint32_t)-1,
    kCTFontUIFontUser             = 0,
    kCTFontUIFontUserFixedPitch   = 1,
    kCTFontUIFontSystem           = 2,
    kCTFontUIFontEmphasizedSystem = 3,
    kCTFontUIFontSmallSystem      = 4,
    kCTFontUIFontMiniSystem       = 6,
    kCTFontUIFontMenuItem         = 10,
    kCTFontUIFontLabel            = 20,
    kCTFontUIFontSystemItalic     = 27,
    kCTFontUIFontSystemThin       = 102,
    kCTFontUIFontSystemLight      = 103,
    kCTFontUIFontSystemUltraLight = 104
};

enum {
    kCTFontFallbackOptionNone          = 0,
    kCTFontFallbackOptionSystem        = (1 << 0),
    kCTFontFallbackOptionUserInstalled = (1 << 1),
    kCTFontFallbackOptionDefault       = kCTFontFallbackOptionSystem | kCTFontFallbackOptionUserInstalled
};

enum {
    kCTFontShapeWithKerning            = (1 << 0),
    kCTFontShapeWithClusterComposition = (1 << 1),
    kCTFontShapeRightToLeft            = (1 << 2)
};

enum { kCTFontTransformApplyShaping = (1 << 0), kCTFontTransformApplyPositioning = (1 << 1) };

/* sfnt tags. Tiger exports its own as CFStrings, which is an implementation
 * detail of CTFontCopyTable; every tag WebCore uses is an integer here. */
enum {
    kCTFontTableGDEF = 'GDEF', kCTFontTableGPOS = 'GPOS', kCTFontTableGSUB = 'GSUB',
    kCTFontTableMATH = 'MATH', kCTFontTableSTAT = 'STAT', kCTFontTableSVG  = 'SVG ',
    kCTFontTableOS2  = 'OS/2', kCTFontTableVORG = 'VORG', kCTFontTableCOLR = 'COLR',
    kCTFontTableCPAL = 'CPAL', kCTFontTableCBDT = 'CBDT', kCTFontTableSbix = 'sbix',
    kCTFontTableTrak = 'trak', kCTFontTableFvar = 'fvar', kCTFontTableHead = 'head',
    kCTFontTableHhea = 'hhea', kCTFontTableHmtx = 'hmtx', kCTFontTableVhea = 'vhea',
    kCTFontTableVmtx = 'vmtx', kCTFontTableCmap = 'cmap', kCTFontTableGlyf = 'glyf',
    kCTFontTableLoca = 'loca', kCTFontTableMaxp = 'maxp', kCTFontTableName = 'name',
    kCTFontTablePost = 'post', kCTFontTableKern = 'kern', kCTFontTableMort = 'mort',
    kCTFontTableMorx = 'morx', kCTFontTableFeat = 'feat', kCTFontTableJust = 'just'
};

/* Name keys Tiger exports. */
CT_EXTERN const CFStringRef kCTFontCopyrightNameKey;
CT_EXTERN const CFStringRef kCTFontFamilyNameKey;
CT_EXTERN const CFStringRef kCTFontSubFamilyNameKey;
CT_EXTERN const CFStringRef kCTFontStyleNameKey;
CT_EXTERN const CFStringRef kCTFontUniqueNameKey;
CT_EXTERN const CFStringRef kCTFontFullNameKey;
CT_EXTERN const CFStringRef kCTFontVersionNameKey;
CT_EXTERN const CFStringRef kCTFontPostScriptNameKey;
CT_EXTERN const CFStringRef kCTFontTrademarkNameKey;
CT_EXTERN const CFStringRef kCTFontManufacturerNameKey;
CT_EXTERN const CFStringRef kCTFontDesignerNameKey;
CT_EXTERN const CFStringRef kCTFontDescriptionNameKey;

/* Tiger spells them without the Font infix; map the modern names onto them. */
CT_EXTERN const CFStringRef kCTFullNameKey;
CT_EXTERN const CFStringRef kCTFamilyNameKey;
CT_EXTERN const CFStringRef kCTStyleNameKey;
CT_EXTERN const CFStringRef kCTPostScriptNameKey;

CT_EXTERN CFTypeID CTFontGetTypeID(void);

/* --- case 2: Tiger reads the size as a by-value double -------------------
 * The modern headers spell these CGFloat. On this binary they are `movsd`, so
 * a CGFloat call would misalign every later argument and quietly produce a
 * font of size 0. The caller's float converts at the call site. */
CT_EXTERN CTFontRef CTFontCreateWithName(CFStringRef name, double size, const CGAffineTransform* matrix);
CT_EXTERN CTFontRef CTFontCreateWithFontDescriptor(CTFontDescriptorRef, double size, const CGAffineTransform* matrix);
CT_EXTERN CTFontRef CTFontCreateWithGraphicsFont(CGFontRef, double size, const CGAffineTransform* matrix, CTFontDescriptorRef);
CT_EXTERN CTFontRef CTFontCreateCopyWithAttributes(CTFontRef, double size, const CGAffineTransform* matrix, CTFontDescriptorRef);
CT_EXTERN CTFontRef CTFontCreateWithPlatformFont(uint32_t atsFont, double size, const CGAffineTransform* matrix, CTFontDescriptorRef);

/* --- case 1: Tiger's ABI already matches --------------------------------
 * Scalar returns come back in ST(0), so a CGFloat return is read correctly
 * whether the callee computed a float or a double. Only parameters matter. */
CT_EXTERN CTFontRef CTFontCreateForString(CTFontRef, CFStringRef, CFRange);
CT_EXTERN CTFontDescriptorRef CTFontCopyFontDescriptor(CTFontRef);
CT_EXTERN CFTypeRef CTFontCopyAttribute(CTFontRef, CFStringRef attribute);
CT_EXTERN CFStringRef CTFontCopyPostScriptName(CTFontRef);
CT_EXTERN CFStringRef CTFontCopyFamilyName(CTFontRef);
CT_EXTERN CFStringRef CTFontCopyDisplayName(CTFontRef);
CT_EXTERN CFStringRef CTFontCopyName(CTFontRef, CFStringRef nameKey);
CT_EXTERN CFStringRef CTFontCopyLocalizedName(CTFontRef, CFStringRef nameKey, CFStringRef* language);
CT_EXTERN CFCharacterSetRef CTFontCopyCharacterSet(CTFontRef);
CT_EXTERN CFStringEncoding CTFontGetStringEncoding(CTFontRef);
CT_EXTERN CFArrayRef CTFontCopySupportedLocales(CTFontRef);
CT_EXTERN CFArrayRef CTFontCopyTraits(CTFontRef);
CT_EXTERN CFArrayRef CTFontCopyFeatures(CTFontRef);
CT_EXTERN CFArrayRef CTFontCopyFeatureSettings(CTFontRef);
CT_EXTERN CFArrayRef CTFontCopyVariationAxes(CTFontRef);
CT_EXTERN CFDictionaryRef CTFontCopyVariation(CTFontRef);
CT_EXTERN CFArrayRef CTFontCopyDefaultCascadeList(CTFontRef);
CT_EXTERN CGFontRef CTFontGetGraphicsFont(CTFontRef, CTFontDescriptorRef*);
CT_EXTERN uint32_t CTFontGetPlatformFont(CTFontRef, CTFontDescriptorRef*);
CT_EXTERN CTFontSymbolicTraits CTFontGetSymbolicTraits(CTFontRef);
CT_EXTERN CGFloat CTFontGetSize(CTFontRef);
CT_EXTERN CGAffineTransform CTFontGetMatrix(CTFontRef);
CT_EXTERN CGFloat CTFontGetAscent(CTFontRef);
CT_EXTERN CGFloat CTFontGetDescent(CTFontRef);
CT_EXTERN CGFloat CTFontGetLeading(CTFontRef);
CT_EXTERN CGFloat CTFontGetCapHeight(CTFontRef);
CT_EXTERN CGFloat CTFontGetXHeight(CTFontRef);
CT_EXTERN CGFloat CTFontGetUnderlinePosition(CTFontRef);
CT_EXTERN CGFloat CTFontGetUnderlineThickness(CTFontRef);
CT_EXTERN CGFloat CTFontGetSlantAngle(CTFontRef);
CT_EXTERN CGFloat CTFontGetMaximumAdvance(CTFontRef);
CT_EXTERN CGRect CTFontGetBoundingBox(CTFontRef);
CT_EXTERN unsigned CTFontGetUnitsPerEm(CTFontRef);
CT_EXTERN CFIndex CTFontGetNumberOfGlyphs(CTFontRef);
CT_EXTERN CGGlyph CTFontGetGlyphWithName(CTFontRef, CFStringRef glyphName);
CT_EXTERN bool CTFontGetGlyphsForCharacters(CTFontRef, const UniChar characters[], CGGlyph glyphs[], CFIndex count);
CT_EXTERN void CTFontGetSideBearingsForGlyphs(CTFontRef, CTFontOrientation, const CGGlyph[], CGFloat[], CFIndex);
CT_EXTERN void CTFontApplyToContext(CTFontRef, CGContextRef);

/* --- case 3a: Tiger's takes different arguments -------------------------- */
CT_EXTERN CFDataRef CTFontCopyTable(CTFontRef, CTFontTableTag, CTFontTableOptions)
    CT_TIGER_ADAPTER(TigerCTFontCopyTable);
CT_EXTERN double CTFontGetAdvancesForGlyphs(CTFontRef, CTFontOrientation, const CGGlyph[], CGSize advances[], CFIndex count)
    CT_TIGER_ADAPTER(TigerCTFontGetAdvancesForGlyphs);
CT_EXTERN CGRect CTFontGetBoundingRectsForGlyphs(CTFontRef, CTFontOrientation, const CGGlyph[], CGRect rects[], CFIndex count)
    CT_TIGER_ADAPTER(TigerCTFontGetBoundingRectsForGlyphs);
CT_EXTERN CTFontRef CTFontCreateUIFontForLocale(CTFontUIFontType, CGFloat size, CFStringRef locale)
    CT_TIGER_ADAPTER(TigerCTFontCreateUIFontForLocale);

/* --- case 3b: Tiger has no such function -------------------------------- */
CT_EXTERN CFIndex CTFontGetGlyphCount(CTFontRef);
CT_EXTERN CFStringRef CTFontCopyFullName(CTFontRef);
CT_EXTERN CGFontRef CTFontCopyGraphicsFont(CTFontRef, CTFontDescriptorRef*);
CT_EXTERN CFArrayRef CTFontCopyAvailableTables(CTFontRef, CTFontTableOptions);
CT_EXTERN bool CTFontHasTable(CTFontRef, CTFontTableTag);
CT_EXTERN CGPathRef CTFontCreatePathForGlyph(CTFontRef, CGGlyph, const CGAffineTransform*);
CT_EXTERN void CTFontDrawGlyphs(CTFontRef, const CGGlyph glyphs[], const CGPoint positions[], size_t count, CGContextRef);
CT_EXTERN bool CTFontGetGlyphsForCharacterRange(CTFontRef, CGGlyph glyphs[], CFRange);
CT_EXTERN bool CTFontGetVerticalGlyphsForCharacters(CTFontRef, const UniChar characters[], CGGlyph glyphs[], CFIndex count);
CT_EXTERN void CTFontGetVerticalTranslationsForGlyphs(CTFontRef, const CGGlyph glyphs[], CGSize translations[], CFIndex count);
CT_EXTERN CTFontRef CTFontCreateUIFontForLanguage(CTFontUIFontType, CGFloat size, CFStringRef language);
CT_EXTERN CTFontRef CTFontCreateWithFontDescriptorAndOptions(CTFontDescriptorRef, CGFloat size, const CGAffineTransform*, CTFontOptions);
CT_EXTERN CFArrayRef CTFontCopyDefaultCascadeListForLanguages(CTFontRef, CFArrayRef languages);
CT_EXTERN bool CTFontIsSystemUIFont(CTFontRef);
CT_EXTERN CTFontUIFontType CTFontGetUIFontType(CTFontRef);
CT_EXTERN CTFontSymbolicTraits CTFontGetPhysicalSymbolicTraits(CTFontRef);
CT_EXTERN CTFontRef CTFontCopyPhysicalFont(CTFontRef);
CT_EXTERN CFBitVectorRef CTFontCopyColorGlyphCoverage(CTFontRef);
CT_EXTERN CFBitVectorRef CTFontCopyGlyphCoverageForFeature(CTFontRef, CFDictionaryRef feature);
CT_EXTERN bool CTFontIsAppleColorEmoji(CTFontRef);
CT_EXTERN CGFloat CTFontGetAccessibilityBoldWeightOfWeight(CGFloat);
CT_EXTERN bool CTFontTransformGlyphs(CTFontRef, CGGlyph glyphs[], CGSize advances[], CFIndex count, CTFontTransformOptions);
CT_EXTERN void CTFontGetUnsummedAdvancesForGlyphsAndStyle(CTFontRef, CTFontOrientation, uint32_t renderingStyle, const CGGlyph[], CGSize advances[], CFIndex count);
CT_EXTERN CTFontRef CTFontCreateForCharacters(CTFontRef, const UniChar characters[], CFIndex length, CFIndex* coveredLength);
CT_EXTERN CTFontRef CTFontCreateForCharactersWithLanguage(CTFontRef, const UniChar characters[], CFIndex length, CFStringRef language, CFIndex* coveredLength);
CT_EXTERN CTFontRef CTFontCreateForCharactersWithLanguageAndOption(CTFontRef, const UniChar characters[], CFIndex length, CFStringRef language, CTFontFallbackOption, CFIndex* coveredLength);
CT_EXTERN CTFontDescriptorRef CTFontCreatePhysicalFontDescriptorForCharactersWithLanguage(CTFontRef, const UniChar characters[], CFIndex length, CFStringRef language, CFIndex* coveredLength);
CT_EXTERN CTFontRef CTFontCreateForCSS(CFStringRef name, uint16_t weight, CTFontSymbolicTraits, CGFloat size);
CT_EXTERN CGFloat CTFontGetSbixImageSizeForGlyphAndContentsScale(CTFontRef, CGGlyph, CGFloat contentsScale);
CT_EXTERN bool CTFontHasComplexColorFormatForGlyph(CTFontRef, CGGlyph);
CT_EXTERN CGRect CTFontGetTypographicBoundsForAdaptiveImageProvider(CTFontRef, CFTypeRef);
CT_EXTERN void CTFontDrawImageFromAdaptiveImageProviderAtPoint(CTFontRef, CFTypeRef, CGPoint, CGContextRef);

/* Text style names, from libtigercompat. */
CT_EXTERN const CFStringRef kCTUIFontTextStyleTitle0;
CT_EXTERN const CFStringRef kCTUIFontTextStyleTitle1;
CT_EXTERN const CFStringRef kCTUIFontTextStyleTitle2;
CT_EXTERN const CFStringRef kCTUIFontTextStyleTitle3;
CT_EXTERN const CFStringRef kCTUIFontTextStyleTitle4;
CT_EXTERN const CFStringRef kCTUIFontTextStyleHeadline;
CT_EXTERN const CFStringRef kCTUIFontTextStyleBody;
CT_EXTERN const CFStringRef kCTUIFontTextStyleSubhead;
CT_EXTERN const CFStringRef kCTUIFontTextStyleFootnote;
CT_EXTERN const CFStringRef kCTUIFontTextStyleCaption1;
CT_EXTERN const CFStringRef kCTUIFontTextStyleCaption2;
CT_EXTERN const CFStringRef kCTUIFontTextStyleShortHeadline;
CT_EXTERN const CFStringRef kCTUIFontTextStyleShortBody;
CT_EXTERN const CFStringRef kCTUIFontTextStyleShortSubhead;
CT_EXTERN const CFStringRef kCTUIFontTextStyleShortFootnote;
CT_EXTERN const CFStringRef kCTUIFontTextStyleShortCaption1;
CT_EXTERN const CFStringRef kCTUIFontTextStyleTallBody;

#endif /* __CTFONT__ */
