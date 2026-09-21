/* TIGER: <Availability.h> arrived in the 10.5 SDK; the 10.4u SDK has only
   <AvailabilityMacros.h>. This fills it in with what WebKit actually reads.

   __MAC_OS_X_VERSION_MIN_REQUIRED stays 1040, so every
   "#if __MAC_OS_X_VERSION_MIN_REQUIRED >= <modern>" gate in WebKit evaluates
   false and the modern-SDK code path is compiled out. That is the point: it is
   the single switch that turns off most of what Tiger cannot provide.

   Availability attributes themselves are no-ops here. Nothing we link against
   is versioned, and clang would otherwise reject __attribute__((availability))
   arguments naming OS versions that this deployment target predates. */
#ifndef __TIGER_AVAILABILITY_H__
#define __TIGER_AVAILABILITY_H__

#include <AvailabilityMacros.h>

#define __MAC_10_0      1000
#define __MAC_10_1      1010
#define __MAC_10_2      1020
#define __MAC_10_3      1030
#define __MAC_10_4      1040
#define __MAC_10_5      1050
#define __MAC_10_6      1060
#define __MAC_10_7      1070
#define __MAC_10_8      1080
#define __MAC_10_9      1090
#define __MAC_10_10   101000
#define __MAC_10_11   101100
#define __MAC_10_12   101200
#define __MAC_10_13   101300
#define __MAC_10_14   101400
#define __MAC_10_15   101500
#define __MAC_11_0    110000
#define __MAC_12_0    120000
#define __MAC_13_0    130000
#define __MAC_14_0    140000
#define __MAC_15_0    150000
#define __MAC_16_0    160000
#define __MAC_26_0    260000
#define __MAC_NA      9999

#define __IPHONE_2_0    20000
#define __IPHONE_NA      9999

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

#endif /* __TIGER_AVAILABILITY_H__ */
