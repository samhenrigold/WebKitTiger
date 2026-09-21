/*
 * ImageIOBase.h -- Tiger SDK overlay.
 *
 * A 10.8 header. Tiger's ImageIO has CGImageSource, CGImageDestination and
 * CGImageProperties but no ImageIOBase.h, and its headers spell the linkage
 * decoration out longhand instead of going through a macro. WebKit's
 * PAL/pal/spi/cg/ImageIOSPI.h includes this file and uses IMAGEIO_EXTERN, so
 * this is the one macro it has to carry. Apple's copy also defines
 * IMAGEIO_AVAILABLE_* availability macros; the overlay's Availability.h already
 * makes those no-ops, so they are not repeated here.
 */
#ifndef __IMAGEIOBASE__
#define __IMAGEIOBASE__

#include <CoreFoundation/CFBase.h>

#ifndef IMAGEIO_EXTERN
#  ifdef __cplusplus
#    define IMAGEIO_EXTERN extern "C"
#  else
#    define IMAGEIO_EXTERN extern
#  endif
#endif

#ifndef IMAGEIO_EXTERN_C_BEGIN
#  ifdef __cplusplus
#    define IMAGEIO_EXTERN_C_BEGIN extern "C" {
#    define IMAGEIO_EXTERN_C_END   }
#  else
#    define IMAGEIO_EXTERN_C_BEGIN
#    define IMAGEIO_EXTERN_C_END
#  endif
#endif

#endif /* __IMAGEIOBASE__ */
