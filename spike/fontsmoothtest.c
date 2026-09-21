/* fontsmoothtest.c -- do Tiger's text antialiasing knobs do anything?
 *
 * cgcompat maps CGContextSetShouldAntialiasFonts onto Tiger's
 * CGContextSetShouldSmoothFonts (cgcompat.c:1076) and notes it has never been
 * verified that the mapping does anything. It also matters that the two are not
 * the same knob even when both work: "antialias fonts" is grayscale coverage,
 * "smooth fonts" is subpixel/LCD rendering. If WebCore asks for text
 * antialiasing off and Tiger only hears "no subpixel smoothing", the glyphs stay
 * antialiased and the request is silently dropped.
 *
 * The context is deliberately OPAQUE (kCGImageAlphaNoneSkipLast). CG disables
 * subpixel smoothing for contexts with an alpha channel even on modern macOS, so
 * testing in an RGBA context would prove nothing about the knob.
 *
 * Measured on 10.4.11, into a bitmap context:
 *
 *   - CGContextSetShouldAntialias WORKS, on paths and on glyphs. Off, text goes
 *     fully aliased: inked 615 -> 247 and antialiased pixels 556 -> 0. This is
 *     the knob that answers a request to turn font antialiasing off.
 *   - CGContextSetShouldSmoothFonts is a NO-OP. On and off produce byte-identical
 *     pixels, and no rendering ever produces a colour fringe. So mapping
 *     CGContextSetShouldAntialiasFonts onto it silently drops the request.
 *   - CGContextSetAllowsAntialiasing works and gates the should-flag.
 *   - CGContextSetAllowsFontSmoothing is a no-op, consistently with the above.
 *   - CGFontSetShouldAntialias, private but exported, IS honoured by the
 *     rasterizer, and it is per-font rather than per-context: a glyph run goes
 *     from inked 593 / antialiased 542 to inked 235 / antialiased 0, while shapes
 *     in the same context stay smooth. This is the faithful target for
 *     CGContextSetShouldAntialiasFonts, which the context-wide
 *     CGContextSetShouldAntialias cannot be without aliasing shapes too.
 *   - All of the above holds on both text paths: CGContextSelectFont with
 *     ShowTextAtPoint, and the CGFontRef plus ShowGlyphsWithAdvances path that
 *     WebCore actually uses. CGFontCreateWithFontName and
 *     CGFontCreateWithDataProvider return NULL on Tiger, so the CGFontRef comes
 *     from ATSFontFindFromName plus CGFontCreateWithPlatformFont.
 *
 * Caveat, stated rather than hidden: this only proves it for BITMAP contexts.
 * CG generally restricts subpixel smoothing to the screen, so a window context
 * could behave differently, and that is not testable headlessly here. It does
 * not weaken the actionable half -- SetShouldAntialias demonstrably turns text
 * antialiasing off and SetShouldSmoothFonts demonstrably does not -- but it does
 * mean "Tiger has no font smoothing at all" is not what was measured.
 *
 * Two independent signals are read out of each rendering:
 *   - grayscale antialiasing: any pixel strictly between black and white
 *   - subpixel smoothing:     any pixel whose R and B differ, i.e. a colour
 *                             fringe on what is a black-on-white drawing
 *
 * Build (from the repo root, in bash):
 *   toolchain/bin/tiger-clang -O1 -o build/fontsmoothtest spike/fontsmoothtest.c \
 *     -F compat/sdk-overlay \
 *     -F sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks \
 *     -ltigercompat -framework ApplicationServices
 */
#include <CoreGraphics/CoreGraphics.h>
#include <ApplicationServices/ApplicationServices.h>
/* The overlay's CoreText headers: they declare Tiger's real signatures, notably
   CTFontCreateWithGraphicsFont taking a double size rather than a CGFloat. */
#include <CoreText/CoreText.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Tiger exports this but the 10.4u SDK does not declare it; private-but-exported
   Tiger API is fair game on this port (see NOTES.md). */
CG_EXTERN void CGContextSetAllowsFontSmoothing(CGContextRef context, bool allows);

/* Private but exported, and the interesting ones: Tiger carries a PER-FONT
   antialias flag, stored in a bit at font+0x3c (verified in the disassembly of
   _CGFontSetShouldAntialias). If the rasterizer honours it, this is the faithful
   target for CGContextSetShouldAntialiasFonts -- it would alias glyphs without
   aliasing shapes, which the context-wide CGContextSetShouldAntialias cannot do.
   Storing a bit and honouring it are different things, so this measures. */
/* CGFontSetShouldAntialias / CGFontShouldAntialias are now declared by
   TigerCompat/CGCompat.h, which CoreText.h pulls in, so they are not redeclared
   here. CGFontCreateWithPlatformFont still needs one. */
CG_EXTERN CGFontRef CGFontCreateWithPlatformFont(void *platformFontReference);

#define W 96
#define H 24
#define STRIDE (W * 4)

static int failures;
static void expect(const char *what, int ok)
{
    printf("%-62s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok)
        ++failures;
}

typedef struct {
    unsigned char px[H * STRIDE];
    int inked;        /* pixels darker than white */
    int grey;         /* pixels strictly between black and white: grayscale AA */
    int fringed;      /* pixels with R != B: subpixel smoothing */
} Shot;

/* smooth/anti of -1 means "do not call the setter at all". */
static void render(Shot *s, int anti, int smooth, int allowsAnti, int allowsSmooth, int drawText)
{
    CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
    memset(s->px, 0xFF, sizeof s->px);          /* opaque white background */
    CGContextRef c = CGBitmapContextCreate(s->px, W, H, 8, STRIDE, rgb,
                                           kCGImageAlphaNoneSkipLast);
    CGColorSpaceRelease(rgb);
    s->inked = s->grey = s->fringed = 0;
    if (!c)
        return;

    if (allowsAnti >= 0)  CGContextSetAllowsAntialiasing(c, allowsAnti != 0);
    if (allowsSmooth >= 0) CGContextSetAllowsFontSmoothing(c, allowsSmooth != 0);
    if (anti >= 0)        CGContextSetShouldAntialias(c, anti != 0);
    if (smooth >= 0)      CGContextSetShouldSmoothFonts(c, smooth != 0);

    CGContextSetRGBFillColor(c, 0, 0, 0, 1);
    if (drawText) {
        CGContextSelectFont(c, "Helvetica", 14.0, kCGEncodingMacRoman);
        CGContextSetTextDrawingMode(c, kCGTextFill);
        CGContextShowTextAtPoint(c, 2, 6, "Hamburgefonstiv", 15);
    } else {
        /* A shape, as a control: this exercises CGContextSetShouldAntialias on a
           path rather than on glyphs. A diagonal edge is the clearest signal. */
        CGContextMoveToPoint(c, 0, 0);
        CGContextAddLineToPoint(c, W, H);
        CGContextAddLineToPoint(c, W, 0);
        CGContextClosePath(c);
        CGContextFillPath(c);
    }
    CGContextRelease(c);

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const unsigned char *p = s->px + y * STRIDE + x * 4;
            if (p[0] != 255 || p[1] != 255 || p[2] != 255)
                ++s->inked;
            for (int k = 0; k < 3; ++k)
                if (p[k] != 0 && p[k] != 255) { ++s->grey; break; }
            if (p[0] != p[2])
                ++s->fringed;
        }
    }
}

static int samePixels(const Shot *a, const Shot *b)
{
    return memcmp(a->px, b->px, sizeof a->px) == 0;
}

static void report(const char *label, const Shot *s)
{
    printf("    %-40s inked=%-5d antialiased=%-5d fringed=%d\n",
           label, s->inked, s->grey, s->fringed);
}

int main(void)
{
    static Shot a, b, c2, d;
    setbuf(stdout, NULL);

    /* Control: does antialiasing work at all, on a path? cgcompat.c:830 turns it
       off deliberately for the two-halves fill, so this had better be real. */
    printf("-- control: CGContextSetShouldAntialias on a diagonal path\n");
    render(&a, 1, -1, -1, -1, 0);
    render(&b, 0, -1, -1, -1, 0);
    report("antialias on", &a);
    report("antialias off", &b);
    expect("the path is drawn at all", a.inked > 0 && b.inked > 0);
    expect("antialias on produces intermediate pixels", a.grey > 0);
    expect("antialias off produces none", b.grey == 0);
    expect("CGContextSetShouldAntialias changes the pixels", !samePixels(&a, &b));

    /* Does text draw, and is it antialiased by default? */
    printf("\n-- text, default state\n");
    render(&a, -1, -1, -1, -1, 1);
    report("no knobs touched", &a);
    expect("text is drawn at all", a.inked > 0);
    if (!a.inked) {
        printf("    text did not draw; the rest of this probe is meaningless\n");
        printf("\n%d failure%s\n", failures, failures == 1 ? "" : "s");
        return 0;
    }

    /* The knob cgcompat's CGContextSetShouldAntialiasFonts maps onto. */
    printf("\n-- CGContextSetShouldSmoothFonts on text\n");
    render(&a, -1, 1, -1, -1, 1);
    render(&b, -1, 0, -1, -1, 1);
    report("smooth fonts on", &a);
    report("smooth fonts off", &b);
    expect("CGContextSetShouldSmoothFonts changes the pixels", !samePixels(&a, &b));
    if (samePixels(&a, &b))
        printf("    NOTE: the setter is accepted and discarded, so mapping\n"
               "          CGContextSetShouldAntialiasFonts onto it is a no-op\n");
    expect("subpixel smoothing actually happens when asked", a.fringed > 0);
    if (!a.fringed)
        printf("    NOTE: no colour fringes, so Tiger renders grayscale here regardless\n");

    /* The knob that would really turn text antialiasing off. */
    printf("\n-- CGContextSetShouldAntialias on text\n");
    render(&c2, 1, -1, -1, -1, 1);
    render(&d, 0, -1, -1, -1, 1);
    report("antialias on", &c2);
    report("antialias off", &d);
    expect("CGContextSetShouldAntialias changes text pixels", !samePixels(&c2, &d));
    expect("antialias off makes text fully aliased", d.grey == 0);
    if (d.grey == 0 && c2.grey > 0)
        printf("    => this, not SetShouldSmoothFonts, is the knob that answers\n"
               "       a request to turn font antialiasing off\n");

    /* The Allows* gates, which sit above the Should* knobs. */
    printf("\n-- CGContextSetAllowsAntialiasing / SetAllowsFontSmoothing\n");
    render(&a, 1, -1, 0, -1, 1);
    render(&b, 1, -1, 1, -1, 1);
    report("allows antialiasing false, should true", &a);
    report("allows antialiasing true,  should true", &b);
    expect("CGContextSetAllowsAntialiasing gates the should-flag", !samePixels(&a, &b));
    render(&a, -1, 1, -1, 0, 1);
    render(&b, -1, 1, -1, 1, 1);
    report("allows font smoothing false, should true", &a);
    report("allows font smoothing true,  should true", &b);
    expect("CGContextSetAllowsFontSmoothing gates the should-flag", !samePixels(&a, &b));

    /* Everything above draws through CGContextSelectFont/ShowTextAtPoint, which
       needs no CGFontRef. WebCore does not: it draws glyph IDs through
       CGContextShowGlyphsWithAdvances from a CGFontRef. Redo the measurement on
       that path, and test Tiger's private per-font antialias flag while we have
       a CGFontRef in hand. CGFontCreateWithFontName and
       CGFontCreateWithDataProvider both return NULL on Tiger, so the only route
       is ATS. */
    printf("\n-- the path WebCore actually uses: CGFontRef + ShowGlyphsWithAdvances\n");
    {
        ATSFontRef ats = ATSFontFindFromName(CFSTR("Helvetica"), kATSOptionFlagsDefault);
        CGFontRef font = (ats != kATSFontRefUnspecified) ? CGFontCreateWithPlatformFont(&ats) : NULL;
        printf("    ATSFontFindFromName -> %lu, CGFontCreateWithPlatformFont -> %p\n",
               (unsigned long)ats, (void *)font);
        expect("a CGFontRef is obtainable at all", font != NULL);
        if (font) {
            /* Glyph IDs via CoreText, which Tiger has privately. */
            CTFontRef ct = CTFontCreateWithGraphicsFont(font, 14.0, NULL, NULL);
            static const UniChar chars[] = { 'H','a','m','b','u','r','g','e','f','o','n','s' };
            enum { NG = sizeof chars / sizeof *chars };
            CGGlyph glyphs[NG];
            CGSize advances[NG];
            int haveGlyphs = 0;
            if (ct && CTFontGetGlyphsForCharacters(ct, chars, glyphs, NG)) {
                haveGlyphs = 1;
                CTFontGetAdvancesForGlyphs(ct, 0, glyphs, advances, NG);
            }
            expect("glyph IDs came back from CoreText", haveGlyphs);

            if (haveGlyphs) {
                Shot g1, g2, g3;
                /* helper: render the glyph run with a given knob setting */
                #define GLYPHRUN(shot, setup) do {                                     \
                    CGColorSpaceRef _rgb = CGColorSpaceCreateDeviceRGB();               \
                    memset((shot).px, 0xFF, sizeof (shot).px);                          \
                    CGContextRef _c = CGBitmapContextCreate((shot).px, W, H, 8, STRIDE,  \
                                        _rgb, kCGImageAlphaNoneSkipLast);               \
                    CGColorSpaceRelease(_rgb);                                          \
                    (shot).inked = (shot).grey = (shot).fringed = 0;                    \
                    if (_c) {                                                           \
                        CGContextSetRGBFillColor(_c, 0, 0, 0, 1);                        \
                        CGContextSetFont(_c, font);                                      \
                        CGContextSetFontSize(_c, 14.0);                                  \
                        CGContextSetTextDrawingMode(_c, kCGTextFill);                    \
                        setup;                                                           \
                        CGContextSetTextPosition(_c, 2, 6);                              \
                        CGContextShowGlyphsWithAdvances(_c, glyphs, advances, NG);        \
                        CGContextRelease(_c);                                            \
                        for (int _y = 0; _y < H; ++_y)                                    \
                        for (int _x = 0; _x < W; ++_x) {                                  \
                            const unsigned char *_p = (shot).px + _y * STRIDE + _x * 4;   \
                            if (_p[0] != 255 || _p[1] != 255 || _p[2] != 255) ++(shot).inked; \
                            for (int _k = 0; _k < 3; ++_k)                                 \
                                if (_p[_k] != 0 && _p[_k] != 255) { ++(shot).grey; break; } \
                            if (_p[0] != _p[2]) ++(shot).fringed;                          \
                        }                                                                  \
                    }                                                                      \
                } while (0)

                GLYPHRUN(g1, CGContextSetShouldSmoothFonts(_c, true));
                GLYPHRUN(g2, CGContextSetShouldSmoothFonts(_c, false));
                report("glyph run, smooth fonts on", &g1);
                report("glyph run, smooth fonts off", &g2);
                expect("glyphs draw through ShowGlyphsWithAdvances", g1.inked > 0);
                expect("SetShouldSmoothFonts changes the glyph path", !samePixels(&g1, &g2));

                /* The private per-font flag. */
                CGFontSetShouldAntialias(font, 0);
                printf("    CGFontShouldAntialias after setting 0 -> %d\n", CGFontShouldAntialias(font));
                GLYPHRUN(g3, (void)0);
                report("glyph run, CGFontSetShouldAntialias(0)", &g3);
                CGFontSetShouldAntialias(font, 1);
                expect("the per-font flag is stored", CGFontShouldAntialias(font) != 0);
                expect("the per-font antialias flag is honoured by the rasterizer",
                       g3.grey == 0 && g3.inked > 0);
                if (g3.grey > 0)
                    printf("    NOTE: the flag is stored but the rasterizer ignores it\n");
                #undef GLYPHRUN
            }
            if (ct) CFRelease(ct);
            CGFontRelease(font);
        }
    }

    printf("\n%s (%d measured difference%s from the documented behaviour)\n",
           failures ? "DIFFERENCES FOUND" : "everything behaved as documented",
           failures, failures == 1 ? "" : "s");
    return 0;   /* a probe reports, it does not gate a build */
}
