/* TIGER: getsegmentdata() is 10.6+, and Tiger does not even have
   getsegbynamefromheader() to build it from, so the load commands are walked
   here directly. Same contract as Apple's: a segment that occupies no file
   bytes (__PAGEZERO) reports its vmsize and returns NULL. */
#ifndef __TIGER_MACH_O_GETSECT_H__
#define __TIGER_MACH_O_GETSECT_H__

#include_next <mach-o/getsect.h>
#include <mach-o/loader.h>
#include <string.h>
#include <stdint.h>

/* TIGER64: on LP64 the header is a mach_header_64 and the load commands are
   LC_SEGMENT_64 / segment_command_64. Apple's getsegmentdata() takes whichever
   header type matches the architecture, so only one of the two is ever declared. */
#ifdef __LP64__

static __inline__ uint8_t *getsegmentdata(const struct mach_header_64 *__mhp,
    const char *__segname, unsigned long *__size)
{
    const struct load_command *__lc = (const struct load_command *)(__mhp + 1);
    uint32_t __i;

    *__size = 0;
    for (__i = 0; __i < __mhp->ncmds; __i++) {
        if (__lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *__sc = (const struct segment_command_64 *)__lc;
            if (!strncmp(__sc->segname, __segname, sizeof(__sc->segname))) {
                *__size = (unsigned long)__sc->vmsize;
                if (!__sc->fileoff && !__sc->filesize)
                    return 0;   /* __PAGEZERO: reserved address space, no data */
                return (uint8_t *)((uintptr_t)__mhp + (uintptr_t)__sc->fileoff);
            }
        }
        __lc = (const struct load_command *)((uintptr_t)__lc + __lc->cmdsize);
    }
    return 0;
}

#else

static __inline__ uint8_t *getsegmentdata(const struct mach_header *__mhp,
    const char *__segname, unsigned long *__size)
{
    const struct load_command *__lc = (const struct load_command *)(__mhp + 1);
    uint32_t __i;

    *__size = 0;
    for (__i = 0; __i < __mhp->ncmds; __i++) {
        if (__lc->cmd == LC_SEGMENT) {
            const struct segment_command *__sc = (const struct segment_command *)__lc;
            if (!strncmp(__sc->segname, __segname, sizeof(__sc->segname))) {
                *__size = __sc->vmsize;
                if (!__sc->fileoff && !__sc->filesize)
                    return 0;   /* __PAGEZERO: reserved address space, no data */
                return (uint8_t *)((uintptr_t)__mhp + (uintptr_t)__sc->fileoff);
            }
        }
        __lc = (const struct load_command *)((uintptr_t)__lc + __lc->cmdsize);
    }
    return 0;
}

#endif /* __LP64__ */

#endif /* __TIGER_MACH_O_GETSECT_H__ */
