/* TIGER SDK OVERLAY: <CoreText/CTFontManager.h> — see CTDefines.h.
 *
 * Tiger has no CTFontManager at all. Every function here is ours, built on
 * ATSFontActivateFromMemory; see CT-SURVEY.md. */
#ifndef __CTFONTMANAGER__
#define __CTFONTMANAGER__

#include <CoreText/CTDefines.h>
#include <CoreText/CTFontDescriptor.h>

typedef int CTFontManagerScope;
enum {
    kCTFontManagerScopeNone    = 0,
    kCTFontManagerScopeProcess = 1,
    kCTFontManagerScopeUser    = 3
};

CT_EXTERN const CFStringRef kCTFontManagerRegisteredFontsChangedNotification;

CT_EXTERN CFArrayRef CTFontManagerCopyAvailableFontFamilyNames(void);
CT_EXTERN CTFontDescriptorRef CTFontManagerCreateFontDescriptorFromData(CFDataRef);
CT_EXTERN CTFontDescriptorRef CTFontManagerCreateMemorySafeFontDescriptorFromData(CFDataRef);
CT_EXTERN CFArrayRef CTFontManagerCreateFontDescriptorsFromData(CFDataRef);
CT_EXTERN CFArrayRef CTFontManagerCreateFontDescriptorsFromURL(CFURLRef);
CT_EXTERN bool CTFontManagerRegisterFontsForURL(CFURLRef, CTFontManagerScope, CFErrorRef*);
CT_EXTERN bool CTFontManagerEnableAllUserFonts(bool postFontChangeNotification);

#endif /* __CTFONTMANAGER__ */
