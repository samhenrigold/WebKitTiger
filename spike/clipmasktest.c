/* clipmasktest.c -- what does Tiger's CGContextClipToMask actually do?
 *
 * cgcompat hit this building the gradient alpha path: with a varying colour ramp
 * the destination alpha came out looking like mask times source colour rather
 * than mask alone, so the function was backed out as unmodelled. It matters
 * because GraphicsContextCG.cpp:1078 (clipToImageBuffer) calls it, and calls it
 * with an RGBA image, which is out of contract even on modern CG -- the call
 * site carries a FIXME saying the image needs to be grayscale.
 *
 * Documented semantics: the mask's sample value scales coverage, 1 paints fully
 * and 0 paints nothing, so filling opaque white through a mask of value m into a
 * cleared premultiplied RGBA context should give exactly (m, m, m, m).
 *
 * Measured on 10.4.11:
 *
 *   - With a DeviceGray non-alpha image the function is CORRECT and matches the
 *     documented semantics exactly. Destination alpha is the mask sample alone,
 *     independent of the fill colour, and the colour channels are colour x mask
 *     because the destination is premultiplied. That premultiplied colour channel
 *     is almost certainly what looked like "alpha came out as mask times source
 *     colour"; the alpha channel itself is the mask.
 *   - A CGImageMaskCreate stencil clips EVERYTHING away.
 *   - An RGBA image clips EVERYTHING away. This is what
 *     GraphicsContextCG::clipToImageBuffer passes, so on Tiger that call makes all
 *     subsequent drawing in the clipped region vanish rather than mask it.
 *   - Case 6 controls for the obvious objection: both of those images render fine
 *     through CGContextDrawImage, so they are well formed and it is ClipToMask
 *     that rejects them, silently.
 *
 * This measures, it does not assume. Every case prints the raw destination bytes.
 *
 * Build (from the repo root, in bash):
 *   toolchain/bin/tiger-clang -O1 -o build/clipmasktest spike/clipmasktest.c \
 *     -F compat/sdk-overlay \
 *     -F sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks \
 *     -ltigercompat -framework ApplicationServices
 */
#include <CoreGraphics/CoreGraphics.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static void expect(const char *what, int ok)
{
    printf("%-58s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok)
        ++failures;
}

/* One destination pixel, premultiplied RGBA, cleared to fully transparent. */
typedef struct { CGContextRef ctx; unsigned char px[4]; } Dest;

static int destInit(Dest *d)
{
    CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
    memset(d->px, 0, sizeof d->px);
    d->ctx = CGBitmapContextCreate(d->px, 1, 1, 8, 4, rgb, kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(rgb);
    return d->ctx != NULL;
}
static void destFree(Dest *d) { if (d->ctx) CGContextRelease(d->ctx); }

/* A 1x1 DeviceGray image whose single sample is `value`. */
static CGImageRef grayMask(unsigned char value, int asImageMask)
{
    static unsigned char storage[1];
    storage[0] = value;
    CGDataProviderRef p = CGDataProviderCreateWithData(NULL, storage, 1, NULL);
    CGImageRef img;
    if (asImageMask)
        img = CGImageMaskCreate(1, 1, 8, 8, 1, p, NULL, false);
    else {
        CGColorSpaceRef gray = CGColorSpaceCreateDeviceGray();
        img = CGImageCreate(1, 1, 8, 8, 1, gray, kCGImageAlphaNone, p, NULL, false,
                            kCGRenderingIntentDefault);
        CGColorSpaceRelease(gray);
    }
    CGDataProviderRelease(p);
    return img;
}

/* A 1x1 premultiplied RGBA image -- what clipToImageBuffer actually passes. */
static CGImageRef rgbaMask(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    static unsigned char storage[4];
    storage[0] = r; storage[1] = g; storage[2] = b; storage[3] = a;
    CGDataProviderRef p = CGDataProviderCreateWithData(NULL, storage, 4, NULL);
    CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
    CGImageRef img = CGImageCreate(1, 1, 8, 32, 4, rgb, kCGImageAlphaPremultipliedLast,
                                   p, NULL, false, kCGRenderingIntentDefault);
    CGColorSpaceRelease(rgb);
    CGDataProviderRelease(p);
    return img;
}

/* Fill one opaque pixel through `mask`, and report the destination bytes. */
static void fillThrough(CGImageRef mask, CGFloat r, CGFloat g, CGFloat b, CGFloat a,
                        unsigned char out[4])
{
    Dest d;
    if (!destInit(&d)) { memset(out, 0xEE, 4); return; }
    CGRect unit = CGRectMake(0, 0, 1, 1);
    if (mask)
        CGContextClipToMask(d.ctx, unit, mask);
    CGContextSetRGBFillColor(d.ctx, r, g, b, a);
    CGContextFillRect(d.ctx, unit);
    memcpy(out, d.px, 4);
    destFree(&d);
}

static void show(const char *label, const unsigned char p[4])
{
    printf("    %-44s r=%3u g=%3u b=%3u a=%3u\n", label, p[0], p[1], p[2], p[3]);
}

int main(void)
{
    unsigned char p[4], q[4];
    char label[96];
    setbuf(stdout, NULL);

    /* Control: no mask at all. If this is not opaque white the rest means nothing. */
    fillThrough(NULL, 1, 1, 1, 1, p);
    show("no mask, fill opaque white", p);
    expect("control: unmasked fill is opaque white", p[3] == 255 && p[0] == 255);

    /* 1. Gray image as mask, documented to scale coverage by the sample value. */
    printf("\n-- gray image mask, fill opaque white, expect a == mask value\n");
    {
        static const unsigned char vals[] = { 0, 64, 128, 192, 255 };
        int graded = 1, anyPaint = 0;
        unsigned char got[5];
        for (unsigned i = 0; i < sizeof vals / sizeof *vals; ++i) {
            CGImageRef m = grayMask(vals[i], 0);
            fillThrough(m, 1, 1, 1, 1, p);
            if (m) CGImageRelease(m);
            got[i] = p[3];
            snprintf(label, sizeof label, "mask=%3u", vals[i]);
            show(label, p);
            if (p[3])
                anyPaint = 1;
            /* allow a rounding slop of 1 either way */
            if (p[3] + 1 < vals[i] || p[3] > vals[i] + 1u)
                graded = 0;
        }
        expect("gray mask paints at all", anyPaint);
        expect("gray mask alpha tracks the sample value", graded);
        {
            int binary = 1;
            for (unsigned i = 0; i < 5; ++i)
                if (got[i] != 0 && got[i] != 255) binary = 0;
            if (binary && anyPaint)
                printf("    NOTE: every result is 0 or 255, so the mask is a stencil, not a ramp\n");
        }
    }

    /* 2. cgcompat's observation: does the source colour leak into the result alpha?
     *    Mask alone should set alpha identically no matter what colour is filled. */
    printf("\n-- same mask, different fill colours, expect identical alpha\n");
    {
        CGImageRef m = grayMask(128, 0);
        unsigned char aWhite, aRed, aGrey, aBlack;
        fillThrough(m, 1, 1, 1, 1, p); aWhite = p[3]; show("fill white", p);
        fillThrough(m, 1, 0, 0, 1, p); aRed   = p[3]; show("fill red", p);
        fillThrough(m, 0.5f, 0.5f, 0.5f, 1, p); aGrey = p[3]; show("fill mid grey", p);
        fillThrough(m, 0, 0, 0, 1, p); aBlack = p[3]; show("fill black", p);
        if (m) CGImageRelease(m);
        expect("result alpha does not depend on the fill colour",
               aWhite == aRed && aRed == aGrey && aGrey == aBlack);
    }

    /* 3. A real image mask (CGImageMaskCreate). For an image mask the sample is
     *    inverted relative to a gray image: 0 paints, 255 blocks. */
    printf("\n-- CGImageMaskCreate stencil, fill opaque white\n");
    {
        CGImageRef m0 = grayMask(0, 1), m255 = grayMask(255, 1);
        fillThrough(m0, 1, 1, 1, 1, p); show("image mask sample=0", p);
        fillThrough(m255, 1, 1, 1, 1, q); show("image mask sample=255", q);
        if (m0) CGImageRelease(m0);
        if (m255) CGImageRelease(m255);
        expect("image mask distinguishes 0 from 255", p[3] != q[3]);
    }

    /* 4. What WebCore actually passes: an RGBA image, out of contract even on
     *    modern CG (GraphicsContextCG.cpp:1078 carries a FIXME about it). */
    printf("\n-- RGBA image as the mask, which is what clipToImageBuffer passes\n");
    {
        CGImageRef opaqueWhite = rgbaMask(255, 255, 255, 255);
        CGImageRef halfWhite   = rgbaMask(128, 128, 128, 128);
        CGImageRef transparent = rgbaMask(0, 0, 0, 0);
        unsigned char a1, a2, a3;
        fillThrough(opaqueWhite, 1, 0, 0, 1, p); a1 = p[3]; show("rgba mask a=255", p);
        fillThrough(halfWhite,   1, 0, 0, 1, p); a2 = p[3]; show("rgba mask a=128", p);
        fillThrough(transparent, 1, 0, 0, 1, p); a3 = p[3]; show("rgba mask a=0",   p);
        if (opaqueWhite) CGImageRelease(opaqueWhite);
        if (halfWhite) CGImageRelease(halfWhite);
        if (transparent) CGImageRelease(transparent);
        expect("RGBA mask alpha changes the result at all", !(a1 == a2 && a2 == a3));
        if (a1 == a2 && a2 == a3)
            printf("    NOTE: an RGBA mask is ignored entirely; clipToImageBuffer would not clip\n");
    }

    /* 5. A ramp, not a single pixel: a varying mask under a varying colour. This is the
     *    case cgcompat described, and it separates the two readings of "alpha came out as
     *    mask times source colour". In a premultiplied buffer the COLOUR channels are
     *    colour x alpha by definition; only the alpha channel answers the question. */
    printf("\n-- 4-pixel ramp: mask 0/85/170/255 under a red-to-blue colour ramp\n");
    {
        static const unsigned char mvals[4] = { 0, 85, 170, 255 };
        static unsigned char mstore[4];
        unsigned char dst[4 * 4];
        int alphaIsMaskAlone = 1, colourIsPremultiplied = 1;
        memcpy(mstore, mvals, 4);
        CGDataProviderRef mp = CGDataProviderCreateWithData(NULL, mstore, 4, NULL);
        CGColorSpaceRef gray = CGColorSpaceCreateDeviceGray();
        CGImageRef ramp = CGImageCreate(4, 1, 8, 8, 4, gray, kCGImageAlphaNone, mp, NULL,
                                        false, kCGRenderingIntentDefault);
        CGColorSpaceRelease(gray);
        CGDataProviderRelease(mp);

        CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
        memset(dst, 0, sizeof dst);
        CGContextRef c = CGBitmapContextCreate(dst, 4, 1, 8, 16, rgb, kCGImageAlphaPremultipliedLast);
        CGColorSpaceRelease(rgb);
        if (c && ramp) {
            CGContextClipToMask(c, CGRectMake(0, 0, 4, 1), ramp);
            /* one opaque fill per column, colour varying across the row */
            for (int i = 0; i < 4; ++i) {
                CGFloat t = (CGFloat)i / 3;
                CGContextSetRGBFillColor(c, 1 - t, 0, t, 1);
                CGContextFillRect(c, CGRectMake(i, 0, 1, 1));
            }
            for (int i = 0; i < 4; ++i) {
                unsigned char *px = dst + i * 4;
                CGFloat t = (CGFloat)i / 3;
                unsigned expR = (unsigned)((1 - t) * mvals[i] + 0.5f);
                unsigned expB = (unsigned)(t * mvals[i] + 0.5f);
                snprintf(label, sizeof label, "col %d mask=%3u", i, mvals[i]);
                show(label, px);
                if (px[3] + 1 < mvals[i] || px[3] > mvals[i] + 1u)
                    alphaIsMaskAlone = 0;
                if (px[0] + 2 < expR || px[0] > expR + 2 || px[2] + 2 < expB || px[2] > expB + 2)
                    colourIsPremultiplied = 0;
            }
        }
        if (c) CGContextRelease(c);
        if (ramp) CGImageRelease(ramp);
        expect("ramp: alpha is the mask alone, independent of colour", alphaIsMaskAlone);
        expect("ramp: colour channels are colour x mask (premultiplied)", colourIsPremultiplied);
    }

    /* 6. Control for cases 3 and 4: are those masks well formed? Draw each one with
     *    CGContextDrawImage, where an image mask is a stencil and an RGBA image is just
     *    an image. If drawing works, the image is fine and CGContextClipToMask is what
     *    rejects it. */
    printf("\n-- control: the same images used with CGContextDrawImage, not as a clip\n");
    {
        CGImageRef stencil = grayMask(0, 1);          /* image mask, 0 should paint */
        CGImageRef rgba = rgbaMask(255, 0, 0, 255);   /* opaque red */
        Dest d;
        int stencilDraws = 0, rgbaDraws = 0;
        if (destInit(&d) && stencil) {
            CGContextSetRGBFillColor(d.ctx, 0, 1, 0, 1);   /* stencil paints the fill colour */
            CGContextDrawImage(d.ctx, CGRectMake(0, 0, 1, 1), stencil);
            show("image mask sample=0 via DrawImage", d.px);
            stencilDraws = d.px[3] != 0;
            destFree(&d);
        }
        if (destInit(&d) && rgba) {
            CGContextDrawImage(d.ctx, CGRectMake(0, 0, 1, 1), rgba);
            show("rgba image via DrawImage", d.px);
            rgbaDraws = d.px[3] != 0;
            destFree(&d);
        }
        if (stencil) CGImageRelease(stencil);
        if (rgba) CGImageRelease(rgba);
        expect("the image mask itself is well formed", stencilDraws);
        expect("the RGBA image itself is well formed", rgbaDraws);
        if (stencilDraws && rgbaDraws)
            printf("    => both images are fine; CGContextClipToMask rejects anything\n"
                   "       that is not a DeviceGray non-alpha image, and clips everything away\n");
    }

    printf("\n%s (%d failure%s)\n", failures ? "MEASURED DIFFERENCES" : "all expectations held",
           failures, failures == 1 ? "" : "s");
    return 0;   /* a probe reports, it does not gate a build */
}
