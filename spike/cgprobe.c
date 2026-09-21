/*
 * cgprobe.c -- behavioural probe of Tiger's OWN CoreGraphics, not our shims.
 *
 * The failure mode this exists for: a function whose signature matches, whose
 * arguments are read, and whose output is still wrong. CGShading dropping the
 * alpha its function returns was one; the 10.5 blend modes compositing as
 * Normal was another. Neither is visible to a static screen.
 *
 * Each check draws into a small bitmap with plain CoreGraphics calls and
 * reports a few sampled pixels. The expected values are produced by running
 * this same file against modern CoreGraphics on the host Mac:
 *
 *   cc -O1 -o build/cgprobe-host spike/cgprobe.c -framework ApplicationServices
 *   build/cgprobe-host --emit > spike/cgprobe-expected.h
 *
 * then it is cross-built for Tiger and run on the box, where it compares.
 *
 *   toolchain/bin/tiger-clang -O1 -o build/cgprobe spike/cgprobe.c \
 *     -F compat/sdk-overlay \
 *     -F sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks \
 *     -ltigercompat -framework ApplicationServices
 *
 * Findings are written up in compat/CG-PROBE.md.
 */

#include <CoreGraphics/CoreGraphics.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 32
#define H 32
#define MAX_VALUES 12

/* Tolerance per sampled channel. Colour management and rasteriser differences
   move values a little between 2005 and now; a real divergence moves them a
   lot. */
#define TOLERANCE 12

static unsigned char* gPixels;
static CGContextRef gContext;
static CGColorSpaceRef gDeviceRGB;

/* Bitmap is premultiplied-first, 32-bit little endian: bytes are B, G, R, A.
   Coordinates are CG's, origin bottom left. */
static void sample(int x, int y, int* out)
{
    const unsigned char* p = gPixels + (size_t)(H - 1 - y) * W * 4 + (size_t)x * 4;
    out[0] = p[2]; out[1] = p[1]; out[2] = p[0]; out[3] = p[3];
}
static int sampleOne(int x, int y, int* values, int n)
{
    int rgba[4];
    sample(x, y, rgba);
    values[n] = rgba[0]; values[n + 1] = rgba[1];
    values[n + 2] = rgba[2]; values[n + 3] = rgba[3];
    return n + 4;
}

static void newContext(uint32_t bitmapInfo, CGColorSpaceRef space)
{
    if (gContext)
        CGContextRelease(gContext);
    memset(gPixels, 0, (size_t)W * H * 4);
    gContext = CGBitmapContextCreate(gPixels, W, H, 8, W * 4,
        space ? space : gDeviceRGB, bitmapInfo);
}
static void newDefaultContext(void)
{
    newContext((uint32_t)kCGImageAlphaPremultipliedFirst | (uint32_t)kCGBitmapByteOrder32Little,
        NULL);
}

static CGImageRef makeImage(int w, int h)
{
    /* Left half red, right half half-alpha blue. */
    CGContextRef c = CGBitmapContextCreate(NULL, w, h, 8, 0, gDeviceRGB,
        (uint32_t)kCGImageAlphaPremultipliedFirst | (uint32_t)kCGBitmapByteOrder32Little);
    CGImageRef image;
    CGContextSetRGBFillColor(c, 1, 0, 0, 1);
    CGContextFillRect(c, CGRectMake(0, 0, w / 2.0, h));
    CGContextSetRGBFillColor(c, 0, 0, 1, 0.5);
    CGContextFillRect(c, CGRectMake(w / 2.0, 0, w / 2.0, h));
    image = CGBitmapContextCreateImage(c);
    CGContextRelease(c);
    return image;
}

/* Destination for the compositing checks: half-alpha green over the whole bitmap. */
static void paintBackdrop(void)
{
    CGContextSetRGBFillColor(gContext, 0, 1, 0, 0.5);
    CGContextFillRect(gContext, CGRectMake(0, 0, W, H));
}

/* ------------------------------------------------------------------ checks */

static int blendMode(int mode, int* v)
{
    /* The source is HALF alpha on purpose. With an opaque source, Copy and
       Normal produce the same pixel, so the check would pass on a CG that
       ignores Copy entirely. */
    newDefaultContext();
    paintBackdrop();
    CGContextSetBlendMode(gContext, (CGBlendMode)mode);
    CGContextSetRGBFillColor(gContext, 1, 0, 0, 0.5);
    CGContextFillRect(gContext, CGRectMake(0, 0, W, H));
    return sampleOne(W / 2, H / 2, v, 0);
}
static int c_blendNormal(int* v) { return blendMode(0, v); }
static int c_blendMultiply(int* v) { return blendMode(1, v); }
static int c_blendScreen(int* v) { return blendMode(2, v); }
static int c_blendCopy(int* v) { return blendMode(17, v); }
static int c_blendXOR(int* v) { return blendMode(25, v); }
static int c_blendDestOver(int* v) { return blendMode(21, v); }
static int c_blendPlusLighter(int* v) { return blendMode(27, v); }
static int c_blendClear(int* v) { return blendMode(16, v); }

static int c_setAlpha(int* v)
{
    newDefaultContext();
    CGContextSetAlpha(gContext, 0.5);
    CGContextSetRGBFillColor(gContext, 1, 0, 0, 1);
    CGContextFillRect(gContext, CGRectMake(0, 0, W, H));
    return sampleOne(W / 2, H / 2, v, 0);
}

static int c_transparencyLayerAlpha(int* v)
{
    newDefaultContext();
    CGContextSetAlpha(gContext, 0.5);
    CGContextBeginTransparencyLayer(gContext, NULL);
    CGContextSetRGBFillColor(gContext, 1, 0, 0, 1);
    CGContextFillRect(gContext, CGRectMake(0, 0, W, H / 2));
    CGContextSetRGBFillColor(gContext, 0, 0, 1, 1);
    CGContextFillRect(gContext, CGRectMake(0, H / 4, W, H / 2));
    CGContextEndTransparencyLayer(gContext);
    /* The layer composites once at 0.5, so the overlap must not double up. */
    return sampleOne(W / 2, H / 2 - 2, v, sampleOne(W / 2, 2, v, 0));
}

static int c_clipToRect(int* v)
{
    newDefaultContext();
    CGContextClipToRect(gContext, CGRectMake(4, 4, 8, 8));
    CGContextSetRGBFillColor(gContext, 1, 0, 0, 1);
    CGContextFillRect(gContext, CGRectMake(0, 0, W, H));
    return sampleOne(20, 20, v, sampleOne(8, 8, v, 0));
}

static int c_clipToRects(int* v)
{
    CGRect rects[2] = { { { 2, 2 }, { 6, 6 } }, { { 20, 20 }, { 6, 6 } } };
    newDefaultContext();
    CGContextClipToRects(gContext, rects, 2);
    CGContextSetRGBFillColor(gContext, 1, 0, 0, 1);
    CGContextFillRect(gContext, CGRectMake(0, 0, W, H));
    return sampleOne(14, 14, v, sampleOne(22, 22, v, sampleOne(4, 4, v, 0)));
}

/* A self-intersecting star: the centre is inside under the nonzero rule and
   outside under even-odd. */
static void addStar(CGContextRef c)
{
    CGContextBeginPath(c);
    CGContextMoveToPoint(c, 16, 30);
    CGContextAddLineToPoint(c, 6, 2);
    CGContextAddLineToPoint(c, 30, 20);
    CGContextAddLineToPoint(c, 2, 20);
    CGContextAddLineToPoint(c, 26, 2);
    CGContextClosePath(c);
}
static int c_fillRuleNonZero(int* v)
{
    newDefaultContext();
    CGContextSetRGBFillColor(gContext, 1, 0, 0, 1);
    addStar(gContext);
    CGContextFillPath(gContext);
    return sampleOne(16, 16, v, 0);
}
static int c_fillRuleEvenOdd(int* v)
{
    newDefaultContext();
    CGContextSetRGBFillColor(gContext, 1, 0, 0, 1);
    addStar(gContext);
    CGContextEOFillPath(gContext);
    return sampleOne(16, 16, v, 0);
}
static int c_eoClip(int* v)
{
    newDefaultContext();
    addStar(gContext);
    CGContextEOClip(gContext);
    CGContextSetRGBFillColor(gContext, 1, 0, 0, 1);
    CGContextFillRect(gContext, CGRectMake(0, 0, W, H));
    return sampleOne(16, 16, v, 0);
}

static int c_arcFill(int* v)
{
    newDefaultContext();
    CGContextSetRGBFillColor(gContext, 1, 0, 0, 1);
    CGContextBeginPath(gContext);
    CGContextAddArc(gContext, 16, 16, 10, 0, 6.2831853, 0);
    CGContextFillPath(gContext);
    return sampleOne(16, 28, v, sampleOne(16, 16, v, 0));
}

static int c_curveFill(int* v)
{
    newDefaultContext();
    CGContextSetRGBFillColor(gContext, 1, 0, 0, 1);
    CGContextBeginPath(gContext);
    CGContextMoveToPoint(gContext, 2, 4);
    CGContextAddCurveToPoint(gContext, 2, 28, 30, 28, 30, 4);
    CGContextClosePath(gContext);
    CGContextFillPath(gContext);
    return sampleOne(16, 24, v, sampleOne(16, 10, v, 0));
}

static int c_lineDash(int* v)
{
    CGFloat lengths[2] = { 4, 4 };
    newDefaultContext();
    CGContextSetRGBStrokeColor(gContext, 1, 0, 0, 1);
    CGContextSetLineWidth(gContext, 4);
    CGContextSetLineDash(gContext, 0, lengths, 2);
    CGContextBeginPath(gContext);
    CGContextMoveToPoint(gContext, 0, 16);
    CGContextAddLineToPoint(gContext, W, 16);
    CGContextStrokePath(gContext);
    /* on at x=2, off at x=6 */
    return sampleOne(6, 16, v, sampleOne(2, 16, v, 0));
}

static int c_lineCapJoin(int* v)
{
    newDefaultContext();
    CGContextSetRGBStrokeColor(gContext, 1, 0, 0, 1);
    CGContextSetLineWidth(gContext, 8);
    CGContextSetLineCap(gContext, kCGLineCapRound);
    CGContextSetLineJoin(gContext, kCGLineJoinRound);
    CGContextBeginPath(gContext);
    CGContextMoveToPoint(gContext, 8, 8);
    CGContextAddLineToPoint(gContext, 24, 8);
    CGContextAddLineToPoint(gContext, 24, 24);
    CGContextStrokePath(gContext);
    /* corner interior, and just past the round cap at the start */
    return sampleOne(5, 8, v, sampleOne(24, 8, v, 0));
}

static int c_replacePathWithStroked(int* v)
{
    newDefaultContext();
    CGContextSetLineWidth(gContext, 6);
    CGContextBeginPath(gContext);
    CGContextMoveToPoint(gContext, 4, 16);
    CGContextAddLineToPoint(gContext, 28, 16);
    CGContextReplacePathWithStrokedPath(gContext);
    CGContextSetRGBFillColor(gContext, 1, 0, 0, 1);
    CGContextFillPath(gContext);
    return sampleOne(16, 22, v, sampleOne(16, 16, v, 0));
}

static CGImageRef makeGrayRampMask(void)
{
    CGColorSpaceRef gray = CGColorSpaceCreateDeviceGray();
    CGContextRef c = CGBitmapContextCreate(NULL, W, H, 8, 0, gray, (uint32_t)kCGImageAlphaNone);
    CGImageRef m;
    int x;
    for (x = 0; x < W; ++x) {
        CGFloat g = (CGFloat)x / (W - 1);
        CGContextSetGrayFillColor(c, g, 1);
        CGContextFillRect(c, CGRectMake(x, 0, 1, H));
    }
    m = CGBitmapContextCreateImage(c);
    CGContextRelease(c);
    CGColorSpaceRelease(gray);
    return m;
}

static int c_clipToMask(int* v)
{
    CGImageRef mask = makeGrayRampMask();
    newDefaultContext();
    CGContextClipToMask(gContext, CGRectMake(0, 0, W, H), mask);
    CGContextSetRGBFillColor(gContext, 1, 0, 0, 1);
    CGContextFillRect(gContext, CGRectMake(0, 0, W, H));
    CGImageRelease(mask);
    /* mask is 0 at the left edge and 1 at the right */
    return sampleOne(29, 16, v, sampleOne(16, 16, v, sampleOne(2, 16, v, 0)));
}

static int c_clipToMaskVaryingColor(int* v)
{
    /* The case that made the gradient path abandon ClipToMask: a source whose
       colour varies across the mask. */
    CGImageRef mask = makeGrayRampMask();
    int x;
    newDefaultContext();
    CGContextClipToMask(gContext, CGRectMake(0, 0, W, H), mask);
    for (x = 0; x < W; ++x) {
        CGFloat t = (CGFloat)x / (W - 1);
        CGContextSetRGBFillColor(gContext, 1 - t, 0, 0, 1);
        CGContextFillRect(gContext, CGRectMake(x, 0, 1, H));
    }
    CGImageRelease(mask);
    return sampleOne(24, 16, v, sampleOne(16, 16, v, sampleOne(8, 16, v, 0)));
}

static int drawImageWithQuality(CGInterpolationQuality q, int* v)
{
    CGImageRef image = makeImage(4, 4);
    newDefaultContext();
    CGContextSetInterpolationQuality(gContext, q);
    CGContextDrawImage(gContext, CGRectMake(0, 0, W, H), image);
    CGImageRelease(image);
    /* Near the seam, where interpolation either blends or does not. */
    return sampleOne(20, 16, v, sampleOne(16, 16, v, sampleOne(4, 16, v, 0)));
}
static int c_imageInterpNone(int* v) { return drawImageWithQuality(kCGInterpolationNone, v); }
static int c_imageInterpHigh(int* v) { return drawImageWithQuality(kCGInterpolationHigh, v); }

static int c_imageSubrect(int* v)
{
    CGImageRef image = makeImage(16, 16);
    CGImageRef sub = CGImageCreateWithImageInRect(image, CGRectMake(8, 0, 8, 16));
    newDefaultContext();
    CGContextDrawImage(gContext, CGRectMake(0, 0, W, H), sub);
    CGImageRelease(sub);
    CGImageRelease(image);
    /* the right half of the source was half-alpha blue */
    return sampleOne(16, 16, v, 0);
}

static int c_imageMaskingColors(int* v)
{
    CGImageRef image = makeImage(16, 16);
    CGFloat range[6] = { 200, 255, 0, 60, 0, 60 }; /* knock out the red */
    CGImageRef masked = CGImageCreateWithMaskingColors(image, range);
    newDefaultContext();
    if (masked)
        CGContextDrawImage(gContext, CGRectMake(0, 0, W, H), masked);
    if (masked)
        CGImageRelease(masked);
    CGImageRelease(image);
    return sampleOne(24, 16, v, sampleOne(8, 16, v, 0));
}

static int c_imageWithMask(int* v)
{
    CGImageRef image = makeImage(16, 16);
    CGImageRef mask = makeGrayRampMask();
    CGImageRef masked = CGImageCreateWithMask(image, mask);
    newDefaultContext();
    if (masked)
        CGContextDrawImage(gContext, CGRectMake(0, 0, W, H), masked);
    if (masked)
        CGImageRelease(masked);
    CGImageRelease(mask);
    CGImageRelease(image);
    return sampleOne(24, 16, v, sampleOne(4, 16, v, 0));
}

static int c_bitmapAlphaLast(int* v)
{
    /* The other byte layout WebCore uses. Sample through the same accessor, so
       a byte-order divergence shows up as swapped channels. */
    int n;
    newContext((uint32_t)kCGImageAlphaPremultipliedLast | (uint32_t)kCGBitmapByteOrder32Big, NULL);
    CGContextSetRGBFillColor(gContext, 1, 0.5, 0, 1);
    CGContextFillRect(gContext, CGRectMake(0, 0, W, H));
    n = sampleOne(16, 16, v, 0);
    newDefaultContext();
    return n;
}

static int c_shadow(int* v)
{
    CGColorRef black = CGColorCreateGenericGray(0, 1);
    newDefaultContext();
    CGContextSetShadowWithColor(gContext, CGSizeMake(4, -4), 2, black);
    CGContextSetRGBFillColor(gContext, 1, 0, 0, 1);
    CGContextFillRect(gContext, CGRectMake(4, 12, 12, 12));
    CGColorRelease(black);
    /* inside the rect, and where the offset shadow should land */
    return sampleOne(19, 9, v, sampleOne(10, 18, v, 0));
}

static int c_layer(int* v)
{
    CGLayerRef layer;
    CGContextRef lc;
    newDefaultContext();
    layer = CGLayerCreateWithContext(gContext, CGSizeMake(8, 8), NULL);
    if (!layer)
        return sampleOne(0, 0, v, 0);
    lc = CGLayerGetContext(layer);
    CGContextSetRGBFillColor(lc, 1, 0, 0, 1);
    CGContextFillRect(lc, CGRectMake(0, 0, 8, 8));
    CGContextDrawLayerInRect(gContext, CGRectMake(8, 8, 16, 16), layer);
    CGLayerRelease(layer);
    return sampleOne(2, 2, v, sampleOne(16, 16, v, 0));
}

static void patternDraw(void* info, CGContextRef c)
{
    (void)info;
    CGContextSetRGBFillColor(c, 1, 0, 0, 1);
    CGContextFillRect(c, CGRectMake(0, 0, 4, 4));
}
static int c_pattern(int* v)
{
    static const CGPatternCallbacks callbacks = { 0, patternDraw, NULL };
    CGColorSpaceRef patternSpace;
    CGPatternRef pattern;
    CGFloat alpha = 1;
    newDefaultContext();
    pattern = CGPatternCreate(NULL, CGRectMake(0, 0, 8, 8), CGAffineTransformIdentity, 8, 8,
        kCGPatternTilingNoDistortion, true, &callbacks);
    if (!pattern)
        return sampleOne(0, 0, v, 0);
    patternSpace = CGColorSpaceCreatePattern(NULL);
    CGContextSetFillColorSpace(gContext, patternSpace);
    CGContextSetFillPattern(gContext, pattern, &alpha);
    CGContextFillRect(gContext, CGRectMake(0, 0, W, H));
    CGColorSpaceRelease(patternSpace);
    CGPatternRelease(pattern);
    /* inside a tile cell, and in the gap between cells */
    return sampleOne(6, 6, v, sampleOne(2, 2, v, 0));
}

static int c_colorConversionOnDraw(int* v)
{
    /* Fill with a DeviceRGB colour into a context whose space is generic RGB:
       whatever conversion CG applies on draw shows up here. */
    CGColorSpaceRef generic = CGColorSpaceCreateWithName(kCGColorSpaceGenericRGB);
    newContext((uint32_t)kCGImageAlphaPremultipliedFirst | (uint32_t)kCGBitmapByteOrder32Little,
        generic ? generic : gDeviceRGB);
    CGContextSetRGBFillColor(gContext, 0, 1, 0, 1);
    CGContextFillRect(gContext, CGRectMake(0, 0, W, H));
    if (generic)
        CGColorSpaceRelease(generic);
    {
        int n = sampleOne(16, 16, v, 0);
        newDefaultContext();
        return n;
    }
}

static int c_shouldAntialias(int* v)
{
    /* A diagonal edge: antialiasing on gives a partial pixel, off gives 0 or 255. */
    int n;
    newDefaultContext();
    CGContextSetShouldAntialias(gContext, false);
    CGContextSetRGBFillColor(gContext, 1, 0, 0, 1);
    CGContextBeginPath(gContext);
    CGContextMoveToPoint(gContext, 0, 0);
    CGContextAddLineToPoint(gContext, W, H);
    CGContextAddLineToPoint(gContext, 0, H);
    CGContextClosePath(gContext);
    CGContextFillPath(gContext);
    n = sampleOne(16, 16, v, 0);
    newDefaultContext();
    CGContextSetShouldAntialias(gContext, true);
    CGContextSetRGBFillColor(gContext, 1, 0, 0, 1);
    CGContextBeginPath(gContext);
    CGContextMoveToPoint(gContext, 0, 0);
    CGContextAddLineToPoint(gContext, W, H);
    CGContextAddLineToPoint(gContext, 0, H);
    CGContextClosePath(gContext);
    CGContextFillPath(gContext);
    return sampleOne(16, 16, v, n);
}

/* ------------------------------------------------------------------ driver */

typedef int (*CheckFn)(int*);
static const struct { const char* name; CheckFn fn; } kChecks[] = {
    { "blendNormal", c_blendNormal },
    { "blendMultiply", c_blendMultiply },
    { "blendScreen", c_blendScreen },
    { "blendCopy", c_blendCopy },
    { "blendXOR", c_blendXOR },
    { "blendDestinationOver", c_blendDestOver },
    { "blendPlusLighter", c_blendPlusLighter },
    { "blendClear", c_blendClear },
    { "setAlpha", c_setAlpha },
    { "transparencyLayerAlpha", c_transparencyLayerAlpha },
    { "clipToRect", c_clipToRect },
    { "clipToRects", c_clipToRects },
    { "fillRuleNonZero", c_fillRuleNonZero },
    { "fillRuleEvenOdd", c_fillRuleEvenOdd },
    { "eoClip", c_eoClip },
    { "arcFill", c_arcFill },
    { "curveFill", c_curveFill },
    { "lineDash", c_lineDash },
    { "lineCapJoin", c_lineCapJoin },
    { "replacePathWithStrokedPath", c_replacePathWithStroked },
    { "clipToMask", c_clipToMask },
    { "clipToMaskVaryingColor", c_clipToMaskVaryingColor },
    { "imageInterpolationNone", c_imageInterpNone },
    { "imageInterpolationHigh", c_imageInterpHigh },
    { "imageSubrect", c_imageSubrect },
    { "imageMaskingColors", c_imageMaskingColors },
    { "imageWithMask", c_imageWithMask },
    { "bitmapAlphaLastBigEndian", c_bitmapAlphaLast },
    { "shadow", c_shadow },
    { "layerDrawInRect", c_layer },
    { "pattern", c_pattern },
    { "colorConversionOnDraw", c_colorConversionOnDraw },
    { "shouldAntialias", c_shouldAntialias },
};
#define CHECK_COUNT ((int)(sizeof(kChecks) / sizeof(kChecks[0])))

#ifndef EMIT_ONLY
#include "cgprobe-expected.h"
#endif

int main(int argc, char** argv)
{
    int emit = argc > 1 && !strcmp(argv[1], "--emit");
    int i, j, n, diverged = 0;
    int values[MAX_VALUES];

    gPixels = (unsigned char*)calloc((size_t)W * H, 4);
    gDeviceRGB = CGColorSpaceCreateDeviceRGB();

    if (emit)
        printf("/* Generated by cgprobe --emit against modern CoreGraphics. Do not edit. */\n"
               "static const struct { const char* name; int count; int values[%d]; }\n"
               "kExpected[] = {\n", MAX_VALUES);

    for (i = 0; i < CHECK_COUNT; ++i) {
        memset(values, 0, sizeof(values));
        n = kChecks[i].fn(values);
        if (emit) {
            printf("    { \"%s\", %d, {", kChecks[i].name, n);
            for (j = 0; j < MAX_VALUES; ++j)
                printf("%s%d", j ? ", " : " ", values[j]);
            printf(" } },\n");
            continue;
        }
#ifndef EMIT_ONLY
        {
            int bad = 0;
            if (strcmp(kExpected[i].name, kChecks[i].name) || kExpected[i].count != n)
                bad = 1;
            else {
                for (j = 0; j < n; ++j) {
                    int d = values[j] - kExpected[i].values[j];
                    if (d > TOLERANCE || d < -TOLERANCE) { bad = 1; break; }
                }
            }
            printf("%-28s %s", kChecks[i].name, bad ? "DIVERGES" : "matches ");
            if (bad) {
                printf("\n   tiger :");
                for (j = 0; j < n; ++j) printf(" %3d", values[j]);
                printf("\n   modern:");
                for (j = 0; j < kExpected[i].count; ++j) printf(" %3d", kExpected[i].values[j]);
                ++diverged;
            }
            printf("\n");
        }
#endif
    }
    if (emit)
        printf("};\n");
    else
        printf("\n%d of %d checks diverge from modern CoreGraphics\n", diverged, CHECK_COUNT);
    return 0;
}
