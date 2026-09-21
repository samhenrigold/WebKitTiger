/* os/lock.h -- Tiger polyfill for os_unfair_lock.
 * ponytail: spin + sched_yield, no kernel wait. Fine for the short critical
 * sections WTF::UnfairLock / libpas use; if a lock is ever held across a page
 * fault this burns a quantum. Upgrade path: pthread_mutex in the slow path. */
#ifndef __OS_LOCK_TIGER__
#define __OS_LOCK_TIGER__

#include <os/base.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OS_LOCK_API_VERSION 20160309

/* Value is the owning thread's mach port name, or 0 when unlocked. */
typedef struct os_unfair_lock_s {
    uint32_t _os_unfair_lock_opaque;
} os_unfair_lock, *os_unfair_lock_t;

#if defined(__cplusplus) && __cplusplus >= 201103L
#define OS_UNFAIR_LOCK_INIT (os_unfair_lock { })
#elif defined(__cplusplus)
#define OS_UNFAIR_LOCK_INIT (os_unfair_lock())
#else
#define OS_UNFAIR_LOCK_INIT ((os_unfair_lock){ 0 })
#endif

typedef uint32_t os_unfair_lock_options_t;
enum {
    OS_UNFAIR_LOCK_NONE = 0x00000000,
    OS_UNFAIR_LOCK_DATA_SYNCHRONIZATION = 0x00010000,
    OS_UNFAIR_LOCK_ADAPTIVE_SPIN = 0x00040000,
};

OS_EXPORT OS_NOTHROW OS_NONNULL_ALL void os_unfair_lock_lock(os_unfair_lock_t lock);
OS_EXPORT OS_NOTHROW OS_NONNULL_ALL bool os_unfair_lock_trylock(os_unfair_lock_t lock);
OS_EXPORT OS_NOTHROW OS_NONNULL_ALL void os_unfair_lock_unlock(os_unfair_lock_t lock);
OS_EXPORT OS_NOTHROW OS_NONNULL_ALL void os_unfair_lock_lock_with_options(os_unfair_lock_t lock, os_unfair_lock_options_t options);
OS_EXPORT OS_NOTHROW OS_NONNULL_ALL void os_unfair_lock_assert_owner(const os_unfair_lock *lock);
OS_EXPORT OS_NOTHROW OS_NONNULL_ALL void os_unfair_lock_assert_not_owner(const os_unfair_lock *lock);

#ifdef __cplusplus
}
#endif

#endif /* __OS_LOCK_TIGER__ */
