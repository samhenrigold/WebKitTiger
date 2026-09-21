/* TIGER SDK OVERLAY: <CoreFoundation/CFError.h> is a 10.5 header, and Tiger's
   CoreFoundation has neither the type nor the API -- CFErrorCreate and friends
   are absent from the binary too (logs/api/tiger-CF.txt).

   Only the opaque type is declared. It exists so that the many out-parameters
   spelled CFErrorRef* in framework SPI and in WebKit compile; every Tiger
   implementation behind them writes NULL. Apple's own guard macro is used, so a
   later real CFError.h would take precedence.

   If something ever needs to create or describe an error, the type gets a real
   CFRuntime class in compat/cfcompat.c; nothing does yet. */
#ifndef __COREFOUNDATION_CFERROR__
#define __COREFOUNDATION_CFERROR__ 1

#include <CoreFoundation/CFBase.h>

typedef struct __CFError *CFErrorRef;

#endif /* __COREFOUNDATION_CFERROR__ */
