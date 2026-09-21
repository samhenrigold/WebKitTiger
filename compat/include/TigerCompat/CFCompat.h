/* TIGER: CoreFoundation entry points added after 10.4, declared for code that
   calls them unconditionally. Implementations live in compat/nscompat.m.

   Include this after <CoreFoundation/CoreFoundation.h>. */
#ifndef TIGERCOMPAT_CFCOMPAT_H
#define TIGERCOMPAT_CFCOMPAT_H

#include <CoreFoundation/CoreFoundation.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CFAutorelease is 10.9+. Same contract: returns its argument, having handed a
   release to the current autorelease pool. */
CFTypeRef CFAutorelease(CFTypeRef object);

/* kCFTimeZoneSystemTimeZoneDidChangeNotification is 10.5+. Defined in
   compat/nscompat.m as a distinct CFStringRef; nothing on Tiger posts it, so a
   client that registers for it simply never hears about time zone changes. */
extern const CFStringRef kCFTimeZoneSystemTimeZoneDidChangeNotification;

/* kCFLocaleCollatorIdentifier is 10.5+. Tiger spells the same locale property
   kCFLocaleCollationIdentifier, so this is an alias rather than a stub. */
extern const CFStringRef kCFLocaleCollatorIdentifier;

/* CFLocaleCopyPreferredLanguages is 10.5+ and genuinely absent from Tiger.
   Implemented in compat/cfcompat.c the way CF-550 does: read the AppleLanguages
   preference. Returns a retained CFArrayRef of CFStringRef, or an empty array
   when the preference is unset. */
CFArrayRef CFLocaleCopyPreferredLanguages(void);

/* CFRunLoopGetMain is exported by Tiger's CoreFoundation (T _CFRunLoopGetMain;
   logs/api/tiger-CF.txt) but not declared in the 10.4u SDK's <CFRunLoop.h>, so
   this is a declaration of the real function rather than a shim. Note it cannot
   implement +[NSRunLoop mainRunLoop]: NSRunLoop and CFRunLoop are not toll-free
   bridged in either direction, so there is no way back from a CFRunLoopRef to
   the NSRunLoop wrapping it. */
CFRunLoopRef CFRunLoopGetMain(void);

/* CFStringCreateWithBytesNoCopy is absent from the 10.4 headers but present in
   Tiger's CoreFoundation binary (logs/api/tiger-CF.txt), so this is a
   declaration of the real function, not a shim. */
CFStringRef CFStringCreateWithBytesNoCopy(CFAllocatorRef alloc, const UInt8 *bytes,
    CFIndex numBytes, CFStringEncoding encoding, Boolean isExternalRepresentation,
    CFAllocatorRef contentsDeallocator);

#ifdef __cplusplus
}
#endif

#ifdef __OBJC__
/* CFBridgingRelease is an ARC helper from the 10.7 SDK; it is an inline in
   Foundation, not an exported symbol, so it is reproduced rather than stubbed. */
#ifndef CFBridgingRelease
#if __has_feature(objc_arc)
static __inline__ id CFBridgingRelease(CFTypeRef __object) {
    return (__bridge_transfer id)__object;
}
#else
static __inline__ id CFBridgingRelease(CFTypeRef __object) {
    return [(id)__object autorelease];
}
#endif
#endif
#endif /* __OBJC__ */

#endif /* TIGERCOMPAT_CFCOMPAT_H */
