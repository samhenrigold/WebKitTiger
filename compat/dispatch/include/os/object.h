/* os/object.h -- Tiger polyfill. Always the non-ObjC (OS_OBJECT_USE_OBJC 0) shape:
 * the 10.4 fragile ObjC runtime has no os_object protocols, and WTF's
 * wtf/darwin/DispatchOSObject.h forward-declares `struct dispatch_queue_s` &co,
 * which only matches the C typedef shape. */
#ifndef __OS_OBJECT_TIGER__
#define __OS_OBJECT_TIGER__

#include <os/base.h>

#define OS_OBJECT_USE_OBJC 0
#define OS_OBJECT_USE_OBJC_RETAIN_RELEASE 0
#define OS_OBJECT_HAVE_OBJC_SUPPORT 0
#define OS_OBJECT_SWIFT3 0
#define OS_OBJECT_BRIDGE
#define OS_OBJECT_RETURNS_RETAINED
#define OS_OBJECT_RETURNS_NOT_RETAINED
#define OS_OBJECT_CONSUMED
#define OS_OBJECT_GLOBAL_OBJECT(type, object) ((OS_OBJECT_BRIDGE type)&(object))

#define OS_OBJECT_CLASS(name) OS_##name

#define OS_OBJECT_DECL_PROTOCOL(name, ...)
#define OS_OBJECT_DECL_IMPL(name, ...) \
        typedef struct name##_s *name##_t
#define OS_OBJECT_DECL(name, ...) \
        typedef struct name##_s *name##_t
#define OS_OBJECT_DECL_SUBCLASS(name, super) \
        typedef struct name##_s *name##_t
#define OS_OBJECT_DECL_CLASS(name) \
        typedef struct name##_s *name##_t
#define OS_OBJECT_DECL_SENDABLE_CLASS(name) OS_OBJECT_DECL_CLASS(name)
#define OS_OBJECT_DECL_SENDABLE_CLASS_AND_SUBCLASS(name, super) \
        OS_OBJECT_DECL_SUBCLASS(name, super)

#ifdef __cplusplus
extern "C" {
#endif

typedef void *os_object_t;

/* Shared refcounting for every os_object/dispatch object this library vends. */
OS_EXPORT void *os_retain(void *object);
OS_EXPORT void os_release(void *object);

#ifdef __cplusplus
}
#endif

#endif /* __OS_OBJECT_TIGER__ */
