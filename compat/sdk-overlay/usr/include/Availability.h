/* TIGER SDK OVERLAY: <Availability.h> arrived in the 10.5 SDK; the 10.4u SDK has
   only <AvailabilityMacros.h>.

   The version constants come from the real thing -- AvailabilityVersions.h is
   copied verbatim from the Xcode 27 macOS SDK and is self-contained -- so every
   __MAC_xx, __IPHONE_xx and friend has its true value.

   The availability *attributes* are deliberately no-ops, and that is the whole
   point of this file. Marking a declaration
   __attribute__((availability(macos,introduced=13.0))) at a 10.4 deployment
   target makes every call to it a -Wunguarded-availability-new diagnostic, and
   WebKit's headers are full of such declarations: with -Werror the build fails,
   and without it the log is unusable. Upstream WebKit hits exactly this against
   any non-internal SDK and solves it the same way, with the VFS overlay in
   WebKitLibraries/AvailabilityOverlay that OptionsCocoa.cmake installs. This
   file is that overlay for Tiger.

   Do not replace this with the modern SDK's copy. The version constants are
   real either way; the attributes are what must not be.

   <AvailabilityMacros.h> is deliberately NOT overlaid: the 10.4u SDK's own copy
   is what its headers were written against, and it defines every
   AVAILABLE_MAC_OS_X_VERSION_10_x_AND_LATER those headers use.

   __MAC_OS_X_VERSION_MIN_REQUIRED stays 1040, which is the single switch that
   compiles out most of what Tiger cannot provide: every
   "#if __MAC_OS_X_VERSION_MIN_REQUIRED >= <modern>" in WebKit evaluates false. */
#ifndef __AVAILABILITY__
#define __AVAILABILITY__

#include <AvailabilityVersions.h>
#include <AvailabilityMacros.h>

#ifndef __MAC_OS_X_VERSION_MIN_REQUIRED
#define __MAC_OS_X_VERSION_MIN_REQUIRED 1040
#endif
#ifndef __MAC_OS_X_VERSION_MAX_ALLOWED
#define __MAC_OS_X_VERSION_MAX_ALLOWED 1040
#endif

/* No iOS family here, but WebKit compares against these unconditionally. */
#ifndef __IPHONE_OS_VERSION_MIN_REQUIRED
#define __IPHONE_OS_VERSION_MIN_REQUIRED 0
#endif
#ifndef __IPHONE_OS_VERSION_MAX_ALLOWED
#define __IPHONE_OS_VERSION_MAX_ALLOWED 0
#endif

#define __OSX_AVAILABLE_STARTING(_mac, _ios)
#define __OSX_AVAILABLE_BUT_DEPRECATED(_macIntro, _macDep, _iosIntro, _iosDep)
#define __OSX_AVAILABLE_BUT_DEPRECATED_MSG(_macIntro, _macDep, _iosIntro, _iosDep, _msg)
#define __OSX_AVAILABLE(_vers)
#define __OSX_DEPRECATED(_start, _dep, _msg)
#define __OS_AVAILABILITY(_target, _availability)
#define __OS_AVAILABILITY_MSG(_target, _availability, _msg)

#define __API_AVAILABLE(...)
#define __API_AVAILABLE_BEGIN(...)
#define __API_AVAILABLE_END
#define __API_DEPRECATED(...)
#define __API_DEPRECATED_WITH_REPLACEMENT(...)
#define __API_DEPRECATED_BEGIN(...)
#define __API_DEPRECATED_END
#define __API_UNAVAILABLE(...)
#define __API_UNAVAILABLE_BEGIN(...)
#define __API_UNAVAILABLE_END
#define __SPI_AVAILABLE(...)
#define __SPI_DEPRECATED(...)
#define __SPI_DEPRECATED_WITH_REPLACEMENT(...)
#define __IOS_PROHIBITED
#define __OSX_PROHIBITED

#endif /* __AVAILABILITY__ */
