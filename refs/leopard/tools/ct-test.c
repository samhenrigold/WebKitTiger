/* Leopard CoreText on Tiger. Prototypes per the 10.5 SDK; CGFloat is float on i386. */
#include <CoreFoundation/CoreFoundation.h>
#include <ApplicationServices/ApplicationServices.h>
#include <dlfcn.h>
#include <stdio.h>

typedef const struct __CTFont *CTFontRef;
typedef const struct __CTFontDescriptor *CTFontDescriptorRef;
typedef const struct __CTLine *CTLineRef;
typedef const struct __CTRun *CTRunRef;

typedef CTFontRef (*fCreateWithName_t)(CFStringRef, float, const CGAffineTransform *);
typedef float (*fGetSize_t)(CTFontRef);
typedef CFStringRef (*fCopyFullName_t)(CTFontRef);
typedef CFIndex (*fGetGlyphCount_t)(CTFontRef);
typedef Boolean (*fGetGlyphs_t)(CTFontRef, const UniChar *, CGGlyph *, CFIndex);
typedef double (*fGetAdvances_t)(CTFontRef, int, const CGGlyph *, CGSize *, CFIndex);
typedef CGFontRef (*fCopyGraphicsFont_t)(CTFontRef, CTFontDescriptorRef *);
typedef CTFontRef (*fWithGraphicsFont_t)(CGFontRef, float, const CGAffineTransform *, CTFontDescriptorRef);
typedef CTLineRef (*lCreate_t)(CFAttributedStringRef);
typedef CFArrayRef (*lGetRuns_t)(CTLineRef);
typedef double (*lGetWidth_t)(CTLineRef, float *, float *, float *);
typedef CFIndex (*rGetCount_t)(CTRunRef);
typedef const CGGlyph * (*rGetGlyphsPtr_t)(CTRunRef);
typedef void (*rGetAdvances_t)(CTRunRef, CFRange, CGSize *);

static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) { pass++; printf("ok   %s\n", m); } \
                         else  { fail++; printf("FAIL %s\n", m); } fflush(stdout); } while (0)

int main(void) {
  void *shim = dlopen("/tmp/leopard/ctshim.dylib", RTLD_LAZY | RTLD_LOCAL);
  printf("shim: %s\n", shim ? "loaded" : dlerror()); fflush(stdout);
  if (!shim) return 1;

  void *h = dlopen("/tmp/leopard/LeopardCT", RTLD_LAZY | RTLD_LOCAL);
  if (!h) { printf("FAIL dlopen CoreText\n  %s\n", dlerror()); return 1; }
  printf("ok   dlopen Leopard CoreText -> %p\n\n", h); fflush(stdout);

  fCreateWithName_t fCreateWithName = (fCreateWithName_t)dlsym(h, "CTFontCreateWithName"); CHECK(fCreateWithName != 0, "dlsym " "CTFontCreateWithName");
  fGetSize_t fGetSize = (fGetSize_t)dlsym(h, "CTFontGetSize"); CHECK(fGetSize != 0, "dlsym " "CTFontGetSize");
  fCopyFullName_t fCopyFullName = (fCopyFullName_t)dlsym(h, "CTFontCopyFullName"); CHECK(fCopyFullName != 0, "dlsym " "CTFontCopyFullName");
  fGetGlyphCount_t fGetGlyphCount = (fGetGlyphCount_t)dlsym(h, "CTFontGetGlyphCount"); CHECK(fGetGlyphCount != 0, "dlsym " "CTFontGetGlyphCount");
  fGetGlyphs_t fGetGlyphs = (fGetGlyphs_t)dlsym(h, "CTFontGetGlyphsForCharacters"); CHECK(fGetGlyphs != 0, "dlsym " "CTFontGetGlyphsForCharacters");
  fGetAdvances_t fGetAdvances = (fGetAdvances_t)dlsym(h, "CTFontGetAdvancesForGlyphs"); CHECK(fGetAdvances != 0, "dlsym " "CTFontGetAdvancesForGlyphs");
  fCopyGraphicsFont_t fCopyGraphicsFont = (fCopyGraphicsFont_t)dlsym(h, "CTFontCopyGraphicsFont"); CHECK(fCopyGraphicsFont != 0, "dlsym " "CTFontCopyGraphicsFont");
  fWithGraphicsFont_t fWithGraphicsFont = (fWithGraphicsFont_t)dlsym(h, "CTFontCreateWithGraphicsFont"); CHECK(fWithGraphicsFont != 0, "dlsym " "CTFontCreateWithGraphicsFont");
  lCreate_t lCreate = (lCreate_t)dlsym(h, "CTLineCreateWithAttributedString"); CHECK(lCreate != 0, "dlsym " "CTLineCreateWithAttributedString");
  lGetRuns_t lGetRuns = (lGetRuns_t)dlsym(h, "CTLineGetGlyphRuns"); CHECK(lGetRuns != 0, "dlsym " "CTLineGetGlyphRuns");
  lGetWidth_t lGetWidth = (lGetWidth_t)dlsym(h, "CTLineGetTypographicBounds"); CHECK(lGetWidth != 0, "dlsym " "CTLineGetTypographicBounds");
  rGetCount_t rGetCount = (rGetCount_t)dlsym(h, "CTRunGetGlyphCount"); CHECK(rGetCount != 0, "dlsym " "CTRunGetGlyphCount");
  rGetGlyphsPtr_t rGetGlyphsPtr = (rGetGlyphsPtr_t)dlsym(h, "CTRunGetGlyphsPtr"); CHECK(rGetGlyphsPtr != 0, "dlsym " "CTRunGetGlyphsPtr");
  rGetAdvances_t rGetAdvances = (rGetAdvances_t)dlsym(h, "CTRunGetAdvances"); CHECK(rGetAdvances != 0, "dlsym " "CTRunGetAdvances");
  CHECK(dlsym(h, "CTFontManagerCreateFontDescriptorFromData") == 0,
        "CTFontManagerCreateFontDescriptorFromData absent (10.6 API), as expected");
  { Dl_info di; extern int dladdr(const void*, Dl_info*);
    if (fCreateWithName && dladdr((void*)fCreateWithName, &di))
      printf("\n     CTFontCreateWithName resolves into: %s\n", di.dli_fname); }
  if (!fCopyFullName || !fGetGlyphCount || !fCopyGraphicsFont) {
    printf("     (some Leopard-only exports still unresolved -- wrong image?)\n"); }
  printf("\n-- calling in --\n"); fflush(stdout);

  CFStringRef name = CFSTR("Helvetica");
  CTFontRef font = fCreateWithName(name, 24.0f, NULL);
  CHECK(font != 0, "CTFontCreateWithName(Helvetica, 24)");
  if (!font) return 1;
  printf("     CTFontGetSize -> %g (expect 24)\n", (double)fGetSize(font));
  CHECK(fGetSize(font) > 23.5f && fGetSize(font) < 24.5f, "size round-trips as CGFloat=float");

  CFStringRef full = fCopyFullName ? fCopyFullName(font) : 0;
  char buf[128] = {0};
  if (full) CFStringGetCString(full, buf, sizeof buf, kCFStringEncodingUTF8);
  printf("     CTFontCopyFullName -> \"%s\"\n", buf);
  CHECK(full && buf[0], "CTFontCopyFullName");

  CFIndex ng = fGetGlyphCount ? fGetGlyphCount(font) : -1;
  printf("     CTFontGetGlyphCount -> %ld\n", (long)ng);
  CHECK(ng > 100, "CTFontGetGlyphCount plausible");

  const UniChar chars[] = { 'A', 'b', 'c' };
  CGGlyph glyphs[3] = {0,0,0};
  Boolean got = fGetGlyphs(font, chars, glyphs, 3);
  printf("     glyphs for 'Abc' -> %u %u %u\n", glyphs[0], glyphs[1], glyphs[2]);
  CHECK(got && glyphs[0] && glyphs[1] && glyphs[2], "CTFontGetGlyphsForCharacters");

  CGSize adv[3];
  double total = fGetAdvances(font, 0 /*horizontal*/, glyphs, adv, 3);
  printf("     advances -> %g %g %g (total %g)\n", adv[0].width, adv[1].width, adv[2].width, total);
  CHECK(adv[0].width > 1 && adv[1].width > 1, "CTFontGetAdvancesForGlyphs (CG glyph path)");

  CGFontRef cg = fCopyGraphicsFont ? fCopyGraphicsFont(font, NULL) : 0;
  CHECK(cg != 0, "CTFontCopyGraphicsFont");
  if (cg) { CTFontRef f2 = fWithGraphicsFont(cg, 12.0f, NULL, NULL);
            CHECK(f2 != 0, "CTFontCreateWithGraphicsFont round trip"); }

  /* shaping */
  CFMutableAttributedStringRef as = CFAttributedStringCreateMutable(NULL, 0);
  CFAttributedStringReplaceString(as, CFRangeMake(0,0), CFSTR("Hello Tiger"));
  CFAttributedStringSetAttribute(as, CFRangeMake(0,11), CFSTR("NSFont"), font);
  CTLineRef line = lCreate(as);
  CHECK(line != 0, "CTLineCreateWithAttributedString");
  if (line) {
    float asc = 0, desc = 0, lead = 0;
    double w = lGetWidth(line, &asc, &desc, &lead);
    printf("     line width=%g ascent=%g descent=%g\n", w, (double)asc, (double)desc);
    CHECK(w > 10, "CTLineGetTypographicBounds");
    CFArrayRef runs = lGetRuns(line);
    CHECK(runs && CFArrayGetCount(runs) > 0, "CTLineGetGlyphRuns");
    if (runs && CFArrayGetCount(runs) > 0) {
      CTRunRef run = (CTRunRef)CFArrayGetValueAtIndex(runs, 0);
      CFIndex n = rGetCount(run);
      printf("     run glyph count = %ld\n", (long)n);
      const CGGlyph *gp = rGetGlyphsPtr(run);
      printf("     CTRunGetGlyphsPtr -> %p", (void*)gp);
      if (gp && n > 0) printf("  first glyphs: %u %u %u", gp[0], gp[1], gp[2]);
      printf("\n");
      CHECK(n > 0, "CTRunGetGlyphCount");
      CGSize *a = calloc(n, sizeof *a);
      rGetAdvances(run, CFRangeMake(0, n), a);
      printf("     CTRunGetAdvances (non-Ptr) -> %g %g %g\n", a[0].width, a[1].width, a[2].width);
      CHECK(a[0].width > 1, "CTRunGetAdvances returns real advances (stub on Tiger's CT)");
      free(a);
    }
  }
  printf("\n%d passed, %d failed\n", pass, fail);
  return fail != 0;
}
