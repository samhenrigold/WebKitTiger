/* TIGER: the risky thing, proven at the pixel.
 *
 * The split port shapes in a 64-bit process with HarfBuzz and rasterises in a
 * 32-bit process with Tiger's CoreText. logs/hb-vs-ct.md showed the two agree
 * on glyph ids and advances. This asks the question that actually matters: if
 * you draw HarfBuzz's glyphs at HarfBuzz's positions with CoreText, do you get
 * the same pixels CoreText would have produced on its own?
 *
 * Two bitmaps per case, identical contexts:
 *   A  HarfBuzz shapes, CTFontDrawGlyphs draws each glyph at its absolute position
 *   B  CTLineDraw does the whole thing
 * then a pixel-for-pixel compare and a PPM diff.
 *
 * Build (bash, repo root):
 *   toolchain/bin/tiger-clang++ -O1 -g -x c++ spike/hbraster.c -o build/hbraster \
 *     -I toolchain/sysroot-i386/usr/include/harfbuzz \
 *     -nostdinc++ -isystem toolchain/sysroot-i386/usr/include/c++/v1 -stdlib=libc++ \
 *     -Fcompat/sdk-overlay \
 *     -F sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks \
 *     -lharfbuzz -lc++ -lc++abi -lunwind -ltigercompat \
 *     -framework CoreFoundation -framework CoreServices -framework ApplicationServices \
 *     -Wl,build/builtins-i386/libclang_rt.builtins-i386.a
 * Run on the box; writes /tmp/hbraster-*.ppm.
 */

#include <CoreText/CoreText.h>
#include <TigerCompat/CTFontHandle.h>
#include <hb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define W 640
#define H 64
#define BYTES (W * H * 4)

static int totalCases, identicalCases;

struct canvas { unsigned char* px; CGContextRef ctx; };

static int makeCanvas(struct canvas* c)
{
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    c->px = (unsigned char*)calloc(1, BYTES);
    c->ctx = c->px ? CGBitmapContextCreate(c->px, W, H, 8, W * 4, space,
        kCGImageAlphaPremultipliedLast) : NULL;
    CGColorSpaceRelease(space);
    if (!c->ctx)
        return 0;
    /* Identical state on both sides, so any pixel difference is positioning
     * rather than rendering setup. */
    CGContextSetRGBFillColor(c->ctx, 1, 1, 1, 1);
    CGContextSetShouldAntialias(c->ctx, true);
    CGContextSetShouldSmoothFonts(c->ctx, false);
    return 1;
}

static void freeCanvas(struct canvas* c)
{
    if (c->ctx) CGContextRelease(c->ctx);
    free(c->px);
}

static void writePPM(const char* path, const unsigned char* a, const unsigned char* b)
{
    FILE* f = fopen(path, "wb");
    int i;
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", W, H * 3);
    /* three panels: HarfBuzz-positioned, CoreText, and the difference */
    for (i = 0; i < W * H; ++i) fprintf(f, "%c%c%c", a[i*4], a[i*4+1], a[i*4+2]);
    for (i = 0; i < W * H; ++i) fprintf(f, "%c%c%c", b[i*4], b[i*4+1], b[i*4+2]);
    for (i = 0; i < W * H; ++i) {
        int d = abs((int)a[i*4] - (int)b[i*4]);
        fprintf(f, "%c%c%c", (char)d, (char)d, (char)d);
    }
    fclose(f);
}

/* ---- HarfBuzz: shape, then draw each glyph where HarfBuzz put it --------- */

static hb_face_t* faceForFile(const char* path, CFDataRef* keep)
{
    FILE* f = fopen(path, "rb");
    long n;
    unsigned char* b;
    hb_blob_t* blob;
    hb_face_t* face;

    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = (unsigned char*)malloc(n);
    if (fread(b, 1, n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
    fclose(f);
    *keep = CFDataCreate(NULL, b, n);
    free(b);
    blob = hb_blob_create((const char*)CFDataGetBytePtr(*keep), (unsigned)CFDataGetLength(*keep),
        HB_MEMORY_MODE_READONLY, NULL, NULL);
    face = hb_face_create(blob, 0);
    hb_blob_destroy(blob);
    return face;
}

static unsigned shapeAndDraw(struct canvas* c, hb_face_t* face, CTFontRef ctFont,
    const hb_codepoint_t* cp, unsigned n, const char* script, int rtl,
    CGFloat size, CGFloat originX, CGFloat originY, double* outWidth)
{
    hb_font_t* font = hb_font_create(face);
    hb_buffer_t* buf = hb_buffer_create();
    hb_glyph_info_t* info;
    hb_glyph_position_t* pos;
    unsigned count = 0, i;
    CGGlyph* glyphs;
    CGPoint* points;
    double penX = originX, penY = originY;

    hb_font_set_scale(font, (int)(size * 64), (int)(size * 64));
    hb_font_set_ppem(font, 0, 0);
    hb_buffer_add_codepoints(buf, cp, (int)n, 0, (int)n);
    hb_buffer_set_direction(buf, rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
    hb_buffer_set_script(buf, hb_script_from_string(script, -1));
    hb_buffer_set_language(buf, hb_language_from_string("en", -1));
    hb_shape(font, buf, NULL, 0);

    info = hb_buffer_get_glyph_infos(buf, &count);
    pos = hb_buffer_get_glyph_positions(buf, &count);
    glyphs = (CGGlyph*)calloc(count ? count : 1, sizeof(CGGlyph));
    points = (CGPoint*)calloc(count ? count : 1, sizeof(CGPoint));

    for (i = 0; i < count; ++i) {
        glyphs[i] = (CGGlyph)info[i].codepoint;
        points[i] = CGPointMake((CGFloat)(penX + pos[i].x_offset / 64.0),
                                (CGFloat)(penY + pos[i].y_offset / 64.0));
        penX += pos[i].x_advance / 64.0;
        penY += pos[i].y_advance / 64.0;
    }
    if (count)
        CTFontDrawGlyphs(ctFont, glyphs, points, count, c->ctx);
    if (outWidth) *outWidth = penX - originX;

    free(glyphs); free(points);
    hb_buffer_destroy(buf);
    hb_font_destroy(font);
    return count;
}

/* ---- CoreText end to end ------------------------------------------------ */

static double drawNative(struct canvas* c, CTFontRef font, const hb_codepoint_t* cp,
    unsigned n, CGFloat originX, CGFloat originY)
{
    UniChar u[256];
    unsigned k = 0, i;
    CFStringRef s;
    CFMutableDictionaryRef attrs;
    CFAttributedStringRef as;
    CTLineRef line;
    double width = 0;

    for (i = 0; i < n && k < 255; ++i)
        if (cp[i] < 0x10000) u[k++] = (UniChar)cp[i];
    s = CFStringCreateWithCharacters(NULL, u, k);
    attrs = CFDictionaryCreateMutable(NULL, 2, &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(attrs, kCTFontAttributeName, font);
    /* Tiger's CTLineDraw takes its colour from the attributed string and
     * ignores the context's fill colour, defaulting to black. Without this the
     * line draws black on a black bitmap and the comparison silently measures
     * nothing at all. */
    {
        CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
        CGFloat white[4] = { 1, 1, 1, 1 };
        CGColorRef colour = CGColorCreate(space, white);
        CFDictionarySetValue(attrs, kCTForegroundColorAttributeName, colour);
        CGColorRelease(colour);
        CGColorSpaceRelease(space);
    }
    as = CFAttributedStringCreate(NULL, s, attrs);
    line = CTLineCreateWithAttributedString(as);
    if (line) {
        CGFloat a = 0, d = 0, l = 0;
        width = CTLineGetTypographicBounds(line, &a, &d, &l);
        CGContextSetTextPosition(c->ctx, originX, originY);
        CTLineDraw(line, c->ctx);
        CFRelease(line);
    }
    CFRelease(as); CFRelease(attrs); CFRelease(s);
    return width;
}

/* Pixel counts say something is different; this says how far, which is the
 * number that decides whether it matters. Both sides' glyph positions are
 * accumulated from their own advances and compared per glyph. */
static void comparePositions(hb_face_t* face, CTFontRef nativeFont, CGFloat size,
    const hb_codepoint_t* cp, unsigned n, const char* script, int rtl,
    double* maxShift, int* shiftedGlyph, unsigned* glyphCount)
{
    hb_font_t* font = hb_font_create(face);
    hb_buffer_t* buf = hb_buffer_create();
    hb_glyph_position_t* pos;
    unsigned count = 0, i, k = 0;
    UniChar u[256];
    CFStringRef str;
    CFStringRef key = kCTFontAttributeName;
    CFDictionaryRef attrs;
    CFAttributedStringRef as;
    CTLineRef line;
    CFArrayRef runs;
    double hbPen = 0, ctPen = 0;

    *maxShift = 0; *shiftedGlyph = -1; *glyphCount = 0;

    hb_font_set_scale(font, (int)(size * 64), (int)(size * 64));
    hb_font_set_ppem(font, 0, 0);
    hb_buffer_add_codepoints(buf, cp, (int)n, 0, (int)n);
    hb_buffer_set_direction(buf, rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
    hb_buffer_set_script(buf, hb_script_from_string(script, -1));
    hb_buffer_set_language(buf, hb_language_from_string("en", -1));
    hb_shape(font, buf, NULL, 0);
    pos = hb_buffer_get_glyph_positions(buf, &count);

    for (i = 0; i < n && k < 255; ++i)
        if (cp[i] < 0x10000) u[k++] = (UniChar)cp[i];
    str = CFStringCreateWithCharacters(NULL, u, k);
    attrs = CFDictionaryCreate(NULL, (const void**)&key, (const void**)&nativeFont, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    as = CFAttributedStringCreate(NULL, str, attrs);
    line = CTLineCreateWithAttributedString(as);
    runs = line ? CTLineGetGlyphRuns(line) : NULL;

    if (runs && CFArrayGetCount(runs)) {
        CTRunRef run = (CTRunRef)CFArrayGetValueAtIndex(runs, 0);
        const CGSize* adv = CTRunGetAdvancesPtr(run);
        CFIndex ctCount = CTRunGetGlyphCount(run);
        unsigned lim = (unsigned)(ctCount < (CFIndex)count ? ctCount : count);

        *glyphCount = lim;
        for (i = 0; i < lim; ++i) {
            double d = fabs(hbPen - ctPen);
            if (d > *maxShift) { *maxShift = d; *shiftedGlyph = (int)i; }
            hbPen += pos[i].x_advance / 64.0;
            if (adv) ctPen += adv[i].width;
        }
    }
    if (line) CFRelease(line);
    CFRelease(as); CFRelease(attrs); CFRelease(str);
    hb_buffer_destroy(buf);
    hb_font_destroy(font);
}

/* ---- the comparison ----------------------------------------------------- */

static void runCase(const char* label, const char* path, CGFloat size,
    const hb_codepoint_t* cp, unsigned n, const char* script, int rtl)
{
    struct canvas A, B;
    CFDataRef keep = NULL;
    hb_face_t* face;
    CTFontRef font = NULL, nativeFont = NULL;
    unsigned i, inkA = 0, inkB = 0, diff = 0, maxDelta = 0;
    double hbWidth = 0, ctWidth = 0;
    char ppm[256];

    ++totalCases;
    if (TigerCTFontForHandle(path, 0, size, 0, &font) != noErr || !font) {
        printf("%-34s cannot resolve %s\n", label, path);
        return;
    }
    face = faceForFile(path, &keep);
    if (!face) { printf("%-34s HarfBuzz cannot read it\n", label); CFRelease(font); return; }

    /* The native side needs a font CoreText will lay out AND draw with.
     *
     * A font from TigerCTFontForHandle comes through CTFontCreateWithGraphicsFont,
     * and Tiger's CTLineDraw silently draws nothing with one of those: the line
     * measures correctly, CTLineGetTypographicBounds returns the right width,
     * and no ink appears. So resolve the same face by PostScript name for the
     * native path. fonthandletest.c already proves the two are metrically
     * identical, so this compares positioning rather than two different fonts.
     *
     * That limitation does not touch the architecture, where the render process
     * only ever draws glyphs at given positions and never lays out a CTLine. */
    {
        CFStringRef ps = CTFontCopyPostScriptName(font);
        CTFontRef byName = ps ? CTFontCreateWithName(ps, size, NULL) : NULL;
        if (ps) CFRelease(ps);
        if (byName) { nativeFont = byName; }
        else nativeFont = (CTFontRef)CFRetain(font);
    }
    if (!makeCanvas(&A) || !makeCanvas(&B)) { printf("%-34s no bitmap\n", label); return; }

    shapeAndDraw(&A, face, font, cp, n, script, rtl, size, 8, 20, &hbWidth);
    ctWidth = drawNative(&B, nativeFont, cp, n, 8, 20);

    for (i = 0; i < (unsigned)(W * H); ++i) {
        unsigned a = A.px[i * 4], b = B.px[i * 4];
        unsigned d = a > b ? a - b : b - a;
        if (a) ++inkA;
        if (b) ++inkB;
        if (d) { ++diff; if (d > maxDelta) maxDelta = d; }
    }
    snprintf(ppm, sizeof(ppm), "/tmp/hbraster-%s.ppm", label);
    for (i = 0; ppm[i]; ++i) if (ppm[i] == ' ' || ppm[i] == '/') { if (i > 13) ppm[i] = '_'; }
    writePPM(ppm, A.px, B.px);

    {
        double maxShift = 0;
        int shifted = -1;
        unsigned glyphs = 0;
        comparePositions(face, nativeFont, size, cp, n, script, rtl, &maxShift, &shifted, &glyphs);
        printf("%-30s %5u px differ (%5.1f%% of ink), max channel %3u | worst glyph shift %.4f pt",
            label, diff, inkB ? 100.0 * diff / inkB : 0.0, maxDelta, maxShift);
        if (shifted >= 0 && maxShift > 0.001)
            printf(" at glyph %d of %u", shifted, glyphs);
        printf("\n");
    }
    if (!diff) ++identicalCases;

    freeCanvas(&A); freeCanvas(&B);
    hb_face_destroy(face);
    if (keep) CFRelease(keep);
    if (nativeFont) CFRelease(nativeFont);
    CFRelease(font);
}

static unsigned toCodepoints(const char* utf8, hb_codepoint_t* out, unsigned cap)
{
    CFStringRef s = CFStringCreateWithCString(NULL, utf8, kCFStringEncodingUTF8);
    CFIndex n, i;
    unsigned k = 0;
    if (!s) return 0;
    n = CFStringGetLength(s);
    for (i = 0; i < n && k < cap; ++i)
        out[k++] = CFStringGetCharacterAtIndex(s, i);
    CFRelease(s);
    return k;
}

int main(void)
{
    hb_codepoint_t cp[256];
    unsigned n;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("HarfBuzz positions rasterised by CoreText, against CoreText end to end\n");
    printf("%d x %d bitmap, antialiased, font smoothing off, identical contexts\n\n", W, H);

    n = toCodepoints("Waving fluffy AV To Typography", cp, 256);
    runCase("LucidaGrande 13 paragraph", "/tmp/LucidaGrande-Tiger-0.ttf", 13, cp, n, "Latn", 0);
    runCase("Helvetica 16 paragraph", "/tmp/Helvetica-Tiger.ttf", 16, cp, n, "Latn", 0);

    n = toCodepoints("Hamburgefonstiv", cp, 256);
    runCase("Helvetica 16 no kern pairs", "/tmp/Helvetica-Tiger.ttf", 16, cp, n, "Latn", 0);
    runCase("LucidaGrande 13 plain", "/tmp/LucidaGrande-Tiger-0.ttf", 13, cp, n, "Latn", 0);

    n = toCodepoints("\330\247\330\250\330\254\330\257", cp, 256);   /* Arabic alef beh jeem dal */
    runCase("GeezaPro 16 arabic RTL", "/tmp/GeezaPro.ttf", 16, cp, n, "Arab", 1);

    n = toCodepoints("\344\270\255\346\226\207\346\270\254\350\251\246", cp, 256);  /* CJK */
    runCase("Hiragino 16 CJK", "/tmp/HiraKakuProW3.otf", 16, cp, n, "Hani", 0);

    printf("\n%d cases, %d pixel-identical\n", totalCases, identicalCases);
    printf("diff images in /tmp/hbraster-*.ppm (three panels: HarfBuzz, CoreText, difference)\n");
    return 0;
}
