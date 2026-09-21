/* TIGER: notify_register_dispatch is 10.6+. Declared here, stubbed in
   compat/cfcompat.c: it reports failure, so the notifications WebKit uses it for
   (memory pressure, time zone changes) simply never fire. */
#ifndef __TIGER_NOTIFY_H__
#define __TIGER_NOTIFY_H__

#include_next <notify.h>

#if defined(__has_include)
#if __has_include(<dispatch/dispatch.h>)
#include <dispatch/dispatch.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef void (^notify_handler_t)(int token);
uint32_t notify_register_dispatch(const char *name, int *out_token, dispatch_queue_t queue, notify_handler_t handler);
#ifdef __cplusplus
}
#endif

#endif
#endif

#endif /* __TIGER_NOTIFY_H__ */
