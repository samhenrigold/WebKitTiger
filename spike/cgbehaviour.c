/* cgbehaviour.c -- the rest of the CoreGraphics behavioural list, measured.
 *
 * Deliberately builds and runs on BOTH Tiger and the host Mac, printing the same
 * KEY=value lines, so modern CoreGraphics is the reference and the comparison is
 * a diff rather than a judgement. Everything here has a matching signature and
 * reads its arguments, which is why the static ABI screen cannot reach any of it.
 *
 * Tiger:
 *   toolchain/bin/tiger-clang -O1 -o build/cgbehaviour spike/cgbehaviour.c \
 *     -F compat/sdk-overlay \
 *     -F sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks \
 *     -ltigercompat -framework ApplicationServices
 * Host:
 *   clang -O1 -o build/cgbehaviour-host spike/cgbehaviour.c \
 *     -framework CoreGraphics -framework ApplicationServices
 *
 * See spike/run-cgbehaviour.sh, which builds both, runs both and diffs them.
 *
 * Measured 10.4.11 against macOS 26 on this Mac:
 *
 *   MATCHES MODERN, no action needed
 *     - CGPatternCreate and CGPatternCreateWithImage2 honour the tiling argument.
 *       NoDistortion differs from the two constant-spacing modes on BOTH ends, and
 *       the two spacing modes agree with each other on both, so the argument is
 *       not being dropped. kCGPatternTilingConstantSpacing is what WebCore passes.
 *     - Transparency layers group correctly under a non-identity CTM: scaled,
 *       rotated and composed all show the same layer-versus-no-layer drop as
 *       modern, within 0.05%.
 *     - CGContextSetLineDash honours the phase, and a null pattern clears it.
 *     - CGContextClipToRects, CGImageCreateWithMaskingColors: identical.
 *
 *   DIFFERS
 *     - Shadows carry a stable ~91.7% of modern's total ink at EVERY radius
 *       (0.905 to 0.949 across blur 0..32), but the blur SATURATES above radius
 *       ~8: at blur 32 Tiger covers 54% of the area at 3.3x the peak alpha. So a
 *       uniform alpha correction would fix small-blur intensity and make large
 *       blurs worse, since those are already too dark and too tight.
 *     - cgcompat's CGContextDrawTiledImage shim SEAMS. It covers exactly the
 *       right pixels, but a fractional tile origin loses ~6% alpha to seams
 *       between tiles, where the real function stays fully opaque. Aligning the
 *       origin to the integer lattice gives exactly inked*255, proving the cause.
 *       GraphicsContextCG.cpp:517 passes a FloatRect from layout, so this fires.
 */
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* SPI on both ends: exported by CoreGraphics but not declared in the public
   headers of either SDK. WebCore declares it the same way in its CG SPI header. */
CG_EXTERN CGPatternRef CGPatternCreateWithImage2(CGImageRef image, CGAffineTransform transform,
                                                 CGPatternTiling tiling);

#define DIM 64
#define STRIDE (DIM * 4)

/* A scratch RGBA canvas, premultiplied, cleared to transparent. */
typedef struct { CGContextRef c; unsigned char px[DIM * STRIDE]; } Canvas;

static int canvasInit(Canvas *v)
{
    CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
    memset(v->px, 0, sizeof v->px);
    v->c = CGBitmapContextCreate(v->px, DIM, DIM, 8, STRIDE, rgb, kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(rgb);
    return v->c != NULL;
}
static void canvasFree(Canvas *v) { if (v->c) CGContextRelease(v->c); }

static unsigned inked(const Canvas *v)
{
    unsigned n = 0;
    for (int i = 0; i < DIM * DIM; ++i)
        if (v->px[i * 4 + 3])
            ++n;
    return n;
}
static unsigned alphaSum(const Canvas *v)
{
    unsigned n = 0;
    for (int i = 0; i < DIM * DIM; ++i)
        n += v->px[i * 4 + 3];
    return n;
}
/* x and y are CG USER coordinates, origin bottom-left. The bitmap rows run the
   other way, so the row index is flipped. Getting this wrong made every point
   sample read a pixel that happened to be empty on both platforms, which looks
   like agreement rather than like a bug. */
static unsigned alphaAt(const Canvas *v, int x, int y)
{
    if (x < 0 || y < 0 || x >= DIM || y >= DIM)
        return 0;
    return v->px[(DIM - 1 - y) * STRIDE + x * 4 + 3];
}
/* A cheap content fingerprint, so two renderings can be compared across machines
   without shipping pixels around. */
static unsigned long digest(const Canvas *v)
{
    unsigned long h = 5381;
    for (int i = 0; i < DIM * STRIDE; ++i)
        h = ((h << 5) + h) ^ v->px[i];
    return h & 0xffffffffUL;
}

static CGImageRef solidImage(int w, int h, unsigned char r, unsigned char g,
                             unsigned char b, unsigned char a)
{
    size_t n = (size_t)w * h * 4;
    unsigned char *px = malloc(n);
    for (size_t i = 0; i < n; i += 4) { px[i] = r; px[i+1] = g; px[i+2] = b; px[i+3] = a; }
    CGDataProviderRef p = CGDataProviderCreateWithData(NULL, px, n, NULL);
    CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
    CGImageRef img = CGImageCreate(w, h, 8, 32, (size_t)w * 4, rgb,
                                   kCGImageAlphaPremultipliedLast, p, NULL, false,
                                   kCGRenderingIntentDefault);
    CGColorSpaceRelease(rgb);
    CGDataProviderRelease(p);
    return img;   /* px intentionally leaked; the process is about to exit */
}

/* ------------------------------------------------------------------ patterns */

static void drawPatternCallback(void *info, CGContextRef c)
{
    (void)info;
    CGContextSetRGBFillColor(c, 1, 0, 0, 1);
    CGContextFillRect(c, CGRectMake(0, 0, 4, 4));      /* ink only a quarter of an 8x8 cell */
}

static void probePatterns(void)
{
    static const CGPatternCallbacks cb = { 0, drawPatternCallback, NULL };
    /* 0 = NoDistortion, 1 = ConstantSpacingMinimalDistortion, 2 = ConstantSpacing */
    for (int tiling = 0; tiling <= 2; ++tiling) {
        Canvas v;
        if (!canvasInit(&v)) continue;
        CGColorSpaceRef pat = CGColorSpaceCreatePattern(NULL);
        CGContextSetFillColorSpace(v.c, pat);
        CGColorSpaceRelease(pat);
        /* A non-integral cell width is what makes the tiling modes differ: the
           spacing modes must choose between distorting the cell and spacing it. */
        CGPatternRef p = CGPatternCreate(NULL, CGRectMake(0, 0, 8, 8),
                                         CGAffineTransformIdentity, 8.3f, 8.3f,
                                         (CGPatternTiling)tiling, true, &cb);
        CGFloat alpha = 1;
        if (p) {
            CGContextSetFillPattern(v.c, p, &alpha);
            CGContextFillRect(v.c, CGRectMake(0, 0, DIM, DIM));
            CGPatternRelease(p);
        }
        printf("pattern.tiling%d.created=%d\n", tiling, p != NULL);
        printf("pattern.tiling%d.inked=%u\n", tiling, inked(&v));
        printf("pattern.tiling%d.digest=%lu\n", tiling, digest(&v));
        canvasFree(&v);
    }

    /* The call WebCore actually makes, via PatternCG.cpp. */
    CGImageRef tile = solidImage(8, 8, 0, 0, 255, 255);
    for (int tiling = 0; tiling <= 2; ++tiling) {
        Canvas v;
        if (!canvasInit(&v)) continue;
        CGColorSpaceRef pat = CGColorSpaceCreatePattern(NULL);
        CGContextSetFillColorSpace(v.c, pat);
        CGColorSpaceRelease(pat);
        CGPatternRef p = CGPatternCreateWithImage2(tile, CGAffineTransformMakeScale(1.04f, 1.04f),
                                                   (CGPatternTiling)tiling);
        CGFloat alpha = 1;
        if (p) {
            CGContextSetFillPattern(v.c, p, &alpha);
            CGContextFillRect(v.c, CGRectMake(0, 0, DIM, DIM));
            CGPatternRelease(p);
        }
        printf("image2.tiling%d.created=%d\n", tiling, p != NULL);
        printf("image2.tiling%d.inked=%u\n", tiling, inked(&v));
        printf("image2.tiling%d.digest=%lu\n", tiling, digest(&v));
        canvasFree(&v);
    }
    CGImageRelease(tile);
}

/* -------------------------------------------------- transparency layers ---- */

/* The grouping only shows up against a GLOBAL alpha. Two opaque rects drawn
   under CGContextSetAlpha(0.5) without a layer each get 0.5 applied separately,
   so the overlap double-composites to 0.75. Inside a transparency layer they
   composite opaque against the layer's transparent backdrop and the 0.5 is
   applied once to the finished group, so the overlap stays 0.5 and alphasum
   drops. Setting a per-fill alpha instead exercises nothing -- the host run
   showed layer and no-layer identical, which is what caught the first version
   of this test. The question is whether the grouping still holds when the CTM
   is not the identity. */
static void layerCase(const char *label, int useLayer, CGAffineTransform ctm)
{
    Canvas v;
    if (!canvasInit(&v)) return;
    CGContextConcatCTM(v.c, ctm);
    CGContextSetAlpha(v.c, 0.5f);
    if (useLayer)
        CGContextBeginTransparencyLayer(v.c, NULL);
    CGContextSetRGBFillColor(v.c, 1, 0, 0, 1);
    CGContextFillRect(v.c, CGRectMake(4, 4, 16, 16));
    CGContextFillRect(v.c, CGRectMake(12, 12, 16, 16));
    if (useLayer)
        CGContextEndTransparencyLayer(v.c);
    printf("layer.%s.inked=%u\n", label, inked(&v));
    printf("layer.%s.alphasum=%u\n", label, alphaSum(&v));
    printf("layer.%s.digest=%lu\n", label, digest(&v));
    canvasFree(&v);
}

static void probeLayers(void)
{
    layerCase("identity.nolayer", 0, CGAffineTransformIdentity);
    layerCase("identity.layer",   1, CGAffineTransformIdentity);
    layerCase("scaled.nolayer",   0, CGAffineTransformMakeScale(1.7f, 1.3f));
    layerCase("scaled.layer",     1, CGAffineTransformMakeScale(1.7f, 1.3f));
    layerCase("rotated.nolayer",  0, CGAffineTransformMakeRotation(0.3f));
    layerCase("rotated.layer",    1, CGAffineTransformMakeRotation(0.3f));
    {
        CGAffineTransform t = CGAffineTransformMakeTranslation(6, 9);
        t = CGAffineTransformScale(t, 1.5f, 0.8f);
        layerCase("composed.nolayer", 0, t);
        layerCase("composed.layer",   1, t);
    }
}

/* ------------------------------------------------------------ line dashes -- */

static void probeDashes(void)
{
    static const CGFloat pattern[] = { 6, 4 };
    const CGFloat phases[] = { 0, 2, 5, 10 };
    for (unsigned i = 0; i < sizeof phases / sizeof *phases; ++i) {
        Canvas v;
        if (!canvasInit(&v)) continue;
        CGContextSetShouldAntialias(v.c, false);
        CGContextSetLineWidth(v.c, 2);
        CGContextSetRGBStrokeColor(v.c, 0, 0, 0, 1);
        CGContextSetLineDash(v.c, phases[i], pattern, 2);
        CGContextMoveToPoint(v.c, 0, 32);
        CGContextAddLineToPoint(v.c, DIM, 32);
        CGContextStrokePath(v.c);
        printf("dash.phase%d.inked=%u\n", (int)phases[i], inked(&v));
        printf("dash.phase%d.digest=%lu\n", (int)phases[i], digest(&v));
        canvasFree(&v);
    }
    /* A zero-length pattern must clear the dash and stroke solid. */
    {
        Canvas v;
        if (canvasInit(&v)) {
            CGContextSetShouldAntialias(v.c, false);
            CGContextSetLineWidth(v.c, 2);
            CGContextSetRGBStrokeColor(v.c, 0, 0, 0, 1);
            CGContextSetLineDash(v.c, 0, NULL, 0);
            CGContextMoveToPoint(v.c, 0, 32);
            CGContextAddLineToPoint(v.c, DIM, 32);
            CGContextStrokePath(v.c);
            printf("dash.cleared.inked=%u\n", inked(&v));
            canvasFree(&v);
        }
    }
}

/* ----------------------------------------------------------- clip to rects - */

static void probeClipToRects(void)
{
    Canvas v;
    if (!canvasInit(&v)) return;
    CGRect rects[2] = { CGRectMake(4, 4, 8, 8), CGRectMake(40, 40, 8, 8) };
    CGContextClipToRects(v.c, rects, 2);
    CGContextSetRGBFillColor(v.c, 0, 1, 0, 1);
    CGContextFillRect(v.c, CGRectMake(0, 0, DIM, DIM));
    printf("cliprects.inked=%u\n", inked(&v));          /* expect 128 = 2 * 8 * 8 */
    printf("cliprects.inside1=%u\n", alphaAt(&v, 6, 6));
    printf("cliprects.inside2=%u\n", alphaAt(&v, 44, 44));
    printf("cliprects.between=%u\n", alphaAt(&v, 24, 24));
    canvasFree(&v);
}

/* ------------------------------------------------------ masking colours ---- */

static void probeMaskingColors(void)
{
    /* A two-colour image: pure red and pure blue, half each. */
    unsigned char *px = malloc(16 * 16 * 4);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            unsigned char *p = px + (y * 16 + x) * 4;
            int red = x < 8;
            p[0] = red ? 255 : 0; p[1] = 0; p[2] = red ? 0 : 255; p[3] = 255;
        }
    CGDataProviderRef prov = CGDataProviderCreateWithData(NULL, px, 16 * 16 * 4, NULL);
    CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
    CGImageRef img = CGImageCreate(16, 16, 8, 32, 64, rgb, kCGImageAlphaNoneSkipLast,
                                   prov, NULL, false, kCGRenderingIntentDefault);
    CGColorSpaceRelease(rgb);
    CGDataProviderRelease(prov);

    /* Mask out red: components are min,max per channel in source sample values. */
    const CGFloat range[6] = { 200, 255, 0, 40, 0, 40 };
    CGImageRef masked = img ? CGImageCreateWithMaskingColors(img, range) : NULL;
    printf("maskcolor.created=%d\n", masked != NULL);
    Canvas v;
    if (canvasInit(&v)) {
        if (masked)
            CGContextDrawImage(v.c, CGRectMake(0, 0, 16, 16), masked);
        printf("maskcolor.inked=%u\n", inked(&v));           /* expect ~128, the blue half */
        printf("maskcolor.redhalf=%u\n", alphaAt(&v, 4, 8)); /* expect 0 if masked */
        printf("maskcolor.bluehalf=%u\n", alphaAt(&v, 12, 8));
        canvasFree(&v);
    }
    if (masked) CGImageRelease(masked);
    if (img) CGImageRelease(img);
}

/* ------------------------------------------------------------- shadows ----- */

static void probeShadows(void)
{
    const CGFloat blurs[] = { 0, 1, 2, 4, 6, 8, 10, 12, 16, 24, 32 };
    for (unsigned i = 0; i < sizeof blurs / sizeof *blurs; ++i) {
        Canvas v;
        if (!canvasInit(&v)) continue;
        CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
        CGFloat comps[4] = { 0, 0, 0, 1 };
        CGColorRef black = CGColorCreate(rgb, comps);
        CGColorSpaceRelease(rgb);
        CGContextSetShadowWithColor(v.c, CGSizeMake(8, -8), blurs[i], black);
        CGColorRelease(black);
        CGContextSetRGBFillColor(v.c, 1, 0, 0, 1);
        CGContextFillRect(v.c, CGRectMake(8, 32, 16, 16));
        /* Total shadow ink, and the peak well inside the shadow's offset copy. */
        /* Sum only what is outside the filled rect, in user coordinates, so the
           rect's own 256 opaque pixels do not swamp the shadow measurement. */
        unsigned shadowInk = 0, peak = 0, shadowPixels = 0;
        for (int y = 0; y < DIM; ++y)
            for (int x = 0; x < DIM; ++x) {
                if (x >= 8 && x < 24 && y >= 32 && y < 48) continue;
                unsigned a = alphaAt(&v, x, y);
                shadowInk += a;
                if (a) ++shadowPixels;
                if (a > peak) peak = a;
            }
        printf("shadow.blur%d.ink=%u\n", (int)blurs[i], shadowInk);
        printf("shadow.blur%d.peak=%u\n", (int)blurs[i], peak);
        printf("shadow.blur%d.pixels=%u\n", (int)blurs[i], shadowPixels);
        printf("shadow.blur%d.inked=%u\n", (int)blurs[i], inked(&v));
        canvasFree(&v);
    }
    /* Shadow off again must leave no trace. */
    {
        Canvas v;
        if (canvasInit(&v)) {
            CGContextSetShadowWithColor(v.c, CGSizeMake(8, -8), 4, NULL);
            CGContextSetRGBFillColor(v.c, 1, 0, 0, 1);
            CGContextFillRect(v.c, CGRectMake(8, 32, 16, 16));
            printf("shadow.off.inked=%u\n", inked(&v));      /* expect exactly 256 */
            canvasFree(&v);
        }
    }
}

/* On Tiger this is cgcompat's draw-loop shim, since Tiger does not export the
   function at all; on the host it is the real one. Comparing them is the point. */
static void probeTiledImage(void)
{
    CGImageRef tile = solidImage(8, 8, 0, 255, 0, 255);
    const CGRect clips[3] = {
        CGRectMake(0, 0, DIM, DIM),           /* whole canvas */
        CGRectMake(10, 10, 20, 20),           /* a window not on the tile lattice */
        CGRectMake(3, 27, 41, 11),            /* deliberately awkward */
    };
    for (int i = 0; i < 3; ++i) {
        Canvas v;
        if (!canvasInit(&v)) continue;
        CGContextClipToRect(v.c, clips[i]);
        /* A tile origin off the lattice, so snapping is exercised. */
        CGContextDrawTiledImage(v.c, CGRectMake(2.5f, 1.5f, 8, 8), tile);
        printf("tiled.clip%d.inked=%u\n", i, inked(&v));
        printf("tiled.clip%d.alphasum=%u\n", i, alphaSum(&v));
        canvasFree(&v);
    }
    /* Is any alpha shortfall caused by fractional tile origins seaming against
       each other, or is it inherent? Repeat clip 0 with the tile origin on the
       integer lattice. Full opaque coverage must give alphasum = inked * 255. */
    {
        Canvas v;
        if (canvasInit(&v)) {
            CGContextClipToRect(v.c, CGRectMake(0, 0, DIM, DIM));
            CGContextDrawTiledImage(v.c, CGRectMake(0, 0, 8, 8), tile);
            printf("tiled.aligned.inked=%u\n", inked(&v));
            printf("tiled.aligned.alphasum=%u\n", alphaSum(&v));
            printf("tiled.aligned.opaque=%d\n", alphaSum(&v) == inked(&v) * 255u);
            canvasFree(&v);
        }
    }
    CGImageRelease(tile);
}

int main(void)
{
    setbuf(stdout, NULL);
#if defined(__i386__)
    printf("platform=tiger\n");
#else
    printf("platform=host\n");
#endif
    probePatterns();
    probeLayers();
    probeDashes();
    probeClipToRects();
    probeMaskingColors();
    probeShadows();
    probeTiledImage();
    return 0;
}
