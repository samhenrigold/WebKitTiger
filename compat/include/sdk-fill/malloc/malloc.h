/* TIGER: malloc_zone_memalign (10.6+) and malloc_zone_pressure_relief (10.7+).
   Stubbed in compat/cfcompat.c: memalign goes through posix_memalign on the
   default zone, pressure_relief does nothing (Tiger's malloc has no equivalent). */
#ifndef __TIGER_MALLOC_MALLOC_H__
#define __TIGER_MALLOC_MALLOC_H__
#include_next <malloc/malloc.h>
#ifdef __cplusplus
extern "C" {
#endif
void *malloc_zone_memalign(malloc_zone_t *zone, size_t alignment, size_t size);
size_t malloc_zone_pressure_relief(malloc_zone_t *zone, size_t goal);
#ifdef __cplusplus
}
#endif
#endif
