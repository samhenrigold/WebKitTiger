/*
 * ct9metrics.c - which cap height does Leopard DP1's CoreText report?
 *
 * ctcompat's live oracle runs 9A241's CoreText on the box and diffs it against
 * Tiger's. It passed cap height, which was 5.8% out against modern. The stated
 * reason is that both were wrong the same way. This checks that directly, by
 * asking all three for the same metric on the same font.
 *
 * DejaVu is activated through ctcompat's CTFontManagerCreateFontDescriptorFromData,
 * which ATS-registers it process-wide, so 9A241's CoreText can then resolve it
 * by PostScript name - the same trick the web-font path relies on.
 *
 * Run on the box: ct9metrics <DejaVuSans.ttf>
 */
#include <CoreFoundation/CoreFoundation.h>
#include <CoreText/CoreText.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

typedef const struct __CTFont *Font9;
typedef Font9 (*byname_t)(CFStringRef, float, const CGAffineTransform *);
typedef float (*metric_t)(Font9);
typedef CFTypeID (*typeid_t)(void);

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "DejaVuSans.ttf";
    FILE *f = fopen(path, "rb");
    if (!f) { printf("FATAL no font\n"); return 2; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    void *b = malloc(n); fread(b, 1, n, f); fclose(f);
    CFDataRef data = CFDataCreate(NULL, (const UInt8 *)b, n); free(b);

    /* ctcompat's adapter: ATS-activates the bytes and returns a name descriptor */
    CTFontDescriptorRef fd = CTFontManagerCreateFontDescriptorFromData(data);
    if (!fd) { printf("FATAL no descriptor\n"); return 3; }

    void *sh = dlopen("/tmp/ct9/ct9shim.dylib", RTLD_LAZY | RTLD_GLOBAL);
    void (*bind)(unsigned long, void *) = sh ? dlsym(sh, "ct9_bind") : NULL;
    void *h = dlopen("/tmp/ct9/LeopardCT9", RTLD_LAZY | RTLD_LOCAL);
    printf("9A241 CoreText loaded: %s\n", h ? "yes" : dlerror());
    if (!h) return 4;

    byname_t  c9   = (byname_t)dlsym(h, "CTFontCreateWithName");
    metric_t  cap9 = (metric_t)dlsym(h, "CTFontGetCapHeight");
    metric_t  xh9  = (metric_t)dlsym(h, "CTFontGetXHeight");
    metric_t  asc9 = (metric_t)dlsym(h, "CTFontGetAscent");
    metric_t  sz9  = (metric_t)dlsym(h, "CTFontGetSize");
    typeid_t  fid  = (typeid_t)dlsym(h, "CTFontGetTypeID");
    if (!c9 || !cap9 || !xh9) { printf("FATAL missing 9A241 symbols\n"); return 5; }

    /* bootstrap the CF bridge table for 9A241, as ct9-test.c does */
    if (bind && fid) {
        Font9 probe = c9(CFSTR("Helvetica"), 12.0f, NULL);
        if (probe) bind((unsigned long)fid(), *(void **)probe);
    }

    printf("\n%-6s %-12s %-12s   %-12s %-12s\n", "size",
           "cap(tiger)", "cap(9A241)", "xh(tiger)", "xh(9A241)");
    static const double sizes[] = { 9, 12, 16, 24, 100 };
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        double s = sizes[i];
        CTFontRef tf = CTFontCreateWithFontDescriptor(fd, s, NULL);
        Font9 nf = c9(CFSTR("DejaVuSans"), (float)s, NULL);
        if (!nf) { printf("%-6.0f 9A241 could not resolve DejaVuSans by name\n", s); continue; }
        printf("%-6.0f %-12.6f %-12.6f   %-12.6f %-12.6f   (9A241 size=%.3f ascent=%.6f)\n", s,
               tf ? (double)CTFontGetCapHeight(tf) : -1, (double)cap9(nf),
               tf ? (double)CTFontGetXHeight(tf) : -1,  (double)xh9(nf),
               (double)sz9(nf), (double)asc9(nf));
        if (tf) CFRelease(tf);
    }
    return 0;
}
