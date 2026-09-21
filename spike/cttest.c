/* TIGER: exercises every CoreText shim in compat/ctcompat.c on the real box.
 *
 * Build (from the repo root, in bash, after `source toolchain/env.sh`):
 *   toolchain/bin/tiger-clang -O1 -g spike/cttest.c -o build/cttest \
 *     -ltigercompat \
 *     -F sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks \
 *     -framework CoreFoundation -framework ApplicationServices -framework CoreText
 * Run:
 *   scp -O build/cttest /System/Library/Fonts/Supplemental/Arial.ttf tiger:/tmp/
 *   ssh tiger /tmp/cttest /tmp/Arial.ttf
 */

/* Built against the SDK overlay's <CoreText/CoreText.h>, not TigerCompat's own
 * header, so this also proves WebCore's modern call style compiles and links:
 * the asm-labelled declarations have to resolve to libtigercompat. */
#include <CoreText/CoreText.h>

/* Declared by WebCore in PAL/pal/spi/cg/CoreGraphicsSPI.h, implemented in
 * ctcompat.c because CoreText is the only route to them on Tiger. */
CFStringRef CGFontCopyFamilyName(CGFontRef);
void CGFontGetGlyphsForUnichars(CGFontRef, const UniChar[], CGGlyph[], size_t);
bool CGFontGetGlyphAdvancesForStyle(CGFontRef, const CGAffineTransform*, uint32_t,
    const CGGlyph[], size_t, CGSize[]);

/* CoreText SPI. Not in the public overlay by design: WebCore declares these for
 * itself in PAL/pal/spi/cf/CoreTextSPI.h, which is where the port will gate
 * them. Spelled here exactly as that header spells them. */
typedef CFOptionFlags CTFontFallbackOption;
typedef CFOptionFlags CTFontShapeOptions;
typedef uint32_t CTFontTransformOptions;
typedef uint32_t CTFontTextStylePlatform;
typedef uint8_t CTCompositionLanguage;
enum { kCTFontFallbackOptionSystem = (1 << 0), kCTFontFallbackOptionUserInstalled = (1 << 1),
       kCTFontFallbackOptionDefault = kCTFontFallbackOptionSystem | kCTFontFallbackOptionUserInstalled };
enum { kCTFontShapeWithKerning = (1 << 0) };
enum { kCTFontTransformApplyPositioning = (1 << 1) };
enum { kCTFontTextStylePlatformDefault = (CTFontTextStylePlatform)-1 };
enum { kCTCompositionLanguageNone = 1 };
CTFontRef CTFontCreateForCharactersWithLanguageAndOption(CTFontRef, const UniChar[], CFIndex, CFStringRef, CTFontFallbackOption, CFIndex*);
CTFontRef CTFontCreateForCharacters(CTFontRef, const UniChar[], CFIndex, CFIndex*);
bool CTFontIsSystemUIFont(CTFontRef);
CTFontUIFontType CTFontGetUIFontType(CTFontRef);
bool CTFontGetGlyphsForCharacterRange(CTFontRef, CGGlyph[], CFRange);
bool CTFontGetVerticalGlyphsForCharacters(CTFontRef, const UniChar[], CGGlyph[], CFIndex);
void CTFontGetVerticalTranslationsForGlyphs(CTFontRef, const CGGlyph[], CGSize[], CFIndex);
CTFontSymbolicTraits CTFontGetPhysicalSymbolicTraits(CTFontRef);
CTFontRef CTFontCopyPhysicalFont(CTFontRef);
CFBitVectorRef CTFontCopyColorGlyphCoverage(CTFontRef);
CFBitVectorRef CTFontCopyGlyphCoverageForFeature(CTFontRef, CFDictionaryRef);
bool CTFontIsAppleColorEmoji(CTFontRef);
bool CTFontHasComplexColorFormatForGlyph(CTFontRef, CGGlyph);
CGFloat CTFontGetSbixImageSizeForGlyphAndContentsScale(CTFontRef, CGGlyph, CGFloat);
CGFloat CTFontGetAccessibilityBoldWeightOfWeight(CGFloat);
bool CTFontTransformGlyphs(CTFontRef, CGGlyph[], CGSize[], CFIndex, CTFontTransformOptions);
void CTFontGetUnsummedAdvancesForGlyphsAndStyle(CTFontRef, CTFontOrientation, uint32_t, const CGGlyph[], CGSize[], CFIndex);
CTFontDescriptorRef CTFontDescriptorCreateWithAttributesAndOptions(CFDictionaryRef, uint32_t);
uint32_t CTFontDescriptorGetOptions(CTFontDescriptorRef);
CGFloat CTFontDescriptorGetTextStyleSize(CFStringRef, CFTypeRef, CTFontTextStylePlatform, CGFloat*, CGFloat*);
bool CTFontDescriptorIsSystemUIFont(CTFontDescriptorRef);
CTFontDescriptorRef CTFontDescriptorCreateLastResort(void);
CTFontDescriptorRef CTFontDescriptorCreateForUIType(CTFontUIFontType, CGFloat, CFStringRef);
CTFontDescriptorRef CTFontDescriptorCreateWithTextStyle(CFStringRef, CFStringRef, CFStringRef);
CTFontDescriptorRef CTFontDescriptorCreateWithTextStyleAndAttributes(CFStringRef, CFStringRef, CFDictionaryRef);
CTFontDescriptorRef CTFontDescriptorCreateForCSSFamily(CFStringRef, CFStringRef);
CTFontRef CTFontCreateWithFontDescriptorAndOptions(CTFontDescriptorRef, CGFloat, const CGAffineTransform*, uint32_t);
CFArrayRef CTFontCopyDefaultCascadeListForLanguages(CTFontRef, CFArrayRef);
CGSize CTRunGetInitialAdvance(CTRunRef);
void CTRunGetBaseAdvancesAndOrigins(CTRunRef, CFRange, CGSize[], CGPoint[]);
CTTypesetterRef CTTypesetterCreateWithUniCharProviderAndOptions(CTUniCharProviderCallback, CTUniCharDisposeCallback, void*, CFDictionaryRef);
void CTParagraphStyleSetCompositionLanguage(CTParagraphStyleRef, CTCompositionLanguage);
CGSize CTFontShapeGlyphs(CTFontRef, CGGlyph glyphs[], CGSize advances[], CGPoint origins[],
    CFIndex indexes[], const UniChar chars[], CFIndex count, CTFontShapeOptions,
    CFStringRef language, void (^handler)(CFRange, CGGlyph**, CGSize**, CGPoint**, CFIndex**));
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int checks;

static void expect(int ok, const char* what)
{
    ++checks;
    if (ok)
        printf("PASS %s\n", what);
    else {
        printf("FAIL %s\n", what);
        ++failures;
    }
}

static void printString(const char* label, CFStringRef s)
{
    char buffer[256];

    if (!s) {
        printf("     %s = (null)\n", label);
        return;
    }
    if (CFStringGetCString(s, buffer, sizeof(buffer), kCFStringEncodingUTF8))
        printf("     %s = %s\n", label, buffer);
}

/* Latin and CJK samples. The CJK string is U+4E2D U+6587 ("Chinese"). */
static const UniChar latin[] = { 'A', 'V', 'i', 'f', 'i' };
static const UniChar cjk[] = { 0x4E2D, 0x6587 };

static CFAttributedStringRef makeAttributedString(CFStringRef text, CTFontRef font)
{
    CFMutableDictionaryRef attributes = CFDictionaryCreateMutable(NULL, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef result;

    CFDictionarySetValue(attributes, kCTFontAttributeName, font);
    result = CFAttributedStringCreate(NULL, text, attributes);
    CFRelease(attributes);
    return result;
}

/* ---- (b) wrappers ------------------------------------------------------ */

static void testWrappers(CTFontRef helvetica)
{
    CTFontDescriptorRef descriptor, copy, bold, lastResort, matched;
    CFMutableDictionaryRef attributes;
    CFStringRef fullName;
    CGFontRef cgFont;
    CFArrayRef cascade, families, matches;
    CTFontRef viaOptions;

    descriptor = CTFontCopyFontDescriptor(helvetica);
    expect(descriptor != NULL, "CTFontCopyFontDescriptor (Tiger)");

    copy = CTFontDescriptorCreateCopyWithAttributes(descriptor, NULL);
    expect(copy != NULL, "CTFontDescriptorCreateCopyWithAttributes, null attributes retains");
    if (copy)
        CFRelease(copy);

    attributes = CFDictionaryCreateMutable(NULL, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(attributes, kCTFontFamilyNameAttribute, CFSTR("Courier"));
    copy = CTFontDescriptorCreateCopyWithAttributes(descriptor, attributes);
    expect(copy != NULL, "CTFontDescriptorCreateCopyWithAttributes");
    if (copy) {
        CFStringRef family = (CFStringRef)CTFontDescriptorCopyAttribute(copy, kCTFontFamilyNameAttribute);
        printString("copied family", family);
        if (family)
            CFRelease(family);
        CFRelease(copy);
    }

    viaOptions = CTFontCreateWithFontDescriptorAndOptions(descriptor, 24.0, NULL, 0);
    expect(viaOptions != NULL && CTFontGetSize(viaOptions) == 24.0,
        "CTFontCreateWithFontDescriptorAndOptions honours the size");
    if (viaOptions)
        CFRelease(viaOptions);

    expect(CTFontDescriptorGetOptions(descriptor) == 0, "CTFontDescriptorGetOptions returns 0");

    copy = CTFontDescriptorCreateWithAttributesAndOptions(attributes, 0);
    expect(copy != NULL, "CTFontDescriptorCreateWithAttributesAndOptions");
    if (copy)
        CFRelease(copy);
    CFRelease(attributes);

    bold = CTFontDescriptorCreateCopyWithSymbolicTraits(descriptor, kCTFontTraitBold, kCTFontTraitBold);
    expect(bold != NULL, "CTFontDescriptorCreateCopyWithSymbolicTraits");
    if (bold) {
        CTFontRef boldFont = CTFontCreateWithFontDescriptor(bold, 16.0, NULL);
        if (boldFont) {
            CFStringRef name = CTFontCopyPostScriptName(boldFont);
            printString("bold PostScript name", name);
            expect((CTFontGetSymbolicTraits(boldFont) & kCTFontTraitBold) != 0,
                "bold descriptor produces a bold font");
            if (name)
                CFRelease(name);
            CFRelease(boldFont);
        } else
            expect(0, "bold descriptor produces a bold font");
        CFRelease(bold);
    }

    {
        int type = 22 /* kTextSpacingType */, selector = 2;
        CFNumberRef typeNumber = CFNumberCreate(NULL, kCFNumberIntType, &type);
        CFNumberRef selectorNumber = CFNumberCreate(NULL, kCFNumberIntType, &selector);
        CTFontDescriptorRef featured = CTFontDescriptorCreateCopyWithFeature(descriptor, typeNumber, selectorNumber);
        expect(featured != NULL, "CTFontDescriptorCreateCopyWithFeature");
        if (featured)
            CFRelease(featured);
        CFRelease(typeNumber);
        CFRelease(selectorNumber);
    }

    matches = CTFontDescriptorCreateMatchingFontDescriptors(descriptor, NULL);
    expect(matches != NULL && CFArrayGetCount(matches) > 0, "CTFontDescriptorCreateMatchingFontDescriptors");
    if (matches) {
        printf("     %ld matching descriptors\n", (long)CFArrayGetCount(matches));
        CFRelease(matches);
    }
    matched = CTFontDescriptorCreateMatchingFontDescriptor(descriptor, NULL);
    expect(matched != NULL, "CTFontDescriptorCreateMatchingFontDescriptor");
    if (matched)
        CFRelease(matched);

    lastResort = CTFontDescriptorCreateLastResort();
    expect(lastResort != NULL, "CTFontDescriptorCreateLastResort");
    if (lastResort) {
        CTFontRef font = CTFontCreateWithFontDescriptor(lastResort, 12.0, NULL);
        CFStringRef family = font ? CTFontCopyFamilyName(font) : NULL;
        printString("last resort family", family);
        expect(font != NULL, "last resort descriptor makes a font");
        if (family)
            CFRelease(family);
        if (font)
            CFRelease(font);
        CFRelease(lastResort);
    }

    fullName = CTFontCopyFullName(helvetica);
    expect(fullName != NULL, "CTFontCopyFullName");
    printString("full name", fullName);
    if (fullName)
        CFRelease(fullName);

    expect(CTFontGetGlyphCount(helvetica) > 0, "CTFontGetGlyphCount");
    printf("     glyph count = %ld\n", (long)CTFontGetGlyphCount(helvetica));

    cgFont = CTFontCopyGraphicsFont(helvetica, NULL);
    expect(cgFont != NULL, "CTFontCopyGraphicsFont");
    if (cgFont)
        CGFontRelease(cgFont);

    expect(CTFontGetPhysicalSymbolicTraits(helvetica) == CTFontGetSymbolicTraits(helvetica),
        "CTFontGetPhysicalSymbolicTraits matches the symbolic traits");

    cascade = CTFontCopyDefaultCascadeListForLanguages(helvetica, NULL);
    expect(cascade != NULL, "CTFontCopyDefaultCascadeListForLanguages");
    if (cascade) {
        printf("     cascade list has %ld entries\n", (long)CFArrayGetCount(cascade));
        CFRelease(cascade);
    }

    families = CTFontManagerCopyAvailableFontFamilyNames();
    expect(families != NULL && CFArrayGetCount(families) > 0, "CTFontManagerCopyAvailableFontFamilyNames");
    if (families) {
        printf("     %ld font families installed\n", (long)CFArrayGetCount(families));
        CFRelease(families);
    }

    if (descriptor)
        CFRelease(descriptor);
}

/* ---- system font, text styles ------------------------------------------ */

static void testSystemFont(CTFontRef helvetica)
{
    CTFontDescriptorRef uiDescriptor, styleDescriptor, cssDescriptor;
    CTFontRef systemFont, viaLanguage;
    CGFloat weight = -1, lineSpacing = -1, size;

    uiDescriptor = CTFontDescriptorCreateForUIType(kCTFontUIFontSystem, 13.0, NULL);
    expect(uiDescriptor != NULL, "CTFontDescriptorCreateForUIType");
    if (uiDescriptor) {
        expect(CTFontDescriptorIsSystemUIFont(uiDescriptor), "CTFontDescriptorIsSystemUIFont on the system font");
        CFRelease(uiDescriptor);
    }

    viaLanguage = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, 13.0, NULL);
    expect(viaLanguage != NULL, "CTFontCreateUIFontForLanguage");
    if (viaLanguage) {
        CFStringRef family = CTFontCopyFamilyName(viaLanguage);
        printString("system font family", family);
        if (family)
            CFRelease(family);
        expect(CTFontIsSystemUIFont(viaLanguage), "CTFontIsSystemUIFont on the system font");
        expect(CTFontGetUIFontType(viaLanguage) == kCTFontUIFontSystem, "CTFontGetUIFontType on the system font");
        CFRelease(viaLanguage);
    }

    expect(!CTFontIsSystemUIFont(helvetica), "CTFontIsSystemUIFont is false for Helvetica");
    expect(CTFontGetUIFontType(helvetica) == (CTFontUIFontType)kCTFontNoFontType,
        "CTFontGetUIFontType is kCTFontNoFontType for Helvetica");

    styleDescriptor = CTFontDescriptorCreateWithTextStyle(kCTUIFontTextStyleBody, NULL, NULL);
    expect(styleDescriptor != NULL, "CTFontDescriptorCreateWithTextStyle");
    if (styleDescriptor) {
        systemFont = CTFontCreateWithFontDescriptor(styleDescriptor, 0, NULL);
        expect(systemFont != NULL && CTFontGetSize(systemFont) == 13.0, "body text style is 13pt");
        if (systemFont)
            CFRelease(systemFont);
        CFRelease(styleDescriptor);
    }

    styleDescriptor = CTFontDescriptorCreateWithTextStyleAndAttributes(kCTUIFontTextStyleHeadline, NULL, NULL);
    expect(styleDescriptor != NULL, "CTFontDescriptorCreateWithTextStyleAndAttributes");
    if (styleDescriptor)
        CFRelease(styleDescriptor);

    size = CTFontDescriptorGetTextStyleSize(kCTUIFontTextStyleTitle1, NULL,
        kCTFontTextStylePlatformDefault, &weight, &lineSpacing);
    expect(size == 22.0 && weight == 0.0 && lineSpacing > 22.0, "CTFontDescriptorGetTextStyleSize");
    printf("     title1: size=%.1f weight=%.2f lineSpacing=%.1f\n",
        (double)size, (double)weight, (double)lineSpacing);

    cssDescriptor = CTFontDescriptorCreateForCSSFamily(kCTFontCSSFamilyMonospace, NULL);
    expect(cssDescriptor != NULL, "CTFontDescriptorCreateForCSSFamily(monospace)");
    if (cssDescriptor) {
        CTFontRef font = CTFontCreateWithFontDescriptor(cssDescriptor, 12.0, NULL);
        CFStringRef family = font ? CTFontCopyFamilyName(font) : NULL;
        printString("css monospace family", family);
        if (family)
            CFRelease(family);
        if (font)
            CFRelease(font);
        CFRelease(cssDescriptor);
    }
}

/* ---- glyphs, metrics, tables ------------------------------------------- */

static void testGlyphs(CTFontRef font)
{
    CGGlyph glyphs[8], rangeGlyphs[128];
    CGSize advances[8], translations[8];
    CFArrayRef tables;
    CGPathRef path;
    CFIndex i;
    double totalAdvance;

    expect(CTFontGetGlyphsForCharacters(font, latin, glyphs, 5), "CTFontGetGlyphsForCharacters (Latin, Tiger)");
    printf("     latin glyphs: %u %u %u %u %u\n", glyphs[0], glyphs[1], glyphs[2], glyphs[3], glyphs[4]);
    expect(glyphs[0] && glyphs[1], "Latin glyphs are non-zero");

    totalAdvance = CTFontGetAdvancesForGlyphs(font, kCTFontOrientationHorizontal, glyphs, advances, 5);
    expect(totalAdvance > 0, "CTFontGetAdvancesForGlyphs through the overlay adapter");
    printf("     latin total advance = %.2f\n", totalAdvance);

    expect(CTFontGetGlyphsForCharacterRange(font, rangeGlyphs, CFRangeMake('A', 26)),
        "CTFontGetGlyphsForCharacterRange covers A-Z");
    expect(rangeGlyphs[0] == glyphs[0], "character range agrees with CTFontGetGlyphsForCharacters for 'A'");
    expect(!CTFontGetGlyphsForCharacterRange(font, rangeGlyphs, CFRangeMake(0xFFF0, 32)),
        "CTFontGetGlyphsForCharacterRange refuses a range crossing U+FFFF");

    expect(CTFontGetVerticalGlyphsForCharacters(font, latin, glyphs, 5),
        "CTFontGetVerticalGlyphsForCharacters");
    CTFontGetVerticalTranslationsForGlyphs(font, glyphs, translations, 5);
    expect(translations[0].height < 0 && translations[0].width < 0,
        "CTFontGetVerticalTranslationsForGlyphs returns a left-and-up offset");
    printf("     vertical translation[0] = (%.2f, %.2f)\n",
        (double)translations[0].width, (double)translations[0].height);

    expect(CTFontHasTable(font, kCTFontTableHead), "CTFontHasTable finds 'head'");
    expect(!CTFontHasTable(font, 'ZZZZ'), "CTFontHasTable rejects a bogus tag");

    tables = CTFontCopyAvailableTables(font, kCTFontTableOptionNoOptions);
    expect(tables != NULL && CFArrayGetCount(tables) > 0, "CTFontCopyAvailableTables");
    if (tables) {
        int sawHead = 0, sawCmap = 0;
        printf("     tables:");
        for (i = 0; i < CFArrayGetCount(tables); ++i) {
            CTFontTableTag tag = (CTFontTableTag)(uintptr_t)CFArrayGetValueAtIndex(tables, i);
            printf(" %c%c%c%c", (char)(tag >> 24), (char)(tag >> 16), (char)(tag >> 8), (char)tag);
            if (tag == kCTFontTableHead)
                sawHead = 1;
            if (tag == 'cmap')
                sawCmap = 1;
        }
        printf("\n");
        expect(sawHead && sawCmap, "available tables include head and cmap");
        expect(CFArrayGetCount(tables) >= 8,
            "CTFontCopyAvailableTables reads the real sfnt directory, not a probe list");
        CFRelease(tables);
    }

    path = CTFontCreatePathForGlyph(font, glyphs[0], NULL);
    expect(path != NULL, "CTFontCreatePathForGlyph");
    if (path) {
        CGRect box = CGPathGetBoundingBox(path);
        printf("     glyph path bounds = %.2f %.2f %.2f %.2f\n",
            (double)box.origin.x, (double)box.origin.y, (double)box.size.width, (double)box.size.height);
        expect(box.size.width > 0 && box.size.height > 0, "glyph path has a non-empty bounding box");
        CGPathRelease(path);
    }
}

static void testDrawing(CTFontRef font)
{
    static unsigned char pixels[64 * 64 * 4];
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef context;
    CGGlyph glyphs[5];
    CGPoint positions[5];
    CGSize advances[5];
    CGFloat x = 2;
    int i, inked = 0;

    memset(pixels, 0, sizeof(pixels));
    context = CGBitmapContextCreate(pixels, 64, 64, 8, 64 * 4, space, kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(space);
    if (!context) {
        expect(0, "CGBitmapContextCreate for CTFontDrawGlyphs");
        return;
    }

    CTFontGetGlyphsForCharacters(font, latin, glyphs, 5);
    CTFontGetAdvancesForGlyphs(font, kCTFontOrientationHorizontal, glyphs, advances, 5);
    for (i = 0; i < 5; ++i) {
        positions[i] = CGPointMake(x, 20);
        x += advances[i].width;
    }
    CGContextSetRGBFillColor(context, 1, 1, 1, 1);
    CTFontDrawGlyphs(font, glyphs, positions, 5, context);

    for (i = 0; i < 64 * 64 * 4; ++i) {
        if (pixels[i]) {
            ++inked;
            break;
        }
    }
    expect(inked != 0, "CTFontDrawGlyphs puts ink on the context");
    CGContextRelease(context);
}

/* ---- lines, runs, frames ----------------------------------------------- */

static const UniChar* provideCharacters(CFIndex stringIndex, CFIndex* charCount,
    CFDictionaryRef* attributes, void* refCon)
{
    CFDictionaryRef runAttributes = (CFDictionaryRef)refCon;

    if (stringIndex >= 5) {
        *charCount = 0;
        return NULL;
    }
    *charCount = 5 - stringIndex;
    if (attributes)
        *attributes = runAttributes;
    return latin + stringIndex;
}

static void testLines(CTFontRef font)
{
    CFStringRef text = CFStringCreateWithCharacters(NULL, latin, 5);
    CFStringRef cjkText = CFStringCreateWithCharacters(NULL, cjk, 2);
    CFAttributedStringRef attributed, cjkAttributed;
    CTLineRef line;
    CFArrayRef runs;
    CTFramesetterRef framesetter;
    CGSize suggested;
    CFRange fitRange;

    attributed = makeAttributedString(text, font);
    line = CTLineCreateWithAttributedString(attributed);
    expect(line != NULL, "CTLineCreateWithAttributedString (Tiger)");

    if (line) {
        CGRect bounds = CTLineGetBoundsWithOptions(line, 0);
        CGRect tight = CTLineGetBoundsWithOptions(line, kCTLineBoundsExcludeTypographicLeading);
        expect(bounds.size.width > 0 && bounds.size.height > 0, "CTLineGetBoundsWithOptions");
        expect(tight.size.height <= bounds.size.height,
            "excluding typographic leading does not grow the line");
        printf("     line bounds = %.2f x %.2f, tight height = %.2f\n",
            (double)bounds.size.width, (double)bounds.size.height, (double)tight.size.height);
        expect(CTLineGetTrailingWhitespaceWidth(line) == 0.0,
            "CTLineGetTrailingWhitespaceWidth is 0 for a line with no trailing space");

        {
            /* The same text with two trailing spaces: the extra width must be
               the width of two spaces, and nothing else. */
            UniChar padded[7];
            CFStringRef paddedText;
            CFAttributedStringRef paddedAttributed;
            CTLineRef paddedLine;

            memcpy(padded, latin, sizeof(latin));
            padded[5] = ' ';
            padded[6] = ' ';
            paddedText = CFStringCreateWithCharacters(NULL, padded, 7);
            paddedAttributed = makeAttributedString(paddedText, font);
            paddedLine = CTLineCreateWithAttributedString(paddedAttributed);
            if (paddedLine) {
                CGGlyph spaceGlyph = 0;
                UniChar space = ' ';
                CGSize spaceAdvance = { 0, 0 };
                double trailing = CTLineGetTrailingWhitespaceWidth(paddedLine);

                CTFontGetGlyphsForCharacters(font, &space, &spaceGlyph, 1);
                CTFontGetAdvancesForGlyphs(font, kCTFontOrientationHorizontal, &spaceGlyph, &spaceAdvance, 1);
                printf("     trailing whitespace = %.2f, two spaces = %.2f\n",
                    trailing, (double)spaceAdvance.width * 2);
                expect(trailing > 0 && trailing == spaceAdvance.width * 2,
                    "CTLineGetTrailingWhitespaceWidth measures two trailing spaces");
                CFRelease(paddedLine);
            } else
                expect(0, "CTLineGetTrailingWhitespaceWidth measures two trailing spaces");
            CFRelease(paddedAttributed);
            CFRelease(paddedText);
        }

        runs = CTLineGetGlyphRuns(line);
        expect(runs != NULL && CFArrayGetCount(runs) > 0, "CTLineGetGlyphRuns (Tiger)");
        if (runs && CFArrayGetCount(runs)) {
            CTRunRef run = (CTRunRef)CFArrayGetValueAtIndex(runs, 0);
            CFIndex count = CTRunGetGlyphCount(run);
            CGSize* baseAdvances = (CGSize*)calloc((size_t)count, sizeof(CGSize));
            CGPoint* origins = (CGPoint*)calloc((size_t)count, sizeof(CGPoint));
            CGSize initial = CTRunGetInitialAdvance(run);

            CTRunGetBaseAdvancesAndOrigins(run, CFRangeMake(0, 0), baseAdvances, origins);
            expect(count > 0 && baseAdvances[0].width > 0, "CTRunGetBaseAdvancesAndOrigins fills advances");
            expect(origins[0].x == 0 && origins[0].y == 0, "CTRunGetBaseAdvancesAndOrigins zeroes origins");
            expect(initial.width == 0 && initial.height == 0, "CTRunGetInitialAdvance is zero");
            printf("     run has %ld glyphs, first advance = %.2f\n",
                (long)count, (double)baseAdvances[0].width);
            {
                /* CTRunGetGlyphs and CTRunGetAdvances are empty on Tiger; the
                   overlay routes them to adapters over the Ptr variants. */
                CGGlyph* runGlyphs = (CGGlyph*)calloc((size_t)count, sizeof(CGGlyph));
                CGSize* runAdvances = (CGSize*)calloc((size_t)count, sizeof(CGSize));
                CTRunGetGlyphs(run, CFRangeMake(0, 0), runGlyphs);
                CTRunGetAdvances(run, CFRangeMake(0, 0), runAdvances);
                expect(runGlyphs[0] == CTRunGetGlyphsPtr(run)[0],
                    "CTRunGetGlyphs adapter returns data where Tiger returns nothing");
                expect(runAdvances[0].width == baseAdvances[0].width,
                    "CTRunGetAdvances adapter returns data where Tiger returns nothing");
                free(runGlyphs);
                free(runAdvances);
            }
            free(baseAdvances);
            free(origins);
        }
        CFRelease(line);
    }

    /* The same text through a unichar provider with options. */
    {
        CFMutableDictionaryRef runAttributes = CFDictionaryCreateMutable(NULL, 1,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFMutableDictionaryRef options = CFDictionaryCreateMutable(NULL, 1,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        int level = 0;
        CFNumberRef levelNumber = CFNumberCreate(NULL, kCFNumberIntType, &level);
        CTTypesetterRef typesetter;

        CFDictionarySetValue(runAttributes, kCTFontAttributeName, font);
        CFDictionarySetValue(options, kCTTypesetterOptionForcedEmbeddingLevel, levelNumber);
        typesetter = CTTypesetterCreateWithUniCharProviderAndOptions(provideCharacters, NULL,
            runAttributes, options);
        expect(typesetter != NULL, "CTTypesetterCreateWithUniCharProviderAndOptions");
        if (typesetter)
            CFRelease(typesetter);
        CFRelease(levelNumber);
        CFRelease(options);
        CFRelease(runAttributes);
    }

    /* CJK through CTLine. */
    cjkAttributed = makeAttributedString(cjkText, font);
    line = CTLineCreateWithAttributedString(cjkAttributed);
    expect(line != NULL, "CJK line");
    if (line) {
        CGFloat ascent = 0, descent = 0, leading = 0;
        double width = CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
        printf("     CJK line width = %.2f (ascent %.2f descent %.2f)\n",
            width, (double)ascent, (double)descent);
        expect(width > 0, "CJK line has a positive width");
        CFRelease(line);
    }

    framesetter = CTFramesetterCreateWithAttributedString(attributed);
    expect(framesetter != NULL, "CTFramesetterCreateWithAttributedString (Tiger)");
    if (framesetter) {
        CGMutablePathRef path = CGPathCreateMutable();
        CTFrameRef frame;

        suggested = CTFramesetterSuggestFrameSizeWithConstraints(framesetter, CFRangeMake(0, 0),
            NULL, CGSizeMake(200, 1000), &fitRange);
        expect(suggested.width > 0 && suggested.height > 0, "CTFramesetterSuggestFrameSizeWithConstraints");
        printf("     suggested frame = %.2f x %.2f, fit range = %ld,%ld\n",
            (double)suggested.width, (double)suggested.height,
            (long)fitRange.location, (long)fitRange.length);

        CGPathAddRect(path, NULL, CGRectMake(0, 0, 200, 100));
        frame = CTFramesetterCreateFrame(framesetter, CFRangeMake(0, 0), path, NULL);
        expect(frame != NULL, "CTFramesetterCreateFrame (Tiger)");
        if (frame) {
            CFArrayRef lines = CTFrameGetLines(frame);
            CFIndex lineCount = lines ? CFArrayGetCount(lines) : 0;
            CGPoint* origins = (CGPoint*)calloc((size_t)(lineCount ? lineCount : 1), sizeof(CGPoint));

            CTFrameGetLineOrigins(frame, CFRangeMake(0, 0), origins);
            expect(lineCount > 0 && origins[0].y > 0 && origins[0].y < 100,
                "CTFrameGetLineOrigins puts the first line inside the frame");
            printf("     %ld frame lines, first origin = (%.2f, %.2f)\n",
                (long)lineCount, (double)origins[0].x, (double)origins[0].y);
            free(origins);
            CFRelease(frame);
        }
        CGPathRelease(path);
        CFRelease(framesetter);
    }

    CFRelease(attributed);
    CFRelease(cjkAttributed);
    CFRelease(text);
    CFRelease(cjkText);
}

/* ---- font fallback ----------------------------------------------------- */

static void testFallback(CTFontRef helvetica)
{
    CFIndex covered = 0;
    CTFontRef fallback = CTFontCreateForCharactersWithLanguageAndOption(helvetica, cjk, 2, NULL,
        kCTFontFallbackOptionDefault, &covered);

    expect(fallback != NULL, "CTFontCreateForCharactersWithLanguageAndOption finds a CJK font");
    if (fallback) {
        CGGlyph glyphs[2] = { 0, 0 };
        CFStringRef family = CTFontCopyFamilyName(fallback);
        printString("CJK fallback family", family);
        printf("     covered length = %ld of 2\n", (long)covered);
        expect(covered > 0, "fallback reports a covered length");
        expect(CTFontGetGlyphsForCharacters(fallback, cjk, glyphs, 2) && glyphs[0],
            "fallback font has glyphs for the CJK characters");
        if (family)
            CFRelease(family);
        CFRelease(fallback);
    }

    expect(CTFontCreateForCharacters(helvetica, latin, 5, &covered) != NULL,
        "CTFontCreateForCharacters");
}

/* ---- web fonts --------------------------------------------------------- */

static void testFontFromData(const char* path)
{
    CFDataRef data;
    CTFontDescriptorRef descriptor;
    CFArrayRef descriptors;
    CTFontRef font;
    FILE* file;
    long length;
    unsigned char* bytes;

    file = fopen(path, "rb");
    if (!file) {
        printf("FAIL cannot open %s\n", path);
        ++failures;
        ++checks;
        return;
    }
    fseek(file, 0, SEEK_END);
    length = ftell(file);
    fseek(file, 0, SEEK_SET);
    bytes = (unsigned char*)malloc((size_t)length);
    if (fread(bytes, 1, (size_t)length, file) != (size_t)length) {
        printf("FAIL short read of %s\n", path);
        ++failures;
        ++checks;
        fclose(file);
        free(bytes);
        return;
    }
    fclose(file);

    data = CFDataCreate(NULL, bytes, length);
    free(bytes);
    printf("     loaded %ld bytes from %s\n", length, path);

    descriptors = CTFontManagerCreateFontDescriptorsFromData(data);
    expect(descriptors != NULL && CFArrayGetCount(descriptors) > 0,
        "CTFontManagerCreateFontDescriptorsFromData");
    if (descriptors) {
        printf("     %ld descriptors in the data\n", (long)CFArrayGetCount(descriptors));
        CFRelease(descriptors);
    }

    descriptor = CTFontManagerCreateFontDescriptorFromData(data);
    expect(descriptor != NULL, "CTFontManagerCreateFontDescriptorFromData");
    if (!descriptor) {
        CFRelease(data);
        return;
    }

    {
        CFStringRef name = (CFStringRef)CTFontDescriptorCopyAttribute(descriptor, kCTFontNameAttribute);
        printString("activated PostScript name", name);
        if (name)
            CFRelease(name);
    }

    font = CTFontCreateWithFontDescriptor(descriptor, 24.0, NULL);
    expect(font != NULL, "the activated descriptor resolves through Tiger's CTFontCreateWithFontDescriptor");
    if (font) {
        CGGlyph glyphs[5];
        CGSize advances[5];
        CFStringRef family = CTFontCopyFamilyName(font);
        double total;

        printString("activated family", family);
        if (family)
            CFRelease(family);

        expect(CTFontGetGlyphsForCharacters(font, latin, glyphs, 5) && glyphs[0],
            "the activated font produces glyphs");
        total = CTFontGetAdvancesForGlyphs(font, kCTFontOrientationHorizontal, glyphs, advances, 5);
        expect(total > 0, "the activated font produces advances");
        printf("     activated font advance for \"AVifi\" = %.2f\n", total);

        {
            CFStringRef text = CFStringCreateWithCharacters(NULL, latin, 5);
            CFAttributedStringRef attributed = makeAttributedString(text, font);
            CTLineRef line = CTLineCreateWithAttributedString(attributed);
            expect(line != NULL && CTLineGetGlyphCount(line) == 5,
                "the activated font shapes through CTLine");
            if (line)
                CFRelease(line);
            CFRelease(attributed);
            CFRelease(text);
        }
        CFRelease(font);
    }

    CFRelease(descriptor);
    CFRelease(data);

    /* The same bytes by URL, and through the registration entry point. */
    {
        CFStringRef pathString = CFStringCreateWithCString(NULL, path, kCFStringEncodingUTF8);
        CFURLRef url = CFURLCreateWithFileSystemPath(NULL, pathString, kCFURLPOSIXPathStyle, false);
        CFArrayRef fromURL = CTFontManagerCreateFontDescriptorsFromURL(url);
        CFErrorRef error = NULL;

        expect(fromURL != NULL && CFArrayGetCount(fromURL) > 0, "CTFontManagerCreateFontDescriptorsFromURL");
        if (fromURL)
            CFRelease(fromURL);
        expect(CTFontManagerRegisterFontsForURL(url, kCTFontManagerScopeProcess, &error),
            "CTFontManagerRegisterFontsForURL");
        expect(CTFontManagerEnableAllUserFonts(false), "CTFontManagerEnableAllUserFonts");
        CFRelease(url);
        CFRelease(pathString);
    }
}

/* ---- the degraded entry points ----------------------------------------- */

static void testDegraded(CTFontRef font)
{
    CGGlyph glyphs[5];
    CGSize advances[5];
    CGPoint origins[5];
    CFIndex indexes[5];
    CGSize initial;
    CTParagraphStyleRef paragraphStyle;

    CTFontGetGlyphsForCharacters(font, latin, glyphs, 5);
    memset(advances, 0, sizeof(advances));
    initial = CTFontShapeGlyphs(font, glyphs, advances, origins, indexes, latin, 5,
        kCTFontShapeWithKerning, NULL, NULL);
    expect(advances[0].width > 0, "CTFontShapeGlyphs fills unshaped advances");
    expect(initial.width == 0 && initial.height == 0, "CTFontShapeGlyphs has no initial advance");
    expect(indexes[4] == 4, "CTFontShapeGlyphs fills identity string indexes");

    memset(advances, 0, sizeof(advances));
    expect(!CTFontTransformGlyphs(font, glyphs, advances, 5, kCTFontTransformApplyPositioning)
        && advances[0].width > 0, "CTFontTransformGlyphs fills advances and deletes nothing");

    expect(CTFontCopyColorGlyphCoverage(font) == NULL, "CTFontCopyColorGlyphCoverage is NULL on Tiger");
    expect(!CTFontIsAppleColorEmoji(font), "CTFontIsAppleColorEmoji is false on Tiger");
    expect(!CTFontHasComplexColorFormatForGlyph(font, glyphs[0]), "CTFontHasComplexColorFormatForGlyph is false");
    expect(CTFontGetSbixImageSizeForGlyphAndContentsScale(font, glyphs[0], 1) == 0,
        "CTFontGetSbixImageSizeForGlyphAndContentsScale is 0");
    expect(CTFontCopyGlyphCoverageForFeature(font, NULL) == NULL, "CTFontCopyGlyphCoverageForFeature is NULL");
    expect(CTFontGetAccessibilityBoldWeightOfWeight(0.4f) == 0.4f,
        "CTFontGetAccessibilityBoldWeightOfWeight is the identity");

    memset(advances, 0, sizeof(advances));
    CTFontGetUnsummedAdvancesForGlyphsAndStyle(font, kCTFontOrientationHorizontal, 0, glyphs, advances, 5);
    expect(advances[0].width > 0, "CTFontGetUnsummedAdvancesForGlyphsAndStyle fills advances");

    paragraphStyle = CTParagraphStyleCreate(NULL, 0);
    CTParagraphStyleSetCompositionLanguage(paragraphStyle, kCTCompositionLanguageNone);
    expect(paragraphStyle != NULL, "CTParagraphStyleSetCompositionLanguage is a survivable no-op");
    if (paragraphStyle)
        CFRelease(paragraphStyle);

    /* The three CoreGraphics font SPI that only CoreText can answer on Tiger. */
    {
        CGFontRef cgFont = CTFontCopyGraphicsFont(font, NULL);
        CFStringRef family = cgFont ? CGFontCopyFamilyName(cgFont) : NULL;
        CGGlyph cgGlyphs[5] = { 0, 0, 0, 0, 0 };
        CGSize cgAdvances[5];
        CGAffineTransform matrix = CGAffineTransformMakeScale(16, 16);

        printString("CGFontCopyFamilyName", family);
        expect(family != NULL, "CGFontCopyFamilyName through CoreText");
        if (family)
            CFRelease(family);

        CGFontGetGlyphsForUnichars(cgFont, latin, cgGlyphs, 5);
        expect(cgGlyphs[0] == glyphs[0] && cgGlyphs[1] == glyphs[1],
            "CGFontGetGlyphsForUnichars agrees with CTFontGetGlyphsForCharacters");

        memset(cgAdvances, 0, sizeof(cgAdvances));
        expect(CGFontGetGlyphAdvancesForStyle(cgFont, &matrix, 0, cgGlyphs, 5, cgAdvances)
            && cgAdvances[0].width > 0, "CGFontGetGlyphAdvancesForStyle fills advances");
        /* The unhinted style must give the linear advance, matching what
           CTFontGetAdvancesForGlyphs reports for the same glyph at 16pt. */
        {
            CGSize ctAdvances[5];
            CTFontGetAdvancesForGlyphs(font, kCTFontOrientationHorizontal, cgGlyphs, ctAdvances, 5);
            expect(cgAdvances[0].width > ctAdvances[0].width - 0.01f
                && cgAdvances[0].width < ctAdvances[0].width + 0.01f,
                "unhinted CGFont advance matches the CoreText advance");
        }
        printf("     CGFont advance[0] at 16pt = %.2f\n", (double)cgAdvances[0].width);
        if (cgFont)
            CGFontRelease(cgFont);
    }

    {
        CTFontRef physical = CTFontCopyPhysicalFont(font);
        expect(physical == font, "CTFontCopyPhysicalFont returns the font itself");
        if (physical)
            CFRelease(physical);
    }
}

int main(int argc, char** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    CTFontRef helvetica = CTFontCreateWithName(CFSTR("Helvetica"), 16.0, NULL);

    printf("-- CoreText compat, Mac OS X 10.4 --\n");
    expect(helvetica != NULL, "CTFontCreateWithName(Helvetica) (Tiger)");
    if (!helvetica)
        return 1;

    printf("\n[wrappers]\n");
    testWrappers(helvetica);
    printf("\n[system font and text styles]\n");
    testSystemFont(helvetica);
    printf("\n[glyphs, metrics, tables]\n");
    testGlyphs(helvetica);
    printf("\n[drawing]\n");
    testDrawing(helvetica);
    printf("\n[lines, runs, frames]\n");
    testLines(helvetica);
    printf("\n[font fallback]\n");
    testFallback(helvetica);
    printf("\n[degraded entry points]\n");
    testDegraded(helvetica);
    if (argc > 1) {
        printf("\n[font from data]\n");
        testFontFromData(argv[1]);
    } else
        printf("\n[font from data] skipped: no font file argument\n");

    CFRelease(helvetica);
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
