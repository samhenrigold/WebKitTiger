/* Leopard 10.5.0 x86_64 CoreFoundation + CoreGraphics + CoreText on Tiger 10.4.11.
   Everything is reached through dlsym so the test links against nothing Leopard. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <dlfcn.h>

typedef const void *CFTypeRef;
typedef const struct __CFString *CFStringRef;
typedef const struct __CFAllocator *CFAllocatorRef;
typedef const struct __CFDictionary *CFDictionaryRef;
typedef const struct __CFArray *CFArrayRef;
typedef const struct __CFAttributedString *CFAttributedStringRef;
typedef long CFIndex;
typedef struct { double width, height; } CGSize64;
typedef struct { double x, y; } CGPoint64;
typedef struct { CFIndex location, length; } CFRange64;
/* CGRect is 32 bytes, so it is passed in memory: it must be declared as the
   struct it is, not as four loose doubles. */
typedef struct { CGPoint64 origin; CGSize64 size; } CGRect64;

static void *L(const char *p) {
    void *h = dlopen(p, RTLD_NOW | RTLD_GLOBAL);
    printf("%-28s %s\n", p, h ? "loaded" : dlerror());
    fflush(stdout);
    return h;
}
#define SYM(h, n) dlsym(h, n)

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);   /* a crash must not lose the line before it */
    printf("pointer width: %d bytes\n\n", (int)sizeof(void *));
    void *cf = L("/tmp/leo64/CoreFoundation");
    if (!cf) return 1;
    void *cg = L("/tmp/leo64/CoreGraphics");
    void *ct = L("/tmp/leo64/CoreText");
    printf("\n");

    CFStringRef (*mkstr)(CFAllocatorRef, const char *, unsigned) = SYM(cf, "CFStringCreateWithCString");
    CFIndex (*slen)(CFStringRef) = SYM(cf, "CFStringGetLength");
    void (*crel)(CFTypeRef) = SYM(cf, "CFRelease");
    CFStringRef s = mkstr ? mkstr(NULL, "CoreFoundation", 0x08000100) : NULL;
    printf("CF  CFStringCreateWithCString  %p  length=%ld\n", (void *)s, s && slen ? (long)slen(s) : -1);

    if (!cg) { printf("\nCoreGraphics did not load; stopping\n"); return 2; }
    /* --- CoreGraphics: a real bitmap, a fill, a pixel read back --- */
    void *(*csDevRGB)(void) = SYM(cg, "CGColorSpaceCreateDeviceRGB");
    void *(*csNamed)(CFStringRef) = SYM(cg, "CGColorSpaceCreateWithName");
    void *(*bmCreate)(void *, size_t, size_t, size_t, size_t, void *, uint32_t) = SYM(cg, "CGBitmapContextCreate");
    void (*setRGBFill)(void *, double, double, double, double) = SYM(cg, "CGContextSetRGBFillColor");
    void (*fillRect)(void *, CGRect64) = SYM(cg, "CGContextFillRect");
    void *(*bmData)(void *) = SYM(cg, "CGBitmapContextGetData");
    CFStringRef *kSRGB = SYM(cg, "kCGColorSpaceSRGB");

    void *space = csDevRGB ? csDevRGB() : NULL;
    printf("CG  CGColorSpaceCreateDeviceRGB %p\n", space);

    const int W = 64, H = 32;
    unsigned char *bits = calloc(1, W * H * 4);
    /* kCGImageAlphaPremultipliedLast = 1 */
    void *ctx = bmCreate ? bmCreate(bits, W, H, 8, W * 4, space, 1) : NULL;
    printf("CG  CGBitmapContextCreate      %p\n", ctx);
    if (ctx) {
        CGRect64 r = { { 0, 0 }, { (double)W, (double)H } };
        if (setRGBFill) setRGBFill(ctx, 0.0, 1.0, 0.0, 1.0);
        if (fillRect) fillRect(ctx, r);
        unsigned char *px = bmData ? bmData(ctx) : bits;
        printf("CG  centre pixel RGBA          %u %u %u %u  (expect 0 255 0 255)\n",
               px[(H/2)*W*4 + (W/2)*4], px[(H/2)*W*4 + (W/2)*4+1],
               px[(H/2)*W*4 + (W/2)*4+2], px[(H/2)*W*4 + (W/2)*4+3]);
    }

    if (!ct) { printf("\nCoreText did not load; stopping\n"); return 3; }
    /* --- CoreText --- */
    void *(*fontWithName)(CFStringRef, double, const void *) = SYM(ct, "CTFontCreateWithName");
    double (*fontSize)(void *) = SYM(ct, "CTFontGetSize");
    double (*fontAscent)(void *) = SYM(ct, "CTFontGetAscent");
    CFStringRef (*fontFamily)(void *) = SYM(ct, "CTFontCopyFamilyName");
    void *(*lineMake)(CFAttributedStringRef) = SYM(ct, "CTLineCreateWithAttributedString");
    CFArrayRef (*lineRuns)(void *) = SYM(ct, "CTLineGetGlyphRuns");
    void (*lineDraw)(void *, void *) = SYM(ct, "CTLineDraw");
    CFIndex (*runCount)(void *) = SYM(ct, "CTRunGetGlyphCount");
    void (*runAdv)(void *, CFRange64, CGSize64 *) = SYM(ct, "CTRunGetAdvances");
    CFStringRef *kFontAttr = SYM(ct, "kCTFontAttributeName");

    void *font = fontWithName ? fontWithName(mkstr(NULL, "Helvetica", 0x08000100), 24.0, NULL) : NULL;
    printf("\nCT  CTFontCreateWithName       %p\n", font);
    if (font) {
        printf("CT  size=%.3f ascent=%.3f\n", fontSize ? fontSize(font) : -1, fontAscent ? fontAscent(font) : -1);
        CFStringRef fam = fontFamily ? fontFamily(font) : NULL;
        char nb[128] = {0};
        int (*getCStr)(CFStringRef, char *, CFIndex, unsigned) = SYM(cf, "CFStringGetCString");
        if (fam && getCStr) getCStr(fam, nb, sizeof nb, 0x08000100);
        printf("CT  family                     \"%s\"\n", nb);
    }
    if (font && lineMake && kFontAttr) {
        CFDictionaryRef (*dictCreate)(CFAllocatorRef, const void **, const void **, CFIndex, const void *, const void *) = SYM(cf, "CFDictionaryCreate");
        CFAttributedStringRef (*asCreate)(CFAllocatorRef, CFStringRef, CFDictionaryRef) = SYM(cf, "CFAttributedStringCreate");
        void *kKeyCB = SYM(cf, "kCFTypeDictionaryKeyCallBacks");
        void *kValCB = SYM(cf, "kCFTypeDictionaryValueCallBacks");
        const void *k[1] = { *kFontAttr }; const void *v[1] = { font };
        CFDictionaryRef at = dictCreate(NULL, k, v, 1, kKeyCB, kValCB);
        CFAttributedStringRef as = asCreate(NULL, mkstr(NULL, "Hamburgevons", 0x08000100), at);
        void *line = lineMake(as);
        printf("CT  CTLineCreateWithAttributedString %p\n", line);
        if (line) {
            CFArrayRef runs = lineRuns ? lineRuns(line) : NULL;
            CFIndex (*arrCount)(CFArrayRef) = SYM(cf, "CFArrayGetCount");
            const void *(*arrGet)(CFArrayRef, CFIndex) = SYM(cf, "CFArrayGetValueAtIndex");
            long nr = runs && arrCount ? (long)arrCount(runs) : -1;
            printf("CT  runs                       %ld\n", nr);
            if (nr > 0 && arrGet) {
                void *run = (void *)arrGet(runs, 0);
                long gc = runCount ? (long)runCount(run) : -1;
                printf("CT  run0 glyphs                %ld\n", gc);
                if (gc > 0 && runAdv) {
                    CGSize64 *adv = calloc(gc, sizeof *adv);
                    CFRange64 rg = { 0, gc };
                    runAdv(run, rg, adv);
                    printf("CT  run0 advances              %.3f %.3f %.3f\n", adv[0].width, adv[1].width, adv[2].width);
                }
            }
            if (ctx && lineDraw) {
                void (*setTextPos)(void *, double, double) = SYM(cg, "CGContextSetTextPosition");
                if (setTextPos) setTextPos(ctx, 2.0, 8.0);
                lineDraw(line, ctx);
                unsigned char *px = bmData ? bmData(ctx) : bits;
                long ink = 0;
                for (int i = 0; i < W * H; i++) if (px[i*4] != 0 || px[i*4+1] != 255) ink++;
                printf("CT  CTLineDraw -> pixels changed from the green fill: %ld\n", ink);
            }
        }
    }
    /* Last, because it is the one that faults: a named colour space wants the
       ColorSync profile machinery, which is not up in a headless Tiger process. */
    if (csNamed && kSRGB) {
        printf("\nCG  CGColorSpaceCreateWithName(sRGB) ... ");
        printf("%p\n", csNamed(*kSRGB));
    }
    printf("\nDONE\n");
    return 0;
}
