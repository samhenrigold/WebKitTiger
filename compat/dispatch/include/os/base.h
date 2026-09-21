/* os/base.h -- attribute macros shared by the libtigerdispatch polyfill headers.
 * Mac OS X 10.4 has no os/ headers at all; this mirrors Apple's macros only as far as
 * WebKit's headers actually need to parse. */
#ifndef __OS_BASE_TIGER__
#define __OS_BASE_TIGER__

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef __has_attribute
#define __has_attribute(x) 0
#endif
#ifndef __has_feature
#define __has_feature(x) 0
#endif
#ifndef __has_extension
#define __has_extension(x) __has_feature(x)
#endif
#ifndef __has_include
#define __has_include(x) 0
#endif

#define OS_EXPORT           extern __attribute__((__visibility__("default")))
#ifndef OS_INLINE
#define OS_INLINE           static __inline__ __attribute__((__always_inline__))
#endif
#define OS_NORETURN         __attribute__((__noreturn__))
#define OS_NOTHROW          __attribute__((__nothrow__))
#define OS_NONNULL1         __attribute__((__nonnull__(1)))
#define OS_NONNULL2         __attribute__((__nonnull__(2)))
#define OS_NONNULL3         __attribute__((__nonnull__(3)))
#define OS_NONNULL4         __attribute__((__nonnull__(4)))
#define OS_NONNULL5         __attribute__((__nonnull__(5)))
#define OS_NONNULL6         __attribute__((__nonnull__(6)))
#define OS_NONNULL7         __attribute__((__nonnull__(7)))
#define OS_NONNULL_ALL      __attribute__((__nonnull__))
#define OS_SENTINEL         __attribute__((__sentinel__))
#define OS_PURE             __attribute__((__pure__))
#define OS_CONST            __attribute__((__const__))
#define OS_WARN_RESULT      __attribute__((__warn_unused_result__))
#define OS_MALLOC           __attribute__((__malloc__))
#define OS_USED             __attribute__((__used__))
#define OS_UNUSED           __attribute__((__unused__))
#define OS_COLD             __attribute__((__cold__))
#define OS_NOT_TAIL_CALLED  __attribute__((__not_tail_called__))
#define OS_FORMAT_PRINTF(a, b) __attribute__((__format__(__printf__, a, b)))
#define OS_SWIFT_UNAVAILABLE(m)
#define OS_NOESCAPE

#define OS_ENUM(_name, _type, ...) \
    typedef _type _name; enum { __VA_ARGS__ }
#define OS_OPTIONS(_name, _type, ...) \
    typedef _type _name; enum { __VA_ARGS__ }
#define OS_CLOSED_ENUM(_name, _type, ...)    OS_ENUM(_name, _type, __VA_ARGS__)
#define OS_CLOSED_OPTIONS(_name, _type, ...) OS_OPTIONS(_name, _type, __VA_ARGS__)

#define OS_ASSUME_NONNULL_BEGIN
#define OS_ASSUME_NONNULL_END

#endif /* __OS_BASE_TIGER__ */
