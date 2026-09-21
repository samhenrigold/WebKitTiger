/* os_log / os_unfair_lock / qos polyfill for Mac OS X 10.4. */

#include "internal.h"

#include <mach/mach_init.h>
#include <os/lock.h>
#include <os/log.h>
#include <os/signpost.h>
#include <sched.h>
#include <sys/qos.h>
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------- os_log --- */

struct os_log_s {
    struct dispatch_object_s base;
    char *subsystem;
    char *category;
};

struct os_log_s _os_log_default = { { 2, TD_KIND_LOG, 1, NULL, NULL }, NULL, NULL };

void td_log_destroy(struct dispatch_object_s *o)
{
    struct os_log_s *l = (struct os_log_s *)o;
    free(l->subsystem);
    free(l->category);
}

/* ------------------------------------------------------ object lifecycle --- */
/* Shared by every object this library vends. It lives here rather than in
 * dispatch.c because the x86_64 build is os.c alone: Tiger ships no 64-bit
 * CoreFoundation, so dispatch.c cannot be built for it, but os_log still can. */

void (*td_dispatch_destroy)(struct dispatch_object_s *);

void td_destroy(struct dispatch_object_s *o)
{
    if (o->td_static)
        return;
    if (o->td_finalizer)
        o->td_finalizer(o->td_context);
    if (o->td_kind == TD_KIND_LOG)
        td_log_destroy(o);
    else if (td_dispatch_destroy)
        td_dispatch_destroy(o);
    free(o);
}

void *os_retain(void *o) { return td_retain_obj(o); }
void os_release(void *o) { td_release_obj(o); }

/* Bit per os_log_type_t we let through. */
static int g_level = -1;

static int td_log_level(void)
{
    if (g_level < 0) {
        const char *v = getenv("TIGER_OS_LOG");
        if (!v || !*v)
            g_level = 2;                                /* error + fault */
        else if (!strcmp(v, "0") || !strcmp(v, "off"))
            g_level = 0;
        else if (!strcmp(v, "debug") || !strcmp(v, "all"))
            g_level = 5;
        else if (!strcmp(v, "info"))
            g_level = 4;
        else
            g_level = 3;                                /* + default */
    }
    return g_level;
}

bool os_log_type_enabled(os_log_t log, os_log_type_t type)
{
    int lvl = td_log_level();
    (void)log;
    if (!lvl)
        return false;
    switch (type) {
    case OS_LOG_TYPE_ERROR:
    case OS_LOG_TYPE_FAULT:   return lvl >= 2;
    case OS_LOG_TYPE_DEFAULT: return lvl >= 3;
    case OS_LOG_TYPE_INFO:    return lvl >= 4;
    case OS_LOG_TYPE_DEBUG:   return lvl >= 5;
    }
    return lvl >= 3;
}

os_log_t os_log_create(const char *subsystem, const char *category)
{
    struct os_log_s *l = (struct os_log_s *)calloc(1, sizeof(*l));
    l->base.td_refcount = 1;
    l->base.td_kind = TD_KIND_LOG;
    l->subsystem = subsystem ? strdup(subsystem) : NULL;
    l->category = category ? strdup(category) : NULL;
    return l;
}

/* os_log format strings carry Apple annotations printf knows nothing about:
 * %{public}s, %{private}d, %{signpost.description:begin_time}llu, ...
 * Strip the braces; everything else is ordinary printf. */
static void td_strip_format(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    for (; *in && o + 1 < cap; in++) {
        if (in[0] == '%' && in[1] == '{') {
            const char *close = strchr(in + 2, '}');
            if (close) {
                out[o++] = '%';
                in = close;             /* loop's ++ steps past '}' */
                continue;
            }
        }
        out[o++] = *in;
    }
    out[o] = '\0';
}

static const char *td_type_name(os_log_type_t t)
{
    switch (t) {
    case OS_LOG_TYPE_ERROR:   return "error";
    case OS_LOG_TYPE_FAULT:   return "fault";
    case OS_LOG_TYPE_INFO:    return "info";
    case OS_LOG_TYPE_DEBUG:   return "debug";
    default:                  return "log";
    }
}

static void td_log_emit(os_log_t log, os_log_type_t type, const char *format, va_list args)
{
    /* ponytail: 2 KiB of format string is plenty for WebKit's log channels;
     * longer ones are truncated rather than heap-allocated. */
    char fmt[2048];

    if (!os_log_type_enabled(log, type))
        return;

    td_strip_format(format ? format : "", fmt, sizeof(fmt));

    flockfile(stderr);
    if (log && log != &_os_log_default && log->subsystem)
        fprintf(stderr, "[%s:%s %s] ", log->subsystem,
            log->category ? log->category : "", td_type_name(type));
    else
        fprintf(stderr, "[%s] ", td_type_name(type));
    vfprintf(stderr, fmt, args);
    {
        size_t n = strlen(fmt);
        if (!n || fmt[n - 1] != '\n')
            fputc('\n', stderr);
    }
    funlockfile(stderr);
}

void _os_log_impl_tiger(os_log_t log, os_log_type_t type, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    td_log_emit(log, type, format, ap);
    va_end(ap);
}

void os_log_with_args(os_log_t log, os_log_type_t type, const char *format, va_list args, void *ret_addr)
{
    (void)ret_addr;
    td_log_emit(log, type, format, args);
}

/* ------------------------------------------------------------- signposts --- */

os_signpost_id_t os_signpost_id_generate(os_log_t log) { (void)log; return 1; }

os_signpost_id_t os_signpost_id_make_with_pointer(os_log_t log, const void *ptr)
{
    (void)log;
    return (os_signpost_id_t)(uintptr_t)ptr;
}

bool os_signpost_enabled(os_log_t log) { (void)log; return false; }

/* -------------------------------------------------------- os_unfair_lock --- */

static uint32_t td_self_id(void)
{
    return (uint32_t)pthread_mach_thread_np(pthread_self());
}

void os_unfair_lock_lock(os_unfair_lock_t l)
{
    uint32_t me = td_self_id();
    unsigned spins = 0;
    while (!__sync_bool_compare_and_swap(&l->_os_unfair_lock_opaque, 0, me)) {
        if (++spins < 128)
            __asm__ __volatile__("pause" ::: "memory");
        else
            sched_yield();
    }
}

bool os_unfair_lock_trylock(os_unfair_lock_t l)
{
    return __sync_bool_compare_and_swap(&l->_os_unfair_lock_opaque, 0, td_self_id());
}

void os_unfair_lock_unlock(os_unfair_lock_t l)
{
    __sync_synchronize();
    l->_os_unfair_lock_opaque = 0;
}

void os_unfair_lock_lock_with_options(os_unfair_lock_t l, os_unfair_lock_options_t options)
{
    (void)options;
    os_unfair_lock_lock(l);
}

void os_unfair_lock_assert_owner(const os_unfair_lock *l)
{
    if (l->_os_unfair_lock_opaque != td_self_id())
        abort();
}

void os_unfair_lock_assert_not_owner(const os_unfair_lock *l)
{
    if (l->_os_unfair_lock_opaque == td_self_id())
        abort();
}

/* --------------------------------------------------------------- sys/qos --- */

qos_class_t qos_class_self(void) { return QOS_CLASS_DEFAULT; }
qos_class_t qos_class_main(void) { return QOS_CLASS_USER_INTERACTIVE; }
