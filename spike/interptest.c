/* interptest.c -- are Tiger's middle interpolation qualities distinct, or aliased?
 *
 * cgcompat's cgprobe already established that None and High differ on a 4x4
 * upscale, so the feature is not simply dead. The narrower question, and the one
 * that would actually bite, is whether Low and Medium are distinct settings or
 * silently collapse onto Default or onto each other. WebCore picks among them
 * deliberately: NativeImageCG pairs interpolation quality with the blend-mode
 * Copy that Tiger also ignores.
 *
 * Medium is 10.6 API. The enum value is passed as an integer regardless, which
 * is the point: the question is what Tiger does with a value it may not know.
 *
 * Measured on 10.4.11: interpolation is BINARY. Only None is distinct. Default,
 * Low, Medium and High all render byte-identically, and all five values are
 * stored correctly by the setter and read back unchanged, so the rasterizer
 * collapses them rather than the setter rejecting them. Corroborating:
 * CGContextGetInterpolationQualityRange reports [0, 0]. That declaration is not
 * a guess -- the disassembly reads three arguments and calls
 * CGRenderingStateGet{Min,Max}InterpolationQuality.
 *
 * Method: upscale a 4x4 black-and-white checkerboard to 32x32 once per quality
 * and compare the rendered bytes pairwise. Also read the quality back after
 * setting it, which separates "stored but ignored" from "rejected at the setter".
 *
 * Build (from the repo root, in bash):
 *   toolchain/bin/tiger-clang -O1 -o build/interptest spike/interptest.c \
 *     -F compat/sdk-overlay \
 *     -F sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks \
 *     -ltigercompat -framework ApplicationServices
 */
#include <CoreGraphics/CoreGraphics.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Exported by Tiger, not declared in the 10.4u SDK. */
CG_EXTERN void CGContextGetInterpolationQualityRange(CGContextRef c, int *lo, int *hi);

#define DST 32
#define STRIDE (DST * 4)

static int failures;
static void expect(const char *what, int ok)
{
    printf("%-58s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok)
        ++failures;
}

static const struct { int q; const char *name; } kQual[] = {
    { 0, "Default" }, { 1, "None" }, { 2, "Low" }, { 3, "High" }, { 4, "Medium" },
};
#define NQ (sizeof kQual / sizeof *kQual)

static CGImageRef checker4x4(void)
{
    static unsigned char px[4 * 4];
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            px[y * 4 + x] = ((x + y) & 1) ? 255 : 0;
    CGDataProviderRef p = CGDataProviderCreateWithData(NULL, px, sizeof px, NULL);
    CGColorSpaceRef gray = CGColorSpaceCreateDeviceGray();
    CGImageRef img = CGImageCreate(4, 4, 8, 8, 4, gray, kCGImageAlphaNone, p, NULL,
                                   false, kCGRenderingIntentDefault);
    CGColorSpaceRelease(gray);
    CGDataProviderRelease(p);
    return img;
}

/* Render the upscale at `q`; returns the quality the context reports back. */
static int renderAt(int q, CGImageRef img, unsigned char out[DST * STRIDE], int *distinctLevels)
{
    CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
    memset(out, 0, DST * STRIDE);
    CGContextRef c = CGBitmapContextCreate(out, DST, DST, 8, STRIDE, rgb,
                                           kCGImageAlphaNoneSkipLast);
    CGColorSpaceRelease(rgb);
    if (!c)
        return -1;
    CGContextSetInterpolationQuality(c, (CGInterpolationQuality)q);
    int readBack = (int)CGContextGetInterpolationQuality(c);
    CGContextDrawImage(c, CGRectMake(0, 0, DST, DST), img);
    CGContextRelease(c);

    /* How many distinct grey levels appear: 2 means hard blocks (no blending),
       more means the scaler interpolated. */
    if (distinctLevels) {
        unsigned char seen[256];
        memset(seen, 0, sizeof seen);
        for (int i = 0; i < DST * DST; ++i)
            seen[out[i * 4]] = 1;
        int n = 0;
        for (int i = 0; i < 256; ++i) n += seen[i];
        *distinctLevels = n;
    }
    return readBack;
}

int main(void)
{
    static unsigned char buf[NQ][DST * STRIDE];
    int readBack[NQ], levels[NQ];
    setbuf(stdout, NULL);

    CGImageRef img = checker4x4();
    expect("the source image was created", img != NULL);
    if (!img)
        return 0;

    {
        int lo = -1, hi = -1;
        CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
        static unsigned char tmp[DST * STRIDE];
        CGContextRef c = CGBitmapContextCreate(tmp, DST, DST, 8, STRIDE, rgb,
                                               kCGImageAlphaNoneSkipLast);
        CGColorSpaceRelease(rgb);
        if (c) {
            CGContextGetInterpolationQualityRange(c, &lo, &hi);
            CGContextRelease(c);
        }
        printf("CGContextGetInterpolationQualityRange -> [%d, %d]\n\n", lo, hi);
    }

    for (unsigned i = 0; i < NQ; ++i) {
        readBack[i] = renderAt(kQual[i].q, img, buf[i], &levels[i]);
        printf("  %-8s set=%d readback=%-3d distinct grey levels=%d\n",
               kQual[i].name, kQual[i].q, readBack[i], levels[i]);
    }
    CGImageRelease(img);

    printf("\n-- which settings the context stores\n");
    for (unsigned i = 0; i < NQ; ++i)
        if (readBack[i] != kQual[i].q)
            printf("    %s (%d) reads back as %d: the setter did not take it\n",
                   kQual[i].name, kQual[i].q, readBack[i]);
    expect("every quality is stored as set",
           readBack[0] == 0 && readBack[1] == 1 && readBack[2] == 2 &&
           readBack[3] == 3 && readBack[4] == 4);

    printf("\n-- which settings actually render differently\n");
    for (unsigned i = 0; i < NQ; ++i)
        for (unsigned j = i + 1; j < NQ; ++j)
            if (!memcmp(buf[i], buf[j], DST * STRIDE))
                printf("    %-8s and %-8s render IDENTICALLY\n", kQual[i].name, kQual[j].name);

    /* The control cgcompat already has: None must differ from High. */
    expect("None and High differ (control)", memcmp(buf[1], buf[3], DST * STRIDE) != 0);
    expect("None produces hard blocks (2 grey levels)", levels[1] == 2);
    expect("High interpolates (more than 2 grey levels)", levels[3] > 2);

    /* The narrow question. */
    expect("Low is distinct from None", memcmp(buf[2], buf[1], DST * STRIDE) != 0);
    expect("Low is distinct from High", memcmp(buf[2], buf[3], DST * STRIDE) != 0);
    expect("Medium is distinct from Low", memcmp(buf[4], buf[2], DST * STRIDE) != 0);
    expect("Medium is distinct from High", memcmp(buf[4], buf[3], DST * STRIDE) != 0);

    printf("\n%s (%d of the expectations did not hold)\n",
           failures ? "DIFFERENCES FOUND" : "everything behaved as documented", failures);
    return 0;   /* a probe reports, it does not gate a build */
}
