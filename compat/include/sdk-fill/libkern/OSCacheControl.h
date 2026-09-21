/* TIGER: <libkern/OSCacheControl.h> is a 10.5 SDK header, but the functions it
   declares are in Tiger's libSystem already (sys_icache_invalidate and
   sys_cache_control are both exported; see logs/api/tiger-libSystem.txt). So
   this only supplies the declarations -- the implementations are the real ones.

   sys_dcache_flush is the exception: it is not exported on Tiger, and on i386
   the caches are coherent in hardware, so it is a no-op here. */
#ifndef __TIGER_LIBKERN_OSCACHECONTROL_H__
#define __TIGER_LIBKERN_OSCACHECONTROL_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void sys_icache_invalidate(void *start, size_t len);
int  sys_cache_control(int function, void *start, size_t len);

static __inline__ void sys_dcache_flush(void *__start, size_t __len)
{
    (void)__start; (void)__len;
}

#ifdef __cplusplus
}
#endif

#endif /* __TIGER_LIBKERN_OSCACHECONTROL_H__ */
