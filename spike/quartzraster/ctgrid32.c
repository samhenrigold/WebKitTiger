/* TIGER quartzraster: how many horizontal subpixel phases does Quartz's glyph
 * cache keep, as a function of point size? Draw one glyph at 1/48 px offsets and
 * hash the patch: offsets in the same cell are byte-identical, so the run lengths
 * are the grid. Also does the same on y. */
#include <CoreText/CoreText.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PATCH = 160, PEN = 20, BASE = 120, STEPS = 48 };

static unsigned hash(const unsigned char* p, int n)
{ unsigned h = 2166136261u; int i; for (i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; } return h; }

static double gScale = 1.0;
static unsigned render(CTFontRef font, CGGlyph g, double dx, double dy)
{
    static unsigned char* px;
    CGColorSpaceRef sp = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx;
    CGPoint at;
    unsigned h;
    if (!px) px = (unsigned char*)malloc(PATCH * PATCH * 4);
    memset(px, 0xff, PATCH * PATCH * 4);
    ctx = CGBitmapContextCreate(px, PATCH, PATCH, 8, PATCH * 4, sp, kCGImageAlphaNoneSkipLast);
    CGColorSpaceRelease(sp);
    CGContextSetShouldAntialias(ctx, true);
    CGContextSetShouldSmoothFonts(ctx, false);
    CGContextSetRGBFillColor(ctx, 0, 0, 0, 1);
    if (gScale != 1.0) CGContextScaleCTM(ctx, gScale, gScale);
    at = CGPointMake((PEN + dx) / gScale, (PATCH - BASE - dy) / gScale);
    CTFontDrawGlyphs(font, &g, &at, 1, ctx);
    h = hash(px, PATCH * PATCH * 4);
    CGContextRelease(ctx);
    return h;
}

static int gQuiet;
static void cells(const char* fontName, double size, unsigned short ch)
{
    CFStringRef nm = CFStringCreateWithCString(NULL, fontName, kCFStringEncodingUTF8);
    CTFontRef font = CTFontCreateWithName(nm, size, NULL);
    UniChar u = ch; CGGlyph g = 0;
    int k, axis;
    CTFontGetGlyphsForCharacters(font, &u, &g, 1);
    (void)gQuiet;
    for (axis = 0; axis < 2; ++axis) {
        unsigned prev = 0; int n = 0; int starts[64]; int ns = 0;
        for (k = 0; k < STEPS; ++k) {
            unsigned h = render(font, g, axis ? 0 : k / (double)STEPS, axis ? k / (double)STEPS : 0);
            if (!k || h != prev) { starts[ns++] = k; ++n; }
            prev = h;
        }
        printf("%c:%d cells [", axis ? 'y' : 'x', n);
        for (k = 0; k < ns && k < 10; ++k) printf("%s%d", k ? " " : "", starts[k]);
        printf("]  ");
    }
    printf("\n");
    CFRelease(font); CFRelease(nm);
}

int main(int argc, char** argv)
{
    double s;
    if (argc > 1 && !strcmp(argv[1], "tiny")) {
        printf("# tiny sizes, Helvetica 'o'\n");
        for (s = 1.0; s <= 9.0; s += 0.125) { printf("%7.3f  ", s); cells("Helvetica", s, 'o'); }
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "scale")) {
        /* is the phase count chosen from the point size or the device pixel size? */
        double sc[] = { 0.5, 1.0, 2.0, 3.0 }; int j;
        for (j = 0; j < 4; ++j) {
            double szs[] = { 6, 8, 12, 16, 24, 40 }; int i;
            gScale = sc[j];
            for (i = 0; i < 6; ++i) { printf("scale %.1f size %5.1f -> device %5.1f px  ", sc[j], szs[i], sc[j]*szs[i]); cells("Helvetica", szs[i], 'o'); }
        }
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "fine")) {
        printf("# fine threshold scan, Helvetica 'o'\n");
        for (s = 5.0; s <= 40.0; s += 0.125) { printf("%7.3f  ", s); cells("Helvetica", s, 'o'); }
        return 0;
    }
    { double sizes[] = { 6,7,8,9,10,11,12,13,16,17,24,32,36,48,64 }; int i;
      for (i = 0; i < 15; ++i) cells("Helvetica", sizes[i], 'o'); }
    return 0;
}
