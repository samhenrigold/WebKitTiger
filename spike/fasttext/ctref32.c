/* TIGER fasttext: the CoreText reference.
 *
 * Draws spike/fasttext/sample.h with CoreText into a bitmap the same size the
 * 64-bit cairo/FreeType side uses, and dumps two things:
 *
 *   ref-<mode>.bin   the pixels (so the 64-bit side, which has libpng, can turn
 *                    them into a PNG and score against them)
 *   ref.glyphs       glyph ids and device positions per line, so the cairo side
 *                    rasterises *exactly* the layout CoreText chose. Shaping is
 *                    already proven identical in spike/textpixel; this spike is
 *                    only about what the pixels look like, so layout is taken
 *                    from here rather than re-derived.
 *
 * usage: ctref32 <outdir> smooth|gray
 *   smooth = CGContextSetShouldSmoothFonts(true), which is what the box does by
 *            default (AppleFontSmoothing unset -> Tiger's LCD "medium").
 */
#include <CoreText/CoreText.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sample.h"

/* Grid probe: draw the same line at k/16 px offsets and hash the pixels. Offsets
 * that fall in the same quantisation cell render identically, so the run lengths
 * of equal hashes ARE the grid. x is the positive control: we already know from
 * the main experiment that Quartz floors x to 1/4 px, so x must come out in
 * groups of four. */
static unsigned hashPixels(const unsigned char* p, int n)
{
    unsigned h = 2166136261u; int i;
    for (i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
    return h;
}

static void gridProbe(int smooth)
{
    enum { PW = 420, PH = 44 };
    const SampleLine* s = &kSample[3];           /* Helvetica 16 */
    CFStringRef name = CFStringCreateWithCString(NULL, s->psName, kCFStringEncodingUTF8);
    CTFontRef font = CTFontCreateWithName(name, s->size, NULL);
    CFStringRef text = CFStringCreateWithCString(NULL, s->utf8, kCFStringEncodingUTF8);
    CFStringRef key = kCTFontAttributeName;
    CFDictionaryRef attrs = CFDictionaryCreate(NULL, (const void**)&key, (const void**)&font, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef as = CFAttributedStringCreate(NULL, text, attrs);
    CTLineRef line = CTLineCreateWithAttributedString(as);
    int axis, k;

    for (axis = 0; axis < 2; ++axis) {
        printf("# CoreText %c-grid probe (offsets of 1/16 px; equal hashes = same cell)\n",
            axis ? 'y' : 'x');
        for (k = 0; k < 16; ++k) {
            unsigned char* px = (unsigned char*)calloc(PW * PH, 4);
            CGColorSpaceRef sp = CGColorSpaceCreateDeviceRGB();
            CGContextRef c = CGBitmapContextCreate(px, PW, PH, 8, PW * 4, sp, kCGImageAlphaNoneSkipLast);
            double d = k / 16.0;
            CGColorSpaceRelease(sp);
            CGContextSetRGBFillColor(c, 1, 1, 1, 1);
            CGContextFillRect(c, CGRectMake(0, 0, PW, PH));
            CGContextSetShouldAntialias(c, true);
            CGContextSetShouldSmoothFonts(c, smooth);
            CGContextSetRGBFillColor(c, 0, 0, 0, 1);
            /* top-down baseline 30; CG y grows up, so a downward shift subtracts */
            /* ONE glyph: with many glyphs at different fractional x, a quantiser
             * never reproduces a bitmap exactly and the probe reads as "continuous"
             * no matter what the grid is. The y axis is safe either way because
             * every glyph on a line shares its y. */
            {   CFArrayRef rr = CTLineGetGlyphRuns(line);
                CTRunRef r0 = (CTRunRef)CFArrayGetValueAtIndex(rr, 0);
                const CGGlyph* gg = CTRunGetGlyphsPtr(r0);
                CGGlyph one = gg[0];
                CGPoint at = CGPointMake(10 + (axis ? 0 : d), PH - 30 - (axis ? d : 0));
                CTFontDrawGlyphs(font, &one, &at, 1, c);
            }
            printf("%c %2d/16 = %.4f  %08x\n", axis ? 'y' : 'x', k, d, hashPixels(px, PW * PH * 4));
            CGContextRelease(c); free(px);
        }
    }
    CFRelease(line); CFRelease(as); CFRelease(attrs); CFRelease(text); CFRelease(font); CFRelease(name);
}

int main(int argc, char** argv)
{
    const char* outdir = argc > 1 ? argv[1] : ".";
    int smooth = argc > 2 && !strcmp(argv[2], "smooth");
    unsigned char* px = (unsigned char*)calloc(FT_W * FT_H, 4);
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(px, FT_W, FT_H, 8, FT_W * 4, space,
        kCGImageAlphaNoneSkipLast);
    char path[512];
    FILE* f;
    int i, y;
    int coloured = 0;

    CGColorSpaceRelease(space);
    if (!ctx) { fprintf(stderr, "no bitmap context\n"); return 1; }

    if (argc > 2 && !strcmp(argv[2], "ygrid")) { gridProbe(1); return 0; }

    CGContextSetRGBFillColor(ctx, 1, 1, 1, 1);
    CGContextFillRect(ctx, CGRectMake(0, 0, FT_W, FT_H));
    CGContextSetShouldAntialias(ctx, true);
    CGContextSetShouldSmoothFonts(ctx, smooth);
    CGContextSetRGBFillColor(ctx, 0, 0, 0, 1);

    sprintf(path, "%s/ref.glyphs", outdir);
    f = fopen(path, "w");

    for (i = 0; i < FT_LINES; ++i) {
        const SampleLine* s = &kSample[i];
        double baseline = FT_LINE0 + i * FT_LEADING;      /* from the top */
        CFStringRef name = CFStringCreateWithCString(NULL, s->psName, kCFStringEncodingUTF8);
        CTFontRef font = CTFontCreateWithName(name, s->size, NULL);
        CFStringRef text = CFStringCreateWithCString(NULL, s->utf8, kCFStringEncodingUTF8);
        CFStringRef key = kCTFontAttributeName;
        CFDictionaryRef attrs = CFDictionaryCreate(NULL, (const void**)&key, (const void**)&font, 1,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFAttributedStringRef as = CFAttributedStringCreate(NULL, text, attrs);
        CTLineRef line = CTLineCreateWithAttributedString(as);
        CFArrayRef runs = CTLineGetGlyphRuns(line);
        CFIndex r;
        double pen = FT_X0;

        CGContextSetTextPosition(ctx, FT_X0, FT_H - baseline);
        CTLineDraw(line, ctx);

        for (r = 0; r < CFArrayGetCount(runs); ++r) {
            CTRunRef run = (CTRunRef)CFArrayGetValueAtIndex(runs, r);
            const CGSize* adv = CTRunGetAdvancesPtr(run);
            const CGGlyph* g = CTRunGetGlyphsPtr(run);
            CFIndex n = CTRunGetGlyphCount(run), k;
            CTFontRef runFont = (CTFontRef)CFDictionaryGetValue(CTRunGetAttributes(run), kCTFontAttributeName);
            CFStringRef runName = CTFontCopyPostScriptName(runFont);
            char psn[128] = "";
            CFStringGetCString(runName, psn, sizeof(psn), kCFStringEncodingUTF8);
            CFRelease(runName);
            /* one output line per run: the run's real face may differ from the
             * requested one when CoreText fell back. */
            fprintf(f, "%d\t%s\t%.4f\t%d", i, psn, CTFontGetSize(runFont), (int)n);
            /* No CTRunGetPositionsPtr on 10.4; accumulate advances, which is
             * exactly what spike/textpixel proved matches CoreText's own pen. */
            for (k = 0; k < n; ++k) {
                fprintf(f, "\t%u,%.4f,%.4f", (unsigned)g[k], pen, baseline);
                pen += adv[k].width;
            }
            fprintf(f, "\n");
        }
        CFRelease(line); CFRelease(as); CFRelease(attrs);
        CFRelease(text); CFRelease(font); CFRelease(name);
    }
    fclose(f);

    /* Did Quartz actually produce subpixel-coloured pixels in a bitmap context? */
    for (i = 0; i < FT_W * FT_H && !coloured; ++i)
        if (px[i * 4] != px[i * 4 + 1] || px[i * 4 + 1] != px[i * 4 + 2]) coloured = 1;
    fprintf(stderr, "%s: subpixel-coloured pixels present: %s\n",
        smooth ? "smooth" : "gray", coloured ? "yes" : "no");

    sprintf(path, "%s/ref-%s.bin", outdir, smooth ? "smooth" : "gray");
    f = fopen(path, "wb");
    fwrite(FT_MAGIC, 1, 4, f);
    { int32_t d[2]; d[0] = FT_W; d[1] = FT_H; fwrite(d, 4, 2, f); }
    /* CGBitmapContext's origin is bottom-left, but its *memory* is already
     * top-down: row 0 is the top scanline. No flip needed. */
    for (y = 0; y < FT_H; ++y)
        fwrite(px + (size_t)y * FT_W * 4, 4, FT_W, f);
    fclose(f);
    fprintf(stderr, "wrote %s\n", path);
    return 0;
}
