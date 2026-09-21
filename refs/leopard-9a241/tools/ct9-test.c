/* 9A241 CoreText on Tiger 10.4.11: full functional check with the CF-bridge bootstrap. */
#include <CoreFoundation/CoreFoundation.h>
#include <ApplicationServices/ApplicationServices.h>
#include <dlfcn.h>
#include <stdio.h>
typedef const struct __CTFont *CTFontRef; typedef const struct __CTFontDescriptor *CTFontDescriptorRef;
typedef const struct __CTLine *CTLineRef; typedef const struct __CTRun *CTRunRef;
typedef CFTypeID(*fid_t)(void);
typedef CFTypeID(*did_t)(void);
typedef CTFontRef(*fname_t)(CFStringRef,float,const CGAffineTransform*);
typedef float(*fsize_t)(CTFontRef);
typedef CFStringRef(*ffull_t)(CTFontRef);
typedef CFIndex(*fcount_t)(CTFontRef);
typedef CGFontRef(*fcg_t)(CTFontRef,CTFontDescriptorRef*);
typedef CTFontRef(*fforchars_t)(CTFontRef,const UniChar*,CFIndex);
typedef Boolean(*fglyphs_t)(CTFontRef,const UniChar*,CGGlyph*,CFIndex);
typedef double(*fadv_t)(CTFontRef,int,const CGGlyph*,CGSize*,CFIndex);
typedef CTFontDescriptorRef(*dnew_t)(CFDictionaryRef);
typedef CTFontDescriptorRef(*dmatch_t)(CTFontDescriptorRef,CFSetRef);
typedef CFArrayRef(*dall_t)(CTFontDescriptorRef,CFSetRef);
typedef CTFontDescriptorRef(*dtraits_t)(CTFontDescriptorRef,uint32_t,uint32_t);
typedef CTLineRef(*lmake_t)(CFAttributedStringRef);
typedef CFArrayRef(*lruns_t)(CTLineRef);
typedef double(*lb_t)(CTLineRef,float*,float*,float*);
typedef CFIndex(*rcount_t)(CTRunRef);
typedef const CGGlyph*(*rgp_t)(CTRunRef);
typedef void(*radv_t)(CTRunRef,CFRange,CGSize*);
static int pass, fail;
#define CHECK(c,m) do{ if(c){pass++;printf("ok   %s\n",m);} else {fail++;printf("FAIL %s\n",m);} fflush(stdout);}while(0)
int main(void){
  void *sh=dlopen("/tmp/l9/ct9shim.dylib",RTLD_LAZY|RTLD_GLOBAL);
  void (*bind)(unsigned long,void*)=dlsym(sh,"ct9_bind");
  void *h=dlopen("/tmp/l9/LeopardCT9",RTLD_LAZY|RTLD_LOCAL);
  CHECK(h!=0,"dlopen 9A241 CoreText"); if(!h){printf("  %s\n",dlerror());return 1;}
  fid_t fid=(fid_t)dlsym(h,"CTFontGetTypeID");
  did_t did=(did_t)dlsym(h,"CTFontDescriptorGetTypeID");
  fname_t fname=(fname_t)dlsym(h,"CTFontCreateWithName");
  fsize_t fsize=(fsize_t)dlsym(h,"CTFontGetSize");
  ffull_t ffull=(ffull_t)dlsym(h,"CTFontCopyFullName");
  fcount_t fcount=(fcount_t)dlsym(h,"CTFontGetGlyphCount");
  fcg_t fcg=(fcg_t)dlsym(h,"CTFontCopyGraphicsFont");
  fforchars_t fforchars=(fforchars_t)dlsym(h,"CTFontCreateForCharacters");
  fglyphs_t fglyphs=(fglyphs_t)dlsym(h,"CTFontGetGlyphsForCharacters");
  fadv_t fadv=(fadv_t)dlsym(h,"CTFontGetAdvancesForGlyphs");
  dnew_t dnew=(dnew_t)dlsym(h,"CTFontDescriptorCreateWithAttributes");
  dmatch_t dmatch=(dmatch_t)dlsym(h,"CTFontDescriptorCreateMatchingFontDescriptor");
  dall_t dall=(dall_t)dlsym(h,"CTFontDescriptorCreateMatchingFontDescriptors");
  dtraits_t dtraits=(dtraits_t)dlsym(h,"CTFontDescriptorCreateCopyWithSymbolicTraits");
  lmake_t lmake=(lmake_t)dlsym(h,"CTLineCreateWithAttributedString");
  lruns_t lruns=(lruns_t)dlsym(h,"CTLineGetGlyphRuns");
  lb_t lb=(lb_t)dlsym(h,"CTLineGetTypographicBounds");
  rcount_t rcount=(rcount_t)dlsym(h,"CTRunGetGlyphCount");
  rgp_t rgp=(rgp_t)dlsym(h,"CTRunGetGlyphsPtr");
  radv_t radv=(radv_t)dlsym(h,"CTRunGetAdvances");
  CFStringRef *kFam=dlsym(h,"kCTFontFamilyNameAttribute");

  /* bootstrap: register the types, then teach the shim's table the isa each one's
     instances really carry, since CF 368 does not use CF 401's per-type isa scheme. */
  CFTypeID tFont=fid(), tDesc=did();
  CTFontRef probe=fname(CFSTR("Helvetica"),12.0f,NULL);
  if(probe) bind(tFont,*(void**)probe);
  CFStringRef k=kFam?*kFam:CFSTR("NSFontFamilyAttribute"), v=CFSTR("Times");
  CFDictionaryRef at=CFDictionaryCreate(NULL,(const void**)&k,(const void**)&v,1,
      &kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
  CTFontDescriptorRef pd=dnew(at);
  if(pd) bind(tDesc,*(void**)pd);
  printf("     bootstrap: CTFont typeID=%lu isa=%p; CTFontDescriptor typeID=%lu isa=%p\n\n",
         (unsigned long)tFont, probe?*(void**)probe:0, (unsigned long)tDesc, pd?*(void**)pd:0);

  CTFontRef f=fname(CFSTR("Helvetica"),24.0f,NULL);
  CHECK(f!=0,"CTFontCreateWithName");
  CHECK(fsize(f)>23.5f&&fsize(f)<24.5f,"CTFontGetSize == 24 (CGFloat=float ABI)");
  char b[128]={0}; CFStringRef n=ffull(f); if(n) CFStringGetCString(n,b,sizeof b,kCFStringEncodingUTF8);
  printf("     full name \"%s\", %ld glyphs\n",b,(long)fcount(f));
  CHECK(n&&b[0],"CTFontCopyFullName [gained]"); CHECK(fcount(f)>100,"CTFontGetGlyphCount [gained]");
  CHECK(fcg(f,NULL)!=0,"CTFontCopyGraphicsFont [gained]");
  const UniChar cs[]={'A','b','c'}; CGGlyph g[3]={0,0,0};
  CHECK(fglyphs(f,cs,g,3)&&g[0]&&g[1]&&g[2],"CTFontGetGlyphsForCharacters");
  CGSize adv[3]; fadv(f,0,g,adv,3);
  printf("     advances %g %g %g (Helvetica 24pt: expect 16.008 13.348 12)\n",adv[0].width,adv[1].width,adv[2].width);
  CHECK(adv[0].width>15.9&&adv[0].width<16.1,"CTFontGetAdvancesForGlyphs matches the font's real metrics");
  const UniChar han[]={0x4e00};
  CTFontRef ff=fforchars?fforchars(f,han,1):0;
  CHECK(ff!=0,"CTFontCreateForCharacters fallback [gained]");
  CHECK(dmatch(pd,NULL)!=0,"CTFontDescriptorCreateMatchingFontDescriptor [gained]");
  CFArrayRef all=dall(pd,NULL);
  printf("     matching descriptors for Times: %ld\n",all?(long)CFArrayGetCount(all):-1);
  CHECK(all&&CFArrayGetCount(all)>0,"CTFontDescriptorCreateMatchingFontDescriptors [gained]");
  CHECK(dtraits(pd,0x2,0x2)!=0,"CTFontDescriptorCreateCopyWithSymbolicTraits [gained, absent from 10.5.8]");

  CFMutableAttributedStringRef as=CFAttributedStringCreateMutable(NULL,0);
  CFAttributedStringReplaceString(as,CFRangeMake(0,0),CFSTR("Hello Tiger"));
  CFAttributedStringSetAttribute(as,CFRangeMake(0,11),CFSTR("NSFont"),f);
  CTLineRef ln=lmake(as); CHECK(ln!=0,"CTLineCreateWithAttributedString");
  float a,d,l; double w=lb(ln,&a,&d,&l);
  printf("     line w=%g ascent=%g descent=%g\n",w,(double)a,(double)d);
  CHECK(w>10&&a>18&&a<19,"CTLineGetTypographicBounds matches Helvetica 24pt ascent");
  CFArrayRef runs=lruns(ln); CTRunRef r=(CTRunRef)CFArrayGetValueAtIndex(runs,0);
  CFIndex kk=rcount(r); const CGGlyph *gp=rgp(r);
  CHECK(kk==11&&gp,"CTRunGetGlyphCount + CTRunGetGlyphsPtr");
  CGSize *aa=calloc(kk,sizeof *aa); radv(r,CFRangeMake(0,kk),aa);
  CHECK(aa[0].width>1,"CTRunGetAdvances real (a stub on Tiger's own CT)");

  int bad=0; const char *fams[]={"Helvetica","Times","Courier","Geneva","Monaco"};
  for(int i=0;i<400;i++){
    CFStringRef nm=CFStringCreateWithCString(NULL,fams[i%5],kCFStringEncodingUTF8);
    CTFontRef q=fname(nm,8.0f+(i%40),NULL);
    if(!q||fsize(q)!=(float)(8+(i%40))) bad++;
    else { CFMutableAttributedStringRef s2=CFAttributedStringCreateMutable(NULL,0);
           CFAttributedStringReplaceString(s2,CFRangeMake(0,0),CFSTR("The quick brown fox 0123"));
           CFAttributedStringSetAttribute(s2,CFRangeMake(0,24),CFSTR("NSFont"),q);
           CTLineRef l2=lmake(s2); float x,y,z; if(!l2||lb(l2,&x,&y,&z)<=0) bad++; 
           if(l2) CFRelease(l2); CFRelease(s2); CFRelease(q); }
    CFRelease(nm);
  }
  printf("     soak: 400 font+line cycles over 5 families\n");
  CHECK(bad==0,"soak clean");
  printf("\n%d passed, %d failed\n",pass,fail);
  return fail!=0;
}
