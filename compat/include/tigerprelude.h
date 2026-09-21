/* Forced-include prelude for building modern code against the Mac OS X 10.4u SDK.
 * Declares libc/libSystem entry points Tiger lacks; libtigercompat implements them. */
#ifndef TIGER_PRELUDE_H
#define TIGER_PRELUDE_H
#ifndef __ASSEMBLER__
#include <sys/types.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <stdio.h>
#include <dirent.h>
#include <fcntl.h>
#ifndef O_CLOEXEC
/* TIGER: no atomic close-on-exec on open(); fine for our own build-tool temp dirs. */
#define O_CLOEXEC 0
#endif
/* TIGER: pthread QOS classes (qos_class_t). Tiger's <pthread.h> predates them;
   the libdispatch polyfill supplies <sys/qos.h>. Guarded so this header still
   works before that tree is on the include path. */
#if defined(__has_include)
#if __has_include(<sys/qos.h>)
#include <sys/qos.h>
#endif
#endif
#ifdef __cplusplus
extern "C" {
#endif
int posix_memalign(void **memptr, size_t alignment, size_t size);
size_t strnlen(const char *s, size_t maxlen);
void *memmem(const void *haystack, size_t haystacklen, const void *needle, size_t needlelen);
ssize_t getline(char **lineptr, size_t *n, FILE *stream);
int pthread_setname_np(const char *name);
int pthread_getname_np(pthread_t thread, char *name, size_t len);
int pthread_threadid_np(pthread_t thread, uint64_t *thread_id);
void arc4random_buf(void *buf, size_t nbytes);
uint32_t arc4random_uniform(uint32_t upper_bound);
typedef int clockid_t;
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 6
#define CLOCK_MONOTONIC_RAW 4
#define CLOCK_PROCESS_CPUTIME_ID 12
#define CLOCK_THREAD_CPUTIME_ID 16
int clock_gettime(clockid_t clk_id, struct timespec *tp);
int clock_getres(clockid_t clk_id, struct timespec *res);
void __eprintf(const char *fmt, const char *file, unsigned line, const char *e) __attribute__((noreturn));
/* TIGER: no *at() family (added to the SDK in 10.10; xnu-792 predates the syscalls too).
   std::filesystem::remove_all() needs these three; libtigercompat emulates them by
   resolving the dirfd to a path (F_GETPATH) and operating on the full path. */
#define AT_FDCWD (-2)
#define AT_REMOVEDIR 0x80
int openat(int dirfd, const char *path, int flags, ...);
int unlinkat(int dirfd, const char *path, int flag);
struct dirent;
DIR *fdopendir(int fd);
#ifdef __cplusplus
}
#endif
/* TIGER: mach time entry points added in 10.12. Tiger has only
   mach_absolute_time (which stops while the machine is asleep). The
   "approximate" variants are exact here, and the "continuous" ones do not count
   sleep; both are acceptable for WebKit's use (monotonic timers and deadlines)
   and are what a Tiger-era program would have used anyway. */
#if defined(__has_include)
#if __has_include(<mach/mach_time.h>)
#include <mach/mach_time.h>
static __inline__ uint64_t mach_approximate_time(void) { return mach_absolute_time(); }
static __inline__ uint64_t mach_continuous_time(void) { return mach_absolute_time(); }
static __inline__ uint64_t mach_continuous_approximate_time(void) { return mach_absolute_time(); }
#endif
#endif

/* TIGER: pthread_attr_set_qos_class_np is 10.10+. Tiger has no per-thread QOS,
   so this accepts the request and does nothing; threads all run at one priority.
   Declared inline because there is nothing to implement. */
#if defined(__has_include)
#if __has_include(<sys/qos.h>)
static __inline__ int pthread_attr_set_qos_class_np(pthread_attr_t *__attr,
    qos_class_t __qos_class, int __relative_priority)
{
    (void)__attr; (void)__qos_class; (void)__relative_priority;
    return 0;
}

static __inline__ int pthread_set_qos_class_self_np(qos_class_t __qos_class,
    int __relative_priority)
{
    (void)__qos_class; (void)__relative_priority;
    return 0;
}

static __inline__ int pthread_get_qos_class_np(pthread_t __pthread,
    qos_class_t *__qos_class, int *__relative_priority)
{
    (void)__pthread;
    if (__qos_class)
        *__qos_class = QOS_CLASS_DEFAULT;
    if (__relative_priority)
        *__relative_priority = 0;
    return 0;
}
#endif
#endif

/* TIGER: mkostemp/mkostemps are 10.10; Tiger has mkstemp and mkstemps. The
   flags argument is O_CLOEXEC in practice, which Tiger's kernel ignores anyway
   (see compat/include/sdk-fill/fcntl.h). */
#if defined(__has_include)
#if __has_include(<unistd.h>)
#include <unistd.h>
static __inline__ int mkostemp(char *__template, int __flags)
{
    (void)__flags;
    return mkstemp(__template);
}
static __inline__ int mkostemps(char *__template, int __suffixlen, int __flags)
{
    (void)__flags;
    return mkstemps(__template, __suffixlen);
}
#endif
#endif

/* TIGER: memset_pattern4/8/16 are in Tiger's libSystem (see
   logs/api/tiger-libSystem.txt) but not in its <string.h>. Declare only; the
   real ones are hand-written SSE. */
#ifdef __cplusplus
extern "C" {
#endif
void memset_pattern4(void *__b, const void *__pattern4, size_t __len);
void memset_pattern8(void *__b, const void *__pattern8, size_t __len);
void memset_pattern16(void *__b, const void *__pattern16, size_t __len);
#ifdef __cplusplus
}
#endif

/* C11 aligned_alloc, absent from Tiger's libc, and the free that matches it.
   Implemented in compat/cfcompat.c -- see there for why the result is not
   free()-able and what calls tiger_aligned_free(). */
#ifdef __cplusplus
extern "C" {
#endif
void *aligned_alloc(size_t __alignment, size_t __size);
void tiger_aligned_free(void *__object);
#ifdef __cplusplus
}
#endif
#ifdef __cplusplus
extern "C" {
#endif
#ifdef __cplusplus
}
#endif
#endif /* __ASSEMBLER__ */
#endif
