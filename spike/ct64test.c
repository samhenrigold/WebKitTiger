/* Leopard x86_64 CoreText on Tiger, without a graphics context.
   CGBitmapContextCreate hangs, so everything here avoids needing one:
   font creation, metrics, glyph mapping, line layout and run advances. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dlfcn.h>
typedef const void *CFTypeRef; typedef const struct __CFString *CFStringRef;
typedef const struct __CFAllocator *CFAllocatorRef;
typedef const struct __CFDictionary *CFDictionaryRef;
typedef const struct __CFArray *CFArrayRef;
typedef const struct __CFAttributedString *CFAttributedStringRef;
typedef const struct __CFData *CFDataRef;
typedef long CFIndex;
typedef struct { double width, height; } CGSize64;
typedef struct { CFIndex location, length; } CFRange64;
static int ok, bad;
#define CHECK(c,m) do{ if(c){ok++;printf("ok   %s\n",m);} else {bad++;printf("FAIL %s\n",m);} }while(0)
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    void *cf = dlopen("/tmp/leo64/CoreFoundation", RTLD_NOW|RTLD_GLOBAL);
    void *cg = dlopen("/tmp/leo64/CoreGraphics", RTLD_NOW|RTLD_GLOBAL);
    void *ct = dlopen("/tmp/leo64/CoreText", RTLD_NOW|RTLD_GLOBAL);
    CHECK(cf && cg && ct, "CoreFoundation, CoreGraphics and CoreText all load");
    if (!ct) return 1;

    CFStringRef (*mkstr)(CFAllocatorRef,const char*,unsigned) = dlsym(cf,"CFStringCreateWithCString");
    int (*getcstr)(CFStringRef,char*,CFIndex,unsigned) = dlsym(cf,"CFStringGetCString");
    CFIndex (*arrCount)(CFArrayRef) = dlsym(cf,"CFArrayGetCount");
    const void *(*arrGet)(CFArrayRef,CFIndex) = dlsym(cf,"CFArrayGetValueAtIndex");

    void *(*fontName)(CFStringRef,double,const void*) = dlsym(ct,"CTFontCreateWithName");
    double (*fSize)(void*) = dlsym(ct,"CTFontGetSize");
    double (*fAsc)(void*) = dlsym(ct,"CTFontGetAscent");
    double (*fDesc)(void*) = dlsym(ct,"CTFontGetDescent");
    CFStringRef (*fFam)(void*) = dlsym(ct,"CTFontCopyFamilyName");
    unsigned (*fUPEM)(void*) = dlsym(ct,"CTFontGetUnitsPerEm");
    CFIndex (*fGlyphCount)(void*) = dlsym(ct,"CTFontGetGlyphCount");
    unsigned char (*fGlyphs)(void*,const unsigned short*,unsigned short*,CFIndex) = dlsym(ct,"CTFontGetGlyphsForCharacters");
    void *(*lineMake)(CFAttributedStringRef) = dlsym(ct,"CTLineCreateWithAttributedString");
    CFArrayRef (*lineRuns)(void*) = dlsym(ct,"CTLineGetGlyphRuns");
    CFIndex (*runGC)(void*) = dlsym(ct,"CTRunGetGlyphCount");
    void (*runAdv)(void*,CFRange64,CGSize64*) = dlsym(ct,"CTRunGetAdvances");
    const unsigned short *(*runGlyphsPtr)(void*) = dlsym(ct,"CTRunGetGlyphsPtr");
    CFStringRef *kFontAttr = dlsym(ct,"kCTFontAttributeName");
    void *mgrFromData = dlsym(ct,"CTFontManagerCreateFontDescriptorFromData");
    printf("     CTFontManagerCreateFontDescriptorFromData: %s\n", mgrFromData ? "present" : "absent (10.6 API)");

    void *font = fontName ? fontName(mkstr(NULL,"Helvetica",0x08000100), 24.0, NULL) : NULL;
    CHECK(font != NULL, "CTFontCreateWithName(Helvetica, 24)");
    if (!font) { printf("\n%d passed, %d failed\n", ok, bad); return 2; }
    char nb[128] = {0}; CFStringRef fam = fFam ? fFam(font) : NULL;
    if (fam && getcstr) getcstr(fam, nb, sizeof nb, 0x08000100);
    printf("     family=\"%s\" size=%.3f ascent=%.3f descent=%.3f upem=%u glyphs=%ld\n",
           nb, fSize?fSize(font):-1, fAsc?fAsc(font):-1, fDesc?fDesc(font):-1,
           fUPEM?fUPEM(font):0, fGlyphCount?(long)fGlyphCount(font):-1);
    CHECK(fSize && fSize(font) > 23.5 && fSize(font) < 24.5, "CTFontGetSize == 24");
    CHECK(fAsc && fAsc(font) > 0, "CTFontGetAscent positive");
    CHECK(nb[0] != 0, "CTFontCopyFamilyName non-empty");

    const unsigned short chars[] = { 'A','V','A',' ','f','i' };
    unsigned short g[6] = {0,0,0,0,0,0};
    unsigned char got = fGlyphs ? fGlyphs(font, chars, g, 6) : 0;
    printf("     glyphs: %u %u %u %u %u %u\n", g[0],g[1],g[2],g[3],g[4],g[5]);
    CHECK(got && g[0] && g[1], "CTFontGetGlyphsForCharacters");

    if (kFontAttr && lineMake) {
        CFDictionaryRef (*dictCreate)(CFAllocatorRef,const void**,const void**,CFIndex,const void*,const void*) = dlsym(cf,"CFDictionaryCreate");
        CFAttributedStringRef (*asCreate)(CFAllocatorRef,CFStringRef,CFDictionaryRef) = dlsym(cf,"CFAttributedStringCreate");
        const void *k[1] = { *kFontAttr }; const void *v[1] = { font };
        CFDictionaryRef at = dictCreate(NULL,k,v,1,dlsym(cf,"kCFTypeDictionaryKeyCallBacks"),dlsym(cf,"kCFTypeDictionaryValueCallBacks"));
        CFAttributedStringRef as = asCreate(NULL, mkstr(NULL,"Hamburgevons",0x08000100), at);
        void *line = lineMake(as);
        CHECK(line != NULL, "CTLineCreateWithAttributedString");
        if (line) {
            CFArrayRef runs = lineRuns ? lineRuns(line) : NULL;
            long nr = runs && arrCount ? (long)arrCount(runs) : -1;
            printf("     runs=%ld\n", nr);
            CHECK(nr > 0, "CTLineGetGlyphRuns");
            if (nr > 0) {
                void *run = (void*)arrGet(runs, 0);
                long gc = runGC ? (long)runGC(run) : -1;
                printf("     run0 glyphs=%ld\n", gc);
                CHECK(gc == 12, "CTRunGetGlyphCount == 12");
                const unsigned short *gp = runGlyphsPtr ? runGlyphsPtr(run) : NULL;
                if (gp) printf("     run0 glyph ids: %u %u %u\n", gp[0],gp[1],gp[2]);
                if (gc > 0 && runAdv) {
                    CGSize64 *adv = calloc(gc, sizeof *adv);
                    CFRange64 rg = { 0, gc };
                    runAdv(run, rg, adv);
                    printf("     run0 advances: %.3f %.3f %.3f\n", adv[0].width, adv[1].width, adv[2].width);
                    CHECK(adv[0].width > 1.0, "CTRunGetAdvances returns real advances");
                }
            }
        }
    }
    printf("\n%d passed, %d failed\n", ok, bad);
    return bad != 0;
}
