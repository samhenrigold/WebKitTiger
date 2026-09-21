/* Shim supplying the 39 symbols Leopard CoreText imports that Tiger 10.4.11 lacks.
   Real mappings where Tiger has an older equivalent; logging stubs elsewhere so a
   run reports exactly which gaps the exercised code paths actually touch. */
#include <CoreFoundation/CoreFoundation.h>
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
static const char *g_last = "(none)";
static void onfatal(int sig) {
  const char *m = "\n*** crashed; last shim call: ";
  write(2, m, strlen(m)); write(2, g_last, strlen(g_last)); write(2, "\n", 1);
  _exit(128 + sig);
}
__attribute__((constructor)) static void arm(void) {
  signal(SIGSEGV, onfatal); signal(SIGBUS, onfatal); signal(SIGILL, onfatal); signal(SIGABRT, onfatal);
}

static void note(const char *n) {
  static const char *seen[64]; static int ns;
  g_last = n;
  for (int i = 0; i < ns; i++) if (seen[i] == n) return;
  if (ns < 64) seen[ns++] = n;
  g_last = n;
  fprintf(stderr, "    [shim] called: %s\n", n);
}
#define STUB(ret, name, ...) ret name(__VA_ARGS__) { note(#name); return (ret)0; }
#define STUBV(name, ...)     void name(__VA_ARGS__)  { note(#name); }

/* ---- non-lazy data: these five are what block the load ---- */
int __CFRuntimeClassTableSize = 1024;
const CFStringRef kATSAutoActivationConfirmDontShowAgainKey = CFSTR("ATSAutoActivationConfirmDontShowAgain");
const CFStringRef kATSAutoActivationConfirmResultKey        = CFSTR("ATSAutoActivationConfirmResult");
const CFStringRef kLSItemQuarantineProperties               = CFSTR("LSItemQuarantineProperties");
const CFStringRef kCGColorBlack                             = CFSTR("kCGColorBlack");

/* ---- Tiger has an older equivalent: map onto it ---- */
extern bool CGFontGetGlyphTransformedAdvances(CGFontRef, const CGAffineTransform *, const CGGlyph[], size_t, CGSize[]);
extern bool CGFontGetGlyphTransformedBBoxes(CGFontRef, const CGAffineTransform *, const CGGlyph[], size_t, CGRect[]);
extern bool CGFontGetGlyphBoundingBoxes(CGFontRef, const CGGlyph[], size_t, CGRect[]);

bool CGFontGetGlyphAdvancesForStyle(CGFontRef f, const CGAffineTransform *m, int style,
                                    const CGGlyph g[], size_t n, CGSize adv[]) {
  note("CGFontGetGlyphAdvancesForStyle -> CGFontGetGlyphTransformedAdvances");
  CGAffineTransform id = CGAffineTransformIdentity;
  return CGFontGetGlyphTransformedAdvances(f, m ? m : &id, g, n, adv);
}
bool CGFontGetGlyphBBoxesForStyle(CGFontRef f, const CGAffineTransform *m, int style,
                                  const CGGlyph g[], size_t n, CGRect r[]) {
  note("CGFontGetGlyphBBoxesForStyle -> CGFontGetGlyphTransformedBBoxes");
  CGAffineTransform id = CGAffineTransformIdentity;
  return CGFontGetGlyphTransformedBBoxes(f, m ? m : &id, g, n, r);
}
bool CGFontGetGlyphBBoxes(CGFontRef f, const CGGlyph g[], size_t n, CGRect r[]) {
  note("CGFontGetGlyphBBoxes -> CGFontGetGlyphBoundingBoxes");
  return CGFontGetGlyphBoundingBoxes(f, g, n, r);
}
int CGFontGetRenderingStyleForMode(int mode, bool a, bool b) { note("CGFontGetRenderingStyleForMode"); return 0; }

extern void CGContextShowGlyphsWithAdvances(CGContextRef, const CGGlyph[], const CGSize[], size_t);
void CGContextShowGlyphsAtPositions(CGContextRef c, const CGGlyph g[], const CGPoint p[], size_t n) {
  note("CGContextShowGlyphsAtPositions -> CGContextShowGlyphsWithAdvances");
  if (!n) return;
  CGSize *adv = (CGSize *)calloc(n, sizeof *adv);
  for (size_t i = 0; i + 1 < n; i++) { adv[i].width = p[i+1].x - p[i].x; adv[i].height = p[i+1].y - p[i].y; }
  CGContextSetTextPosition(c, p[0].x, p[0].y);
  CGContextShowGlyphsWithAdvances(c, g, adv, n);
  free(adv);
}

CGColorRef CGColorGetConstantColor(CFStringRef name) {
  note("CGColorGetConstantColor");
  static CGColorRef black;
  if (!black) { CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
                float cc[4] = {0,0,0,1}; black = CGColorCreate(cs, cc); CGColorSpaceRelease(cs); }
  return black;
}
CFArrayRef CFLocaleCopyPreferredLanguages(void) {
  note("CFLocaleCopyPreferredLanguages");
  CFArrayRef a = (CFArrayRef)CFPreferencesCopyAppValue(CFSTR("AppleLanguages"), kCFPreferencesCurrentApplication);
  if (a && CFGetTypeID(a) == CFArrayGetTypeID()) return a;
  if (a) CFRelease(a);
  CFStringRef en = CFSTR("en");
  return CFArrayCreate(NULL, (const void **)&en, 1, &kCFTypeArrayCallBacks);
}
/* CF 476 toll-free-bridge registration; a no-op just leaves CT types unbridged to NS. */
STUBV(_CFRuntimeBridgeClasses, CFTypeID t, const char *n)

/* ---- no Tiger equivalent: logging stubs ---- */
STUB(void *, CFStringOpenUText, CFStringRef s, void *ut, void *err)
STUB(int, ubidi_getParagraph, const void *b, int i, int *s, int *e, unsigned char *l, int *err)
STUB(void *, ubrk_setUText, void *bi, void *ut, int *err)
STUB(void *, utext_close, void *ut)
STUB(long long, utext_getNativeIndex, const void *ut)
STUB(long long, utext_getPreviousNativeIndex, void *ut)
STUB(int, utext_next32, void *ut)
STUB(int, utext_previous32From, void *ut, long long i)
STUB(void *, utext_setup, void *ut, int extra, int *err)

STUB(int, ATSCopyFontForAutoActivationIfNecessary, CFStringRef n, void *o)
STUB(int, ATSFontActivateFromFileReference, const void *r, int c, int f, int k, void *o, void *p)
STUB(int, ATSFontCanBeAutoActivated, CFStringRef n, unsigned char *o)
STUB(CFTypeRef, ATSFontCopyAutoActivationConfirmDialogResult, void)
CFTypeRef ATSFontCopyMDQueryResult(CFStringRef q) { note("ATSFontCopyMDQueryResult"); return CFArrayCreate(NULL,NULL,0,&kCFTypeArrayCallBacks); }
STUB(int, ATSFontFindSuitcaseFromLWFN, const void *r, void *o)
STUB(int, ATSFontGetAutoActivationSettingForApplication, CFStringRef b, int *o)
STUB(int, ATSFontGetContainerFromFileReference, const void *r, void *o)
STUB(int, ATSFontGetFileReference, unsigned f, void *o)
STUB(int, ATSFontGetGlobalAutoActivationSetting, void)
STUB(int, ATSFontSetAutoActivationSettingForApplication, int s, CFStringRef b)
CFTypeRef FOCopyFontMetaData(void *f) { note("FOCopyFontMetaData"); return CFDictionaryCreate(NULL,NULL,NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks); }
CFTypeRef FOCopyVariationInfo(void *f) { note("FOCopyVariationInfo"); return CFDictionaryCreate(NULL,NULL,NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks); }
STUB(int, FOGetFontTraits, void *f, void *o)
STUB(int, FOIsAppleSystemShippedFont, void *f)
STUB(int, FOSetFlags, void *f, unsigned fl)
STUB(int, LSSetItemAttribute, const void *i, int r, CFStringRef n, CFTypeRef v)
