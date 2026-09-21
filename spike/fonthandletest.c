/* TIGER: does resolving a font by (path, face index) give the same font as
 * resolving it by PostScript name?
 *
 * The split port hands font handles across a process boundary: the 64-bit web
 * process picks and shapes over files, the 32-bit render process rasterises.
 * If the two ways of naming a face disagree the page renders with the wrong
 * font, so this asserts they do not.
 *
 * Build (bash, repo root):
 *   toolchain/bin/tiger-clang -O1 -g -Wall spike/fonthandletest.c -o build/fonthandletest \
 *     -ltigercompat -Fcompat/sdk-overlay \
 *     -F sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks \
 *     -framework CoreFoundation -framework CoreServices -framework ApplicationServices
 * Run on the box, with the test fonts in /tmp.
 */
/* Does resolving by (path, index) give the same font as resolving by name? */
#include <CoreText/CoreText.h>
#include <TigerCompat/CTFontHandle.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int checks, fails;
static void ok(int c, const char* m){ ++checks; if(c) printf("PASS %s\n",m); else {printf("FAIL %s\n",m); ++fails;} }
static CFDataRef load(const char*p){FILE*f=fopen(p,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
 unsigned char*b=(unsigned char*)malloc(n);if(fread(b,1,n,f)!=(size_t)n){fclose(f);free(b);return NULL;}fclose(f);
 CFDataRef d=CFDataCreate(NULL,b,n);free(b);return d;}
static void compare(const char* label, CTFontRef a, CTFontRef b){
  char m[160]; const UniChar t[5]={'H','a','m','b','u'};
  CGGlyph ga[5],gb[5]; CGSize aa[5],ab[5]; int same=1; double worst=0;
  if(!a||!b){ snprintf(m,sizeof m,"%s: one side is null",label); ok(0,m); return; }
  CTFontGetGlyphsForCharacters(a,t,ga,5); CTFontGetGlyphsForCharacters(b,t,gb,5);
  CTFontGetAdvancesForGlyphs(a,kCTFontOrientationHorizontal,ga,aa,5);
  CTFontGetAdvancesForGlyphs(b,kCTFontOrientationHorizontal,gb,ab,5);
  for(int i=0;i<5;i++){ if(ga[i]!=gb[i]) same=0; double d=fabs(aa[i].width-ab[i].width); if(d>worst)worst=d; }
  snprintf(m,sizeof m,"%s: glyphs and advances identical (worst %.5f)",label,worst);
  ok(same && worst<0.0001, m);
  snprintf(m,sizeof m,"%s: ascent/descent identical",label);
  ok(CTFontGetAscent(a)==CTFontGetAscent(b) && CTFontGetDescent(a)==CTFontGetDescent(b), m);
}
int main(void){
  setvbuf(stdout,NULL,_IONBF,0);
  struct { const char* path; const char* ps; } t[] = {
    {"/System/Library/Fonts/Geeza Pro.ttf","GeezaPro"},
    {"/tmp/Arial.ttf","ArialMT"},
    {"/tmp/DejaVuSans.ttf","DejaVuSans"},
    {"/tmp/Helvetica-Tiger.ttf","Helvetica"},
    {"/tmp/HiraKakuProW3.otf","HiraKakuPro-W3"} };
  for(unsigned i=0;i<5;i++){
    CTFontRef byHandle=NULL;
    OSStatus s=TigerCTFontForHandle(t[i].path,0,16.0,0,&byHandle);
    char m[200];
    snprintf(m,sizeof m,"%s resolves by handle",t[i].path);
    ok(s==noErr && byHandle, m);
    if(!byHandle) continue;
    CFStringRef ps=CTFontCopyPostScriptName(byHandle); char got[128]="";
    if(ps)CFStringGetCString(ps,got,128,kCFStringEncodingUTF8);
    printf("     -> %s\n",got);
    CFStringRef n=CFStringCreateWithCString(NULL,got,kCFStringEncodingUTF8);
    CTFontRef byName=CTFontCreateWithName(n,16.0,NULL);
    snprintf(m,sizeof m,"%s",got);
    compare(m,byHandle,byName);
    if(byName)CFRelease(byName); if(ps)CFRelease(ps); CFRelease(n); CFRelease(byHandle);
  }
  /* web font path: bytes, not a path */
  { CFDataRef d=load("/tmp/DejaVuSans.ttf"); CTFontRef f=NULL;
    OSStatus s=TigerCTFontForData(d,0,16.0,0,&f);
    ok(s==noErr&&f,"TigerCTFontForData resolves a face from bytes");
    if(f){ CTFontRef byName=CTFontCreateWithName(CFSTR("DejaVuSans"),16.0,NULL);
      compare("DejaVuSans from data",f,byName); if(byName)CFRelease(byName); CFRelease(f);} 
    if(d)CFRelease(d); }
  /* cache behaviour */
  { CTFontRef a=NULL,b=NULL; unsigned act=0,fonts=0;
    TigerCTFontForHandle("/tmp/DejaVuSans.ttf",0,24.0,0,&a);
    TigerCTFontForHandle("/tmp/DejaVuSans.ttf",0,24.0,0,&b);
    ok(a==b,"same handle and size returns the cached font");
    TigerCTFontHandleCacheStats(&act,&fonts);
    printf("     activations=%u cached fonts=%u\n",act,fonts);
    ok(act>0&&fonts>0,"caches are populated");
    if(a)CFRelease(a); if(b)CFRelease(b);
    TigerCTFontHandleFlushCache();
    TigerCTFontHandleCacheStats(&act,&fonts);
    ok(fonts==0,"flush empties the font cache"); }
  /* oblique flag */
  { CTFontRef plain=NULL,obl=NULL;
    TigerCTFontForHandle("/tmp/DejaVuSans.ttf",0,16.0,0,&plain);
    TigerCTFontForHandle("/tmp/DejaVuSans.ttf",0,16.0,kTigerCTFontSyntheticOblique,&obl);
    CGAffineTransform m1=plain?CTFontGetMatrix(plain):CGAffineTransformIdentity;
    CGAffineTransform m2=obl?CTFontGetMatrix(obl):CGAffineTransformIdentity;
    ok(plain&&obl&&m1.c==0&&m2.c!=0,"oblique flag shears the font matrix");
    printf("     plain c=%.3f oblique c=%.3f\n",(double)m1.c,(double)m2.c);
    if(plain)CFRelease(plain); if(obl)CFRelease(obl); }
  printf("\n%d checks, %d failures\n",checks,fails);
  return fails?1:0;
}
