/* TIGER SDK OVERLAY: <os/availability.h> is a 10.10 SDK header. It defines the
   API_* spellings of the availability attributes; they are no-ops here for the
   reason given in <Availability.h>, and must stay that way. */
#ifndef __OS_AVAILABILITY__
#define __OS_AVAILABILITY__

#include <Availability.h>

#define API_AVAILABLE(...)
#define API_AVAILABLE_BEGIN(...)
#define API_AVAILABLE_END
#define API_DEPRECATED(...)
#define API_DEPRECATED_WITH_REPLACEMENT(...)
#define API_DEPRECATED_BEGIN(...)
#define API_DEPRECATED_END
#define API_UNAVAILABLE(...)
#define API_UNAVAILABLE_BEGIN(...)
#define API_UNAVAILABLE_END
#define SPI_AVAILABLE(...)
#define SPI_DEPRECATED(...)
#define SPI_DEPRECATED_WITH_REPLACEMENT(...)

#endif /* __OS_AVAILABILITY__ */
