/* libSystem gaps on Mac OS X 10.4, implemented on what Tiger has. */
#include "include/tigerprelude.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/time.h>
#include <mach/mach.h>
#include <mach/mach_time.h>

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    if (alignment < sizeof(void*) || (alignment & (alignment - 1)))
        return EINVAL;
    /* Tiger's malloc guarantees 16-byte alignment (tiny-region quantum); valloc gives page alignment. */
    if (alignment <= 16) { void *p = malloc(size ? size : 1); if (!p) return ENOMEM; *memptr = p; return 0; }
    /* ponytail: everything above 16 gets page alignment from valloc, which is what
       macports-legacy-support's posix_memalign_emulation.c does and for the same reason: the
       result must stay free()-able (and malloc_zone_free()-able on the default zone, which
       bmalloc's SystemHeap relies on), so an over-allocate-and-offset scheme is out.
       Known ceiling: an alignment above the 4096-byte page is silently under-satisfied.
       Nothing on this port asks for one -- bmalloc routes alignments that large through
       SystemHeap::memalignLarge/tryVMAllocate, not through here. Upgrade: none available
       without a zone-aware allocator, which Tiger's malloc does not expose. */
    void *p = valloc(size ? size : 1); if (!p) return ENOMEM; *memptr = p; return 0;
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
    if (!*lineptr || !*n) { *n = 128; *lineptr = malloc(*n); if (!*lineptr) return -1; }
    size_t len = 0; int c;
    while ((c = fgetc(stream)) != EOF) {
        if (len + 2 > *n) { size_t nn = *n * 2; char *np = realloc(*lineptr, nn); if (!np) return -1; *lineptr = np; *n = nn; }
        (*lineptr)[len++] = (char)c;
        if (c == '\n') break;
    }
    if (!len && c == EOF) return -1;
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
_Bool _dyld_find_unwind_sections(void *addr, struct dyld_unwind_sections *info)
{
    uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; ++i) {
        const struct mach_header *mh = _dyld_get_image_header(i);
        intptr_t slide = _dyld_get_image_vmaddr_slide(i);
        const struct section *text = getsectbynamefromheader(mh, "__TEXT", "__text");
        if (!text) continue;
        uintptr_t start = text->addr + slide, end = start + text->size;
        if ((uintptr_t)addr < start || (uintptr_t)addr >= end) continue;
        unsigned long len = 0;
        const struct section *s = getsectbynamefromheader(mh, "__TEXT", "__eh_frame");
        info->mh = mh;
        info->dwarf_section = s ? (const void*)(s->addr + slide) : NULL; info->dwarf_section_length = s ? s->size : 0;
        s = getsectbynamefromheader(mh, "__TEXT", "__unwind_info");
        info->compact_unwind_section = s ? (const void*)(s->addr + slide) : NULL; info->compact_unwind_section_length = s ? s->size : 0;
        (void)len;
        return 1;
    }
    return 0;
}
