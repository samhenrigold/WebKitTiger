/* TIGER SDK OVERLAY: <CoreFoundation/CFError.h> is a 10.5 header, and Tiger's
   CoreFoundation has neither the type nor the API -- CFErrorCreate and friends
   are absent from the binary too (logs/api/tiger-CF.txt).

   Only the opaque type is declared. It exists so that the many out-parameters
   spelled CFErrorRef* in framework SPI and in WebKit compile; every Tiger
   implementation behind them writes NULL. Apple's own guard macro is used, so a
   later real CFError.h would take precedence.

   One thing now needs to DESCRIBE one: FontCacheCoreText.cpp logs the error out
   of CTFontManagerRegisterFontsForURL. Tiger's implementation of that call (the
   ctcompat shim) writes NULL into the out-parameter, so the description is
   fixed text and there is still no CFError object anywhere in the process. If
   something ever needs to CREATE one, the type gets a real CFRuntime class in
   compat/cfcompat.c. */
#ifndef __COREFOUNDATION_CFERROR__
#define __COREFOUNDATION_CFERROR__ 1

#include <CoreFoundation/CFBase.h>

typedef struct __CFError *CFErrorRef;

#ifdef __cplusplus
extern "C" {
#endif
/* Returns a retained string, as Apple's does. */
CFStringRef CFErrorCopyDescription(CFErrorRef);
#ifdef __cplusplus
}
#endif

#endif /* __COREFOUNDATION_CFERROR__ */
