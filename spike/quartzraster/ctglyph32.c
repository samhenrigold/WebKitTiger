/* TIGER quartzraster: dump Quartz's own glyph rasters, one glyph at a time, at a
 * sweep of subpixel offsets, so the 64-bit model rasteriser can be scored against
 * the pixels directly with no layout, no cairo and no compositing in between.
 *
 * Output: glyphs.bin
 *   magic "QGP1", int32 count, int32 patch (=PATCH)
 *   per record: char psName[128]; float size; int32 glyph; float dx, dy;
 *               uint8 cov[PATCH*PATCH]      (255 = full ink)
 * The glyph is drawn with its pen at (PEN + dx, PEN + dy) in top-down device
 * coordinates inside a PATCH x PATCH bitmap.
 */
#include <CoreText/CoreText.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PATCH = 48, PEN = 12 };   /* pen at x=12, baseline y=36 top-down */
#define BASE 36

/* flat-topped, flat-bottomed glyphs across sizes: if Quartz grid-fits in y, the
 * top edge of an H lands on a pixel boundary (no partial row) at every size. */
static struct { const char* name; double size; unsigned short ch; } kCases2[] = {
    { "Helvetica", 9, 'H' }, { "Helvetica", 10, 'H' }, { "Helvetica", 11, 'H' },
    { "Helvetica", 12, 'H' }, { "Helvetica", 13, 'H' }, { "Helvetica", 14, 'H' },
    { "Helvetica", 15, 'H' }, { "Helvetica", 16, 'H' }, { "Helvetica", 17, 'H' },
    { "Helvetica", 18, 'H' }, { "Helvetica", 19, 'H' }, { "Helvetica", 20, 'H' },
    { "Helvetica", 24, 'H' }, { "Helvetica", 32, 'H' }, { "Helvetica", 48, 'H' },
    { "Helvetica", 12, 'x' }, { "Helvetica", 13, 'x' }, { "Helvetica", 14, 'x' },
    { "Helvetica", 15, 'x' }, { "Helvetica", 16, 'x' }, { "Helvetica", 17, 'x' },
    { "Times-Roman", 12, 'H' }, { "Times-Roman", 14, 'H' }, { "Times-Roman", 16, 'H' },
    { "Times-Roman", 12, 'x' }, { "Times-Roman", 16, 'x' },
    { "LucidaGrande", 11, 'H' }, { "LucidaGrande", 13, 'H' }, { "LucidaGrande", 16, 'H' },
    { "LucidaGrande", 11, 'x' }, { "LucidaGrande", 13, 'x' }, { "LucidaGrande", 16, 'x' },
};
static const struct { const char* name; double size; unsigned short ch; } kCases[] = {
    { "Helvetica",          16, 'n' },
    { "Helvetica",          16, 'o' },
    { "Helvetica-Bold",     16, 'g' },
    { "LucidaGrande",       13, 'e' },
    { "LucidaGrande",       13, 'i' },
    { "Times-Roman",        16, 'a' },
    { "Times-Roman",        16, 'z' },
    { "Helvetica",          64, 'o' },   /* big: hinting and 26.6 effects shrink */
    { "LucidaGrande",       10, 's' },
};
enum { NCASES = sizeof(kCases) / sizeof(kCases[0]) };
enum { NCASES2 = sizeof(kCases2) / sizeof(kCases2[0]) };

int main(int argc, char** argv)
{
    const char* out = argc > 1 ? argv[1] : "glyphs.bin";
    FILE* f = fopen(out, "wb");
    int32_t count = 0, patch = PATCH;
    int c, k;
    long countPos;
    int edges = argc > 2 && !strcmp(argv[2], "edges");
    const struct { const char* name; double size; unsigned short ch; } *cases = edges ? kCases2 : kCases;
    int ncases = edges ? NCASES2 : NCASES, nsteps = edges ? 1 : 96;
    if (argc > 2 && !strcmp(argv[2], "sizes")) {
        static struct { const char* name; double size; unsigned short ch; } sweep[300];
        int m = 0; double sz;
        const char* fn = argc > 3 ? argv[3] : "Helvetica";
        for (sz = 9.0; sz <= 30.01; sz += 0.25) { sweep[m].name = fn; sweep[m].size = sz; sweep[m].ch = 'H'; ++m; }
        cases = (void*)sweep; ncases = m; nsteps = 1; edges = 1;
    }

    fwrite("QGP1", 1, 4, f);
    countPos = ftell(f);
    fwrite(&count, 4, 1, f);
    fwrite(&patch, 4, 1, f);

    for (c = 0; c < ncases; ++c) {
        CFStringRef nm = CFStringCreateWithCString(NULL, cases[c].name, kCFStringEncodingUTF8);
        CTFontRef font = CTFontCreateWithName(nm, cases[c].size, NULL);
        CFStringRef psRef = CTFontCopyPostScriptName(font);
        char ps[128] = "";
        UniChar u = cases[c].ch;
        CGGlyph g = 0;
        CFStringGetCString(psRef, ps, sizeof(ps), kCFStringEncodingUTF8);
        CTFontGetGlyphsForCharacters(font, &u, &g, 1);
        if (!g) { fprintf(stderr, "no glyph for %c in %s\n", cases[c].ch, ps); continue; }

        for (k = 0; k < nsteps; ++k) {
            /* k < 48: x sweep in 1/48 px. k >= 48: y sweep in 1/48 px. */
            double dx = k < 48 ? (k % 48) / 48.0 : 0.0;
            double dy = k < 48 ? 0.0 : (k % 48) / 48.0;
            unsigned char* px = (unsigned char*)malloc(PATCH * PATCH * 4);
            unsigned char cov[PATCH * PATCH];
            CGColorSpaceRef sp = CGColorSpaceCreateDeviceRGB();
            CGContextRef ctx;
            CGPoint at;
            int i;
            float fs = (float)cases[c].size, fdx = (float)dx, fdy = (float)dy;
            int32_t gi = g;

            memset(px, 0xff, PATCH * PATCH * 4);
            ctx = CGBitmapContextCreate(px, PATCH, PATCH, 8, PATCH * 4, sp, kCGImageAlphaNoneSkipLast);
            CGColorSpaceRelease(sp);
            CGContextSetShouldAntialias(ctx, true);
            CGContextSetShouldSmoothFonts(ctx, false);
            CGContextSetRGBFillColor(ctx, 0, 0, 0, 1);
            at = CGPointMake(PEN + dx, PATCH - BASE - dy);   /* CG y is up */
            CTFontDrawGlyphs(font, &g, &at, 1, ctx);
            for (i = 0; i < PATCH * PATCH; ++i) cov[i] = 255 - px[i * 4];
            fwrite(ps, 1, 128, f);
            fwrite(&fs, 4, 1, f);
            fwrite(&gi, 4, 1, f);
            fwrite(&fdx, 4, 1, f);
            fwrite(&fdy, 4, 1, f);
            fwrite(cov, 1, PATCH * PATCH, f);
            ++count;
            CGContextRelease(ctx); free(px);
        }
        CFRelease(psRef); CFRelease(font); CFRelease(nm);
    }
    fseek(f, countPos, SEEK_SET);
    fwrite(&count, 4, 1, f);
    fclose(f);
    fprintf(stderr, "wrote %s: %d patches of %dx%d\n", out, count, PATCH, PATCH);
    return 0;
}
