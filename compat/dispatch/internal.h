/* Shared object header for libtigerdispatch. Every object we vend starts with
 * struct dispatch_object_s so os_retain/os_release/dispatch_retain/dispatch_release
 * can all funnel through one refcount. */
#ifndef TIGERDISPATCH_INTERNAL_H
#define TIGERDISPATCH_INTERNAL_H

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

enum td_kind {
    TD_KIND_QUEUE = 1,
    TD_KIND_GROUP,
    TD_KIND_SEMAPHORE,
    TD_KIND_SOURCE,
    TD_KIND_DATA,
    TD_KIND_LOG,
};

struct dispatch_object_s {
    volatile int32_t td_refcount;
    uint8_t td_kind;
    uint8_t td_static;          /* never freed (global queues, OS_LOG_DEFAULT) */
    void *td_context;
    void (*td_finalizer)(void *);
};

void td_destroy(struct dispatch_object_s *o);   /* dispatch.c */
void td_log_destroy(struct dispatch_object_s *o); /* os.c */

static inline void *td_retain_obj(void *p)
{
    struct dispatch_object_s *o = (struct dispatch_object_s *)p;
    if (o)
        __sync_fetch_and_add(&o->td_refcount, 1);
    return p;
}

static inline void td_release_obj(void *p)
{
    struct dispatch_object_s *o = (struct dispatch_object_s *)p;
    if (o && __sync_sub_and_fetch(&o->td_refcount, 1) == 0)
        td_destroy(o);
}

#endif
