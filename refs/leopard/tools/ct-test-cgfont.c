/* Does Leopard CoreText work via the CGFont path (what WebCore uses for web fonts),
   bypassing the ATS descriptor matching that CTFontCreateWithName goes through? */
#include <CoreFoundation/CoreFoundation.h>
#include <ApplicationServices/ApplicationServices.h>
#include <dlfcn.h>
#include <stdio.h>
extern CGFontRef CGFontCreateWithDataProvider(CGDataProviderRef);
extern CGFontRef CGFontCreateWithName(CFStringRef);
extern CGFontRef CGFontCreateWithPlatformFont(void *);
typedef const struct __CTFont *CTFontRef;
typedef const struct __CTFontDescriptor *CTFontDescriptorRef;
typedef CTFontRef (*withCG_t)(CGFontRef, float, const CGAffineTransform *, CTFontDescriptorRef);
typedef float (*getSize_t)(CTFontRef);
typedef CFIndex (*glyphCount_t)(CTFontRef);
typedef Boolean (*getGlyphs_t)(CTFontRef, const UniChar *, CGGlyph *, CFIndex);
typedef double (*getAdv_t)(CTFontRef, int, const CGGlyph *, CGSize *, CFIndex);
typedef CFStringRef (*copyName_t)(CTFontRef);

int main(void) {
  dlopen("/tmp/leopard/ctshim.dylib", RTLD_LAZY|RTLD_GLOBAL);
  void *h = dlopen("/tmp/leopard/LeopardCT", RTLD_LAZY|RTLD_LOCAL);
  if (!h) { printf("FAIL dlopen: %s\n", dlerror()); return 1; }

  /* Tiger's classic route: ATS font reference -> CGFont. */
  ATSFontRef ats = ATSFontFindFromName(CFSTR("Chalkboard"), kATSOptionFlagsDefault);
  printf("ATSFontFindFromName(Chalkboard) = %u\n", (unsigned)ats); fflush(stdout);
  CGFontRef cg = ats ? CGFontCreateWithPlatformFont(&ats) : 0;
  printf("CGFontCreateWithPlatformFont = %p\n", cg); fflush(stdout);
  if (!cg) return 1;

  withCG_t withCG = (withCG_t)dlsym(h, "CTFontCreateWithGraphicsFont");
  getSize_t getSize = (getSize_t)dlsym(h, "CTFontGetSize");
  glyphCount_t gc  = (glyphCount_t)dlsym(h, "CTFontGetGlyphCount");
  getGlyphs_t gg   = (getGlyphs_t)dlsym(h, "CTFontGetGlyphsForCharacters");
  getAdv_t ga      = (getAdv_t)dlsym(h, "CTFontGetAdvancesForGlyphs");
  copyName_t cn    = (copyName_t)dlsym(h, "CTFontCopyFullName");

  CTFontRef f = withCG(cg, 24.0f, NULL, NULL);
  printf("CTFontCreateWithGraphicsFont -> %p\n", f); fflush(stdout);
  if (!f) return 1;
  printf("CTFontGetSize -> %g (expect 24)\n", (double)getSize(f)); fflush(stdout);
  printf("CTFontGetGlyphCount -> %ld\n", (long)gc(f)); fflush(stdout);
  CFStringRef n = cn(f); char b[128] = {0};
  if (n) CFStringGetCString(n, b, sizeof b, kCFStringEncodingUTF8);
  printf("CTFontCopyFullName -> \"%s\"\n", b); fflush(stdout);
  const UniChar cs[] = {'A','b','c'}; CGGlyph g[3] = {0,0,0};
  printf("GetGlyphsForCharacters -> %d, glyphs %u %u %u\n", gg(f, cs, g, 3), g[0], g[1], g[2]); fflush(stdout);
  CGSize adv[3];
  double t = ga(f, 0, g, adv, 3);
  printf("GetAdvancesForGlyphs -> total %g, widths %g %g %g\n", t, adv[0].width, adv[1].width, adv[2].width);
  printf("\nDONE\n");
  return 0;
}
