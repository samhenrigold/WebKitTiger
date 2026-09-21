/* TIGER SDK OVERLAY: <CoreText/CTFontCollection.h> — see CTDefines.h. */
#ifndef __CTFONTCOLLECTION__
#define __CTFONTCOLLECTION__
#include <CoreText/CTDefines.h>
#include <CoreText/CTFontDescriptor.h>
typedef const struct __CTFontCollection* CTFontCollectionRef;
CT_EXTERN const CFStringRef kCTFontCollectionRemoveDuplicatesOption;
CT_EXTERN CFTypeID CTFontCollectionGetTypeID(void);
CT_EXTERN CTFontCollectionRef CTFontCollectionCreateFromAvailableFonts(CFDictionaryRef options);
CT_EXTERN CTFontCollectionRef CTFontCollectionCreateWithFontDescriptors(CFArrayRef, CFDictionaryRef options);
CT_EXTERN CTFontCollectionRef CTFontCollectionCreateCopyWithFontDescriptors(CTFontCollectionRef, CFArrayRef, CFDictionaryRef options);
CT_EXTERN CFArrayRef CTFontCollectionCreateMatchingFontDescriptors(CTFontCollectionRef);
#endif /* __CTFONTCOLLECTION__ */
