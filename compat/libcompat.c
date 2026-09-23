/* libSystem gaps on Mac OS X 10.4, implemented on what Tiger has. */
#include "include/tigerprelude.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/time.h>
#include <mach/mach.h>
#include <mach/mach_time.h>

/* ---------------------------------------------------------------- posix_memalign ---
 *
 * Tiger has no posix_memalign and no malloc_zone_memalign (10.6). malloc gives 16-byte
 * alignment (the tiny-region quantum) and valloc gives page alignment, which covers
 * everything up to 4096 bytes. Above that there is nothing to reuse, so this allocates
 * pages directly and registers a malloc zone so that plain free() still works.
 *
 * The zone is how free() finds us. Libc-391.5.22 gen/malloc.c free() calls
 * find_registered_zone(), which walks malloc_zones in registration order calling
 * zone->size(zone, ptr) and hands the pointer to the first zone that claims it.
 * malloc_zone_register() appends, so the default scalable zone stays malloc_zones[0] and
 * our size() is only consulted for pointers it has already rejected. malloc_size() and
 * realloc() use the same lookup, so both work on these blocks for free.
 */
#include <malloc/malloc.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

struct tiger_aligned_block { void *ptr; size_t len; size_t align; };

static pthread_mutex_t alignedLock = PTHREAD_MUTEX_INITIALIZER;
static struct tiger_aligned_block *alignedBlocks;   /* sorted ascending by ptr */
static size_t alignedCount, alignedCapacity;
/* Bounds of everything we have ever handed out. size() has to be fast for negative
   answers, and a pointer outside this window is certainly not ours, so the common case
   never takes the lock. Only ever widened, so a stale read can say "maybe mine" (we then
   take the lock and search) but never "not mine" about a live block of ours. */
static uintptr_t alignedLo = (uintptr_t)-1, alignedHi;

/* Index of the first block whose ptr is >= key. Call with alignedLock held. */
static size_t tigerAlignedLowerBound(const void *key)
{
    size_t lo = 0, hi = alignedCount;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if ((uintptr_t)alignedBlocks[mid].ptr < (uintptr_t)key) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

/* Call with alignedLock held. Returns NULL unless ptr is the start of one of our blocks. */
static struct tiger_aligned_block *tigerAlignedFind(const void *ptr)
{
    size_t i = tigerAlignedLowerBound(ptr);
    if (i < alignedCount && alignedBlocks[i].ptr == ptr) return &alignedBlocks[i];
    return NULL;
}

/* Call with alignedLock held. The table goes through malloc_zone_* on the default zone
   rather than plain realloc() on purpose: realloc() would call find_registered_zone(),
   which calls every zone's size() including ours, which wants this same lock. Naming the
   zone skips that lookup entirely and makes the reentrancy impossible rather than merely
   unlikely. */
static int tigerAlignedInsert(void *ptr, size_t len, size_t align)
{
    if (alignedCount == alignedCapacity) {
        size_t cap = alignedCapacity ? alignedCapacity * 2 : 16;
        malloc_zone_t *dz = malloc_default_zone();
        struct tiger_aligned_block *nb = alignedBlocks
            ? malloc_zone_realloc(dz, alignedBlocks, cap * sizeof(*nb))
            : malloc_zone_malloc(dz, cap * sizeof(*nb));
        if (!nb) return 0;
        alignedBlocks = nb;
        alignedCapacity = cap;
    }
    size_t i = tigerAlignedLowerBound(ptr);
    memmove(&alignedBlocks[i + 1], &alignedBlocks[i], (alignedCount - i) * sizeof(*alignedBlocks));
    alignedBlocks[i].ptr = ptr;
    alignedBlocks[i].len = len;
    alignedBlocks[i].align = align;
    ++alignedCount;
    if ((uintptr_t)ptr < alignedLo) alignedLo = (uintptr_t)ptr;
    if ((uintptr_t)ptr + len > alignedHi) alignedHi = (uintptr_t)ptr + len;
    return 1;
}

/* mmap `size` bytes at an `align`-aligned address, trimming the slack back off so exactly
   the aligned region stays mapped. Returns NULL on failure. */
static void *tigerAlignedMap(size_t align, size_t size)
{
    size_t page = (size_t)getpagesize();
    size_t len = (size + page - 1) & ~(page - 1);
    if (!len) len = page;                       /* size 0 still gets a unique page */
    if (len > (size_t)-1 - align) return NULL;  /* len + align would wrap */

    char *base = mmap(NULL, len + align, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (base == MAP_FAILED) return NULL;

    char *aligned = (char *)(((uintptr_t)base + align - 1) & ~(uintptr_t)(align - 1));
    size_t head = (size_t)(aligned - base);
    size_t tail = align - head;                 /* (len + align) - head - len */
    if (head) munmap(base, head);
    if (tail) munmap(aligned + len, tail);

    pthread_mutex_lock(&alignedLock);
    int ok = tigerAlignedInsert(aligned, len, align);
    pthread_mutex_unlock(&alignedLock);
    if (!ok) { munmap(aligned, len); return NULL; }
    return aligned;
}

/* ---- the zone ---- */

static size_t tigerZoneSize(malloc_zone_t *zone, const void *ptr)
{
    (void)zone;
    uintptr_t p = (uintptr_t)ptr;
    if (p < alignedLo || p >= alignedHi) return 0;   /* the fast negative answer */
    pthread_mutex_lock(&alignedLock);
    struct tiger_aligned_block *b = tigerAlignedFind(ptr);
    size_t len = b ? b->len : 0;
    pthread_mutex_unlock(&alignedLock);
    return len;
}

static void tigerZoneFree(malloc_zone_t *zone, void *ptr)
{
    (void)zone;
    if (!ptr) return;
    pthread_mutex_lock(&alignedLock);
    struct tiger_aligned_block *b = tigerAlignedFind(ptr);
    size_t len = 0;
    if (b) {
        len = b->len;
        size_t i = (size_t)(b - alignedBlocks);
        memmove(&alignedBlocks[i], &alignedBlocks[i + 1], (alignedCount - i - 1) * sizeof(*alignedBlocks));
        --alignedCount;
    }
    pthread_mutex_unlock(&alignedLock);
    if (len) munmap(ptr, len);
}

static void *tigerZoneMalloc(malloc_zone_t *zone, size_t size)
{
    (void)zone;
    return tigerAlignedMap((size_t)getpagesize(), size);
}

static void *tigerZoneCalloc(malloc_zone_t *zone, size_t n, size_t size)
{
    (void)zone;
    if (n && size > (size_t)-1 / n) return NULL;
    return tigerAlignedMap((size_t)getpagesize(), n * size);   /* MAP_ANON is already zeroed */
}

static void *tigerZoneValloc(malloc_zone_t *zone, size_t size) { return tigerZoneMalloc(zone, size); }

/* Libc's realloc() short-circuits a shrink itself (it returns old_ptr when old_size >=
   new_size), so this is only reached to grow. Keep the caller's alignment. */
static void *tigerZoneRealloc(malloc_zone_t *zone, void *ptr, size_t size)
{
    (void)zone;
    if (!ptr) return tigerZoneMalloc(zone, size);
    pthread_mutex_lock(&alignedLock);
    struct tiger_aligned_block *b = tigerAlignedFind(ptr);
    size_t oldLen = b ? b->len : 0, align = b ? b->align : 0;
    pthread_mutex_unlock(&alignedLock);
    if (!oldLen) return NULL;                   /* not ours; Libc never gets here */
    if (size <= oldLen) return ptr;
    void *fresh = tigerAlignedMap(align, size);
    if (!fresh) return NULL;
    memcpy(fresh, ptr, oldLen);
    tigerZoneFree(zone, ptr);
    return fresh;
}

static void tigerZoneDestroy(malloc_zone_t *zone) { (void)zone; }   /* never unregistered */

static kern_return_t tigerZoneEnumerator(task_t task, void *ctx, unsigned type_mask,
                                         vm_address_t zone_address, memory_reader_t reader,
                                         vm_range_recorder_t recorder)
{
    (void)task; (void)ctx; (void)type_mask; (void)zone_address; (void)reader; (void)recorder;
    return KERN_SUCCESS;    /* reports no pointers rather than lying about them */
}
static size_t tigerZoneGoodSize(malloc_zone_t *zone, size_t size)
{
    (void)zone;
    size_t page = (size_t)getpagesize();
    return (size + page - 1) & ~(page - 1);
}
static boolean_t tigerZoneCheck(malloc_zone_t *zone) { (void)zone; return 1; }
static void tigerZonePrint(malloc_zone_t *zone, boolean_t verbose)
{
    (void)verbose;
    malloc_printf("%s: %d over-aligned block(s)\n", zone->zone_name, (int)alignedCount);
}
static void tigerZoneLog(malloc_zone_t *zone, void *address) { (void)zone; (void)address; }
static void tigerZoneForceLock(malloc_zone_t *zone) { (void)zone; pthread_mutex_lock(&alignedLock); }
static void tigerZoneForceUnlock(malloc_zone_t *zone) { (void)zone; pthread_mutex_unlock(&alignedLock); }
static void tigerZoneStatistics(malloc_zone_t *zone, malloc_statistics_t *stats)
{
    (void)zone;
    pthread_mutex_lock(&alignedLock);
    size_t bytes = 0;
    for (size_t i = 0; i < alignedCount; ++i) bytes += alignedBlocks[i].len;
    stats->blocks_in_use = (unsigned)alignedCount;
    stats->size_in_use = bytes;
    stats->max_size_in_use = bytes;
    stats->size_allocated = bytes;
    pthread_mutex_unlock(&alignedLock);
}

static struct malloc_introspection_t tigerZoneIntrospect = {
    tigerZoneEnumerator, tigerZoneGoodSize, tigerZoneCheck, tigerZonePrint,
    tigerZoneLog, tigerZoneForceLock, tigerZoneForceUnlock, tigerZoneStatistics
};

static malloc_zone_t tigerAlignedZone;
static pthread_once_t tigerAlignedZoneOnce = PTHREAD_ONCE_INIT;

static void tigerAlignedZoneInit(void)
{
    tigerAlignedZone.size = tigerZoneSize;
    tigerAlignedZone.malloc = tigerZoneMalloc;
    tigerAlignedZone.calloc = tigerZoneCalloc;
    tigerAlignedZone.valloc = tigerZoneValloc;
    tigerAlignedZone.free = tigerZoneFree;
    tigerAlignedZone.realloc = tigerZoneRealloc;
    tigerAlignedZone.destroy = tigerZoneDestroy;
    tigerAlignedZone.zone_name = "TigerAlignedZone";
    tigerAlignedZone.introspect = &tigerZoneIntrospect;
    tigerAlignedZone.version = 3;   /* what Libc-391.5.22 create_scalable_zone() sets */
    malloc_zone_register(&tigerAlignedZone);
}

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    if (alignment < sizeof(void *) || (alignment & (alignment - 1)))
        return EINVAL;
    /* Tiger's malloc guarantees 16-byte alignment (the tiny-region quantum). */
    if (alignment <= 16) {
        void *p = malloc(size ? size : 1);
        if (!p) return ENOMEM;
        *memptr = p;
        return 0;
    }
    /* valloc is page-aligned and belongs to the default zone, so ordinary free() handles it. */
    if (alignment <= (size_t)getpagesize()) {
        void *p = valloc(size ? size : 1);
        if (!p) return ENOMEM;
        *memptr = p;
        return 0;
    }
    /* Above a page there is nothing in Tiger's libc to reuse. JavaScriptCore's MarkedBlock
       asks for 16 KB, so this has to be real. */
    pthread_once(&tigerAlignedZoneOnce, tigerAlignedZoneInit);
    void *p = tigerAlignedMap(alignment, size);
    if (!p) return ENOMEM;
    *memptr = p;
    return 0;
}

size_t strnlen(const char *s, size_t maxlen) { const char *e = memchr(s, 0, maxlen); return e ? (size_t)(e - s) : maxlen; }

void *memmem(const void *h, size_t hl, const void *n, size_t nl)
{
    /* Apple's Libc memmem (FreeBSD string/memmem.c) returns NULL for an empty needle or
       haystack, not the haystack pointer. Match it so a Tiger build and a macOS build of
       WTF::find(span, span) agree. */
    if (!hl || !nl) return NULL;
    if (hl < nl) return NULL;
    const char *p = h, *end = p + hl - nl + 1;
    for (; p < end; ++p) if (*p == *(const char*)n && !memcmp(p, n, nl)) return (void*)p;
    return NULL;
}

ssize_t getline(char **lineptr, size_t *n, FILE *stream)
{
    if (!lineptr || !n) { errno = EINVAL; return -1; }
    if (!*lineptr || !*n) { *n = BUFSIZ; *lineptr = malloc(*n); if (!*lineptr) return -1; }
    size_t len = 0; int c;
    while ((c = fgetc(stream)) != EOF) {
        if (len + 2 > *n) { size_t nn = *n * 2; char *np = realloc(*lineptr, nn); if (!np) return -1; *lineptr = np; *n = nn; }
        (*lineptr)[len++] = (char)c;
        if (c == '\n') break;
    }
    /* BSD getdelim, which is what macports-legacy-support ships and what Apple's Libc has:
       a real read error is -1 even with a partial line buffered, and only a clean EOF with
       nothing read is the "no more lines" -1. Returning the partial line on error would let
       a caller treat truncated input as a complete last line. */
    if (c == EOF && (ferror(stream) || !len)) return -1;
    (*lineptr)[len] = 0;
    return (ssize_t)len;
}

int pthread_setname_np(const char *name) { (void)name; return 0; }
int pthread_getname_np(pthread_t t, char *name, size_t len) { (void)t; if (len) name[0] = 0; return 0; }
int pthread_threadid_np(pthread_t t, uint64_t *id) { *id = (uint64_t)(uintptr_t)(t ? t : pthread_self()); return 0; }

void arc4random_buf(void *buf, size_t n)
{
    unsigned char *p = buf;
    while (n >= 4) { uint32_t r = arc4random(); memcpy(p, &r, 4); p += 4; n -= 4; }
    if (n) { uint32_t r = arc4random(); memcpy(p, &r, n); }
}
uint32_t arc4random_uniform(uint32_t upper)
{
    if (upper < 2) return 0;
    uint32_t min = -upper % upper, r;
    do r = arc4random(); while (r < min);
    return r % upper;
}

int clock_gettime(clockid_t clk, struct timespec *tp)
{
    if (clk == CLOCK_REALTIME) {
        struct timeval tv; gettimeofday(&tv, NULL);
        tp->tv_sec = tv.tv_sec; tp->tv_nsec = tv.tv_usec * 1000; return 0;
    }
    static mach_timebase_info_data_t tb;
    if (!tb.denom) mach_timebase_info(&tb);
    uint64_t ns = mach_absolute_time() * tb.numer / tb.denom;
    tp->tv_sec = ns / 1000000000ull; tp->tv_nsec = ns % 1000000000ull;
    return 0;
}
int clock_getres(clockid_t clk, struct timespec *res) { (void)clk; res->tv_sec = 0; res->tv_nsec = 1000; return 0; }

/* 10.4's libm trunc()/truncf() lose the sign of a zero result: trunc(-0.5) is +0, C99
   (and roundsd, and JS's Math.trunc) say -0. Probed on the box (NOTES 2026-09-23): no
   other rounding or elementary function there gets a signed zero wrong. This archive is
   linked ahead of libSystem, so these are the process's trunc. */
#include <math.h>
#include <stdint.h>
double trunc(double x)
{
    if (!(fabs(x) < 4503599627370496.0)) /* NaN, infinities, and |x| >= 2^52: already integral */
        return x;
    return copysign((double)(int64_t)x, x);
}
float truncf(float x)
{
    if (!(fabsf(x) < 8388608.0f)) /* 2^23 */
        return x;
    return copysignf((float)(int32_t)x, x);
}

/* Apple clang emits __bzero for zeroing memsets on some Darwin targets. */
void __bzero(void *p, size_t n) { memset(p, 0, n); }

/* 10.4u SDK's assert.h expands assert() to a call to this (old-style, pre-__assert_rtn
   fallback); Tiger's libSystem has __assert_rtn but not __eprintf. */
void __eprintf(const char *fmt, const char *file, unsigned line, const char *e)
{
    fprintf(stderr, fmt, file, line, e);
    abort();
}

/* ponytail: Tiger has no *at() family. std::filesystem::remove_all() (built with
   LIBCXX_ENABLE_FILESYSTEM=ON) needs openat/unlinkat/fdopendir. Emulate them by resolving
   the directory fd to an absolute path (F_GETPATH) and operating on the full path instead
   of a true at-relative kernel lookup. Not race-free against a concurrent rename of the
   directory like the real syscall would be; acceptable for a build tool operating on its
   own temp/output trees. Upgrade: none possible pre-10.10 kernel, N/A on this target. */
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>

static int tiger_resolve_at(int dirfd, const char *path, char *out, size_t outsize)
{
    if (path[0] == '/') {
        if (strlcpy(out, path, outsize) >= outsize) { errno = ENAMETOOLONG; return -1; }
        return 0;
    }
    char dirbuf[PATH_MAX];
    if (dirfd == AT_FDCWD) {
        if (!getcwd(dirbuf, sizeof dirbuf)) return -1;
    } else if (fcntl(dirfd, F_GETPATH, dirbuf) == -1) {
        return -1;
    }
    if (snprintf(out, outsize, "%s/%s", dirbuf, path) >= (int)outsize) { errno = ENAMETOOLONG; return -1; }
    return 0;
}

int openat(int dirfd, const char *path, int flags, ...)
{
    char full[PATH_MAX * 2];
    if (tiger_resolve_at(dirfd, path, full, sizeof full) == -1) return -1;
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap; va_start(ap, flags); mode = (mode_t)va_arg(ap, int); va_end(ap);
    }
    return open(full, flags, mode);
}

int unlinkat(int dirfd, const char *path, int flag)
{
    char full[PATH_MAX * 2];
    if (tiger_resolve_at(dirfd, path, full, sizeof full) == -1) return -1;
    return (flag & AT_REMOVEDIR) ? rmdir(full) : unlink(full);
}

DIR *fdopendir(int fd)
{
    /* ponytail: real fdopendir() keeps using the caller's fd (which remove_all_impl() also
       reuses afterwards as a parent_directory for further openat/unlinkat calls); we can't
       read directory entries through an arbitrary fd here, so open a second fd via opendir()
       and leave the original one open rather than closing it out from under the caller.
       Leaks one fd per nesting level of a remove_all() until process exit -- fine for the
       shallow trees a build tool churns through. Upgrade: none without real *at() support. */
    char pathbuf[PATH_MAX];
    if (fcntl(fd, F_GETPATH, pathbuf) == -1) return NULL;
    return opendir(pathbuf);
}

/* Tiger's libSystem has copyfile() (path-to-path) but not fcopyfile() (fd-to-fd); libc++'s
   filesystem::copy_file() needs the latter. ponytail: plain read/write loop, ignores state
   and flags beyond the COPYFILE_DATA libc++ always passes. Upgrade: acl/xattr preservation
   if some other caller needs COPYFILE_ACL/XATTR/STAT. */
#include <copyfile.h>
copyfile_state_t copyfile_state_alloc(void) { return (copyfile_state_t)malloc(1); }
int copyfile_state_free(copyfile_state_t s) { free(s); return 0; }
int fcopyfile(int from_fd, int to_fd, copyfile_state_t state, copyfile_flags_t flags)
{
    (void)state; (void)flags;
    char buf[65536];
    ssize_t n;
    while ((n = read(from_fd, buf, sizeof buf)) > 0) {
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = write(to_fd, buf + off, (size_t)(n - off));
            if (w < 0) { if (errno == EINTR) continue; return -1; }
            off += w;
        }
    }
    return (n < 0) ? -1 : 0;
}

/* libunwind on Darwin locates EH info via this dyld API (10.6+). Walk loaded images ourselves. */
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
struct dyld_unwind_sections {
    const struct mach_header *mh;
    const void *dwarf_section; uintptr_t dwarf_section_length;
    const void *compact_unwind_section; uintptr_t compact_unwind_section_length;
};
#ifdef __LP64__
/* 64-bit images carry section_64 records; the 32-bit accessor reads garbage addresses. */
#define tc_section          section_64
#define tc_getsectbyname    getsectbynamefromheader_64
#define tc_mach_header      mach_header_64
#else
#define tc_section          section
#define tc_getsectbyname    getsectbynamefromheader
#define tc_mach_header      mach_header
#endif
_Bool _dyld_find_unwind_sections(void *addr, struct dyld_unwind_sections *info)
{
    uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; ++i) {
        const struct tc_mach_header *mh = (const struct tc_mach_header *)_dyld_get_image_header(i);
        intptr_t slide = _dyld_get_image_vmaddr_slide(i);
        const struct tc_section *text = tc_getsectbyname(mh, "__TEXT", "__text");
        if (!text) continue;
        uintptr_t start = (uintptr_t)text->addr + slide, end = start + (uintptr_t)text->size;
        if ((uintptr_t)addr < start || (uintptr_t)addr >= end) continue;
        unsigned long len = 0;
        const struct tc_section *s = tc_getsectbyname(mh, "__TEXT", "__eh_frame");
        info->mh = (const struct mach_header *)mh;
        info->dwarf_section = s ? (const void*)((uintptr_t)s->addr + slide) : NULL; info->dwarf_section_length = s ? (uintptr_t)s->size : 0;
        s = tc_getsectbyname(mh, "__TEXT", "__unwind_info");
        info->compact_unwind_section = s ? (const void*)((uintptr_t)s->addr + slide) : NULL; info->compact_unwind_section_length = s ? (uintptr_t)s->size : 0;
        (void)len;
        return 1;
    }
    return 0;
}
