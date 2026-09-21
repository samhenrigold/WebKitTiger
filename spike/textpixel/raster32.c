/* The 32-bit side: resolve the handle the 64-bit process sent, draw exactly the
 * glyphs it was given at exactly the positions it was given, and compare
 * against CoreText laying out the same text in the same face on its own.
 *
 * This process makes no font decisions. That is the design: it cannot ask
 * CoreText what it would have chosen, so anything it draws was chosen by the
 * shaper. The comparison is the check that the shaper chose correctly.
 */

#include "common.h"
#include <CoreText/CoreText.h>
#include <TigerCompat/CTFontHandle.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <math.h>
#include <mach/mach_time.h>

#define W 520
#define H 48

static int cases, identical, fellBackCases, notdefCases;
static double maxShiftSeen;

/* ---- web font bytes, assembled from the wire ---------------------------- */

#define MAXDATA 4
static struct { unsigned char* bytes; uint32_t total, got; CFDataRef data; } g_data[MAXDATA];

static void takeChunk(const TPDataMsg* m)
{
    if (m->dataId >= MAXDATA) return;
    if (!g_data[m->dataId].bytes) {
        g_data[m->dataId].bytes = (unsigned char*)malloc(m->totalBytes);
        g_data[m->dataId].total = m->totalBytes;
    }
    if (m->offset + m->length <= g_data[m->dataId].total) {
        memcpy(g_data[m->dataId].bytes + m->offset, m->bytes, m->length);
        g_data[m->dataId].got += m->length;
    }
}

/* ---- drawing ------------------------------------------------------------- */

struct canvas { unsigned char* px; CGContextRef ctx; };

static int makeCanvas(struct canvas* c)
{
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    c->px = (unsigned char*)calloc(1, W * H * 4);
    c->ctx = c->px ? CGBitmapContextCreate(c->px, W, H, 8, W * 4, space,
        kCGImageAlphaPremultipliedLast) : NULL;
    CGColorSpaceRelease(space);
    if (!c->ctx) return 0;
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

static CTFontRef resolve(const TPRunMsg* m)
{
    CTFontRef font = NULL;
    if (m->dataId >= 0) {
        int id = m->dataId;
        if (id >= MAXDATA || !g_data[id].bytes) return NULL;
        if (!g_data[id].data) {
            uint64_t t0, t1;
            mach_timebase_info_data_t tb;
            CTFontRef probe = NULL;
            mach_timebase_info(&tb);
            g_data[id].data = CFDataCreate(NULL, g_data[id].bytes, g_data[id].got);
            t0 = mach_absolute_time();
            TigerCTFontForData(g_data[id].data, 0, 16, 0, &probe);
            t1 = mach_absolute_time();
            if (probe) CFRelease(probe);
            printf("  web font %u: %u bytes registered by ATS activation in %.1f ms\n",
                (unsigned)id, g_data[id].got,
                (double)(t1 - t0) * tb.numer / tb.denom / 1e6);
        }
        /* The web font is registered once, here, through ATS activation from
         * memory. CGFontCreateWithDataProvider is not an option: Tiger exports
         * it and it returns NULL for .ttf and .dfont alike, which CT-SURVEY.md
         * records. The cost is one activation per distinct blob, and the bytes
         * stay resident for the process lifetime because ATS reads them for as
         * long as the container lives. */
        TigerCTFontForData(g_data[id].data, m->faceIndex, m->size, 0, &font);
    } else {
        TigerCTFontForHandle(m->path, m->faceIndex, m->size, 0, &font);
    }
    return font;
}

static void drawShaped(struct canvas* c, CTFontRef font, const TPRunMsg* m,
    CGFloat ox, CGFloat oy, unsigned* notdef)
{
    CGPoint pts[TP_MAX_GLYPHS];
    CGGlyph glyphs[TP_MAX_GLYPHS];
    unsigned i;

    *notdef = 0;
    for (i = 0; i < m->glyphCount && i < TP_MAX_GLYPHS; ++i) {
        glyphs[i] = (CGGlyph)m->glyphs[i];
        pts[i] = CGPointMake(ox + m->posX[i], oy + m->posY[i]);
        if (!glyphs[i]) ++*notdef;
    }
    if (m->glyphCount)
        CTFontDrawGlyphs(font, glyphs, pts, m->glyphCount, c->ctx);
}

static double drawNative(struct canvas* c, CTFontRef font, const TPRunMsg* m,
    CGFloat ox, CGFloat oy)
{
    CFStringRef s = CFStringCreateWithCharacters(NULL, (const UniChar*)m->text, m->textLength);
    CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(NULL, 2,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGFloat white[4] = { 1, 1, 1, 1 };
    CGColorRef colour = CGColorCreate(space, white);
    CFAttributedStringRef as;
    CTLineRef line;
    double width = 0;

    CFDictionarySetValue(attrs, kCTFontAttributeName, font);
    /* Tiger's CTLineDraw takes its colour from here and ignores the context. */
    CFDictionarySetValue(attrs, kCTForegroundColorAttributeName, colour);
    as = CFAttributedStringCreate(NULL, s, attrs);
    line = CTLineCreateWithAttributedString(as);
    if (line) {
        CGFloat a = 0, d = 0, l = 0;
        width = CTLineGetTypographicBounds(line, &a, &d, &l);
        CGContextSetTextPosition(c->ctx, ox, oy);
        CTLineDraw(line, c->ctx);
        CFRelease(line);
    }
    CGColorRelease(colour); CGColorSpaceRelease(space);
    CFRelease(as); CFRelease(attrs); CFRelease(s);
    return width;
}

/* What the wire actually cost in position: the shaper's absolute positions
 * against the positions CoreText would have used for the same face and text.
 * Pixel counts say whether anything moved; this says how far. */
static double positionDelta(CTFontRef nativeFont, const TPRunMsg* m, int* worstGlyph)
{
    CFStringRef s = CFStringCreateWithCharacters(NULL, (const UniChar*)m->text, m->textLength);
    CFStringRef key = kCTFontAttributeName;
    CFDictionaryRef attrs = CFDictionaryCreate(NULL, (const void**)&key,
        (const void**)&nativeFont, 1, &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef as = CFAttributedStringCreate(NULL, s, attrs);
    CTLineRef line = CTLineCreateWithAttributedString(as);
    CFArrayRef runs = line ? CTLineGetGlyphRuns(line) : NULL;
    double worst = 0, pen = 0;

    *worstGlyph = -1;
    if (runs && CFArrayGetCount(runs)) {
        CTRunRef run = (CTRunRef)CFArrayGetValueAtIndex(runs, 0);
        const CGSize* adv = CTRunGetAdvancesPtr(run);
        CFIndex n = CTRunGetGlyphCount(run);
        unsigned lim = (unsigned)(n < (CFIndex)m->glyphCount ? n : m->glyphCount);
        unsigned i;
        for (i = 0; i < lim; ++i) {
            double d = fabs((double)m->posX[i] - pen);
            if (d > worst) { worst = d; *worstGlyph = (int)i; }
            if (adv) pen += adv[i].width;
        }
    }
    if (line) CFRelease(line);
    CFRelease(as); CFRelease(attrs); CFRelease(s);
    return worst;
}

static void handleRun(const TPRunMsg* m)
{
    struct canvas A, B;
    CTFontRef font = resolve(m);
    CTFontRef nativeFont = NULL;
    unsigned i, inkA = 0, inkB = 0, diff = 0, maxDelta = 0, notdef = 0;
    double hbWidth = 0, ctWidth = 0;
    char origin[64] = "";

    ++cases;
    if (!font) {
        printf("%-22s  UNRESOLVED handle\n", m->label);
        return;
    }
    {   /* Tiger's CTLineDraw refuses to render a font made from a CGFont, which
         * is what the handle resolver returns, so the native side resolves the
         * same face by PostScript name. fonthandletest.c proves the two are
         * metrically identical. */
        CFStringRef ps = CTFontCopyPostScriptName(font);
        if (ps) {
            CFStringGetCString(ps, origin, sizeof(origin), kCFStringEncodingUTF8);
            nativeFont = CTFontCreateWithName(ps, m->size, NULL);
            CFRelease(ps);
        }
        if (!nativeFont) nativeFont = (CTFontRef)CFRetain(font);
    }
    if (!makeCanvas(&A) || !makeCanvas(&B)) { CFRelease(font); return; }

    drawShaped(&A, font, m, 6, 16, &notdef);
    ctWidth = drawNative(&B, nativeFont, m, 6, 16);
    for (i = 0; i < m->glyphCount; ++i) hbWidth += 0;   /* positions are absolute */

    for (i = 0; i < (unsigned)(W * H); ++i) {
        unsigned a = A.px[i * 4], b = B.px[i * 4];
        unsigned d = a > b ? a - b : b - a;
        if (a) ++inkA;
        if (b) ++inkB;
        if (d) { ++diff; if (d > maxDelta) maxDelta = d; }
    }
    if (!diff) ++identical;
    if (m->fellBack) ++fellBackCases;
    if (notdef) ++notdefCases;

    {
        int worstGlyph = -1;
        double shift = positionDelta(nativeFont, m, &worstGlyph);
        printf("%-24s %-16s %2u gl%s  differ %4u (%5.1f%%) max %3u  worst shift %.4f pt%s\n",
            m->label, origin, m->glyphCount, notdef ? " NOTDEF" : "",
            diff, inkB ? 100.0 * diff / inkB : 0.0, maxDelta, shift,
            m->fellBack ? "  [fell back]" : "");
        if (shift > maxShiftSeen) maxShiftSeen = shift;
    }
    (void)hbWidth; (void)inkA; (void)ctWidth;

    freeCanvas(&A); freeCanvas(&B);
    CFRelease(nativeFont);
    CFRelease(font);
}

int main(int argc, char** argv)
{
    mach_port_t self;
    char service[64];
    pid_t child;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 4) { fprintf(stderr, "usage: raster32 <shaper64> <manifest> <webfont>\n"); return 2; }

    snprintf(service, sizeof(service), "com.webkittiger.textpixel.%d", (int)getpid());
    mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &self);
    mach_port_insert_right(mach_task_self(), self, self, MACH_MSG_TYPE_MAKE_SEND);
    if (bootstrap_register(bootstrap_port, service, self) != KERN_SUCCESS) {
        fprintf(stderr, "bootstrap_register failed\n"); return 1;
    }

    child = fork();   /* Tiger has no posix_spawn */
    if (child == 0) {
        execl(argv[1], argv[1], service, argv[2], argv[3], (char*)NULL);
        _exit(127);
    }

    printf("shaped in a 64-bit process, rasterised in this 32-bit one\n\n");
    printf("%-24s %-16s %s\n", "case", "face chosen there", "comparison here");

    for (;;) {
        union { TPRunRcv run; TPDataRcv data; TPSimpleRcv simple; mach_msg_header_t h; } in;
        mach_msg_return_t r = mach_msg(&in.h, MACH_RCV_MSG, 0, sizeof(in), self,
            30000, MACH_PORT_NULL);
        if (r != MACH_MSG_SUCCESS) { printf("\nreceive stopped: 0x%x\n", r); break; }
        if (in.h.msgh_id == TP_MSG_RUN) handleRun(&in.run.msg);
        else if (in.h.msgh_id == TP_MSG_FONTDATA) takeChunk(&in.data.msg);
        else if (in.h.msgh_id == TP_MSG_DONE) break;
    }
    waitpid(child, NULL, 0);

    printf("\n%d cases, %d pixel-identical, %d needed fallback, %d drew a .notdef\n",
        cases, identical, fellBackCases, notdefCases);
    printf("worst per-glyph position disagreement over the wire: %.4f pt\n", maxShiftSeen);
    return notdefCases ? 1 : 0;
}
