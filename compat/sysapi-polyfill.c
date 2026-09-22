/* TIGER: private system entry points that WebCore's Cocoa arm reaches for and
   Mac OS X 10.4 does not have, in the order of operations this port follows:
   public Tiger API first, then Tiger's private API, then a faithful
   re-implementation, then our own. Everything here is the "our own" rung for
   things Tiger has no notion of at all, so each is the honest trivial answer.
   Built into libtigercompat.a by compat/Makefile (which globs *.c). */
#include <CoreFoundation/CoreFoundation.h>
#include <stdbool.h>

/* ---- AccessibilitySupport (10.11+): the "Increase contrast / bold text" switch.
   Tiger has no such preference; the value is false and never changes, so the
   notification name exists only to be registered for. */
bool _AXSEnhanceTextLegibilityEnabled(void)
{
    return false;
}
const CFStringRef kAXSEnhanceTextLegibilityChangedNotification = CFSTR("com.apple.accessibility.EnhanceTextLegibilityChanged");

/* ---- libsystem_featureflags (10.15+): Apple-internal feature flags. Off. */
bool _os_feature_enabled_impl(const char *domain, const char *feature)
{
    (void)domain; (void)feature;
    return false;
}

/* ---- CFNetwork's web-services provider registry (10.6+). The one caller
   (PAL::defaultSearchProviderDisplayName) falls back to "Google" on NULL, which
   is also what Tiger's Safari searched with. */
const CFStringRef kCFWebServicesTypeWebSearch = CFSTR("WebSearch");
const CFStringRef kCFWebServicesProviderDefaultDisplayNameKey = CFSTR("DefaultDisplayName");
CFDictionaryRef _CFWebServicesCopyProviderInfo(CFStringRef type, void *reserved)
{
    (void)type; (void)reserved;
    return NULL;
}
