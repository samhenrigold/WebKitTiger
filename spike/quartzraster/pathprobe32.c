/* TIGER quartzraster: what is CoreGraphics' scan converter actually computing?
 *
 * Black box. Fill axis-aligned rectangles with fractional edges into an offscreen
 * bitmap and read the coverage back. Exact-area rasterisation gives coverage that
 * is linear in the covered fraction; N-times supersampling gives a staircase with
 * N steps. The step count and the phase of the steps ARE the sampling grid.
 *
 * Build (i386, on the Mac): tiger-clang -O1 pathprobe32.c -o pathprobe32 \
 *   -framework ApplicationServices -framework CoreFoundation
 * Run on the box: ./pathprobe32
 */
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { W = 64, H = 64 };
static unsigned char* g_px;
static CGContextRef g_ctx;

static void newCtx(void)
{
    CGColorSpaceRef sp = CGColorSpaceCreateDeviceRGB();
    if (g_ctx) CGContextRelease(g_ctx);
    if (!g_px) g_px = (unsigned char*)malloc(W * H * 4);
    memset(g_px, 0xff, W * H * 4);
    g_ctx = CGBitmapContextCreate(g_px, W, H, 8, W * 4, sp, kCGImageAlphaNoneSkipLast);
    CGColorSpaceRelease(sp);
    CGContextSetShouldAntialias(g_ctx, true);
    CGContextSetRGBFillColor(g_ctx, 0, 0, 0, 1);
}

/* coverage of device pixel (col, row-from-top), 0..1 */
static double cov(int col, int rowTop)
{
    return (255.0 - g_px[((size_t)rowTop * W + col) * 4]) / 255.0;
}

static void fillRect(double x, double yTop, double w, double h)
{
    /* yTop is in top-down device space; CG's user space here is bottom-up */
    CGContextFillRect(g_ctx, CGRectMake(x, H - yTop - h, w, h));
}

static void hdr(const char* s) { printf("\n## %s\n", s); }

int main(void)
{
    int k, i;

    /* 1. horizontal: a rect whose right edge sweeps across one pixel.
     *    coverage of that pixel vs. the exact covered fraction. */
    hdr("h-width sweep: rect x=[20, 20+w), rows 20..27 (whole pixels); pixel 20");
    printf("# w      exact   cov      cov*255\n");
    for (k = 0; k <= 64; ++k) {
        double w = k / 64.0;
        newCtx(); fillRect(20.0, 20.0, w, 8.0);
        printf("%6.4f  %6.4f  %6.4f  %7.2f\n", w, w, cov(20, 23), cov(20, 23) * 255.0);
    }

    /* 2. horizontal position: a 1px-wide rect sliding across; both pixels. */
    hdr("h-position sweep: rect x=[20+d, 21+d), w=1; pixels 20 and 21");
    printf("# d      exact20 cov20   exact21 cov21   sum\n");
    for (k = 0; k <= 64; ++k) {
        double d = k / 64.0;
        newCtx(); fillRect(20.0 + d, 20.0, 1.0, 8.0);
        printf("%6.4f  %6.4f  %6.4f  %6.4f  %6.4f  %6.4f\n",
            d, 1.0 - d, cov(20, 23), d, cov(21, 23), cov(20, 23) + cov(21, 23));
    }

    /* 3. vertical: same, on the other axis. */
    hdr("v-height sweep: rect y=[20, 20+h) top-down, cols 20..27; row 20");
    printf("# h      exact   cov      cov*255\n");
    for (k = 0; k <= 64; ++k) {
        double h = k / 64.0;
        newCtx(); fillRect(20.0, 20.0, 8.0, h);
        printf("%6.4f  %6.4f  %6.4f  %7.2f\n", h, h, cov(23, 20), cov(23, 20) * 255.0);
    }

    hdr("v-position sweep: rect y=[20+d, 21+d), h=1; rows 20 and 21");
    printf("# d      exact20 cov20   exact21 cov21   sum\n");
    for (k = 0; k <= 64; ++k) {
        double d = k / 64.0;
        newCtx(); fillRect(20.0, 20.0 + d, 8.0, 1.0);
        printf("%6.4f  %6.4f  %6.4f  %6.4f  %6.4f  %6.4f\n",
            d, 1.0 - d, cov(20, 20), d, cov(20, 21), cov(20, 20) + cov(20, 21));
    }

    /* 4. a 2-D fractional corner: does a w*h fractional square give w*h? */
    hdr("corner: rect w=h=t at a pixel origin; coverage should be t*t if area-exact");
    printf("# t      t*t     cov\n");
    for (k = 0; k <= 16; ++k) {
        double t = k / 16.0;
        newCtx(); fillRect(20.0, 21.0 - t, t, t);
        printf("%6.4f  %6.4f  %6.4f\n", t, t * t, cov(20, 20));
    }

    /* 5. a shallow wedge: one triangle, read a whole row of edge pixels. The
     *    sequence of coverages along a slanted edge exposes the vertical sample
     *    count directly (N-row supersampling can only produce multiples of 1/N
     *    per sub-row, and the sequence repeats with period N). */
    hdr("wedge: triangle (10,40)-(50,40)-(50,20) top-down, edge row coverages");
    newCtx();
    CGContextBeginPath(g_ctx);
    CGContextMoveToPoint(g_ctx, 10, H - 40);
    CGContextAddLineToPoint(g_ctx, 50, H - 40);
    CGContextAddLineToPoint(g_ctx, 50, H - 20);
    CGContextClosePath(g_ctx);
    CGContextFillPath(g_ctx);
    for (i = 20; i < 41; ++i) {
        int j;
        printf("row %2d:", i);
        for (j = 10; j < 51; ++j) {
            double c = cov(j, i);
            if (c > 0.001 && c < 0.999) printf(" %d:%.4f", j, c);
        }
        printf("\n");
    }

    /* 6. a nearly-vertical edge, to read the horizontal sample count per sub-row */
    hdr("thin sliver: rect x=[20, 20+w), h=1 row at a time, w = k/96");
    printf("# w      cov\n");
    for (k = 0; k <= 96; ++k) {
        double w = k / 96.0;
        newCtx(); fillRect(20.0, 20.0, w, 1.0);
        printf("%8.5f %8.5f %7.2f\n", w, cov(20, 20), cov(20, 20) * 255.0);
    }
    return 0;
}
