/* libtigerdispatch -- GCD on pthreads + mach + CFRunLoop, for Mac OS X 10.4.
 * See SURVEY.md for the API surface this needs to cover. */

#include "internal.h"

#include <CoreFoundation/CoreFoundation.h>
#include <Block.h>
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <mach/mach_time.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <unistd.h>

#include <dispatch/dispatch.h>

/* CFRunLoopGetMain ships in Tiger's CoreFoundation but is missing from the
 * 10.4u SDK headers (it was only declared publicly in 10.5). */
extern CFRunLoopRef CFRunLoopGetMain(void);

/* ---------------------------------------------------------------- time --- */

static double g_ns_per_tick = 1.0;

static void td_time_init(void)
{
    mach_timebase_info_data_t tb;
    if (mach_timebase_info(&tb) == KERN_SUCCESS && tb.denom)
        g_ns_per_tick = (double)tb.numer / (double)tb.denom;
}

static uint64_t td_now(void)
{
    return (uint64_t)(mach_absolute_time() * g_ns_per_tick);
}

dispatch_time_t dispatch_time(dispatch_time_t when, int64_t delta)
{
    if (when == DISPATCH_TIME_FOREVER)
        return DISPATCH_TIME_FOREVER;
    if (when == DISPATCH_TIME_NOW || when == DISPATCH_WALLTIME_NOW)
        when = td_now();
    if (delta < 0 && (uint64_t)(-delta) > when)
        return 0;
    return when + (uint64_t)delta;
}

dispatch_time_t dispatch_walltime(const struct timespec *when, int64_t delta)
{
    uint64_t base = td_now();
    if (when) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        int64_t skew = ((int64_t)when->tv_sec - (int64_t)tv.tv_sec) * (int64_t)NSEC_PER_SEC
            + ((int64_t)when->tv_nsec - (int64_t)tv.tv_usec * 1000);
        base = (uint64_t)((int64_t)base + skew);
    }
    return dispatch_time(base, delta);
}

/* Absolute timespec for pthread_cond_timedwait, from a dispatch_time_t. */
static void td_deadline_to_timespec(uint64_t deadline, struct timespec *ts)
{
    uint64_t now = td_now();
    uint64_t rel = deadline > now ? deadline - now : 0;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t abs_ns = (uint64_t)tv.tv_sec * NSEC_PER_SEC + (uint64_t)tv.tv_usec * 1000 + rel;
    ts->tv_sec = (time_t)(abs_ns / NSEC_PER_SEC);
    ts->tv_nsec = (long)(abs_ns % NSEC_PER_SEC);
}

/* -------------------------------------------------------------- objects --- */

typedef struct td_item {
    struct td_item *next;
    dispatch_block_t block;
    dispatch_function_t fn;
    void *ctx;
    int barrier;
    struct td_sema *waiter;         /* non-NULL for the sync variants */
} td_item;

#define TD_MAX_SPECIFIC 8

struct dispatch_queue_s {
    struct dispatch_object_s base;
    char *label;
    int width;                      /* 1 = serial, INT_MAX = concurrent */
    int active;                     /* items currently executing */
    int barrier_running;
    int suspend;
    int in_ready;
    int is_main;
    int autorelease;
    struct dispatch_queue_s *target;
    td_item *head, *tail;
    struct dispatch_queue_s *ready_next;
    struct { const void *key; void *ctx; dispatch_function_t dtor; } spec[TD_MAX_SPECIFIC];
    int nspec;
};

struct dispatch_queue_attr_s {
    int concurrent;
    int autorelease;
};

const struct dispatch_queue_attr_s _dispatch_queue_attr_concurrent = { 1, 0 };
const struct dispatch_queue_attr_s _dispatch_queue_attr_serial_arp = { 0, 1 };
const struct dispatch_queue_attr_s _dispatch_queue_attr_concurrent_arp = { 1, 1 };

struct dispatch_group_s {
    struct dispatch_object_s base;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    long count;
    struct td_notify *notify;
};

struct dispatch_semaphore_s {
    struct dispatch_object_s base;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    long value;
};

struct dispatch_data_s {
    struct dispatch_object_s base;
    const void *buf;
    size_t size;
    dispatch_block_t destructor;    /* NULL => we own buf and free() it */
};

struct dispatch_source_type_s { int id; };

struct dispatch_source_s {
    struct dispatch_object_s base;
    dispatch_source_type_t type;
    uintptr_t handle, mask;
    dispatch_queue_t queue;
    dispatch_block_t handler, cancel_handler, reg_handler;
    dispatch_function_t handler_f, cancel_f;
    int suspend;                    /* sources are created suspended */
    int cancelled;
    uintptr_t data;
    /* timer state */
    int timer_set;
    int armed;
    uint64_t t_start, t_interval;
};

/* Only the identity of these matters. */
const struct dispatch_source_type_s _dispatch_source_type_timer = { 1 };
const struct dispatch_source_type_s _dispatch_source_type_memorypressure = { 2 };
const struct dispatch_source_type_s _dispatch_source_type_vm = { 3 };
const struct dispatch_source_type_s _dispatch_source_type_vnode = { 4 };
const struct dispatch_source_type_s _dispatch_source_type_mach_recv = { 5 };
const struct dispatch_source_type_s _dispatch_source_type_mach_send = { 6 };
const struct dispatch_source_type_s _dispatch_source_type_read = { 7 };
const struct dispatch_source_type_s _dispatch_source_type_write = { 8 };
const struct dispatch_source_type_s _dispatch_source_type_signal = { 9 };
const struct dispatch_source_type_s _dispatch_source_type_proc = { 10 };
const struct dispatch_source_type_s _dispatch_source_type_data_add = { 11 };
const struct dispatch_source_type_s _dispatch_source_type_data_or = { 12 };
const struct dispatch_source_type_s _dispatch_source_type_data_replace = { 13 };

/* ------------------------------------------------------- tiny semaphore --- */

typedef struct td_sema {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int signalled;
} td_sema;

static void td_sema_init(td_sema *s)
{
    pthread_mutex_init(&s->lock, NULL);
    pthread_cond_init(&s->cond, NULL);
    s->signalled = 0;
}

static void td_sema_wait(td_sema *s)
{
    pthread_mutex_lock(&s->lock);
    while (!s->signalled)
        pthread_cond_wait(&s->cond, &s->lock);
    pthread_mutex_unlock(&s->lock);
}

static void td_sema_signal(td_sema *s)
{
    pthread_mutex_lock(&s->lock);
    s->signalled = 1;
    pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->lock);
}

static void td_sema_destroy(td_sema *s)
{
    pthread_mutex_destroy(&s->lock);
    pthread_cond_destroy(&s->cond);
}

/* ---------------------------------------------------------- global state --- */

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;
static dispatch_queue_t g_ready_head, g_ready_tail;
static pthread_key_t g_curq_key;
static int g_workers;
static int g_workers_max;

static struct dispatch_queue_s g_global[4];     /* background, low, default, high */
static struct dispatch_queue_s g_main_q;

/* main-queue drain */
static pthread_mutex_t g_main_lock = PTHREAD_MUTEX_INITIALIZER;
static td_item *g_main_head, *g_main_tail;
static CFRunLoopSourceRef g_main_src;

static pthread_once_t g_init_once = PTHREAD_ONCE_INIT;

static void td_worker_spawn_locked(void);
static void td_main_wake(void);

static void td_queue_init(struct dispatch_queue_s *q, const char *label, int width)
{
    memset(q, 0, sizeof(*q));
    q->base.td_refcount = 1;
    q->base.td_kind = TD_KIND_QUEUE;
    q->base.td_static = 1;
    q->label = (char *)label;
    q->width = width;
}

static void td_init(void)
{
    static const char *names[4] = {
        "com.apple.root.background-qos", "com.apple.root.utility-qos",
        "com.apple.root.default-qos", "com.apple.root.user-interactive-qos"
    };
    int i;
    int ncpu = 1;
    size_t len = sizeof(ncpu);

    td_time_init();
    pthread_key_create(&g_curq_key, NULL);

    for (i = 0; i < 4; i++)
        td_queue_init(&g_global[i], names[i], INT_MAX);
    td_queue_init(&g_main_q, "com.apple.main-thread", 1);
    g_main_q.is_main = 1;
    g_main_q.autorelease = 1;

    if (sysctlbyname("hw.ncpu", &ncpu, &len, NULL, 0) != 0 || ncpu < 1)
        ncpu = 1;
    /* ponytail: fixed pool, no dynamic growth or thread reclamation. A blocking
     * work item can starve the pool; raise the multiplier or add an overcommit
     * pool if that ever bites. */
    g_workers_max = ncpu * 2;
    if (g_workers_max < 4)
        g_workers_max = 4;
}

static void td_ensure_init(void) { pthread_once(&g_init_once, td_init); }

/* -------------------------------------------------- autorelease via dlsym --- */
/* Wrapping work items in an NSAutoreleasePool without linking ObjC: look the
 * runtime up at load time and no-op if it isn't there. */

typedef void *td_id;
typedef void *td_sel;
static td_id (*td_getClass)(const char *);
static td_sel (*td_regSel)(const char *);
static td_id (*td_msgSend)(td_id, td_sel, ...);
static td_id td_pool_class;
static td_sel td_sel_alloc, td_sel_init, td_sel_drain;

static void td_objc_init(void)
{
    td_getClass = (td_id (*)(const char *))dlsym(RTLD_DEFAULT, "objc_getClass");
    td_regSel = (td_sel (*)(const char *))dlsym(RTLD_DEFAULT, "sel_registerName");
    td_msgSend = (td_id (*)(td_id, td_sel, ...))dlsym(RTLD_DEFAULT, "objc_msgSend");
    if (!td_getClass || !td_regSel || !td_msgSend)
        return;
    td_pool_class = td_getClass("NSAutoreleasePool");
    td_sel_alloc = td_regSel("alloc");
    td_sel_init = td_regSel("init");
    td_sel_drain = td_regSel("release");
}

static void *td_pool_push(void)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, td_objc_init);
    if (!td_pool_class)
        return NULL;
    return td_msgSend(td_msgSend(td_pool_class, td_sel_alloc), td_sel_init);
}

static void td_pool_pop(void *pool)
{
    if (pool)
        td_msgSend(pool, td_sel_drain);
}

/* ----------------------------------------------------------- item running --- */

static td_item *td_item_new(dispatch_block_t block, dispatch_function_t fn, void *ctx, int barrier)
{
    td_item *it = (td_item *)calloc(1, sizeof(td_item));
    if (block)
        it->block = Block_copy(block);
    it->fn = fn;
    it->ctx = ctx;
    it->barrier = barrier;
    return it;
}

static void td_item_run(td_item *it, dispatch_queue_t q)
{
    dispatch_queue_t prev = (dispatch_queue_t)pthread_getspecific(g_curq_key);
    void *pool = q->autorelease ? td_pool_push() : NULL;
    pthread_setspecific(g_curq_key, q);
    if (it->block)
        it->block();
    else if (it->fn)
        it->fn(it->ctx);
    pthread_setspecific(g_curq_key, prev);
    td_pool_pop(pool);
    if (it->waiter)
        td_sema_signal(it->waiter);
    if (it->block)
        Block_release(it->block);
    free(it);
}

/* Call with g_lock held. */
static int td_can_run_head(dispatch_queue_t q)
{
    if (!q->head || q->suspend || q->barrier_running)
        return 0;
    if (q->head->barrier)
        return q->active == 0;
    return q->active < q->width;
}

static void td_maybe_ready(dispatch_queue_t q)
{
    if (q->in_ready || !td_can_run_head(q))
        return;
    q->in_ready = 1;
    q->ready_next = NULL;
    if (g_ready_tail)
        g_ready_tail->ready_next = q;
    else
        g_ready_head = q;
    g_ready_tail = q;
    if (g_workers < g_workers_max)
        td_worker_spawn_locked();
    pthread_cond_signal(&g_cond);
}

static void *td_worker(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&g_lock);
    for (;;) {
        dispatch_queue_t q;
        td_item *it;

        while (!g_ready_head)
            pthread_cond_wait(&g_cond, &g_lock);

        q = g_ready_head;
        g_ready_head = q->ready_next;
        if (!g_ready_head)
            g_ready_tail = NULL;
        q->in_ready = 0;

        if (!td_can_run_head(q))
            continue;

        it = q->head;
        q->head = it->next;
        if (!q->head)
            q->tail = NULL;
        q->active++;
        int was_barrier = it->barrier;
        if (was_barrier)
            q->barrier_running = 1;
        td_maybe_ready(q);
        pthread_mutex_unlock(&g_lock);

        td_item_run(it, q);     /* frees `it` */

        pthread_mutex_lock(&g_lock);
        q->active--;
        if (was_barrier)
            q->barrier_running = 0;
        td_maybe_ready(q);
        pthread_mutex_unlock(&g_lock);
        td_release_obj(q);
        pthread_mutex_lock(&g_lock);
    }
    return NULL;
}

static void td_worker_spawn_locked(void)
{
    pthread_t t;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, 512 * 1024);
    if (pthread_create(&t, &attr, td_worker, NULL) == 0)
        g_workers++;
    pthread_attr_destroy(&attr);
}

/* -------------------------------------------------------- main queue drain --- */

static void td_main_perform(void *info)
{
    (void)info;
    for (;;) {
        td_item *it;
        pthread_mutex_lock(&g_main_lock);
        it = g_main_head;
        if (it) {
            g_main_head = it->next;
            if (!g_main_head)
                g_main_tail = NULL;
        }
        pthread_mutex_unlock(&g_main_lock);
        if (!it)
            break;
        td_item_run(it, &g_main_q);
    }
}

/* Tiger has no CFRunLoopPerformBlock, so we drive a version-0 source ourselves. */
static void td_main_wake(void)
{
    if (!g_main_src) {
        pthread_mutex_lock(&g_main_lock);
        if (!g_main_src) {
            CFRunLoopSourceContext ctx;
            memset(&ctx, 0, sizeof(ctx));
            ctx.perform = td_main_perform;
            g_main_src = CFRunLoopSourceCreate(kCFAllocatorDefault, 0, &ctx);
            CFRunLoopAddSource(CFRunLoopGetMain(), g_main_src, kCFRunLoopCommonModes);
        }
        pthread_mutex_unlock(&g_main_lock);
    }
    CFRunLoopSourceSignal(g_main_src);
    CFRunLoopWakeUp(CFRunLoopGetMain());
}

/* ------------------------------------------------------------- enqueueing --- */

static void td_enqueue(dispatch_queue_t q, td_item *it)
{
    td_ensure_init();
    if (!q)
        q = &g_global[2];

    if (q->is_main) {
        pthread_mutex_lock(&g_main_lock);
        if (g_main_tail)
            g_main_tail->next = it;
        else
            g_main_head = it;
        g_main_tail = it;
        pthread_mutex_unlock(&g_main_lock);
        td_main_wake();
        return;
    }

    td_retain_obj(q);
    pthread_mutex_lock(&g_lock);
    if (q->tail)
        q->tail->next = it;
    else
        q->head = it;
    q->tail = it;
    td_maybe_ready(q);
    pthread_mutex_unlock(&g_lock);
}

/* ------------------------------------------------------------------ queues --- */

static dispatch_queue_t td_queue_create(const char *label, dispatch_queue_attr_t attr, dispatch_queue_t target)
{
    struct dispatch_queue_s *q = (struct dispatch_queue_s *)calloc(1, sizeof(*q));
    td_ensure_init();
    q->base.td_refcount = 1;
    q->base.td_kind = TD_KIND_QUEUE;
    q->label = label ? strdup(label) : NULL;
    q->width = (attr && attr->concurrent) ? INT_MAX : 1;
    q->autorelease = attr ? attr->autorelease : 0;
    q->target = target;
    return q;
}

dispatch_queue_t dispatch_queue_create(const char *label, dispatch_queue_attr_t attr)
{
    return td_queue_create(label, attr, NULL);
}

dispatch_queue_t dispatch_queue_create_with_target(const char *label, dispatch_queue_attr_t attr, dispatch_queue_t target)
{
    return td_queue_create(label, attr, target);
}

/* ponytail: QoS is dropped on the floor; the attr comes back unchanged. */
dispatch_queue_attr_t dispatch_queue_attr_make_with_qos_class(dispatch_queue_attr_t attr, dispatch_qos_class_t qos, int rel)
{
    (void)qos; (void)rel;
    return attr;
}

dispatch_queue_attr_t dispatch_queue_attr_make_initially_inactive(dispatch_queue_attr_t attr)
{
    return attr;
}

dispatch_queue_main_t dispatch_get_main_queue(void)
{
    td_ensure_init();
    return (dispatch_queue_main_t)&g_main_q;
}

dispatch_queue_global_t dispatch_get_global_queue(intptr_t identifier, uintptr_t flags)
{
    int i = 2;
    (void)flags;
    td_ensure_init();
    if (identifier >= DISPATCH_QUEUE_PRIORITY_HIGH)
        i = 3;
    else if (identifier >= DISPATCH_QUEUE_PRIORITY_DEFAULT)
        i = 2;
    else if (identifier >= DISPATCH_QUEUE_PRIORITY_LOW)
        i = 1;
    else
        i = 0;
    return (dispatch_queue_global_t)&g_global[i];
}

dispatch_queue_t dispatch_get_current_queue(void)
{
    dispatch_queue_t q;
    td_ensure_init();
    q = (dispatch_queue_t)pthread_getspecific(g_curq_key);
    if (q)
        return q;
    return pthread_main_np() ? &g_main_q : &g_global[2];
}

const char *dispatch_queue_get_label(dispatch_queue_t q)
{
    if (!q)
        q = dispatch_get_current_queue();
    return q->label ? q->label : "";
}

void dispatch_queue_set_specific(dispatch_queue_t q, const void *key, void *ctx, dispatch_function_t dtor)
{
    int i;
    if (!q || q->is_main)
        return;
    pthread_mutex_lock(&g_lock);
    for (i = 0; i < q->nspec; i++) {
        if (q->spec[i].key == key) {
            if (q->spec[i].dtor && q->spec[i].ctx)
                q->spec[i].dtor(q->spec[i].ctx);
            q->spec[i].ctx = ctx;
            q->spec[i].dtor = dtor;
            pthread_mutex_unlock(&g_lock);
            return;
        }
    }
    /* ponytail: 8 keys per queue, silently dropped past that. WTF uses one. */
    if (q->nspec < TD_MAX_SPECIFIC) {
        q->spec[q->nspec].key = key;
        q->spec[q->nspec].ctx = ctx;
        q->spec[q->nspec].dtor = dtor;
        q->nspec++;
    }
    pthread_mutex_unlock(&g_lock);
}

void *dispatch_queue_get_specific(dispatch_queue_t q, const void *key)
{
    void *out = NULL;
    if (!q)
        return NULL;
    pthread_mutex_lock(&g_lock);
    for (; q; q = q->target) {
        int i;
        for (i = 0; i < q->nspec; i++) {
            if (q->spec[i].key == key) {
                out = q->spec[i].ctx;
                goto done;
            }
        }
    }
done:
    pthread_mutex_unlock(&g_lock);
    return out;
}

void *dispatch_get_specific(const void *key)
{
    dispatch_queue_t q;
    td_ensure_init();
    q = (dispatch_queue_t)pthread_getspecific(g_curq_key);
    return q ? dispatch_queue_get_specific(q, key) : NULL;
}

void dispatch_main(void)
{
    td_ensure_init();
    td_main_wake();
    CFRunLoopRun();
    for (;;)
        pause();
}

/* -------------------------------------------------------------- submission --- */

void dispatch_async(dispatch_queue_t q, dispatch_block_t block)
{
    td_enqueue(q, td_item_new(block, NULL, NULL, 0));
}

void dispatch_async_f(dispatch_queue_t q, void *ctx, dispatch_function_t work)
{
    td_enqueue(q, td_item_new(NULL, work, ctx, 0));
}

void dispatch_barrier_async(dispatch_queue_t q, dispatch_block_t block)
{
    td_enqueue(q, td_item_new(block, NULL, NULL, 1));
}

void dispatch_barrier_async_f(dispatch_queue_t q, void *ctx, dispatch_function_t work)
{
    td_enqueue(q, td_item_new(NULL, work, ctx, 1));
}

static void td_sync(dispatch_queue_t q, dispatch_block_t block, dispatch_function_t fn, void *ctx, int barrier)
{
    td_sema sema;
    td_item *it;

    td_ensure_init();
    if (!q)
        q = &g_global[2];

    /* Running inline is legal whenever ordering can't be observed: on the main
     * thread for the main queue, and on a concurrent queue (which promises no
     * ordering) for a non-barrier item. It also keeps dispatch_sync from
     * deadlocking a fully-busy worker pool. */
    if ((q->is_main && pthread_main_np()) || (!barrier && q->width > 1)
        || (dispatch_queue_t)pthread_getspecific(g_curq_key) == q) {
        dispatch_queue_t prev = (dispatch_queue_t)pthread_getspecific(g_curq_key);
        pthread_setspecific(g_curq_key, q);
        if (block)
            block();
        else if (fn)
            fn(ctx);
        pthread_setspecific(g_curq_key, prev);
        return;
    }

    td_sema_init(&sema);
    it = td_item_new(block, fn, ctx, barrier);
    it->waiter = &sema;
    td_enqueue(q, it);
    td_sema_wait(&sema);
    td_sema_destroy(&sema);
}

void dispatch_sync(dispatch_queue_t q, dispatch_block_t block) { td_sync(q, block, NULL, NULL, 0); }
void dispatch_sync_f(dispatch_queue_t q, void *ctx, dispatch_function_t work) { td_sync(q, NULL, work, ctx, 0); }
void dispatch_barrier_sync(dispatch_queue_t q, dispatch_block_t block) { td_sync(q, block, NULL, NULL, 1); }
void dispatch_barrier_sync_f(dispatch_queue_t q, void *ctx, dispatch_function_t work) { td_sync(q, NULL, work, ctx, 1); }

/* ponytail: dispatch_apply runs the iterations serially on the calling thread.
 * Farming them out to the fixed pool risks self-deadlock when the caller is
 * itself a pool thread. Upgrade path: an overcommit pool just for apply. */
void dispatch_apply(size_t iterations, dispatch_queue_t q, void (^block)(size_t))
{
    size_t i;
    (void)q;
    for (i = 0; i < iterations; i++)
        block(i);
}

void dispatch_apply_f(size_t iterations, dispatch_queue_t q, void *ctx, void (*work)(void *, size_t))
{
    size_t i;
    (void)q;
    for (i = 0; i < iterations; i++)
        work(ctx, i);
}

dispatch_block_t dispatch_block_create(dispatch_block_flags_t flags, dispatch_block_t block)
{
    (void)flags;
    return Block_copy(block);
}

dispatch_block_t dispatch_block_create_with_qos_class(dispatch_block_flags_t flags, dispatch_qos_class_t qos, int rel, dispatch_block_t block)
{
    (void)flags; (void)qos; (void)rel;
    return Block_copy(block);
}

/* -------------------------------------------------------------------- once --- */

/* ponytail: one recursive mutex guards every dispatch_once in the process.
 * Nested onces on different predicates work; contention is irrelevant because
 * the fast path never takes the lock. */
static pthread_mutex_t g_once_lock;
static pthread_once_t g_once_lock_once = PTHREAD_ONCE_INIT;

static void g_once_lock_init(void)
{
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&g_once_lock, &a);
    pthread_mutexattr_destroy(&a);
}

#define TD_ONCE_DONE (~(intptr_t)0)

void dispatch_once_f(dispatch_once_t *pred, void *ctx, dispatch_function_t fn)
{
    if (*(volatile dispatch_once_t *)pred == TD_ONCE_DONE) {
        __sync_synchronize();
        return;
    }
    pthread_once(&g_once_lock_once, g_once_lock_init);
    pthread_mutex_lock(&g_once_lock);
    if (*pred != TD_ONCE_DONE) {
        fn(ctx);
        __sync_synchronize();
        *pred = TD_ONCE_DONE;
    }
    pthread_mutex_unlock(&g_once_lock);
}

static void td_call_block(void *ctx) { ((dispatch_block_t)ctx)(); }

void dispatch_once(dispatch_once_t *pred, dispatch_block_t block)
{
    dispatch_once_f(pred, (void *)block, td_call_block);
}

/* ------------------------------------------------------------------ timers --- */

typedef struct td_timer {
    struct td_timer *next;
    uint64_t deadline;
    uint64_t interval;              /* 0 = one-shot */
    dispatch_queue_t queue;
    dispatch_block_t block;
    dispatch_function_t fn;
    void *ctx;
    dispatch_source_t source;       /* non-NULL for timer sources */
    int cancelled;
} td_timer;

static pthread_mutex_t g_tlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_tcond = PTHREAD_COND_INITIALIZER;
static td_timer *g_timers;
static int g_timer_thread;

static void td_timer_insert_locked(td_timer *t)
{
    td_timer **p = &g_timers;
    while (*p && (*p)->deadline <= t->deadline)
        p = &(*p)->next;
    t->next = *p;
    *p = t;
    pthread_cond_signal(&g_tcond);
}

static void td_source_fire(dispatch_source_t s);

static void *td_timer_thread(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&g_tlock);
    for (;;) {
        td_timer *t;
        uint64_t now;

        while (!g_timers)
            pthread_cond_wait(&g_tcond, &g_tlock);

        now = td_now();
        if (g_timers->deadline > now) {
            struct timespec ts;
            td_deadline_to_timespec(g_timers->deadline, &ts);
            pthread_cond_timedwait(&g_tcond, &g_tlock, &ts);
            continue;
        }

        t = g_timers;
        g_timers = t->next;

        if (t->cancelled) {
            dispatch_source_t src = t->source;
            dispatch_block_t blk = t->block;
            free(t);
            pthread_mutex_unlock(&g_tlock);
            if (blk)
                Block_release(blk);
            if (src)
                td_release_obj(src);
            pthread_mutex_lock(&g_tlock);
            continue;
        }

        if (t->source) {
            dispatch_source_t s = t->source;
            if (t->interval) {
                uint64_t n = td_now();
                do {
                    t->deadline += t->interval;
                } while (t->deadline <= n);
                td_timer_insert_locked(t);
            }
            int repeating = t->interval != 0;
            pthread_mutex_unlock(&g_tlock);
            td_source_fire(s);
            if (!repeating) {
                free(t);
                td_release_obj(s);      /* may re-enter g_tlock; must be unlocked */
            }
            pthread_mutex_lock(&g_tlock);
            continue;
        }

        pthread_mutex_unlock(&g_tlock);
        td_enqueue(t->queue, td_item_new(t->block, t->fn, t->ctx, 0));
        if (t->block)
            Block_release(t->block);
        if (t->queue)
            td_release_obj(t->queue);
        free(t);
        pthread_mutex_lock(&g_tlock);
    }
    return NULL;
}

static void td_timer_thread_ensure(void)
{
    if (!g_timer_thread) {
        pthread_t th;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&th, &attr, td_timer_thread, NULL) == 0)
            g_timer_thread = 1;
        pthread_attr_destroy(&attr);
    }
}

static void td_after(dispatch_time_t when, dispatch_queue_t q, dispatch_block_t block, dispatch_function_t fn, void *ctx)
{
    td_timer *t;
    td_ensure_init();
    if (when == DISPATCH_TIME_FOREVER)
        return;
    t = (td_timer *)calloc(1, sizeof(td_timer));
    t->deadline = when;
    t->queue = q ? q : &g_global[2];
    td_retain_obj(t->queue);
    if (block)
        t->block = Block_copy(block);
    t->fn = fn;
    t->ctx = ctx;
    pthread_mutex_lock(&g_tlock);
    td_timer_thread_ensure();
    td_timer_insert_locked(t);
    pthread_mutex_unlock(&g_tlock);
}

void dispatch_after(dispatch_time_t when, dispatch_queue_t q, dispatch_block_t block)
{
    td_after(when, q, block, NULL, NULL);
}

void dispatch_after_f(dispatch_time_t when, dispatch_queue_t q, void *ctx, dispatch_function_t work)
{
    td_after(when, q, NULL, work, ctx);
}

/* ------------------------------------------------------------------ groups --- */

typedef struct td_notify {
    struct td_notify *next;
    dispatch_queue_t queue;
    dispatch_block_t block;
    dispatch_function_t fn;
    void *ctx;
} td_notify;

dispatch_group_t dispatch_group_create(void)
{
    struct dispatch_group_s *g = (struct dispatch_group_s *)calloc(1, sizeof(*g));
    td_ensure_init();
    g->base.td_refcount = 1;
    g->base.td_kind = TD_KIND_GROUP;
    pthread_mutex_init(&g->lock, NULL);
    pthread_cond_init(&g->cond, NULL);
    return g;
}

void dispatch_group_enter(dispatch_group_t g)
{
    pthread_mutex_lock(&g->lock);
    g->count++;
    pthread_mutex_unlock(&g->lock);
}

void dispatch_group_leave(dispatch_group_t g)
{
    td_notify *n = NULL;
    pthread_mutex_lock(&g->lock);
    if (--g->count <= 0) {
        g->count = 0;
        n = g->notify;
        g->notify = NULL;
        pthread_cond_broadcast(&g->cond);
    }
    pthread_mutex_unlock(&g->lock);
    while (n) {
        td_notify *next = n->next;
        td_enqueue(n->queue, td_item_new(n->block, n->fn, n->ctx, 0));
        if (n->block)
            Block_release(n->block);
        free(n);
        n = next;
    }
}

intptr_t dispatch_group_wait(dispatch_group_t g, dispatch_time_t timeout)
{
    intptr_t r = 0;
    pthread_mutex_lock(&g->lock);
    while (g->count > 0) {
        if (timeout == DISPATCH_TIME_FOREVER) {
            pthread_cond_wait(&g->cond, &g->lock);
        } else {
            struct timespec ts;
            td_deadline_to_timespec(timeout, &ts);
            if (pthread_cond_timedwait(&g->cond, &g->lock, &ts) == ETIMEDOUT) {
                r = g->count > 0 ? ~(intptr_t)0 : 0;
                break;
            }
        }
    }
    pthread_mutex_unlock(&g->lock);
    return r;
}

static void td_group_notify(dispatch_group_t g, dispatch_queue_t q, dispatch_block_t block, dispatch_function_t fn, void *ctx)
{
    int fire = 0;
    pthread_mutex_lock(&g->lock);
    if (g->count <= 0) {
        fire = 1;
    } else {
        td_notify *n = (td_notify *)calloc(1, sizeof(td_notify));
        n->queue = q;
        n->block = block ? Block_copy(block) : NULL;
        n->fn = fn;
        n->ctx = ctx;
        n->next = g->notify;
        g->notify = n;
    }
    pthread_mutex_unlock(&g->lock);
    if (fire)
        td_enqueue(q, td_item_new(block, fn, ctx, 0));
}

void dispatch_group_notify(dispatch_group_t g, dispatch_queue_t q, dispatch_block_t block)
{
    td_group_notify(g, q, block, NULL, NULL);
}

void dispatch_group_notify_f(dispatch_group_t g, dispatch_queue_t q, void *ctx, dispatch_function_t work)
{
    td_group_notify(g, q, NULL, work, ctx);
}

typedef struct { dispatch_group_t group; dispatch_block_t block; dispatch_function_t fn; void *ctx; } td_gasync;

static void td_group_async_run(void *p)
{
    td_gasync *a = (td_gasync *)p;
    if (a->block)
        a->block();
    else if (a->fn)
        a->fn(a->ctx);
    dispatch_group_leave(a->group);
    if (a->block)
        Block_release(a->block);
    td_release_obj(a->group);
    free(a);
}

void dispatch_group_async(dispatch_group_t g, dispatch_queue_t q, dispatch_block_t block)
{
    td_gasync *a = (td_gasync *)calloc(1, sizeof(td_gasync));
    a->group = (dispatch_group_t)td_retain_obj(g);
    a->block = Block_copy(block);
    dispatch_group_enter(g);
    td_enqueue(q, td_item_new(NULL, td_group_async_run, a, 0));
}

void dispatch_group_async_f(dispatch_group_t g, dispatch_queue_t q, void *ctx, dispatch_function_t work)
{
    td_gasync *a = (td_gasync *)calloc(1, sizeof(td_gasync));
    a->group = (dispatch_group_t)td_retain_obj(g);
    a->fn = work;
    a->ctx = ctx;
    dispatch_group_enter(g);
    td_enqueue(q, td_item_new(NULL, td_group_async_run, a, 0));
}

/* -------------------------------------------------------------- semaphores --- */

dispatch_semaphore_t dispatch_semaphore_create(intptr_t value)
{
    struct dispatch_semaphore_s *s;
    if (value < 0)
        return NULL;
    s = (struct dispatch_semaphore_s *)calloc(1, sizeof(*s));
    s->base.td_refcount = 1;
    s->base.td_kind = TD_KIND_SEMAPHORE;
    pthread_mutex_init(&s->lock, NULL);
    pthread_cond_init(&s->cond, NULL);
    s->value = (long)value;
    return s;
}

intptr_t dispatch_semaphore_wait(dispatch_semaphore_t s, dispatch_time_t timeout)
{
    intptr_t r = 0;
    pthread_mutex_lock(&s->lock);
    while (s->value <= 0) {
        if (timeout == DISPATCH_TIME_FOREVER) {
            pthread_cond_wait(&s->cond, &s->lock);
        } else if (timeout == DISPATCH_TIME_NOW) {
            r = ~(intptr_t)0;
            goto out;
        } else {
            struct timespec ts;
            td_deadline_to_timespec(timeout, &ts);
            if (pthread_cond_timedwait(&s->cond, &s->lock, &ts) == ETIMEDOUT) {
                if (s->value <= 0) {
                    r = ~(intptr_t)0;
                    goto out;
                }
            }
        }
    }
    s->value--;
out:
    pthread_mutex_unlock(&s->lock);
    return r;
}

intptr_t dispatch_semaphore_signal(dispatch_semaphore_t s)
{
    intptr_t woke;
    pthread_mutex_lock(&s->lock);
    s->value++;
    woke = s->value <= 0;
    pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->lock);
    return woke;
}

/* ----------------------------------------------------------------- sources --- */

dispatch_source_t dispatch_source_create(dispatch_source_type_t type, uintptr_t handle, uintptr_t mask, dispatch_queue_t queue)
{
    struct dispatch_source_s *s = (struct dispatch_source_s *)calloc(1, sizeof(*s));
    td_ensure_init();
    s->base.td_refcount = 1;
    s->base.td_kind = TD_KIND_SOURCE;
    s->type = type;
    s->handle = handle;
    s->mask = mask;
    s->queue = queue ? queue : &g_global[2];
    td_retain_obj(s->queue);
    s->suspend = 1;     /* sources start suspended, like the real thing */
    return s;
}

void dispatch_source_set_event_handler(dispatch_source_t s, dispatch_block_t h)
{
    if (s->handler)
        Block_release(s->handler);
    s->handler = h ? Block_copy(h) : NULL;
}

void dispatch_source_set_event_handler_f(dispatch_source_t s, dispatch_function_t h) { s->handler_f = h; }

void dispatch_source_set_cancel_handler(dispatch_source_t s, dispatch_block_t h)
{
    if (s->cancel_handler)
        Block_release(s->cancel_handler);
    s->cancel_handler = h ? Block_copy(h) : NULL;
}

void dispatch_source_set_cancel_handler_f(dispatch_source_t s, dispatch_function_t h) { s->cancel_f = h; }

void dispatch_source_set_registration_handler(dispatch_source_t s, dispatch_block_t h)
{
    if (s->reg_handler)
        Block_release(s->reg_handler);
    s->reg_handler = h ? Block_copy(h) : NULL;
}

uintptr_t dispatch_source_get_handle(dispatch_source_t s) { return s->handle; }
uintptr_t dispatch_source_get_mask(dispatch_source_t s) { return s->mask; }
uintptr_t dispatch_source_get_data(dispatch_source_t s) { return s->data; }

static void td_source_fire(dispatch_source_t s)
{
    if (s->cancelled || s->suspend > 0)
        return;
    if (s->handler)
        td_enqueue(s->queue, td_item_new(s->handler, NULL, NULL, 0));
    else if (s->handler_f)
        td_enqueue(s->queue, td_item_new(NULL, s->handler_f, s->base.td_context, 0));
}

void dispatch_source_merge_data(dispatch_source_t s, uintptr_t value)
{
    __sync_fetch_and_or((volatile long *)&s->data, (long)value);
    td_source_fire(s);
}

/* Cancel any armed timer belonging to this source. Caller must not hold g_tlock. */
static void td_source_disarm(dispatch_source_t s)
{
    td_timer **p;
    int released = 0;
    pthread_mutex_lock(&g_tlock);
    p = &g_timers;
    while (*p) {
        if ((*p)->source == s) {
            td_timer *t = *p;
            *p = t->next;
            if (t->block)
                Block_release(t->block);
            free(t);
            released++;
            continue;
        }
        p = &(*p)->next;
    }
    pthread_cond_signal(&g_tcond);
    pthread_mutex_unlock(&g_tlock);
    s->armed = 0;
    /* Drop the timer's references outside g_tlock: the last one can destroy the
     * source, and that path takes g_tlock again. */
    while (released--)
        td_release_obj(s);
}

static void td_source_arm(dispatch_source_t s)
{
    td_timer *t;
    if (s->type != DISPATCH_SOURCE_TYPE_TIMER || !s->timer_set || s->cancelled || s->suspend > 0 || s->armed)
        return;
    t = (td_timer *)calloc(1, sizeof(td_timer));
    t->deadline = s->t_start;
    t->interval = s->t_interval;
    t->source = (dispatch_source_t)td_retain_obj(s);
    s->armed = 1;
    pthread_mutex_lock(&g_tlock);
    td_timer_thread_ensure();
    td_timer_insert_locked(t);
    pthread_mutex_unlock(&g_tlock);
}

void dispatch_source_set_timer(dispatch_source_t s, dispatch_time_t start, uint64_t interval, uint64_t leeway)
{
    (void)leeway;
    if (start == DISPATCH_TIME_NOW)
        start = td_now();
    if (interval == DISPATCH_TIME_FOREVER)
        interval = 0;
    if (s->armed)
        td_source_disarm(s);
    s->t_start = start;
    s->t_interval = interval;
    s->timer_set = 1;
    td_source_arm(s);
}

void dispatch_source_cancel(dispatch_source_t s)
{
    if (s->cancelled)
        return;
    s->cancelled = 1;
    td_source_disarm(s);
    if (s->cancel_handler)
        td_enqueue(s->queue, td_item_new(s->cancel_handler, NULL, NULL, 0));
    else if (s->cancel_f)
        td_enqueue(s->queue, td_item_new(NULL, s->cancel_f, s->base.td_context, 0));
}

intptr_t dispatch_source_testcancel(dispatch_source_t s) { return s->cancelled; }

/* -------------------------------------------------------------------- data --- */

const struct dispatch_data_s _dispatch_data_empty = { { 2, TD_KIND_DATA, 1, NULL, NULL }, NULL, 0, NULL };
const dispatch_block_t _dispatch_data_destructor_free = (dispatch_block_t)(void *)&_dispatch_data_empty;

dispatch_data_t dispatch_data_create(const void *buffer, size_t size, dispatch_queue_t queue, dispatch_block_t destructor)
{
    struct dispatch_data_s *d = (struct dispatch_data_s *)calloc(1, sizeof(*d));
    (void)queue;
    d->base.td_refcount = 1;
    d->base.td_kind = TD_KIND_DATA;
    d->size = size;
    if (destructor == DISPATCH_DATA_DESTRUCTOR_DEFAULT) {
        void *copy = size ? malloc(size) : NULL;
        if (copy && buffer)
            memcpy(copy, buffer, size);
        d->buf = copy;
        d->destructor = NULL;               /* we own it */
    } else if (destructor == DISPATCH_DATA_DESTRUCTOR_FREE) {
        d->buf = buffer;
        d->destructor = NULL;               /* free() it, same path */
    } else {
        d->buf = buffer;
        d->destructor = Block_copy(destructor);
    }
    return d;
}

size_t dispatch_data_get_size(dispatch_data_t d) { return d ? d->size : 0; }

/* ponytail: our data objects are always a single contiguous region, so the
 * applier is invoked exactly once. Concat copies rather than building a rope. */
bool dispatch_data_apply(dispatch_data_t d, bool (^applier)(dispatch_data_t, size_t, const void *, size_t))
{
    if (!d || !d->size)
        return true;
    return applier(d, 0, d->buf, d->size);
}

dispatch_data_t dispatch_data_create_map(dispatch_data_t d, const void **buf, size_t *size)
{
    if (buf)
        *buf = d ? d->buf : NULL;
    if (size)
        *size = d ? d->size : 0;
    return (dispatch_data_t)td_retain_obj((void *)d);
}

dispatch_data_t dispatch_data_create_concat(dispatch_data_t a, dispatch_data_t b)
{
    size_t na = dispatch_data_get_size(a), nb = dispatch_data_get_size(b);
    char *p = (char *)malloc((na + nb) ? (na + nb) : 1);
    dispatch_data_t r;
    if (na)
        memcpy(p, a->buf, na);
    if (nb)
        memcpy(p + na, b->buf, nb);
    r = dispatch_data_create(p, na + nb, NULL, DISPATCH_DATA_DESTRUCTOR_FREE);
    return r;
}

dispatch_data_t dispatch_data_create_subrange(dispatch_data_t d, size_t offset, size_t length)
{
    size_t n = dispatch_data_get_size(d);
    if (offset > n)
        offset = n;
    if (offset + length > n)
        length = n - offset;
    return dispatch_data_create((const char *)d->buf + offset, length, NULL, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
}

/* ------------------------------------------------- retain/release/suspend --- */

void td_destroy(struct dispatch_object_s *o)
{
    if (o->td_static)
        return;
    if (o->td_finalizer)
        o->td_finalizer(o->td_context);

    switch (o->td_kind) {
    case TD_KIND_QUEUE: {
        struct dispatch_queue_s *q = (struct dispatch_queue_s *)o;
        int i;
        for (i = 0; i < q->nspec; i++)
            if (q->spec[i].dtor && q->spec[i].ctx)
                q->spec[i].dtor(q->spec[i].ctx);
        free(q->label);
        break;
    }
    case TD_KIND_GROUP: {
        struct dispatch_group_s *g = (struct dispatch_group_s *)o;
        pthread_mutex_destroy(&g->lock);
        pthread_cond_destroy(&g->cond);
        break;
    }
    case TD_KIND_SEMAPHORE: {
        struct dispatch_semaphore_s *s = (struct dispatch_semaphore_s *)o;
        pthread_mutex_destroy(&s->lock);
        pthread_cond_destroy(&s->cond);
        break;
    }
    case TD_KIND_SOURCE: {
        struct dispatch_source_s *s = (struct dispatch_source_s *)o;
        td_source_disarm(s);
        if (s->handler)
            Block_release(s->handler);
        if (s->cancel_handler)
            Block_release(s->cancel_handler);
        if (s->reg_handler)
            Block_release(s->reg_handler);
        td_release_obj(s->queue);
        break;
    }
    case TD_KIND_DATA: {
        struct dispatch_data_s *d = (struct dispatch_data_s *)o;
        if (d->destructor) {
            d->destructor();
            Block_release(d->destructor);
        } else {
            free((void *)d->buf);
        }
        break;
    }
    case TD_KIND_LOG:
        td_log_destroy(o);
        break;
    default:
        break;
    }
    free(o);
}

void dispatch_retain(dispatch_object_t o) { td_retain_obj(o._do); }
void dispatch_release(dispatch_object_t o) { td_release_obj(o._do); }
void *os_retain(void *o) { return td_retain_obj(o); }
void os_release(void *o) { td_release_obj(o); }

void *dispatch_get_context(dispatch_object_t o) { return o._do ? o._do->td_context : NULL; }

void dispatch_set_context(dispatch_object_t o, void *ctx)
{
    if (o._do)
        o._do->td_context = ctx;
}

void dispatch_set_finalizer_f(dispatch_object_t o, dispatch_function_t f)
{
    if (o._do)
        o._do->td_finalizer = f;
}

void dispatch_set_target_queue(dispatch_object_t o, dispatch_queue_t target)
{
    /* ponytail: the target only affects dispatch_queue_get_specific lookup;
     * execution still goes straight to the shared worker pool. */
    if (o._do && o._do->td_kind == TD_KIND_QUEUE)
        ((struct dispatch_queue_s *)o._do)->target = target;
}

void dispatch_suspend(dispatch_object_t o)
{
    struct dispatch_object_s *b = o._do;
    if (!b)
        return;
    if (b->td_kind == TD_KIND_SOURCE) {
        struct dispatch_source_s *s = (struct dispatch_source_s *)b;
        if (++s->suspend == 1 && s->armed)
            td_source_disarm(s);
    } else if (b->td_kind == TD_KIND_QUEUE) {
        struct dispatch_queue_s *q = (struct dispatch_queue_s *)b;
        pthread_mutex_lock(&g_lock);
        q->suspend++;
        pthread_mutex_unlock(&g_lock);
    }
}

void dispatch_resume(dispatch_object_t o)
{
    struct dispatch_object_s *b = o._do;
    if (!b)
        return;
    if (b->td_kind == TD_KIND_SOURCE) {
        struct dispatch_source_s *s = (struct dispatch_source_s *)b;
        if (s->suspend > 0 && --s->suspend == 0) {
            if (s->reg_handler)
                td_enqueue(s->queue, td_item_new(s->reg_handler, NULL, NULL, 0));
            td_source_arm(s);
        }
    } else if (b->td_kind == TD_KIND_QUEUE) {
        struct dispatch_queue_s *q = (struct dispatch_queue_s *)b;
        pthread_mutex_lock(&g_lock);
        if (q->suspend > 0)
            q->suspend--;
        td_maybe_ready(q);
        pthread_mutex_unlock(&g_lock);
    }
}

void dispatch_activate(dispatch_object_t o)
{
    struct dispatch_object_s *b = o._do;
    if (b && b->td_kind == TD_KIND_SOURCE && ((struct dispatch_source_s *)b)->suspend > 0)
        dispatch_resume(o);
}
