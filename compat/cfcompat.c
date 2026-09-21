/* TIGER: libSystem entry points Mac OS X 10.4 lacks that need a real
   implementation rather than a header declaration. Owned by the WebKit CMake
   track; declarations live in compat/include/sdk-fill/.

   Built into libtigercompat.a by compat/Makefile (which globs *.c). */

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- <execinfo.h>, 10.5+ ------------------------------------------------ */

/* i386 frame layout: [saved ebp][return address]. Walk the chain, stopping on a
   null or non-ascending frame pointer, which is what a leaf frame compiled with
   -fomit-frame-pointer or the outermost frame looks like. Diagnostics only. */
int backtrace(void **array, int size)
{
    void **frame = (void **)__builtin_frame_address(0);
    int count = 0;

    while (count < size && frame) {
        void **next = (void **)frame[0];
        void *ret = frame[1];
        if (!ret)
            break;
        array[count++] = ret;
        /* The chain must climb, and by a sane amount, or we are off the rails. */
        if (next <= frame || (char *)next - (char *)frame > (1 << 20))
            break;
        frame = next;
    }
    return count;
}

char **backtrace_symbols(void *const *array, int size)
{
    /* One allocation: the char* table followed by the strings, matching the
       contract that the caller frees the result with a single free(). */
    const size_t maxLine = 512;
    size_t bytes = (size_t)size * (sizeof(char *) + maxLine);
    char **result = (char **)malloc(bytes);
    char *strings;
    int i;

    if (!result)
        return NULL;
    strings = (char *)(result + size);

    for (i = 0; i < size; i++) {
        Dl_info info;
        char *line = strings + (size_t)i * maxLine;
        if (dladdr(array[i], &info) && info.dli_sname) {
            const char *image = info.dli_fname ? strrchr(info.dli_fname, '/') : NULL;
            image = image ? image + 1 : (info.dli_fname ? info.dli_fname : "???");
            snprintf(line, maxLine, "%-3d %-35s 0x%08lx %s + %ld", i, image,
                (unsigned long)array[i], info.dli_sname,
                (long)((char *)array[i] - (char *)info.dli_saddr));
        } else
            snprintf(line, maxLine, "%-3d %-35s 0x%08lx", i, "???", (unsigned long)array[i]);
        result[i] = line;
    }
    return result;
}

void backtrace_symbols_fd(void *const *array, int size, int fd)
{
    char **symbols = backtrace_symbols(array, size);
    int i;
    if (!symbols)
        return;
    for (i = 0; i < size; i++) {
        write(fd, symbols[i], strlen(symbols[i]));
        write(fd, "\n", 1);
    }
    free(symbols);
}

/* TIGER64: Mac OS X 10.4's x86_64 userland is libSystem only, so there is no
   libdispatch and no CoreFoundation for that architecture. The <execinfo.h>,
   <malloc/malloc.h>, dyld and libcache sections below are plain libSystem and
   build for both; notify_register_dispatch and the CoreFoundation section are
   compiled for i386 only. See logs/jsc64-spike.md. */
#ifndef __LP64__

/* ---- <notify.h>, notify_register_dispatch is 10.6+ ---------------------- */

#include <notify.h>

/* Stub: reports failure without registering. The callers (memory pressure,
   time zone change) treat a non-zero status as "no notifications available". */
uint32_t notify_register_dispatch(const char *name, int *out_token,
    dispatch_queue_t queue, notify_handler_t handler)
{
    (void)name; (void)queue; (void)handler;
    if (out_token)
        *out_token = -1;
    return NOTIFY_STATUS_FAILED;
}

#endif /* !__LP64__ */

/* ---- <malloc/malloc.h>, 10.6+ / 10.7+ ---------------------------------- */

#include <malloc/malloc.h>

/* Only correct for the default zone, which is the only zone Tiger callers of
   this shim use (see bmalloc's SystemHeap, which forces the default zone). */
void *malloc_zone_memalign(malloc_zone_t *zone, size_t alignment, size_t size)
{
    void *p = 0;
    (void)zone;
    if (posix_memalign(&p, alignment, size) != 0)
        return 0;
    return p;
}

/* Tiger's malloc has no pressure-relief entry point; report nothing released. */
size_t malloc_zone_pressure_relief(malloc_zone_t *zone, size_t goal)
{
    (void)zone; (void)goal;
    return 0;
}

/* ---- dyld introspection added after 10.4 -------------------------------- */

#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <uuid/uuid.h>

/* dyld_image_header_containing_address, 10.6. dladdr already answers this:
   dli_fbase is the Mach-O header of the image the address falls in. */
const struct mach_header *dyld_image_header_containing_address(const void *addr)
{
    Dl_info info;
    if (!dladdr(addr, &info))
        return 0;
    return (const struct mach_header *)info.dli_fbase;
}

/* _dyld_get_image_uuid, 10.6. LC_UUID predates Tiger, so this can be answered
   for real by walking the load commands. */
int _dyld_get_image_uuid(const struct mach_header *mh, uuid_t uuid)
{
    const struct load_command *lc;
    uint32_t i;

    if (!mh)
        return 0;
#ifdef __LP64__
    /* TIGER64: the caller passes a mach_header_64 through this mach_header* -- that
       is how Apple's own prototype is spelled -- and it is 4 bytes longer. */
    lc = (const struct load_command *)((const struct mach_header_64 *)mh + 1);
#else
    lc = (const struct load_command *)(mh + 1);
#endif
    for (i = 0; i < mh->ncmds; i++) {
        if (lc->cmd == LC_UUID) {
            memcpy(uuid, ((const struct uuid_command *)lc)->uuid, sizeof(uuid_t));
            return 1;
        }
        lc = (const struct load_command *)((uintptr_t)lc + lc->cmdsize);
    }
    return 0;
}

/* Tiger has no dyld shared cache: every library is mapped from its own file. */
int _dyld_get_shared_cache_uuid(uuid_t uuid)
{
    memset(uuid, 0, sizeof(uuid_t));
    return 0;
}

const char *dyld_shared_cache_file_path(void)
{
    return "";
}

/* _dyld_get_dlopen_image_header, 10.14. Tiger's dyld exposes no mapping from a
   dlopen handle back to its header, so callers get nothing. */
const struct mach_header *_dyld_get_dlopen_image_header(void *handle)
{
    (void)handle;
    return 0;
}

/* dyld_get_program_sdk_version, 10.10, in dyld's packed form: 10.4.0. */
uint32_t dyld_get_program_sdk_version(void)
{
    return 0x000A0400;
}

/* ---- libcache, 10.5+ ---------------------------------------------------- */

/* Tiger has no unified cache layer to notify, and no memory-pressure events to
   simulate. WebKit calls this to shed caches under pressure; doing nothing
   simply means nothing extra is dropped. */
void cache_simulate_memory_warning_event(uint64_t level)
{
    (void)level;
}

/* ---- CoreFoundation, <TigerCompat/CFCompat.h> --------------------------- */

#ifndef __LP64__

#include <CoreFoundation/CoreFoundation.h>

/* CFLocaleCopyPreferredLanguages, 10.5+. CF-550's implementation reads the
   AppleLanguages preference and returns it as an array of CFStrings; that is
   done here through CFPreferences so this stays a C file. Returns an empty
   array rather than NULL when the preference is missing, which is what the real
   one does on a machine with no language list. */
CFArrayRef CFLocaleCopyPreferredLanguages(void)
{
    CFPropertyListRef value = CFPreferencesCopyAppValue(CFSTR("AppleLanguages"),
        kCFPreferencesCurrentApplication);

    if (value) {
        if (CFGetTypeID(value) == CFArrayGetTypeID())
            return (CFArrayRef)value;
        CFRelease(value);
    }
    return CFArrayCreate(kCFAllocatorDefault, NULL, 0, &kCFTypeArrayCallBacks);
}

/* aligned_alloc lives in compat/include/tigerprelude.h as an inline over
   posix_memalign, which compat/libcompat.c implements for real: malloc at or
   below 16 bytes of alignment, valloc up to a page, and above that an mmap'd
   region registered as a malloc zone so plain free() still finds it. An earlier
   version of this file over-allocated and returned an interior pointer, which
   needed its own free; that is gone now that the platform does it properly. */

#endif /* !__LP64__ */
