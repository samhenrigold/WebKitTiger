/*
 * ctdraw.c - does Tiger put the ink in the same place as modern CoreText?
 *
 * ctprobe.c compared metrics and glyph ids but never drew anything, so an
 * offset or scale error in the drawing path would not have shown up. This
 * rasterises the same string, from the same bundled font, into an 8-bit grey
 * bitmap on both machines and compares what landed.
 *
 * Antialiasing and hinting differ between the two rasterisers, so exact pixels
 * will never match. What must match is where the ink is and how much of it
 * there is: the inked bounding box, the total coverage, and the centroid. A
 * translation error moves the centroid and the box; a scale error changes the
 * box and the coverage together. Both are caught without demanding identical
 * pixels.
 *
 * Two drawing paths are measured, because WebCore uses both:
 *   - CTLineDraw, which is ctcompat's adapter on Tiger (Tiger's own takes an
 *     extra CFRange and draws nothing when the range is stack junk);
 *   - CGContextShowGlyphsWithAdvances, the glyph-level path.
 *
 * Run: ctdraw <font.ttf>
 */

#include <CoreFoundation/CoreFoundation.h>
#include <CoreText/CoreText.h>
#include <CoreGraphics/CoreGraphics.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 400
#define H 80
#define ORIGIN_X 20.0
#define ORIGIN_Y 24.0

/* No 'fi' or 'fl': the ligature would apply on the CTLine path and not on the
   glyph path, and this is about placement, not shaping. */
static const UniChar kText[] = { 'H','a','m','b','u','r','g','e','v','o','n','s',' ','1','2','3' };
#define NTEXT ((CFIndex)(sizeof kText / sizeof kText[0]))

static CFDataRef readfile(const char *p) {
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    void *b = malloc((size_t)n);
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
    fclose(f);
    CFDataRef d = CFDataCreate(NULL, (const UInt8 *)b, n);
    free(b);
    return d;
}

/* An 8-bit grey canvas, white, so ink is darkness. */
static CGContextRef make_canvas(unsigned char **bits) {
    *bits = (unsigned char *)malloc(W * H);
    memset(*bits, 0xFF, W * H);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceGray();
    CGContextRef c = CGBitmapContextCreate(*bits, W, H, 8, W, cs, kCGImageAlphaNone);
    CGColorSpaceRelease(cs);
    if (!c) return NULL;
    CGContextSetGrayFillColor(c, 0.0, 1.0);
    return c;
}

/* Reduce the canvas to numbers that survive a different antialiaser. */
static void measure(const char *key, const unsigned char *bits) {
    long inkPixels = 0;
    double coverage = 0, sx = 0, sy = 0;
    int x0 = W, y0 = H, x1 = -1, y1 = -1;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int v = bits[y * W + x];
            double ink = (255.0 - v) / 255.0;
            if (ink <= 0.02) continue;          /* ignore the faintest antialiasing */
            inkPixels++;
            coverage += ink;
            sx += ink * x; sy += ink * y;
            if (x < x0) x0 = x;
            if (y < y0) y0 = y;
            if (x > x1) x1 = x;
            if (y > y1) y1 = y;
        }
    }
    if (x1 < 0) { printf("%-44s EMPTY\n", key); return; }
    printf("%-44s bbox={%d,%d,%d,%d} w=%d h=%d coverage=%.2f centroid=(%.3f,%.3f) pixels=%ld\n",
           key, x0, y0, x1, y1, x1 - x0 + 1, y1 - y0 + 1,
           coverage, sx / coverage, sy / coverage, inkPixels);
    fflush(stdout);
}

static void draw_ctline(CGContextRef c, CTFontRef font) {
    CFStringRef s = CFStringCreateWithCharacters(NULL, kText, NTEXT);
    CFStringRef k[1]; CFTypeRef v[1];
    k[0] = kCTFontAttributeName; v[0] = font;
    CFDictionaryRef at = CFDictionaryCreate(NULL, (const void **)k, (const void **)v, 1,
                                            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef as = CFAttributedStringCreate(NULL, s, at);
    CTLineRef line = CTLineCreateWithAttributedString(as);
    if (line) {
        CGContextSetTextPosition(c, ORIGIN_X, ORIGIN_Y);
        CTLineDraw(line, c);
        CFRelease(line);
    }
    CFRelease(as); CFRelease(at); CFRelease(s);
}

static void draw_glyphs(CGContextRef c, CTFontRef font, double size) {
    CGGlyph g[NTEXT];
    CGSize adv[NTEXT];
    memset(g, 0, sizeof g);
    CTFontGetGlyphsForCharacters(font, kText, g, NTEXT);
    CTFontGetAdvancesForGlyphs(font, kCTFontOrientationHorizontal, g, adv, NTEXT);
    CGFontRef cg = CTFontCopyGraphicsFont(font, NULL);
    if (!cg) return;
    CGContextSetFont(c, cg);
    CGContextSetFontSize(c, size);
    CGContextSetTextPosition(c, ORIGIN_X, ORIGIN_Y);
    CGContextShowGlyphsWithAdvances(c, g, adv, NTEXT);
    CGFontRelease(cg);
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "DejaVuSans.ttf";
    printf("# ctdraw\n");
    CFDataRef data = readfile(path);
    if (!data) { printf("FATAL cannot read %s\n", path); return 2; }
    CTFontDescriptorRef fd = CTFontManagerCreateFontDescriptorFromData(data);
    if (!fd) { printf("FATAL no descriptor\n"); return 3; }

    static const double sizes[] = { 16.0, 24.0 };
    for (unsigned si = 0; si < sizeof sizes / sizeof sizes[0]; si++) {
        double sz = sizes[si];
        CTFontRef font = CTFontCreateWithFontDescriptor(fd, sz, NULL);
        if (!font) { printf("size%.0f FATAL no font\n", sz); continue; }

        /* The typographic width is what the ink should span, near enough. */
        {
            CGGlyph g[NTEXT]; CGSize adv[NTEXT];
            memset(g, 0, sizeof g);
            CTFontGetGlyphsForCharacters(font, kText, g, NTEXT);
            CTFontGetAdvancesForGlyphs(font, kCTFontOrientationHorizontal, g, adv, NTEXT);
            double sum = 0;
            for (CFIndex i = 0; i < NTEXT; i++) sum += adv[i].width;
            char k[128];
            snprintf(k, sizeof k, "size%.0f.advanceWidth", sz);
            printf("%-44s %.4f\n", k, sum);
        }

        for (int aa = 0; aa < 2; aa++) {
            unsigned char *bits = NULL;
            char k[128];

            CGContextRef c = make_canvas(&bits);
            if (!c) { printf("FATAL no bitmap context\n"); return 4; }
            CGContextSetShouldAntialias(c, aa ? true : false);
            CGContextSetShouldSmoothFonts(c, false);
            draw_ctline(c, font);
            snprintf(k, sizeof k, "size%.0f.aa%d.CTLineDraw", sz, aa);
            measure(k, bits);
            CGContextRelease(c); free(bits);

            c = make_canvas(&bits);
            CGContextSetShouldAntialias(c, aa ? true : false);
            CGContextSetShouldSmoothFonts(c, false);
            draw_glyphs(c, font, sz);
            snprintf(k, sizeof k, "size%.0f.aa%d.ShowGlyphsWithAdvances", sz, aa);
            measure(k, bits);
            CGContextRelease(c); free(bits);
        }
        CFRelease(font);
    }
    printf("\n## end\n");
    CFRelease(fd); CFRelease(data);
    return 0;
}
