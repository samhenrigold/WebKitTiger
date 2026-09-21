/* dispatch/dispatch.h -- libtigerdispatch: a small GCD polyfill for Mac OS X 10.4.
 *
 * Source compatible with Apple's libdispatch, NOT ABI compatible. Implemented on
 * pthreads + mach_absolute_time + CFRunLoop (for the main queue). Only the surface
 * WebKit's WTF/JSC/bmalloc/WebCore/WebKitLegacy actually use is here; see SURVEY.md.
 */
#ifndef __DISPATCH_TIGER__
#define __DISPATCH_TIGER__

#include <os/base.h>
#include <os/object.h>
#include <sys/qos.h>
#include <sys/types.h>
#include <stdint.h>

#ifndef DISPATCH_H
#define DISPATCH_H
#endif

#define DISPATCH_API_VERSION 20180109
#define DISPATCH_EXPORT             OS_EXPORT
#define DISPATCH_INLINE             OS_INLINE
#define DISPATCH_NOTHROW            OS_NOTHROW
#define DISPATCH_NONNULL1           OS_NONNULL1
#define DISPATCH_NONNULL2           OS_NONNULL2
#define DISPATCH_NONNULL3           OS_NONNULL3
#define DISPATCH_NONNULL4           OS_NONNULL4
#define DISPATCH_NONNULL_ALL        OS_NONNULL_ALL
#define DISPATCH_MALLOC             OS_MALLOC
#define DISPATCH_WARN_RESULT        OS_WARN_RESULT
#define DISPATCH_PURE               OS_PURE
#define DISPATCH_CONST              OS_CONST
#define DISPATCH_NOESCAPE
#define DISPATCH_NOTHROW_NONNULL_ALL DISPATCH_NOTHROW DISPATCH_NONNULL_ALL
#define DISPATCH_RETURNS_RETAINED   OS_OBJECT_RETURNS_RETAINED
#define DISPATCH_DECL(name)         OS_OBJECT_DECL_CLASS(name)
#define DISPATCH_DECL_SUBCLASS(name, base) OS_OBJECT_DECL_CLASS(name)
#define DISPATCH_SOURCE_TYPE_DECL(name) \
        DISPATCH_EXPORT const struct dispatch_source_type_s _dispatch_source_type_##name
#define DISPATCH_SWIFT3_OVERLAY
#define DISPATCH_ASSUME_NONNULL_BEGIN
#define DISPATCH_ASSUME_NONNULL_END

/* ---- object types ------------------------------------------------------- */

DISPATCH_DECL(dispatch_queue);
DISPATCH_DECL(dispatch_queue_global);
DISPATCH_DECL(dispatch_queue_serial);
DISPATCH_DECL(dispatch_queue_concurrent);
DISPATCH_DECL(dispatch_queue_main);
DISPATCH_DECL(dispatch_group);
DISPATCH_DECL(dispatch_semaphore);
DISPATCH_DECL(dispatch_source);
DISPATCH_DECL(dispatch_data);
DISPATCH_DECL(dispatch_io);
DISPATCH_DECL(dispatch_queue_attr);

struct dispatch_object_s;

#ifdef __cplusplus
/* extern "C++" so these still compile when a client includes us from inside its
 * own extern "C" block (WebKit's WTF_EXTERN_C_BEGIN does exactly that): a member
 * template cannot have C linkage, and an empty struct warns under -Wextern-c-compat. */
extern "C++" {

/* The queue "subclasses" have to stay distinct types (WTF specializes traits on
 * each of them) while still converting to dispatch_queue_t. Empty base classes
 * give us both; the library's real layout lives in its own C translation unit. */
struct dispatch_queue_s { };
struct dispatch_queue_global_s : dispatch_queue_s { };
struct dispatch_queue_serial_s : dispatch_queue_s { };
struct dispatch_queue_concurrent_s : dispatch_queue_s { };
struct dispatch_queue_main_s : dispatch_queue_serial_s { };

/* Apple's C++ shape: a converting wrapper so any dispatch_*_t binds to dispatch_object_t. */
typedef struct dispatch_object_t {
    dispatch_object_t(struct dispatch_object_s *o) : _do(o) { }
    template<typename T> dispatch_object_t(T *o) : _do(reinterpret_cast<struct dispatch_object_s *>(o)) { }
    operator struct dispatch_object_s *() const { return _do; }
    struct dispatch_object_s *_do;
} dispatch_object_t;

} /* extern "C++" */
#else
typedef union {
    struct dispatch_object_s *_do;
    dispatch_queue_t _dq;
    dispatch_queue_attr_t _dqa;
    dispatch_group_t _dg;
    dispatch_source_t _ds;
    dispatch_semaphore_t _dsema;
    dispatch_data_t _ddata;
    dispatch_io_t _dchannel;
    void *_object;
} dispatch_object_t __attribute__((__transparent_union__));
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*dispatch_function_t)(void *);
typedef void (^dispatch_block_t)(void);

/* ---- retain/release/context --------------------------------------------- */

DISPATCH_EXPORT void dispatch_retain(dispatch_object_t object);
DISPATCH_EXPORT void dispatch_release(dispatch_object_t object);
DISPATCH_EXPORT void *dispatch_get_context(dispatch_object_t object);
DISPATCH_EXPORT void dispatch_set_context(dispatch_object_t object, void *context);
DISPATCH_EXPORT void dispatch_set_finalizer_f(dispatch_object_t object, dispatch_function_t finalizer);
DISPATCH_EXPORT void dispatch_suspend(dispatch_object_t object);
DISPATCH_EXPORT void dispatch_resume(dispatch_object_t object);
DISPATCH_EXPORT void dispatch_activate(dispatch_object_t object);
DISPATCH_EXPORT void dispatch_set_target_queue(dispatch_object_t object, dispatch_queue_t queue);

/* ---- time --------------------------------------------------------------- */

typedef uint64_t dispatch_time_t;
#define DISPATCH_TIME_NOW     (0ull)
#define DISPATCH_TIME_FOREVER (~0ull)
#define DISPATCH_WALLTIME_NOW (~1ull)
#ifndef NSEC_PER_SEC
#define NSEC_PER_SEC   1000000000ull
#endif
#ifndef NSEC_PER_MSEC
#define NSEC_PER_MSEC  1000000ull
#endif
#ifndef USEC_PER_SEC
#define USEC_PER_SEC   1000000ull
#endif
#ifndef NSEC_PER_USEC
#define NSEC_PER_USEC  1000ull
#endif

DISPATCH_EXPORT dispatch_time_t dispatch_time(dispatch_time_t when, int64_t delta);
DISPATCH_EXPORT dispatch_time_t dispatch_walltime(const struct timespec *when, int64_t delta);

/* ---- queues ------------------------------------------------------------- */

#define DISPATCH_QUEUE_SERIAL     ((dispatch_queue_attr_t)0)
DISPATCH_EXPORT const struct dispatch_queue_attr_s _dispatch_queue_attr_concurrent;
#define DISPATCH_QUEUE_CONCURRENT ((dispatch_queue_attr_t)&_dispatch_queue_attr_concurrent)
DISPATCH_EXPORT const struct dispatch_queue_attr_s _dispatch_queue_attr_serial_arp;
DISPATCH_EXPORT const struct dispatch_queue_attr_s _dispatch_queue_attr_concurrent_arp;
#define DISPATCH_QUEUE_SERIAL_WITH_AUTORELEASE_POOL     ((dispatch_queue_attr_t)&_dispatch_queue_attr_serial_arp)
#define DISPATCH_QUEUE_CONCURRENT_WITH_AUTORELEASE_POOL ((dispatch_queue_attr_t)&_dispatch_queue_attr_concurrent_arp)

#define DISPATCH_TARGET_QUEUE_DEFAULT ((dispatch_queue_t)0)
#define DISPATCH_CURRENT_QUEUE_LABEL  ((dispatch_queue_t)0)

#define DISPATCH_QUEUE_PRIORITY_HIGH        2
#define DISPATCH_QUEUE_PRIORITY_DEFAULT     0
#define DISPATCH_QUEUE_PRIORITY_LOW         (-2)
#define DISPATCH_QUEUE_PRIORITY_BACKGROUND  INT16_MIN

typedef qos_class_t dispatch_qos_class_t;

DISPATCH_EXPORT DISPATCH_RETURNS_RETAINED dispatch_queue_t
dispatch_queue_create(const char *label, dispatch_queue_attr_t attr);
DISPATCH_EXPORT DISPATCH_RETURNS_RETAINED dispatch_queue_t
dispatch_queue_create_with_target(const char *label, dispatch_queue_attr_t attr, dispatch_queue_t target);
DISPATCH_EXPORT dispatch_queue_attr_t
dispatch_queue_attr_make_with_qos_class(dispatch_queue_attr_t attr, dispatch_qos_class_t qos_class, int relative_priority);
DISPATCH_EXPORT dispatch_queue_attr_t
dispatch_queue_attr_make_initially_inactive(dispatch_queue_attr_t attr);

DISPATCH_EXPORT dispatch_queue_main_t dispatch_get_main_queue(void);
DISPATCH_EXPORT dispatch_queue_global_t dispatch_get_global_queue(intptr_t identifier, uintptr_t flags);
DISPATCH_EXPORT dispatch_queue_t dispatch_get_current_queue(void);
DISPATCH_EXPORT const char *dispatch_queue_get_label(dispatch_queue_t queue);

DISPATCH_EXPORT void dispatch_queue_set_specific(dispatch_queue_t queue, const void *key, void *context, dispatch_function_t destructor);
DISPATCH_EXPORT void *dispatch_queue_get_specific(dispatch_queue_t queue, const void *key);
DISPATCH_EXPORT void *dispatch_get_specific(const void *key);

DISPATCH_EXPORT void dispatch_main(void) OS_NORETURN;

/* ---- submission --------------------------------------------------------- */

DISPATCH_EXPORT void dispatch_async(dispatch_queue_t queue, dispatch_block_t block);
DISPATCH_EXPORT void dispatch_async_f(dispatch_queue_t queue, void *context, dispatch_function_t work);
DISPATCH_EXPORT void dispatch_sync(dispatch_queue_t queue, DISPATCH_NOESCAPE dispatch_block_t block);
DISPATCH_EXPORT void dispatch_sync_f(dispatch_queue_t queue, void *context, dispatch_function_t work);
DISPATCH_EXPORT void dispatch_barrier_async(dispatch_queue_t queue, dispatch_block_t block);
DISPATCH_EXPORT void dispatch_barrier_async_f(dispatch_queue_t queue, void *context, dispatch_function_t work);
DISPATCH_EXPORT void dispatch_barrier_sync(dispatch_queue_t queue, DISPATCH_NOESCAPE dispatch_block_t block);
DISPATCH_EXPORT void dispatch_barrier_sync_f(dispatch_queue_t queue, void *context, dispatch_function_t work);
DISPATCH_EXPORT void dispatch_after(dispatch_time_t when, dispatch_queue_t queue, dispatch_block_t block);
DISPATCH_EXPORT void dispatch_after_f(dispatch_time_t when, dispatch_queue_t queue, void *context, dispatch_function_t work);
DISPATCH_EXPORT void dispatch_apply(size_t iterations, dispatch_queue_t queue, DISPATCH_NOESCAPE void (^block)(size_t));
DISPATCH_EXPORT void dispatch_apply_f(size_t iterations, dispatch_queue_t queue, void *context, void (*work)(void *, size_t));

/* dispatch_block_create*: we ignore QoS, so this is just a Block_copy. */
#define DISPATCH_BLOCK_BARRIER              0x1
#define DISPATCH_BLOCK_DETACHED             0x2
#define DISPATCH_BLOCK_ASSIGN_CURRENT       0x4
#define DISPATCH_BLOCK_NO_QOS_CLASS         0x8
#define DISPATCH_BLOCK_INHERIT_QOS_CLASS    0x10
#define DISPATCH_BLOCK_ENFORCE_QOS_CLASS    0x20
typedef unsigned long dispatch_block_flags_t;

DISPATCH_EXPORT DISPATCH_RETURNS_RETAINED dispatch_block_t
dispatch_block_create(dispatch_block_flags_t flags, dispatch_block_t block);
DISPATCH_EXPORT DISPATCH_RETURNS_RETAINED dispatch_block_t
dispatch_block_create_with_qos_class(dispatch_block_flags_t flags, dispatch_qos_class_t qos_class, int relative_priority, dispatch_block_t block);

/* ---- once --------------------------------------------------------------- */

typedef intptr_t dispatch_once_t;
DISPATCH_EXPORT void dispatch_once_f(dispatch_once_t *predicate, void *context, dispatch_function_t function);
DISPATCH_EXPORT void dispatch_once(dispatch_once_t *predicate, DISPATCH_NOESCAPE dispatch_block_t block);

/* ---- groups ------------------------------------------------------------- */

DISPATCH_EXPORT DISPATCH_RETURNS_RETAINED dispatch_group_t dispatch_group_create(void);
DISPATCH_EXPORT void dispatch_group_enter(dispatch_group_t group);
DISPATCH_EXPORT void dispatch_group_leave(dispatch_group_t group);
DISPATCH_EXPORT intptr_t dispatch_group_wait(dispatch_group_t group, dispatch_time_t timeout);
DISPATCH_EXPORT void dispatch_group_notify(dispatch_group_t group, dispatch_queue_t queue, dispatch_block_t block);
DISPATCH_EXPORT void dispatch_group_notify_f(dispatch_group_t group, dispatch_queue_t queue, void *context, dispatch_function_t work);
DISPATCH_EXPORT void dispatch_group_async(dispatch_group_t group, dispatch_queue_t queue, dispatch_block_t block);
DISPATCH_EXPORT void dispatch_group_async_f(dispatch_group_t group, dispatch_queue_t queue, void *context, dispatch_function_t work);

/* ---- semaphores --------------------------------------------------------- */

DISPATCH_EXPORT DISPATCH_RETURNS_RETAINED dispatch_semaphore_t dispatch_semaphore_create(intptr_t value);
DISPATCH_EXPORT intptr_t dispatch_semaphore_wait(dispatch_semaphore_t dsema, dispatch_time_t timeout);
DISPATCH_EXPORT intptr_t dispatch_semaphore_signal(dispatch_semaphore_t dsema);

/* ---- sources ------------------------------------------------------------ */

struct dispatch_source_type_s;
typedef const struct dispatch_source_type_s *dispatch_source_type_t;

DISPATCH_SOURCE_TYPE_DECL(timer);
DISPATCH_SOURCE_TYPE_DECL(memorypressure);
DISPATCH_SOURCE_TYPE_DECL(vm);
DISPATCH_SOURCE_TYPE_DECL(vnode);
DISPATCH_SOURCE_TYPE_DECL(mach_recv);
DISPATCH_SOURCE_TYPE_DECL(mach_send);
DISPATCH_SOURCE_TYPE_DECL(read);
DISPATCH_SOURCE_TYPE_DECL(write);
DISPATCH_SOURCE_TYPE_DECL(signal);
DISPATCH_SOURCE_TYPE_DECL(proc);
DISPATCH_SOURCE_TYPE_DECL(data_add);
DISPATCH_SOURCE_TYPE_DECL(data_or);
DISPATCH_SOURCE_TYPE_DECL(data_replace);

#define DISPATCH_SOURCE_TYPE_TIMER           (&_dispatch_source_type_timer)
#define DISPATCH_SOURCE_TYPE_MEMORYPRESSURE  (&_dispatch_source_type_memorypressure)
#define DISPATCH_SOURCE_TYPE_VM              (&_dispatch_source_type_vm)
#define DISPATCH_SOURCE_TYPE_VNODE           (&_dispatch_source_type_vnode)
#define DISPATCH_SOURCE_TYPE_MACH_RECV       (&_dispatch_source_type_mach_recv)
#define DISPATCH_SOURCE_TYPE_MACH_SEND       (&_dispatch_source_type_mach_send)
#define DISPATCH_SOURCE_TYPE_READ            (&_dispatch_source_type_read)
#define DISPATCH_SOURCE_TYPE_WRITE           (&_dispatch_source_type_write)
#define DISPATCH_SOURCE_TYPE_SIGNAL          (&_dispatch_source_type_signal)
#define DISPATCH_SOURCE_TYPE_PROC            (&_dispatch_source_type_proc)
#define DISPATCH_SOURCE_TYPE_DATA_ADD        (&_dispatch_source_type_data_add)
#define DISPATCH_SOURCE_TYPE_DATA_OR         (&_dispatch_source_type_data_or)
#define DISPATCH_SOURCE_TYPE_DATA_REPLACE    (&_dispatch_source_type_data_replace)

enum {
    DISPATCH_MEMORYPRESSURE_NORMAL = 0x01,
    DISPATCH_MEMORYPRESSURE_WARN = 0x02,
    DISPATCH_MEMORYPRESSURE_CRITICAL = 0x04,
};
enum {
    DISPATCH_VNODE_DELETE = 0x1,
    DISPATCH_VNODE_WRITE = 0x2,
    DISPATCH_VNODE_EXTEND = 0x4,
    DISPATCH_VNODE_ATTRIB = 0x8,
    DISPATCH_VNODE_LINK = 0x10,
    DISPATCH_VNODE_RENAME = 0x20,
    DISPATCH_VNODE_REVOKE = 0x40,
    DISPATCH_VNODE_FUNLOCK = 0x100,
};
enum {
    DISPATCH_MACH_SEND_DEAD = 0x1,
    DISPATCH_MACH_SEND_POSSIBLE = 0x8,
};
enum {
    DISPATCH_PROC_EXIT = 0x80000000,
    DISPATCH_PROC_FORK = 0x40000000,
    DISPATCH_PROC_EXEC = 0x20000000,
    DISPATCH_PROC_SIGNAL = 0x08000000,
};
#define DISPATCH_TIMER_STRICT 0x1

DISPATCH_EXPORT DISPATCH_RETURNS_RETAINED dispatch_source_t
dispatch_source_create(dispatch_source_type_t type, uintptr_t handle, uintptr_t mask, dispatch_queue_t queue);
DISPATCH_EXPORT void dispatch_source_set_event_handler(dispatch_source_t source, dispatch_block_t handler);
DISPATCH_EXPORT void dispatch_source_set_event_handler_f(dispatch_source_t source, dispatch_function_t handler);
DISPATCH_EXPORT void dispatch_source_set_cancel_handler(dispatch_source_t source, dispatch_block_t handler);
DISPATCH_EXPORT void dispatch_source_set_cancel_handler_f(dispatch_source_t source, dispatch_function_t handler);
DISPATCH_EXPORT void dispatch_source_set_registration_handler(dispatch_source_t source, dispatch_block_t handler);
DISPATCH_EXPORT void dispatch_source_cancel(dispatch_source_t source);
DISPATCH_EXPORT intptr_t dispatch_source_testcancel(dispatch_source_t source);
DISPATCH_EXPORT uintptr_t dispatch_source_get_handle(dispatch_source_t source);
DISPATCH_EXPORT uintptr_t dispatch_source_get_mask(dispatch_source_t source);
DISPATCH_EXPORT uintptr_t dispatch_source_get_data(dispatch_source_t source);
DISPATCH_EXPORT void dispatch_source_merge_data(dispatch_source_t source, uintptr_t value);
DISPATCH_EXPORT void dispatch_source_set_timer(dispatch_source_t source, dispatch_time_t start, uint64_t interval, uint64_t leeway);

/* ---- data --------------------------------------------------------------- */

DISPATCH_EXPORT const struct dispatch_data_s _dispatch_data_empty;
#define dispatch_data_empty ((dispatch_data_t)&_dispatch_data_empty)
DISPATCH_EXPORT const dispatch_block_t _dispatch_data_destructor_free;
#define DISPATCH_DATA_DESTRUCTOR_DEFAULT ((dispatch_block_t)0)
#define DISPATCH_DATA_DESTRUCTOR_FREE    (_dispatch_data_destructor_free)

DISPATCH_EXPORT DISPATCH_RETURNS_RETAINED dispatch_data_t
dispatch_data_create(const void *buffer, size_t size, dispatch_queue_t queue, dispatch_block_t destructor);
DISPATCH_EXPORT size_t dispatch_data_get_size(dispatch_data_t data);
DISPATCH_EXPORT bool dispatch_data_apply(dispatch_data_t data,
    DISPATCH_NOESCAPE bool (^applier)(dispatch_data_t region, size_t offset, const void *buffer, size_t size));
DISPATCH_EXPORT DISPATCH_RETURNS_RETAINED dispatch_data_t
dispatch_data_create_map(dispatch_data_t data, const void **buffer_ptr, size_t *size_ptr);
DISPATCH_EXPORT DISPATCH_RETURNS_RETAINED dispatch_data_t
dispatch_data_create_concat(dispatch_data_t data1, dispatch_data_t data2);
DISPATCH_EXPORT DISPATCH_RETURNS_RETAINED dispatch_data_t
dispatch_data_create_subrange(dispatch_data_t data, size_t offset, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* __DISPATCH_TIGER__ */
