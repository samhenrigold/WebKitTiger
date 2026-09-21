/* TIGER: CoreText entry points Mac OS X 10.4.11 lacks, built on Tiger's own
 * private CoreText plus ATS and CoreGraphics.
 *
 * Classification of all 66 missing names, and why each one is a wrapper, real
 * code, a stub, or nothing at all, is in compat/CT-SURVEY.md. Read that first.
 *
 * Built into libtigercompat.a by compat/Makefile (which globs *.c).
 */

#include <TigerCompat/CTCompat.h>

#include <stdlib.h>
#include <string.h>

/* ---- declarations the 10.4u SDK is missing but Tiger's dylibs export ----
 *
 * CGFontCreateWithDataProvider, CGFontGetGlyphPath and CGFontGetUnitsPerEm are
 * exported by Tiger's CoreGraphics but declared nowhere in the 10.4u SDK. They
 * live in <TigerCompat/CGCompat.h>, which CTCompat.h includes.
 *
 * ATS activation needs nothing here either: ATSFontActivateFromMemory,
 * ATSFontFindFromContainer, ATSFontGetPostScriptName and
 * ATSFontGetTableDirectory are all properly declared in the 10.4u SDK's
 * <ATS/ATSFont.h>.
 */

/* Tiger CoreText's own spelling, leading underscore included. */
CFArrayRef _CTFontDescriptorCopyAvailableFontFamilyNames(void);

/* ======================================================================== */
/* (b) thin wrappers over Tiger's older names                               */
/* ======================================================================== */

CTFontRef CTFontCreateWithFontDescriptorAndOptions(CTFontDescriptorRef descriptor, CGFloat size,
    const CGAffineTransform* matrix, CTFontOptions options)
{
    (void)options; /* Tiger has no font options. */
    return CTFontCreateWithFontDescriptor(descriptor, size, matrix);
}

/* Defined further down, next to the rest of the system-font handling: Tiger's
 * CTFontCreateUIFontForLocale is an empty stub, so this cannot be a wrapper. */
CTFontRef CTFontCreateUIFontForLanguage(CTFontUIFontType, CGFloat, CFStringRef);

CTFontDescriptorRef CTFontDescriptorCreateWithAttributesAndOptions(CFDictionaryRef attributes,
    CTFontDescriptorOptions options)
{
    (void)options;
    return CTFontDescriptorCreateWithAttributes(attributes);
}

CTFontDescriptorRef CTFontDescriptorCreateCopyWithAttributes(CTFontDescriptorRef descriptor,
    CFDictionaryRef attributes)
{
    if (!descriptor)
        return NULL;
    /* WebCore passes a null dictionary when a font has no creation attributes. */
    if (!attributes)
        return (CTFontDescriptorRef)CFRetain(descriptor);
    return CTFontDescriptorCopyWithAttributes(descriptor, attributes);
}

CTFontDescriptorRef CTFontDescriptorCreateCopyWithFeature(CTFontDescriptorRef descriptor,
    CFNumberRef type, CFNumberRef selector)
{
    return descriptor ? CTFontDescriptorCopyWithFeature(descriptor, type, selector) : NULL;
}

CTFontDescriptorOptions CTFontDescriptorGetOptions(CTFontDescriptorRef descriptor)
{
    (void)descriptor;
    return 0;
}

CFArrayRef CTFontDescriptorCreateMatchingFontDescriptors(CTFontDescriptorRef descriptor,
    CFSetRef mandatoryAttributes)
{
    return descriptor ? CTFontDescriptorCopyMatchingFontDescriptors(descriptor, mandatoryAttributes) : NULL;
}

CTFontDescriptorRef CTFontDescriptorCreateMatchingFontDescriptor(CTFontDescriptorRef descriptor,
    CFSetRef mandatoryAttributes)
{
    CTFontDescriptorRef result = NULL;
    CFArrayRef matches = CTFontDescriptorCreateMatchingFontDescriptors(descriptor, mandatoryAttributes);

    if (matches) {
        if (CFArrayGetCount(matches))
            result = (CTFontDescriptorRef)CFRetain(CFArrayGetValueAtIndex(matches, 0));
        CFRelease(matches);
    }
    return result;
}

CFArrayRef CTFontCopyDefaultCascadeListForLanguages(CTFontRef font, CFArrayRef languages)
{
    (void)languages; /* Tiger's cascade list is not language-keyed. */
    return font ? CTFontCopyDefaultCascadeList(font) : NULL;
}

CFIndex CTFontGetGlyphCount(CTFontRef font)
{
    return font ? CTFontGetNumberOfGlyphs(font) : 0;
}

CFStringRef CTFontCopyFullName(CTFontRef font)
{
    return font ? CTFontCopyName(font, kCTFullNameKey) : NULL;
}

CGFontRef CTFontCopyGraphicsFont(CTFontRef font, CTFontDescriptorRef* outDescriptor)
{
    CGFontRef cgFont = font ? CTFontGetGraphicsFont(font, outDescriptor) : NULL;
    return cgFont ? CGFontRetain(cgFont) : NULL;
}

CFArrayRef CTFontManagerCopyAvailableFontFamilyNames(void)
{
    return _CTFontDescriptorCopyAvailableFontFamilyNames();
}

/* ======================================================================== */
/* (c) real implementations                                                 */
/* ======================================================================== */

/* ---- the system font, and telling it apart from everything else -------- */

/* Tiger's CTFontCreateUIFontForLocale is exported but its body is
 * `xor eax, eax; ret`, so there is no UI font API to forward to and no way to
 * ask CoreText what the system font is. On 10.4 it is Lucida Grande, at the
 * sizes AppKit uses (system 13, small 11, mini 9, menu 14, label 10), so the
 * table below is the whole of it. Linking AppKit just to read
 * +[NSFont systemFontSize] is not worth it in a C compat library. */
#define TIGER_SYSTEM_FONT_FAMILY CFSTR("Lucida Grande")

static CFStringRef systemFontFamilyName(void)
{
    return TIGER_SYSTEM_FONT_FAMILY;
}

/* The font a UI type resolves to: family, default size, and whether it is bold. */
static void uiFontForType(CTFontUIFontType type, CFStringRef* outFamily, CGFloat* outSize, int* outBold)
{
    *outFamily = TIGER_SYSTEM_FONT_FAMILY;
    *outSize = 13;
    *outBold = 0;
    switch (type) {
    case kCTFontUIFontEmphasizedSystem:
        *outBold = 1;
        break;
    case kCTFontUIFontSmallSystem:
        *outSize = 11;
        break;
    case kCTFontUIFontMiniSystem:
        *outSize = 9;
        break;
    case kCTFontUIFontMenuItem:
        *outSize = 14;
        break;
    case kCTFontUIFontLabel:
        *outSize = 10;
        break;
    case kCTFontUIFontUserFixedPitch:
        *outFamily = CFSTR("Monaco");
        *outSize = 10;
        break;
    case kCTFontUIFontUser:
        *outFamily = CFSTR("Helvetica");
        *outSize = 12;
        break;
    default:
        break;
    }
}

/* Tiger's own CTFontCreateUIFontForLocale is a stub, so build the font from the
 * table. Everything that wants a UI font goes through here. */
static CTFontRef createUIFont(CTFontUIFontType type, CGFloat size, CFStringRef language)
{
    CFStringRef family;
    CGFloat defaultSize;
    int bold;
    CTFontDescriptorRef descriptor;
    CTFontRef font;

    (void)language; /* Tiger has one system font, not one per script. */
    uiFontForType(type, &family, &defaultSize, &bold);
    if (!(size > 0))
        size = defaultSize;

    descriptor = CTFontDescriptorCreateWithNameAndSize(family, size);
    if (!descriptor)
        return NULL;
    if (bold) {
        CTFontDescriptorRef boldDescriptor =
            CTFontDescriptorCreateCopyWithSymbolicTraits(descriptor, kCTFontTraitBold, kCTFontTraitBold);
        if (boldDescriptor) {
            CFRelease(descriptor);
            descriptor = boldDescriptor;
        }
    }
    font = CTFontCreateWithFontDescriptor(descriptor, size, NULL);
    CFRelease(descriptor);
    return font;
}

/* Tiger descriptors do not carry kCTFontFamilyNameAttribute, so fall back to
 * realising the descriptor and asking the font. Returns a copy. */
static CFStringRef descriptorFamilyName(CTFontDescriptorRef descriptor)
{
    CFStringRef family;
    CTFontRef font;

    if (!descriptor)
        return NULL;
    family = (CFStringRef)CTFontDescriptorCopyAttribute(descriptor, kCTFontFamilyNameAttribute);
    if (family) {
        if (CFGetTypeID(family) == CFStringGetTypeID())
            return family;
        CFRelease(family);
    }
    font = CTFontCreateWithFontDescriptor(descriptor, 12.0, NULL);
    if (!font)
        return NULL;
    family = CTFontCopyFamilyName(font);
    CFRelease(font);
    return family;
}

bool CTFontIsSystemUIFont(CTFontRef font)
{
    CFStringRef systemFamily = systemFontFamilyName();
    CFStringRef family;
    bool result;

    if (!font || !systemFamily)
        return false;

    family = CTFontCopyFamilyName(font);
    if (!family)
        return false;
    result = CFStringCompare(family, systemFamily, 0) == kCFCompareEqualTo;
    CFRelease(family);
    return result;
}

bool CTFontDescriptorIsSystemUIFont(CTFontDescriptorRef descriptor)
{
    CFStringRef systemFamily = systemFontFamilyName();
    CFStringRef family;
    bool result;

    if (!descriptor || !systemFamily)
        return false;

    family = descriptorFamilyName(descriptor);
    if (!family)
        return false;
    result = CFStringCompare(family, systemFamily, 0) == kCFCompareEqualTo;
    CFRelease(family);
    return result;
}

CTFontUIFontType CTFontGetUIFontType(CTFontRef font)
{
    /* Tiger cannot tell which UI role a font was created for, only whether it
     * is the system font at all. Serializing a system font by its UI type and
     * rebuilding it with CTFontDescriptorCreateForUIType round-trips; anything
     * else goes by PostScript name, which is the conservative path. */
    return CTFontIsSystemUIFont(font) ? kCTFontUIFontSystem : (CTFontUIFontType)kCTFontNoFontType;
}

CTFontSymbolicTraits CTFontGetPhysicalSymbolicTraits(CTFontRef font)
{
    /* No font composition on Tiger: the physical font is the font. */
    return font ? CTFontGetSymbolicTraits(font) : 0;
}

CTFontRef CTFontCopyPhysicalFont(CTFontRef font)
{
    return font ? (CTFontRef)CFRetain(font) : NULL;
}

/* ---- descriptors ------------------------------------------------------- */

CTFontRef CTFontCreateUIFontForLanguage(CTFontUIFontType type, CGFloat size, CFStringRef language)
{
    return createUIFont(type, size, language);
}

CTFontDescriptorRef CTFontDescriptorCreateForUIType(CTFontUIFontType type, CGFloat size, CFStringRef language)
{
    CTFontRef font = createUIFont(type, size, language);
    CTFontDescriptorRef descriptor;

    if (!font)
        return NULL;
    descriptor = CTFontCopyFontDescriptor(font);
    CFRelease(font);
    return descriptor;
}

CTFontDescriptorRef CTFontDescriptorCreateLastResort(void)
{
    /* Tiger ships /System/Library/Fonts/LastResort.ttf. */
    return CTFontDescriptorCreateWithNameAndSize(CFSTR("LastResort"), 0);
}

CTFontDescriptorRef CTFontDescriptorCreateCopyWithSymbolicTraits(CTFontDescriptorRef descriptor,
    CTFontSymbolicTraits value, CTFontSymbolicTraits mask)
{
    CTFontRef font, variant;
    CTFontDescriptorRef result;

    if (!descriptor)
        return NULL;

    /* Merging {kCTFontTraitsAttribute: {kCTFontSymbolicTrait: ...}} into the
     * descriptor does not work on Tiger: its matcher ignores the symbolic trait
     * and falls back to the system font, so asking Helvetica for bold returns
     * Lucida Grande. Tiger's own CTFontCreateVariantWithMatchingSymbolicTraits
     * is the entry point that resolves a face within a family, so go through a
     * realised font and back. */
    font = CTFontCreateWithFontDescriptor(descriptor, 12.0, NULL);
    if (!font)
        return NULL;

    variant = CTFontCreateVariantWithMatchingSymbolicTraits(font, 12.0, NULL, value, mask);
    CFRelease(font);
    if (!variant) {
        /* No such face in this family. CoreText returns NULL here too, but
         * WebCore treats NULL as "descriptor unusable", so hand back the
         * original untouched and let the caller synthesise. */
        return (CTFontDescriptorRef)CFRetain(descriptor);
    }
    result = CTFontCopyFontDescriptor(variant);
    CFRelease(variant);
    return result;
}

/* Dynamic Type does not exist on Tiger, so a text style maps to a fixed macOS
 * point size. Sizes follow the macOS metrics for the equivalent styles. */
static const struct { const CFStringRef* style; CGFloat size; CGFloat weight; } textStyleTable[] = {
    { &kCTUIFontTextStyleTitle0, 26, 0.0 },
    { &kCTUIFontTextStyleTitle1, 22, 0.0 },
    { &kCTUIFontTextStyleTitle2, 17, 0.0 },
    { &kCTUIFontTextStyleTitle3, 15, 0.0 },
    { &kCTUIFontTextStyleTitle4, 13, 0.0 },
    { &kCTUIFontTextStyleHeadline, 13, 0.4 },
    { &kCTUIFontTextStyleShortHeadline, 13, 0.4 },
    { &kCTUIFontTextStyleBody, 13, 0.0 },
    { &kCTUIFontTextStyleShortBody, 13, 0.0 },
    { &kCTUIFontTextStyleTallBody, 13, 0.0 },
    { &kCTUIFontTextStyleSubhead, 11, 0.0 },
    { &kCTUIFontTextStyleShortSubhead, 11, 0.0 },
    { &kCTUIFontTextStyleFootnote, 10, 0.0 },
    { &kCTUIFontTextStyleShortFootnote, 10, 0.0 },
    { &kCTUIFontTextStyleCaption1, 10, 0.0 },
    { &kCTUIFontTextStyleShortCaption1, 10, 0.0 },
    { &kCTUIFontTextStyleCaption2, 10, 0.0 }
};

static void lookUpTextStyle(CFStringRef style, CGFloat* outSize, CGFloat* outWeight)
{
    size_t i;

    *outSize = 13;
    *outWeight = 0.0;
    if (!style)
        return;
    for (i = 0; i < sizeof(textStyleTable) / sizeof(textStyleTable[0]); ++i) {
        if (CFStringCompare(style, *textStyleTable[i].style, 0) == kCFCompareEqualTo) {
            *outSize = textStyleTable[i].size;
            *outWeight = textStyleTable[i].weight;
            return;
        }
    }
}

CGFloat CTFontDescriptorGetTextStyleSize(CFStringRef style, CFTypeRef sizeCategory,
    CTFontTextStylePlatform platform, CGFloat* weight, CGFloat* lineSpacing)
{
    CGFloat size, styleWeight;

    (void)sizeCategory; /* Tiger has no content size category. */
    (void)platform;
    lookUpTextStyle(style, &size, &styleWeight);
    if (weight)
        *weight = styleWeight;
    if (lineSpacing)
        *lineSpacing = size * 1.2;
    return size;
}

CTFontDescriptorRef CTFontDescriptorCreateWithTextStyle(CFStringRef style, CFStringRef sizeCategory,
    CFStringRef language)
{
    CGFloat size, weight;
    CTFontDescriptorRef base;

    (void)sizeCategory;
    lookUpTextStyle(style, &size, &weight);
    base = CTFontDescriptorCreateForUIType(kCTFontUIFontSystem, size, language);
    if (base && weight > 0.0) {
        CTFontDescriptorRef bold = CTFontDescriptorCreateCopyWithSymbolicTraits(base,
            kCTFontTraitBold, kCTFontTraitBold);
        if (bold) {
            CFRelease(base);
            base = bold;
        }
    }
    return base;
}

CTFontDescriptorRef CTFontDescriptorCreateWithTextStyleAndAttributes(CFStringRef style,
    CFStringRef sizeCategory, CFDictionaryRef attributes)
{
    CTFontDescriptorRef base = CTFontDescriptorCreateWithTextStyle(style, sizeCategory, NULL);
    CTFontDescriptorRef result;

    if (!base || !attributes)
        return base;
    result = CTFontDescriptorCopyWithAttributes(base, attributes);
    CFRelease(base);
    return result;
}

CTFontDescriptorRef CTFontDescriptorCreateForCSSFamily(CFStringRef cssFamily, CFStringRef language)
{
    CFStringRef name;

    if (!cssFamily)
        return NULL;
    if (CFStringCompare(cssFamily, kCTFontCSSFamilySystemUI, 0) == kCFCompareEqualTo)
        return CTFontDescriptorCreateForUIType(kCTFontUIFontSystem, 0, language);
    if (CFStringCompare(cssFamily, kCTFontCSSFamilySerif, 0) == kCFCompareEqualTo)
        name = CFSTR("Times");
    else if (CFStringCompare(cssFamily, kCTFontCSSFamilySansSerif, 0) == kCFCompareEqualTo)
        name = CFSTR("Helvetica");
    else if (CFStringCompare(cssFamily, kCTFontCSSFamilyMonospace, 0) == kCFCompareEqualTo)
        name = CFSTR("Courier");
    else if (CFStringCompare(cssFamily, kCTFontCSSFamilyCursive, 0) == kCFCompareEqualTo)
        name = CFSTR("Apple Chancery");
    else if (CFStringCompare(cssFamily, kCTFontCSSFamilyFantasy, 0) == kCFCompareEqualTo)
        name = CFSTR("Papyrus");
    else
        return NULL;
    return CTFontDescriptorCreateWithNameAndSize(name, 0);
}

/* ---- tables ------------------------------------------------------------ */

/* Tiger's CTFontCopyTable is keyed by the table's four-character name as a
 * CFString, not by an integer tag. Its own kCTFontTableGSUB and friends are
 * CFStrings holding exactly those four characters, so the conversion is the
 * obvious one. */
static CFStringRef createTableName(CTFontTableTag tag)
{
    char name[5];

    name[0] = (char)(tag >> 24);
    name[1] = (char)(tag >> 16);
    name[2] = (char)(tag >> 8);
    name[3] = (char)tag;
    name[4] = '\0';
    return CFStringCreateWithCString(NULL, name, kCFStringEncodingMacRoman);
}

bool CTFontHasTable(CTFontRef font, CTFontTableTag tag)
{
    CFStringRef name;
    CFDataRef table;

    if (!font)
        return false;
    name = createTableName(tag);
    if (!name)
        return false;
    table = CTFontCopyTable(font, name);
    CFRelease(name);
    if (!table)
        return false;
    CFRelease(table);
    return true;
}

/* Tier 1. Tiger's ATS hands over the font's real sfnt table directory, so there
 * is no need to guess which tables exist: ATSFontGetTableDirectory is declared
 * in the 10.4u SDK and exported by the box. The buffer it fills is a plain sfnt
 * offset table (uint32 version, uint16 numTables, three more uint16) followed
 * by numTables 16-byte records whose first four bytes are the tag, all
 * big-endian. Leopard's CTFontCopyAvailableTables reads the same directory. */
CFArrayRef CTFontCopyAvailableTables(CTFontRef font, CTFontTableOptions options)
{
    ATSFontRef atsFont;
    ByteCount size = 0;
    unsigned char* directory;
    CFMutableArrayRef result;
    unsigned tableCount, i;

    (void)options;
    if (!font)
        return NULL;

    atsFont = CTFontGetPlatformFont(font, NULL);
    if (!atsFont)
        return NULL;
    if (ATSFontGetTableDirectory(atsFont, 0, NULL, &size) != noErr || size < 12)
        return NULL;

    directory = (unsigned char*)malloc((size_t)size);
    if (!directory)
        return NULL;
    if (ATSFontGetTableDirectory(atsFont, size, directory, &size) != noErr) {
        free(directory);
        return NULL;
    }

    tableCount = (unsigned)CFSwapInt16BigToHost(*(const UInt16*)(directory + 4));
    if (12 + (size_t)tableCount * 16 > (size_t)size)
        tableCount = (unsigned)((size - 12) / 16);

    /* Tags are stored as values, not objects, exactly as CoreText does it:
     * callers read them back with (CTFontTableTag)(uintptr_t)element. */
    result = CFArrayCreateMutable(NULL, (CFIndex)tableCount, NULL);
    for (i = 0; i < tableCount; ++i) {
        UInt32 tag = CFSwapInt32BigToHost(*(const UInt32*)(directory + 12 + i * 16));
        CFArrayAppendValue(result, (const void*)(uintptr_t)tag);
    }
    free(directory);
    return result;
}

/* ---- glyphs ------------------------------------------------------------ */

bool CTFontGetGlyphsForCharacterRange(CTFontRef font, CGGlyph glyphs[], CFRange range)
{
    UniChar buffer[256];
    CFIndex done = 0;
    bool complete = true;

    if (!font || range.length <= 0)
        return false;
    /* Glyph pages never cross the BMP, and one glyph per character only holds
     * below U+10000; refuse rather than mis-fill. */
    if (range.location < 0 || range.location + range.length > 0x10000)
        return false;

    while (done < range.length) {
        CFIndex chunk = range.length - done;
        CFIndex i;

        if (chunk > (CFIndex)(sizeof(buffer) / sizeof(buffer[0])))
            chunk = (CFIndex)(sizeof(buffer) / sizeof(buffer[0]));
        for (i = 0; i < chunk; ++i)
            buffer[i] = (UniChar)(range.location + done + i);
        if (!CTFontGetGlyphsForCharacters(font, buffer, glyphs + done, chunk))
            complete = false;
        done += chunk;
    }
    return complete;
}

bool CTFontGetVerticalGlyphsForCharacters(CTFontRef font, const UniChar characters[],
    CGGlyph glyphs[], CFIndex count)
{
    /* Tiger has no vertical glyph substitution; the upright forms are all there is. */
    return font ? CTFontGetGlyphsForCharacters(font, characters, glyphs, count) : false;
}

/* The vertical origin of a glyph, in font units, from the font's VORG table.
 * Returns 0 and sets *found to 0 when the font has no VORG. VORG is
 * { uint16 major, uint16 minor, int16 defaultVertOriginY,
 *   uint16 numVertOriginYMetrics, { uint16 glyph, int16 vertOriginY }[] },
 * big-endian, with the entries sorted by glyph index. */
static int verticalOriginsFromVORG(CTFontRef font, const CGGlyph glyphs[], CFIndex count, short* origins)
{
    CFStringRef name = createTableName(kCTFontTableVORG);
    CFDataRef table = NULL;
    const unsigned char* bytes;
    CFIndex length, i;
    short defaultOrigin;
    unsigned entryCount, entry;

    if (name) {
        table = CTFontCopyTable(font, name);
        CFRelease(name);
    }
    if (!table)
        return 0;

    bytes = CFDataGetBytePtr(table);
    length = CFDataGetLength(table);
    if (length < 8) {
        CFRelease(table);
        return 0;
    }
    defaultOrigin = (short)CFSwapInt16BigToHost(*(const UInt16*)(bytes + 4));
    entryCount = (unsigned)CFSwapInt16BigToHost(*(const UInt16*)(bytes + 6));
    if (8 + (CFIndex)entryCount * 4 > length)
        entryCount = (unsigned)((length - 8) / 4);

    for (i = 0; i < count; ++i)
        origins[i] = defaultOrigin;
    /* Entries are sorted, but a glyph run is short and unsorted, so a linear
     * scan per glyph would be O(n*m). Walk the table once instead and patch the
     * glyphs it mentions. */
    for (entry = 0; entry < entryCount; ++entry) {
        const unsigned char* record = bytes + 8 + entry * 4;
        CGGlyph glyph = (CGGlyph)CFSwapInt16BigToHost(*(const UInt16*)record);
        short origin = (short)CFSwapInt16BigToHost(*(const UInt16*)(record + 2));
        for (i = 0; i < count; ++i) {
            if (glyphs[i] == glyph)
                origins[i] = origin;
        }
    }
    CFRelease(table);
    return 1;
}

void CTFontGetVerticalTranslationsForGlyphs(CTFontRef font, const CGGlyph glyphs[],
    CGSize translations[], CFIndex count)
{
    CGSize* advances;
    short* origins;
    CGFloat ascent, scale;
    CFIndex i;

    if (!font || count <= 0)
        return;

    /* Apple does this through CGGetGlyphDeviceMetrics, a CoreGraphics private
     * that Tiger's CoreGraphics does not export, so neither tier 1 nor tier 2
     * is reachable. The next best thing is the font's own VORG table, which is
     * where a CJK font records its per-glyph vertical origin; without one, the
     * ascent is the conventional origin. The horizontal half is always half the
     * advance, which centres the glyph on the vertical baseline. */
    ascent = (CGFloat)CTFontGetAscent(font);
    advances = (CGSize*)calloc((size_t)count, sizeof(CGSize));
    origins = (short*)calloc((size_t)count, sizeof(short));
    if (!advances || !origins) {
        for (i = 0; i < count; ++i)
            translations[i] = CGSizeMake(0, -ascent);
        free(advances);
        free(origins);
        return;
    }

    CTFontGetAdvancesForGlyphs(font, glyphs, advances, count);

    scale = 0;
    if (verticalOriginsFromVORG(font, glyphs, count, origins)) {
        CGFloat unitsPerEm = (CGFloat)CTFontGetUnitsPerEm(font);
        if (unitsPerEm > 0)
            scale = (CGFloat)CTFontGetSize(font) / unitsPerEm;
    }

    for (i = 0; i < count; ++i) {
        CGFloat originY = scale > 0 ? origins[i] * scale : ascent;
        translations[i] = CGSizeMake(-advances[i].width / 2, -originY);
    }
    free(advances);
    free(origins);
}

CGPathRef CTFontCreatePathForGlyph(CTFontRef font, CGGlyph glyph, const CGAffineTransform* matrix)
{
    CGFontRef cgFont;
    CGAffineTransform combined;
    CGPathRef path;

    if (!font)
        return NULL;
    cgFont = CTFontGetGraphicsFont(font, NULL);
    if (!cgFont)
        return NULL;

    /* CGFontGetGlyphPath returns the path in em units, so scale by size/upem
     * the way CTFontCreatePathForGlyph does, then apply the caller's matrix. */
    {
        CGFloat size = (CGFloat)CTFontGetSize(font);
        CGFloat upem = (CGFloat)CGFontGetUnitsPerEm(cgFont);
        CGAffineTransform scale = CGAffineTransformMakeScale(size / upem, size / upem);
        CGAffineTransform fontMatrix = CTFontGetMatrix(font);
        combined = CGAffineTransformConcat(scale, fontMatrix);
        if (matrix)
            combined = CGAffineTransformConcat(combined, *matrix);
    }

    path = CGFontGetGlyphPath(cgFont, &combined, 0, glyph);
    /* CGFontGetGlyphPath follows the Get rule; CTFontCreatePathForGlyph follows
     * the Create rule, so hand back a copy the caller owns. */
    return path ? CGPathCreateCopy(path) : NULL;
}

void CTFontDrawGlyphs(CTFontRef font, const CGGlyph glyphs[], const CGPoint positions[],
    size_t count, CGContextRef context)
{
    CGFontRef cgFont;
    size_t i;

    if (!font || !context || !count)
        return;
    cgFont = CTFontGetGraphicsFont(font, NULL);
    if (!cgFont)
        return;

    CGContextSaveGState(context);
    CGContextSetFont(context, cgFont);
    CGContextSetFontSize(context, (float)CTFontGetSize(font));
    /* Tiger has no CGContextShowGlyphsAtPositions, and ...WithAdvances would
     * mean converting absolute positions to deltas and losing the y offsets. */
    for (i = 0; i < count; ++i)
        CGContextShowGlyphsAtPoint(context, positions[i].x, positions[i].y, &glyphs[i], 1);
    CGContextRestoreGState(context);
}

bool CTFontTransformGlyphs(CTFontRef font, CGGlyph glyphs[], CGSize advances[], CFIndex count,
    CTFontTransformOptions options)
{
    (void)options;
    if (font && advances && count > 0)
        CTFontGetAdvancesForGlyphs(font, glyphs, advances, count);
    return false; /* No glyph was deleted, because nothing was shaped. */
}

/* ---- font fallback ----------------------------------------------------- */

CTFontRef CTFontCreateForCharactersWithLanguageAndOption(CTFontRef currentFont,
    const UniChar characters[], CFIndex length, CFStringRef language,
    CTFontFallbackOption option, CFIndex* coveredLength)
{
    CFStringRef string;
    CTFontRef fallback;

    (void)language; /* Tiger's fallback is not language-aware... */
    (void)option;   /* ...and cannot be restricted to system or user fonts. */

    if (coveredLength)
        *coveredLength = 0;
    if (!currentFont || length <= 0)
        return NULL;

    string = CFStringCreateWithCharacters(NULL, characters, length);
    if (!string)
        return NULL;
    fallback = CTFontCreateForString(currentFont, string, CFRangeMake(0, length));
    CFRelease(string);
    if (!fallback)
        return NULL;

    if (coveredLength) {
        /* CTFontCreateForString does not report coverage, so measure it: count
         * the leading characters the chosen font actually has glyphs for. */
        CFIndex i;
        for (i = 0; i < length; ++i) {
            CGGlyph glyph = 0;
            if (!CTFontGetGlyphsForCharacters(fallback, &characters[i], &glyph, 1) || !glyph)
                break;
        }
        *coveredLength = i ? i : length;
    }
    return fallback;
}

CTFontRef CTFontCreateForCharactersWithLanguage(CTFontRef currentFont, const UniChar characters[],
    CFIndex length, CFStringRef language, CFIndex* coveredLength)
{
    return CTFontCreateForCharactersWithLanguageAndOption(currentFont, characters, length, language,
        kCTFontFallbackOptionDefault, coveredLength);
}

CTFontRef CTFontCreateForCharacters(CTFontRef currentFont, const UniChar characters[],
    CFIndex length, CFIndex* coveredLength)
{
    return CTFontCreateForCharactersWithLanguageAndOption(currentFont, characters, length, NULL,
        kCTFontFallbackOptionDefault, coveredLength);
}

CTFontDescriptorRef CTFontCreatePhysicalFontDescriptorForCharactersWithLanguage(CTFontRef currentFont,
    const UniChar characters[], CFIndex length, CFStringRef language, CFIndex* coveredLength)
{
    CTFontRef font = CTFontCreateForCharactersWithLanguageAndOption(currentFont, characters, length,
        language, kCTFontFallbackOptionDefault, coveredLength);
    CTFontDescriptorRef descriptor;

    if (!font)
        return NULL;
    descriptor = CTFontCopyFontDescriptor(font);
    CFRelease(font);
    return descriptor;
}

CTFontRef CTFontCreateForCSS(CFStringRef name, uint16_t weight, CTFontSymbolicTraits traits, CGFloat size)
{
    CTFontDescriptorRef descriptor = CTFontDescriptorCreateWithNameAndSize(name, size);
    CTFontRef font;

    (void)weight; /* Tiger matches weight only through the symbolic bold bit. */
    if (!descriptor)
        return NULL;
    if (traits) {
        CTFontDescriptorRef withTraits = CTFontDescriptorCreateCopyWithSymbolicTraits(descriptor, traits, traits);
        if (withTraits) {
            CFRelease(descriptor);
            descriptor = withTraits;
        }
    }
    font = CTFontCreateWithFontDescriptor(descriptor, size, NULL);
    CFRelease(descriptor);
    return font;
}

/* ---- web fonts: bytes in, descriptor out ------------------------------- */

/* ATS reads the caller's buffer for as long as the container is active and
 * Tiger has no way to copy it in, so every activated blob is retained for the
 * process lifetime. A page loading many @font-face rules leaks their bytes.
 * ponytail: deliberate leak, add a container cache keyed on the data if web
 * font churn ever shows up in RPRVT. */
static CFMutableArrayRef activatedFontData(void)
{
    static CFMutableArrayRef retained = NULL;
    if (!retained)
        retained = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    return retained;
}

/* Activates the bytes with ATS and returns the container, or 0. On success the
 * data is retained forever. */
static ATSFontContainerRef activateFontData(CFDataRef data)
{
    ATSFontContainerRef container = 0;
    CFMutableArrayRef keepAlive;
    OSStatus status;

    if (!data || !CFDataGetLength(data))
        return 0;

    keepAlive = activatedFontData();
    if (!keepAlive)
        return 0;
    CFArrayAppendValue(keepAlive, data);

    status = ATSFontActivateFromMemory((void*)CFDataGetBytePtr(data), (ByteCount)CFDataGetLength(data),
        kATSFontContextLocal, kATSFontFormatUnspecified, NULL, kATSOptionFlagsDefault, &container);
    if (status != noErr || !container) {
        CFArrayRemoveValueAtIndex(keepAlive, CFArrayGetCount(keepAlive) - 1);
        return 0;
    }
    return container;
}

/* Every font in the container, as descriptors named by PostScript name. Once
 * ATS knows the font, Tiger's own CoreText resolves such a descriptor, which is
 * what lets the rest of WebCore's descriptor plumbing work untouched. */
static CFArrayRef descriptorsForContainer(ATSFontContainerRef container)
{
    ATSFontRef stackFonts[8];
    ATSFontRef* fonts = stackFonts;
    ItemCount count = 0, i;
    CFMutableArrayRef result;

    if (!container)
        return NULL;
    if (ATSFontFindFromContainer(container, kATSOptionFlagsDefault, 0, NULL, &count) != noErr || !count)
        return NULL;
    if (count > sizeof(stackFonts) / sizeof(stackFonts[0])) {
        fonts = (ATSFontRef*)calloc((size_t)count, sizeof(ATSFontRef));
        if (!fonts)
            return NULL;
    }
    if (ATSFontFindFromContainer(container, kATSOptionFlagsDefault, count, fonts, &count) != noErr) {
        if (fonts != stackFonts)
            free(fonts);
        return NULL;
    }

    result = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    for (i = 0; i < count; ++i) {
        CFStringRef psName = NULL;
        if (ATSFontGetPostScriptName(fonts[i], kATSOptionFlagsDefault, &psName) == noErr && psName) {
            CTFontDescriptorRef descriptor = CTFontDescriptorCreateWithNameAndSize(psName, 0);
            if (descriptor) {
                CFArrayAppendValue(result, descriptor);
                CFRelease(descriptor);
            }
            CFRelease(psName);
        }
    }
    if (fonts != stackFonts)
        free(fonts);

    if (!CFArrayGetCount(result)) {
        CFRelease(result);
        return NULL;
    }
    return result;
}

CFArrayRef CTFontManagerCreateFontDescriptorsFromData(CFDataRef data)
{
    return descriptorsForContainer(activateFontData(data));
}

CTFontDescriptorRef CTFontManagerCreateFontDescriptorFromData(CFDataRef data)
{
    CFArrayRef descriptors = CTFontManagerCreateFontDescriptorsFromData(data);
    CTFontDescriptorRef result = NULL;

    if (descriptors) {
        result = (CTFontDescriptorRef)CFRetain(CFArrayGetValueAtIndex(descriptors, 0));
        CFRelease(descriptors);
    }
    return result;
}

CTFontDescriptorRef CTFontManagerCreateMemorySafeFontDescriptorFromData(CFDataRef data)
{
    /* There is no hardened font parser on Tiger. This is why the
     * HAVE(CTFONTMANAGER_CREATEMEMORYSAFEFONTDESCRIPTORFROMDATA) gate should be
     * off: callers asking for the safe parser must not silently get ATS. */
    return CTFontManagerCreateFontDescriptorFromData(data);
}

static CFDataRef createDataFromURL(CFURLRef url)
{
    CFDataRef data = NULL;
    SInt32 error = 0;

    if (!url)
        return NULL;
    if (!CFURLCreateDataAndPropertiesFromResource(NULL, url, &data, NULL, NULL, &error))
        return NULL;
    return data;
}

CFArrayRef CTFontManagerCreateFontDescriptorsFromURL(CFURLRef url)
{
    CFDataRef data = createDataFromURL(url);
    CFArrayRef result;

    if (!data)
        return NULL;
    result = CTFontManagerCreateFontDescriptorsFromData(data);
    CFRelease(data); /* activateFontData took its own reference on success. */
    return result;
}

bool CTFontManagerRegisterFontsForURL(CFURLRef url, CTFontManagerScope scope, CFErrorRef* error)
{
    CFArrayRef descriptors;

    (void)scope; /* ATS activation here is always process-local. */
    if (error)
        *error = NULL;
    descriptors = CTFontManagerCreateFontDescriptorsFromURL(url);
    if (!descriptors)
        return false;
    CFRelease(descriptors);
    return true;
}

bool CTFontManagerEnableAllUserFonts(bool postFontChangeNotification)
{
    (void)postFontChangeNotification;
    return true; /* Tiger activates user fonts at login; nothing to enable. */
}

/* ---- lines, runs, frames ----------------------------------------------- */

CGRect CTLineGetBoundsWithOptions(CTLineRef line, CTLineBoundsOptions options)
{
    CGFloat ascent = 0, descent = 0, leading = 0;
    double width;

    if (!line)
        return CGRectZero;
    width = CTLineGetTypographicBounds(line, CFRangeMake(0, 0), &ascent, &descent, &leading);
    if (options & kCTLineBoundsExcludeTypographicLeading)
        leading = 0;
    /* Same shape as CoreText's: origin on the baseline at the line's left edge. */
    return CGRectMake(0, -descent - leading, (CGFloat)width, ascent + descent + leading);
}

double CTLineGetTrailingWhitespaceWidth(CTLineRef line)
{
    CFArrayRef runs;
    CFIndex runIndex;
    double width = 0;

    /* Apple computes this inside TLine::CountTrailingWhitespaceChars, which
     * walks the line's stored characters. Tiger's CoreText has that method too
     * but only as a local symbol, so it cannot be linked against, and the line
     * does not hand back its string. What a run does give back is its glyphs
     * and advances, so measure instead: walk backwards summing the advances of
     * glyphs that are this run's space glyph. That catches the ordinary case,
     * a line broken at spaces, which is the only one WebCore measures. */
    if (!line)
        return 0;
    runs = CTLineGetGlyphRuns(line);
    if (!runs)
        return 0;

    for (runIndex = CFArrayGetCount(runs) - 1; runIndex >= 0; --runIndex) {
        CTRunRef run = (CTRunRef)CFArrayGetValueAtIndex(runs, runIndex);
        CFDictionaryRef attributes = CTRunGetAttributes(run);
        CTFontRef runFont = attributes ? (CTFontRef)CFDictionaryGetValue(attributes, kCTFontAttributeName) : NULL;
        const CGGlyph* glyphs = CTRunGetGlyphsPtr(run);
        const CGSize* advances = CTRunGetAdvancesPtr(run);
        CFIndex glyphCount = CTRunGetGlyphCount(run);
        CGGlyph spaceGlyph = 0;
        UniChar space = ' ';
        CFIndex i;

        if (!runFont || !glyphs || !advances || glyphCount <= 0)
            break;
        if (!CTFontGetGlyphsForCharacters(runFont, &space, &spaceGlyph, 1) || !spaceGlyph)
            break;

        for (i = glyphCount - 1; i >= 0; --i) {
            if (glyphs[i] != spaceGlyph)
                return width;
            width += advances[i].width;
        }
        /* The whole run was whitespace; keep going into the run before it. */
    }
    return width;
}

CGSize CTRunGetInitialAdvance(CTRunRef run)
{
    (void)run;
    return CGSizeZero; /* Tiger has no glyph origins, hence no initial advance. */
}

void CTRunGetBaseAdvancesAndOrigins(CTRunRef run, CFRange range, CGSize baseAdvances[], CGPoint origins[])
{
    CFIndex count;

    if (!run)
        return;
    count = range.length ? range.length : CTRunGetGlyphCount(run) - range.location;
    if (count <= 0)
        return;
    if (baseAdvances) {
        /* Tiger exports CTRunGetAdvances but its body returns immediately, so
         * the only real source of advances is the pointer variant. */
        const CGSize* advances = CTRunGetAdvancesPtr(run);
        if (advances)
            memcpy(baseAdvances, advances + range.location, (size_t)count * sizeof(CGSize));
        else
            memset(baseAdvances, 0, (size_t)count * sizeof(CGSize));
    }
    if (origins)
        memset(origins, 0, (size_t)count * sizeof(CGPoint));
}

CTTypesetterRef CTTypesetterCreateWithUniCharProviderAndOptions(
    const UniChar* (*provider)(CFIndex, CFIndex*, CFDictionaryRef*, void*),
    void (*dispose)(const UniChar*, void*), void* refCon, CFDictionaryRef options)
{
    /* The only option WebCore passes is kCTTypesetterOptionForcedEmbeddingLevel,
     * which Tiger cannot honour: RTL runs fall back to the Bidi algorithm's own
     * reading of the substring. */
    (void)options;
    return CTTypesetterCreateWithUniCharProvider(provider, dispose, refCon);
}

void CTParagraphStyleSetCompositionLanguage(CTParagraphStyleRef style, CTCompositionLanguage language)
{
    /* Tiger's paragraph style is immutable and has no composition language. */
    (void)style;
    (void)language;
}

void CTFrameGetLineOrigins(CTFrameRef frame, CFRange range, CGPoint origins[])
{
    CFArrayRef lines;
    CGRect box;
    CGFloat y;
    CFIndex lineCount, first, last, i;

    if (!frame || !origins)
        return;
    lines = CTFrameGetLines(frame);
    if (!lines)
        return;
    lineCount = CFArrayGetCount(lines);
    box = CGPathGetBoundingBox(CTFrameGetPath(frame));

    first = range.location;
    last = range.length ? first + range.length : lineCount;
    if (last > lineCount)
        last = lineCount;

    /* Walk every line from the top of the frame so that the requested slice
     * lands where it would have, then emit only that slice. */
    y = CGRectGetMaxY(box);
    for (i = 0; i < last; ++i) {
        CGFloat ascent = 0, descent = 0, leading = 0;
        CTLineGetTypographicBounds((CTLineRef)CFArrayGetValueAtIndex(lines, i), CFRangeMake(0, 0),
            &ascent, &descent, &leading);
        y -= ascent;
        if (i >= first)
            origins[i - first] = CGPointMake(CGRectGetMinX(box), y);
        y -= descent + leading;
    }
}

CGSize CTFramesetterSuggestFrameSizeWithConstraints(CTFramesetterRef framesetter, CFRange range,
    CFDictionaryRef attributes, CGSize constraints, CFRange* fitRange)
{
    CGMutablePathRef path;
    CTFrameRef frame;
    CFArrayRef lines;
    CGFloat width = 0, height = 0;
    CFIndex i, count;

    if (fitRange)
        *fitRange = CFRangeMake(0, 0);
    if (!framesetter)
        return CGSizeZero;

    /* CGFLOAT_MAX in a path upsets Tiger's layout; a metre of text is plenty. */
    if (!(constraints.width > 0) || constraints.width > 1.0e6)
        constraints.width = 1.0e6;
    if (!(constraints.height > 0) || constraints.height > 1.0e6)
        constraints.height = 1.0e6;

    path = CGPathCreateMutable();
    CGPathAddRect(path, NULL, CGRectMake(0, 0, constraints.width, constraints.height));
    frame = CTFramesetterCreateFrame(framesetter, range, path, attributes);
    CGPathRelease(path);
    if (!frame)
        return CGSizeZero;

    lines = CTFrameGetLines(frame);
    count = lines ? CFArrayGetCount(lines) : 0;
    for (i = 0; i < count; ++i) {
        CGFloat ascent = 0, descent = 0, leading = 0;
        double lineWidth = CTLineGetTypographicBounds((CTLineRef)CFArrayGetValueAtIndex(lines, i),
            CFRangeMake(0, 0), &ascent, &descent, &leading);
        if ((CGFloat)lineWidth > width)
            width = (CGFloat)lineWidth;
        height += ascent + descent + leading;
    }
    if (fitRange)
        *fitRange = CTFrameGetVisibleStringRange(frame);
    CFRelease(frame);
    return CGSizeMake(width, height);
}

/* ======================================================================== */
/* (d) infeasible on Tiger: honest degradations, all documented in           */
/*     CT-SURVEY.md so the callers can be gated instead                      */
/* ======================================================================== */

CGSize CTFontShapeGlyphs(CTFontRef font, CGGlyph glyphs[], CGSize advances[], CGPoint origins[],
    CFIndex indexes[], const UniChar chars[], CFIndex count, CTFontShapeOptions options,
    CFStringRef language, void (^handler)(CFRange, CGGlyph**, CGSize**, CGPoint**, CFIndex**))
{
    /* Tiger's shaping engine is ATSUI, reachable only through CTTypesetter, and
     * there is no entry point that shapes into caller-owned arrays. This fills
     * unshaped advances and leaves the glyphs alone: correct for simple Latin,
     * wrong for ligatures, kerning, marks and anything bidirectional. Complex
     * text has to go through ComplexTextController, which uses CTTypesetter and
     * does work here. See CT-SURVEY.md (d). */
    CFIndex i;

    (void)origins;
    (void)chars;
    (void)options;
    (void)language;
    (void)handler;

    if (indexes) {
        for (i = 0; i < count; ++i)
            indexes[i] = i;
    }
    if (font && advances && count > 0)
        CTFontGetAdvancesForGlyphs(font, glyphs, advances, count);
    return CGSizeZero;
}

CFBitVectorRef CTFontCopyGlyphCoverageForFeature(CTFontRef font, CFDictionaryRef feature)
{
    /* Would mean parsing morx/GSUB by hand. Empty coverage makes WebCore fall
     * back to synthesised small caps. */
    (void)font;
    (void)feature;
    return NULL;
}

CFBitVectorRef CTFontCopyColorGlyphCoverage(CTFontRef font)
{
    (void)font;
    return NULL; /* Tiger has no color font format: no sbix, COLR or CBDT. */
}

bool CTFontIsAppleColorEmoji(CTFontRef font)
{
    (void)font;
    return false; /* Apple Color Emoji postdates Tiger by six years. */
}

bool CTFontHasComplexColorFormatForGlyph(CTFontRef font, CGGlyph glyph)
{
    (void)font;
    (void)glyph;
    return false;
}

CGFloat CTFontGetSbixImageSizeForGlyphAndContentsScale(CTFontRef font, CGGlyph glyph, CGFloat contentsScale)
{
    (void)font;
    (void)glyph;
    (void)contentsScale;
    return 0;
}

CGRect CTFontGetTypographicBoundsForAdaptiveImageProvider(CTFontRef font, CFTypeRef provider)
{
    (void)font;
    (void)provider;
    return CGRectZero;
}

void CTFontDrawImageFromAdaptiveImageProviderAtPoint(CTFontRef font, CFTypeRef provider,
    CGPoint point, CGContextRef context)
{
    (void)font;
    (void)provider;
    (void)point;
    (void)context;
}

void CTFontGetUnsummedAdvancesForGlyphsAndStyle(CTFontRef font, CTFontOrientation orientation,
    uint32_t renderingStyle, const CGGlyph glyphs[], CGSize advances[], CFIndex count)
{
    /* Tiger has no rendering-style-aware metrics; the summed advances are all
     * there is, and for a single glyph they are the same thing. */
    (void)renderingStyle;
    (void)orientation; /* Tiger's advances take no orientation. */
    if (font && advances && count > 0)
        CTFontGetAdvancesForGlyphs(font, glyphs, advances, count);
}

CGFloat CTFontGetAccessibilityBoldWeightOfWeight(CGFloat weight)
{
    return weight; /* No bold-text accessibility setting on Tiger. */
}

/* ======================================================================== */
/* CoreGraphics font SPI reachable only through CoreText                    */
/* ======================================================================== */

/* Tier 1 for all three: wrap the CGFont in a CTFont and ask CoreText, which is
 * the only thing on Tiger that knows how to answer. Owned here rather than in
 * cgcompat.c so that the CoreGraphics compat file needs no CoreText. */

CFStringRef CGFontCopyFamilyName(CGFontRef cgFont)
{
    CTFontRef font;
    CFStringRef family;

    if (!cgFont)
        return NULL;
    font = CTFontCreateWithGraphicsFont(cgFont, 12.0, NULL, NULL);
    if (!font)
        return NULL;
    family = CTFontCopyFamilyName(font);
    CFRelease(font);
    return family;
}

void CGFontGetGlyphsForUnichars(CGFontRef cgFont, const UniChar characters[], CGGlyph glyphs[],
    size_t count)
{
    CTFontRef font;

    if (!cgFont || !count)
        return;
    memset(glyphs, 0, count * sizeof(CGGlyph));
    font = CTFontCreateWithGraphicsFont(cgFont, 12.0, NULL, NULL);
    if (!font)
        return;
    CTFontGetGlyphsForCharacters(font, characters, glyphs, (CFIndex)count);
    CFRelease(font);
}

bool CGFontGetGlyphAdvancesForStyle(CGFontRef cgFont, const CGAffineTransform* matrix,
    CGFontRenderingStyle style, const CGGlyph glyphs[], size_t count, CGSize advances[])
{
    CTFontRef font;

    /* Tiger has no rendering styles, so the hinted and unhinted advances are
     * the same number. The transform becomes the font matrix at unit size, so
     * the advances come back in the caller's text space. */
    (void)style;
    if (!cgFont || !count || !advances)
        return false;
    font = CTFontCreateWithGraphicsFont(cgFont, 1.0, matrix, NULL);
    if (!font)
        return false;
    CTFontGetAdvancesForGlyphs(font, glyphs, advances, (CFIndex)count);
    CFRelease(font);
    return true;
}

/* ======================================================================== */
/* data symbols Tiger's CoreText does not export                            */
/* ======================================================================== */

/* Tiger's CT ignores attribute keys it does not know, so for most of these the
 * string value only has to be unique. The two aliases below are real: they name
 * attributes Tiger does have, under Tiger's older spelling. */
const CFStringRef kCTFontURLAttribute = (const CFStringRef)CFSTR("NSCTFontFileURLAttribute");
const CFStringRef kCTFontVariationAxesAttribute = (const CFStringRef)CFSTR("NSCTFontVariationAttribute");

const CFStringRef kCTFontReferenceURLAttribute = (const CFStringRef)CFSTR("NSCTFontReferenceURLAttribute");
const CFStringRef kCTFontPostScriptNameAttribute = (const CFStringRef)CFSTR("NSCTFontPostScriptNameAttribute");
const CFStringRef kCTFontOpticalSizeAttribute = (const CFStringRef)CFSTR("NSCTFontOpticalSizeAttribute");
const CFStringRef kCTFontUserInstalledAttribute = (const CFStringRef)CFSTR("NSCTFontUserInstalledAttribute");
const CFStringRef kCTFontEnabledAttribute = (const CFStringRef)CFSTR("NSCTFontEnabledAttribute");
const CFStringRef kCTFontFallbackOptionAttribute = (const CFStringRef)CFSTR("NSCTFontFallbackOptionAttribute");
const CFStringRef kCTFontDescriptorLanguageAttribute = (const CFStringRef)CFSTR("NSCTFontDescriptorLanguageAttribute");
const CFStringRef kCTFontDescriptorTextStyleAttribute = (const CFStringRef)CFSTR("NSCTFontDescriptorTextStyleAttribute");
const CFStringRef kCTFontDescriptorTextStyleEmphasized = (const CFStringRef)CFSTR("UICTFontDescriptorTextStyleEmphasized");
const CFStringRef kCTFontCSSWeightAttribute = (const CFStringRef)CFSTR("NSCTFontCSSWeightAttribute");
const CFStringRef kCTFontCSSWidthAttribute = (const CFStringRef)CFSTR("NSCTFontCSSWidthAttribute");
const CFStringRef kCTFontSizeCategoryAttribute = (const CFStringRef)CFSTR("NSCTFontSizeCategoryAttribute");
const CFStringRef kCTFontTrackAttribute = (const CFStringRef)CFSTR("NSCTFontTrackAttribute");
const CFStringRef kCTFontUnscaledTrackingAttribute = (const CFStringRef)CFSTR("NSCTFontUnscaledTrackingAttribute");
const CFStringRef kCTFontIgnoreLegibilityWeightAttribute = (const CFStringRef)CFSTR("NSCTFontIgnoreLegibilityWeightAttribute");
const CFStringRef kCTFontOrientationAttribute = (const CFStringRef)CFSTR("NSCTFontOrientationAttribute");
const CFStringRef kCTFontPaletteAttribute = (const CFStringRef)CFSTR("NSCTFontPaletteAttribute");
const CFStringRef kCTFontPaletteColorsAttribute = (const CFStringRef)CFSTR("NSCTFontPaletteColorsAttribute");
const CFStringRef kCTFontGradeTrait = (const CFStringRef)CFSTR("NSCTFontGradeTrait");
const CFStringRef kCTFontUIFontDesignTrait = (const CFStringRef)CFSTR("NSCTFontUIFontDesignTrait");
const CFStringRef kCTFontUIFontDesignDefault = (const CFStringRef)CFSTR("NSCTFontUIFontDesignDefault");
const CFStringRef kCTFontUIFontDesignSerif = (const CFStringRef)CFSTR("NSCTFontUIFontDesignSerif");
const CFStringRef kCTFontUIFontDesignMonospaced = (const CFStringRef)CFSTR("NSCTFontUIFontDesignMonospaced");
const CFStringRef kCTFontUIFontDesignRounded = (const CFStringRef)CFSTR("NSCTFontUIFontDesignRounded");
const CFStringRef kCTFontOpenTypeFeatureTag = (const CFStringRef)CFSTR("CTFeatureOpenTypeTag");
const CFStringRef kCTFontOpenTypeFeatureValue = (const CFStringRef)CFSTR("CTFeatureOpenTypeValue");

const CFStringRef kCTFontCSSFamilySerif = (const CFStringRef)CFSTR("serif");
const CFStringRef kCTFontCSSFamilySansSerif = (const CFStringRef)CFSTR("sans-serif");
const CFStringRef kCTFontCSSFamilyCursive = (const CFStringRef)CFSTR("cursive");
const CFStringRef kCTFontCSSFamilyFantasy = (const CFStringRef)CFSTR("fantasy");
const CFStringRef kCTFontCSSFamilyMonospace = (const CFStringRef)CFSTR("monospace");
const CFStringRef kCTFontCSSFamilySystemUI = (const CFStringRef)CFSTR("system-ui");

const CFStringRef kCTFontContentSizeCategoryL = (const CFStringRef)CFSTR("UICTContentSizeCategoryL");
const CFStringRef kCTFontContentSizeCategoryXXXL = (const CFStringRef)CFSTR("UICTContentSizeCategoryXXXL");
const CFStringRef kCTFontManagerRegisteredFontsChangedNotification =
    (const CFStringRef)CFSTR("CTFontManagerRegisteredFontsChangedNotification");

const CFStringRef kCTLanguageAttributeName = (const CFStringRef)CFSTR("NSLanguage");
const CFStringRef kCTStrokeColorAttributeName = (const CFStringRef)CFSTR("CTStrokeColor");
const CFStringRef kCTStrokeWidthAttributeName = (const CFStringRef)CFSTR("CTStrokeWidth");
const CFStringRef kCTVerticalFormsAttributeName = (const CFStringRef)CFSTR("CTVerticalForms");
const CFStringRef kCTFrameMaximumNumberOfLinesAttributeName = (const CFStringRef)CFSTR("CTFrameMaximumNumberOfLines");
const CFStringRef kCTTypesetterOptionForcedEmbeddingLevel = (const CFStringRef)CFSTR("ForcedEmbeddingLevel");

const CFStringRef kCTUIFontTextStyleTitle0 = (const CFStringRef)CFSTR("UICTFontTextStyleTitle0");
const CFStringRef kCTUIFontTextStyleTitle1 = (const CFStringRef)CFSTR("UICTFontTextStyleTitle1");
const CFStringRef kCTUIFontTextStyleTitle2 = (const CFStringRef)CFSTR("UICTFontTextStyleTitle2");
const CFStringRef kCTUIFontTextStyleTitle3 = (const CFStringRef)CFSTR("UICTFontTextStyleTitle3");
const CFStringRef kCTUIFontTextStyleTitle4 = (const CFStringRef)CFSTR("UICTFontTextStyleTitle4");
const CFStringRef kCTUIFontTextStyleHeadline = (const CFStringRef)CFSTR("UICTFontTextStyleHeadline");
const CFStringRef kCTUIFontTextStyleBody = (const CFStringRef)CFSTR("UICTFontTextStyleBody");
const CFStringRef kCTUIFontTextStyleSubhead = (const CFStringRef)CFSTR("UICTFontTextStyleSubhead");
const CFStringRef kCTUIFontTextStyleFootnote = (const CFStringRef)CFSTR("UICTFontTextStyleFootnote");
const CFStringRef kCTUIFontTextStyleCaption1 = (const CFStringRef)CFSTR("UICTFontTextStyleCaption1");
const CFStringRef kCTUIFontTextStyleCaption2 = (const CFStringRef)CFSTR("UICTFontTextStyleCaption2");
const CFStringRef kCTUIFontTextStyleShortHeadline = (const CFStringRef)CFSTR("UICTFontTextStyleShortHeadline");
const CFStringRef kCTUIFontTextStyleShortBody = (const CFStringRef)CFSTR("UICTFontTextStyleShortBody");
const CFStringRef kCTUIFontTextStyleShortSubhead = (const CFStringRef)CFSTR("UICTFontTextStyleShortSubhead");
const CFStringRef kCTUIFontTextStyleShortFootnote = (const CFStringRef)CFSTR("UICTFontTextStyleShortFootnote");
const CFStringRef kCTUIFontTextStyleShortCaption1 = (const CFStringRef)CFSTR("UICTFontTextStyleShortCaption1");
const CFStringRef kCTUIFontTextStyleTallBody = (const CFStringRef)CFSTR("UICTFontTextStyleTallBody");

/* Apple's documented -1..1 scale. The weights are the well-known values; the
 * widths are the less certain half and only affect how closely WebCore's
 * font-stretch matching lines up with a modern macOS. */
const CGFloat kCTFontWeightUltraLight = -0.8f;
const CGFloat kCTFontWeightThin = -0.6f;
const CGFloat kCTFontWeightLight = -0.4f;
const CGFloat kCTFontWeightRegular = 0.0f;
const CGFloat kCTFontWeightMedium = 0.23f;
const CGFloat kCTFontWeightSemibold = 0.3f;
const CGFloat kCTFontWeightBold = 0.4f;
const CGFloat kCTFontWeightHeavy = 0.56f;
const CGFloat kCTFontWeightBlack = 0.62f;

const CGFloat kCTFontWidthUltraCompressed = -0.5f;
const CGFloat kCTFontWidthExtraCompressed = -0.4f;
const CGFloat kCTFontWidthCompressed = -0.3f;
const CGFloat kCTFontWidthExtraCondensed = -0.25f;
const CGFloat kCTFontWidthCondensed = -0.2f;
const CGFloat kCTFontWidthSemiCondensed = -0.1f;
const CGFloat kCTFontWidthStandard = 0.0f;
const CGFloat kCTFontWidthSemiExpanded = 0.1f;
const CGFloat kCTFontWidthExpanded = 0.2f;
const CGFloat kCTFontWidthExtraExpanded = 0.3f;

/* ======================================================================== */
/* Tiger-ABI adapters: same name as the modern API, different function       */
/*                                                                          */
/* These ten are exported by Tiger's CoreText under exactly the name WebCore */
/* calls, but either take different arguments or do nothing at all, so a     */
/* direct call links, runs and returns zeroes. Each adapter below presents   */
/* the modern signature and reaches Tiger's real behaviour underneath. The   */
/* SDK overlay's <CoreText/*.h> binds the public name to these with an asm   */
/* label, so WebCore's own call sites need no change. Details and the        */
/* disassembly they came from are in CT-SURVEY.md.                           */
/* ======================================================================== */

CFDataRef TigerCTFontCopyTable(CTFontRef font, CTFontTableTag tag, CTFontTableOptions options)
{
    CFStringRef name;
    CFDataRef table;

    (void)options; /* Tiger has no table options. */
    if (!font)
        return NULL;
    name = createTableName(tag);
    if (!name)
        return NULL;
    table = CTFontCopyTable(font, name);
    CFRelease(name);
    return table;
}

double TigerCTFontGetAdvancesForGlyphs(CTFontRef font, CTFontOrientation orientation,
    const CGGlyph glyphs[], CGSize advances[], CFIndex count)
{
    (void)orientation; /* Tiger's advances are horizontal only. */
    if (!font || count <= 0)
        return 0;
    return CTFontGetAdvancesForGlyphs(font, glyphs, advances, count).width;
}

CGRect TigerCTFontGetBoundingRectsForGlyphs(CTFontRef font, CTFontOrientation orientation,
    const CGGlyph glyphs[], CGRect rects[], CFIndex count)
{
    (void)orientation;
    if (!font || count <= 0)
        return CGRectZero;
    return CTFontGetBoundingRectsForGlyphs(font, glyphs, rects, count);
}

double TigerCTLineGetTypographicBounds(CTLineRef line, CGFloat* ascent, CGFloat* descent,
    CGFloat* leading)
{
    if (!line)
        return 0;
    /* Tiger only validates the range against the glyph count and returns the
     * whole line's metrics regardless, so {0, 0} always passes. */
    return CTLineGetTypographicBounds(line, CFRangeMake(0, 0), ascent, descent, leading);
}

/* The three CTRunGet*(run, range, buffer) getters are exported but empty on
 * Tiger. Copy out of the pointer variants, which do work. */
static CFIndex runRangeCount(CTRunRef run, CFRange range)
{
    CFIndex glyphCount = CTRunGetGlyphCount(run);
    CFIndex count;

    if (range.location < 0 || range.location > glyphCount)
        return 0;
    count = range.length ? range.length : glyphCount - range.location;
    if (range.location + count > glyphCount)
        count = glyphCount - range.location;
    return count > 0 ? count : 0;
}

void TigerCTRunGetGlyphs(CTRunRef run, CFRange range, CGGlyph buffer[])
{
    const CGGlyph* glyphs;
    CFIndex count;

    if (!run || !buffer)
        return;
    count = runRangeCount(run, range);
    glyphs = CTRunGetGlyphsPtr(run);
    if (count && glyphs)
        memcpy(buffer, glyphs + range.location, (size_t)count * sizeof(CGGlyph));
}

void TigerCTRunGetAdvances(CTRunRef run, CFRange range, CGSize buffer[])
{
    const CGSize* advances;
    CFIndex count;

    if (!run || !buffer)
        return;
    count = runRangeCount(run, range);
    advances = CTRunGetAdvancesPtr(run);
    if (count && advances)
        memcpy(buffer, advances + range.location, (size_t)count * sizeof(CGSize));
}

void TigerCTRunGetStringIndices(CTRunRef run, CFRange range, CFIndex buffer[])
{
    const CFIndex* indices;
    CFIndex count;

    if (!run || !buffer)
        return;
    count = runRangeCount(run, range);
    indices = CTRunGetStringIndicesPtr(run);
    if (count && indices)
        memcpy(buffer, indices + range.location, (size_t)count * sizeof(CFIndex));
}

/* The font a run is drawn with, borrowed, or NULL. */
static CTFontRef runFont(CTRunRef run)
{
    CFDictionaryRef attributes = CTRunGetAttributes(run);
    return attributes ? (CTFontRef)CFDictionaryGetValue(attributes, kCTFontAttributeName) : NULL;
}

void TigerCTRunDraw(CTRunRef run, CGContextRef context, CFRange range)
{
    const CGGlyph* glyphs;
    const CGSize* advances;
    CTFontRef font;
    CGPoint* positions;
    CGPoint pen;
    CFIndex count, i;

    /* Tiger's CTRunDraw is empty. Lay the run out from the context's text
     * position and hand it to CTFontDrawGlyphs. */
    if (!run || !context)
        return;
    font = runFont(run);
    glyphs = CTRunGetGlyphsPtr(run);
    advances = CTRunGetAdvancesPtr(run);
    count = runRangeCount(run, range);
    if (!font || !glyphs || !advances || !count)
        return;

    positions = (CGPoint*)calloc((size_t)count, sizeof(CGPoint));
    if (!positions)
        return;
    pen = CGContextGetTextPosition(context);
    for (i = 0; i < count; ++i) {
        positions[i] = pen;
        pen.x += advances[range.location + i].width;
        pen.y += advances[range.location + i].height;
    }
    CTFontDrawGlyphs(font, glyphs + range.location, positions, (size_t)count, context);
    free(positions);
}

CGRect TigerCTLineGetImageBounds(CTLineRef line, CGContextRef context)
{
    CFArrayRef runs;
    CGRect bounds = CGRectNull;
    CGFloat penX = 0;
    CFIndex runIndex, runCount;

    /* Tiger's CTLineGetImageBounds never looks at its line: it copies a fixed
     * global rect into the struct return. Union the runs' glyph bounding rects
     * instead, walking the pen across the line. */
    (void)context; /* Tiger has no context-dependent hinting to account for. */
    if (!line)
        return CGRectNull;
    runs = CTLineGetGlyphRuns(line);
    runCount = runs ? CFArrayGetCount(runs) : 0;

    for (runIndex = 0; runIndex < runCount; ++runIndex) {
        CTRunRef run = (CTRunRef)CFArrayGetValueAtIndex(runs, runIndex);
        CTFontRef font = runFont(run);
        const CGGlyph* glyphs = CTRunGetGlyphsPtr(run);
        const CGSize* advances = CTRunGetAdvancesPtr(run);
        CFIndex count = CTRunGetGlyphCount(run);
        CGRect* rects;
        CFIndex i;

        if (!font || !glyphs || !advances || count <= 0)
            continue;
        rects = (CGRect*)calloc((size_t)count, sizeof(CGRect));
        if (!rects)
            continue;
        CTFontGetBoundingRectsForGlyphs(font, glyphs, rects, count);
        for (i = 0; i < count; ++i) {
            if (!CGRectIsEmpty(rects[i]))
                bounds = CGRectUnion(bounds, CGRectOffset(rects[i], penX, 0));
            penX += advances[i].width;
        }
        free(rects);
    }
    return bounds;
}

CTFontRef TigerCTFontCreateUIFontForLocale(CTFontUIFontType type, CGFloat size, CFStringRef locale)
{
    /* Tiger's is empty; this is the same table CTFontCreateUIFontForLanguage uses. */
    return createUIFont(type, size, locale);
}
