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

/* =================================================================== CFError */

/* Declared by the SDK overlay's CFError.h. Tiger's CoreFoundation has no
   CFError at all, so every CFErrorRef out-parameter in this port is written
   NULL by its shim; this exists so the one caller that logs a description
   (FontCacheCoreText.cpp, after a failed font registration) links and prints
   something true. If a real CFError ever gets created on this system, this is
   the function that has to learn to read it. */
CFStringRef CFErrorCopyDescription(CFErrorRef error)
{
    if (!error)
        return CFRetain(CFSTR("no error information (Mac OS X 10.4 has no CFError)"));
    return CFRetain(CFSTR("unknown CFError"));
}

/* ========================================================== property lists */

CFPropertyListRef CFPropertyListCreateWithData(CFAllocatorRef allocator, CFDataRef data,
    CFOptionFlags options, CFPropertyListFormat* format, CFErrorRef* error)
{
    CFStringRef errorString = NULL;
    CFPropertyListRef plist;

    if (error)
        *error = NULL;
    plist = CFPropertyListCreateFromXMLData(allocator, data, options, &errorString);
    if (errorString)
        CFRelease(errorString);
    /* Tiger's reader does not report which format it found. Binary is what
       WebKit writes and XML is what it reads from the outside world; callers
       that pass a format pointer only log it. */
    if (format)
        *format = kCFPropertyListXMLFormat_v1_0;
    return plist;
}

CFIndex CFPropertyListWrite(CFPropertyListRef plist, CFWriteStreamRef stream,
    CFPropertyListFormat format, CFOptionFlags options, CFErrorRef* error)
{
    CFStringRef errorString = NULL;
    CFIndex written;

    (void)options; /* 10.6 added no options that Tiger's writer understands. */
    if (error)
        *error = NULL;
    written = CFPropertyListWriteToStream(plist, stream, format, &errorString);
    if (errorString)
        CFRelease(errorString);
    return written;
}

CFDataRef CFPropertyListCreateData(CFAllocatorRef allocator, CFPropertyListRef plist,
    CFPropertyListFormat format, CFOptionFlags options, CFErrorRef* error)
{
    CFWriteStreamRef stream;
    CFDataRef data;

    (void)options;
    if (error)
        *error = NULL;

    if (format == kCFPropertyListXMLFormat_v1_0)
        return CFPropertyListCreateXMLData(allocator, plist);

    /* Binary (and OpenStep) have no Create...Data on Tiger. An allocated-buffer
       write stream is how CFPropertyListWriteToStream is meant to be used for
       an in-memory result; the data comes back out as the stream's
       kCFStreamPropertyDataWritten, which is exactly what LegacyWebArchive.cpp
       does by hand a few lines further on for the same reason. */
    stream = CFWriteStreamCreateWithAllocatedBuffers(allocator, NULL);
    if (!stream)
        return NULL;
    CFWriteStreamOpen(stream);
    if (!CFPropertyListWriteToStream(plist, stream, format, NULL)) {
        CFWriteStreamClose(stream);
        CFRelease(stream);
        return NULL;
    }
    data = (CFDataRef)CFWriteStreamCopyProperty(stream, kCFStreamPropertyDataWritten);
    CFWriteStreamClose(stream);
    CFRelease(stream);
    return data;
}

/* kCFLocaleCurrentLocaleDidChangeNotification is 10.5. The name is Apple's, but
   nothing on Tiger ever posts it: there is no locale-change notification on
   this system, so an observer registered for it simply never fires, which is
   the machine's real behaviour rather than a simplification. */
const CFStringRef kCFLocaleCurrentLocaleDidChangeNotification =
    (const CFStringRef)CFSTR("kCFLocaleCurrentLocaleDidChangeNotification");

/* aligned_alloc lives in compat/include/tigerprelude.h as an inline over
   posix_memalign, which compat/libcompat.c implements for real: malloc at or
   below 16 bytes of alignment, valloc up to a page, and above that an mmap'd
   region registered as a malloc zone so plain free() still finds it. An earlier
   version of this file over-allocated and returned an interior pointer, which
   needed its own free; that is gone now that the platform does it properly. */

#endif /* !__LP64__ */
