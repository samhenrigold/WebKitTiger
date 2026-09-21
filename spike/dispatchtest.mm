// libtigerdispatch smoke test. Runs on the Tiger box; must print PASS lines and exit 0.
#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>
#include <os/lock.h>
#include <os/log.h>
#include <os/signpost.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <unistd.h>
#include <vector>

static int g_failures;

static void check(bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    fflush(stdout);
    if (!ok)
        g_failures++;
}

// ---------------------------------------------------------------- once ----

static dispatch_once_t g_once;
static int g_onceRuns;
static dispatch_once_t g_outerOnce;
static int g_outerRuns;

static void *onceThread(void *)
{
    dispatch_once(&g_once, ^{ __sync_fetch_and_add(&g_onceRuns, 1); usleep(1000); });
    return 0;
}

static void testOnce()
{
    pthread_t t[8];
    for (int i = 0; i < 8; i++)
        pthread_create(&t[i], 0, onceThread, 0);
    for (int i = 0; i < 8; i++)
        pthread_join(t[i], 0);
    check(g_onceRuns == 1, "dispatch_once runs exactly once under contention");

    // Nested once on a different predicate must not deadlock.
    dispatch_once(&g_outerOnce, ^{
        static dispatch_once_t inner;
        dispatch_once(&inner, ^{ g_outerRuns++; });
    });
    check(g_outerRuns == 1, "dispatch_once nests on distinct predicates");
}

// ------------------------------------------------------------ unfair lock --

static os_unfair_lock g_ul = OS_UNFAIR_LOCK_INIT;
static long g_ulCounter;

static void *lockThread(void *)
{
    for (int i = 0; i < 20000; i++) {
        os_unfair_lock_lock(&g_ul);
        g_ulCounter++;
        os_unfair_lock_assert_owner(&g_ul);
        os_unfair_lock_unlock(&g_ul);
    }
    return 0;
}

static void testUnfairLock()
{
    pthread_t t[4];
    for (int i = 0; i < 4; i++)
        pthread_create(&t[i], 0, lockThread, 0);
    for (int i = 0; i < 4; i++)
        pthread_join(t[i], 0);
    check(g_ulCounter == 4 * 20000, "os_unfair_lock serializes 4 threads");

    os_unfair_lock l = OS_UNFAIR_LOCK_INIT;
    bool got = os_unfair_lock_trylock(&l);
    check(got, "os_unfair_lock_trylock on a free lock");
    os_unfair_lock_unlock(&l);
}

// ---------------------------------------------------------------- queues --

static void testSerialQueue()
{
    dispatch_queue_t q = dispatch_queue_create("test.serial", DISPATCH_QUEUE_SERIAL_WITH_AUTORELEASE_POOL);
    __block std::vector<int> order;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);

    for (int i = 0; i < 200; i++)
        dispatch_async(q, ^{ order.push_back(i); });
    dispatch_async(q, ^{ dispatch_semaphore_signal(done); });

    bool timedOut = dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 10 * (int64_t)NSEC_PER_SEC)) != 0;
    bool inOrder = !timedOut && order.size() == 200;
    for (size_t i = 0; inOrder && i < order.size(); i++)
        inOrder = order[i] == (int)i;
    check(inOrder, "serial queue runs 200 blocks in FIFO order");

    // dispatch_sync / barrier_sync from this thread onto the serial queue.
    __block int syncValue = 0;
    dispatch_sync(q, ^{ syncValue = 41; });
    dispatch_barrier_sync(q, ^{ syncValue++; });
    check(syncValue == 42, "dispatch_sync + dispatch_barrier_sync on a serial queue");

    // queue-specific
    static const char key = 0;
    dispatch_queue_set_specific(q, &key, (void *)0xBEEF, NULL);
    __block void *seen = NULL;
    dispatch_sync(q, ^{ seen = dispatch_get_specific(&key); });
    check(seen == (void *)0xBEEF && dispatch_queue_get_specific(q, &key) == (void *)0xBEEF,
        "dispatch_queue_set_specific / get_specific");

    dispatch_release(q);
}

static void testConcurrentQueue()
{
    dispatch_queue_t q = dispatch_queue_create("test.concurrent", DISPATCH_QUEUE_CONCURRENT);
    dispatch_group_t g = dispatch_group_create();
    __block volatile int32_t counter = 0;

    for (int i = 0; i < 500; i++) {
        dispatch_group_enter(g);
        dispatch_async(q, ^{
            __sync_fetch_and_add(&counter, 1);
            dispatch_group_leave(g);
        });
    }
    bool ok = dispatch_group_wait(g, dispatch_time(DISPATCH_TIME_NOW, 10 * (int64_t)NSEC_PER_SEC)) == 0;
    check(ok && counter == 500, "concurrent queue + dispatch_group_enter/leave/wait (500 blocks)");

    // group_notify after the group is already empty, and on a fresh group.
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    dispatch_group_notify(g, q, ^{ dispatch_semaphore_signal(done); });
    check(dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 5 * (int64_t)NSEC_PER_SEC)) == 0,
        "dispatch_group_notify on an empty group");

    dispatch_release(g);
    dispatch_release(q);
}

static void testGlobalQueue()
{
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    __block bool ran = false;
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        ran = true;
        dispatch_semaphore_signal(sem);
    });
    check(dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 5 * (int64_t)NSEC_PER_SEC)) == 0 && ran,
        "dispatch_async onto the global queue");

    check(dispatch_semaphore_wait(sem, DISPATCH_TIME_NOW) != 0, "dispatch_semaphore_wait times out when empty");
}

static void testApplyAndFunctionVariants()
{
    __block volatile int32_t sum = 0;
    dispatch_apply(100, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^(size_t i) {
        __sync_fetch_and_add(&sum, (int32_t)i);
    });
    check(sum == 4950, "dispatch_apply covers every index");

    struct Ctx { dispatch_semaphore_t sem; int value; };
    static Ctx ctx;
    ctx.sem = dispatch_semaphore_create(0);
    ctx.value = 0;
    dispatch_queue_t q = dispatch_queue_create("test.f", DISPATCH_QUEUE_SERIAL);
    dispatch_async_f(q, &ctx, [](void *p) {
        Ctx *c = (Ctx *)p;
        c->value += 7;
        dispatch_semaphore_signal(c->sem);
    });
    dispatch_semaphore_wait(ctx.sem, dispatch_time(DISPATCH_TIME_NOW, 5 * (int64_t)NSEC_PER_SEC));
    dispatch_sync_f(q, &ctx, [](void *p) { ((Ctx *)p)->value += 35; });
    check(ctx.value == 42, "dispatch_async_f + dispatch_sync_f");
    dispatch_release(q);
}

// ----------------------------------------------------------------- after --

static void testAfter()
{
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    uint64_t t0 = (uint64_t)(CFAbsoluteTimeGetCurrent() * 1e9);
    __block uint64_t t1 = 0;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 200 * 1000 * 1000), dispatch_get_global_queue(0, 0), ^{
        t1 = (uint64_t)(CFAbsoluteTimeGetCurrent() * 1e9);
        dispatch_semaphore_signal(sem);
    });
    bool fired = dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 5 * (int64_t)NSEC_PER_SEC)) == 0;
    uint64_t elapsed = fired ? t1 - t0 : 0;
    check(fired && elapsed >= 150000000ull && elapsed < 2000000000ull,
        "dispatch_after fires at roughly the requested delay");
}

// ---------------------------------------------------------------- source --

static void testTimerSource()
{
    dispatch_queue_t q = dispatch_queue_create("test.timer", DISPATCH_QUEUE_SERIAL);
    dispatch_source_t timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, q);
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    __block int ticks = 0;

    dispatch_source_set_event_handler(timer, ^{
        if (++ticks == 5)
            dispatch_semaphore_signal(sem);
    });
    dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW, 50 * 1000 * 1000), 50 * 1000 * 1000, 0);
    dispatch_resume(timer);

    bool ok = dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 10 * (int64_t)NSEC_PER_SEC)) == 0;
    check(ok, "repeating DISPATCH_SOURCE_TYPE_TIMER fires 5 times");

    dispatch_suspend(timer);
    int atSuspend = ticks;
    usleep(300 * 1000);
    check(ticks == atSuspend, "dispatch_suspend stops a timer source");
    dispatch_resume(timer);
    usleep(200 * 1000);
    check(ticks > atSuspend, "dispatch_resume restarts a timer source");

    __block bool cancelled = false;
    dispatch_source_set_cancel_handler(timer, ^{ cancelled = true; });
    dispatch_source_cancel(timer);
    usleep(300 * 1000);
    check(dispatch_source_testcancel(timer) && cancelled, "dispatch_source_cancel + cancel handler");

    dispatch_release(timer);
    dispatch_release(q);
}

static void testInertSources()
{
    // MEMORYPRESSURE / VNODE / MACH_RECV must be creatable and cancellable without
    // firing or crashing -- WebKit builds them unconditionally.
    dispatch_queue_t q = dispatch_queue_create("test.inert", DISPATCH_QUEUE_SERIAL);
    dispatch_source_t mp = dispatch_source_create(DISPATCH_SOURCE_TYPE_MEMORYPRESSURE, 0,
        DISPATCH_MEMORYPRESSURE_NORMAL | DISPATCH_MEMORYPRESSURE_WARN | DISPATCH_MEMORYPRESSURE_CRITICAL, q);
    dispatch_source_set_event_handler(mp, ^{ (void)dispatch_source_get_data(mp); });
    dispatch_resume(mp);
    dispatch_source_cancel(mp);
    dispatch_release(mp);

    dispatch_source_t vn = dispatch_source_create(DISPATCH_SOURCE_TYPE_VNODE, 0, DISPATCH_VNODE_WRITE, q);
    dispatch_resume(vn);
    dispatch_source_cancel(vn);
    dispatch_release(vn);
    dispatch_release(q);
    check(true, "inert MEMORYPRESSURE / VNODE sources create, resume and cancel");
}

// ------------------------------------------------------------------ data --

static void testData()
{
    const char *text = "tigerdispatch";
    dispatch_data_t d = dispatch_data_create(text, strlen(text), NULL, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    __block size_t seen = 0;
    __block bool matches = false;
    dispatch_data_apply(d, ^bool(dispatch_data_t, size_t, const void *buf, size_t size) {
        seen += size;
        matches = size == strlen(text) && !memcmp(buf, text, size);
        return true;
    });
    check(dispatch_data_get_size(d) == strlen(text) && seen == strlen(text) && matches,
        "dispatch_data_create + dispatch_data_apply");
    dispatch_release(d);
}

// ----------------------------------------------------------------- os_log --

static void testOSLog()
{
    os_log_t log = os_log_create("com.tiger.dispatchtest", "Test");
    os_log(log, "plain default line, pointer=%p", (void *)log);
    os_log_error(log, "error line with %{public}s and %d", "a public string", 7);
    os_log_fault(OS_LOG_DEFAULT, "fault line on OS_LOG_DEFAULT");
    os_log_info(log, "info line %s", "(suppressed unless TIGER_OS_LOG=info)");
    os_log_debug(log, "debug line");
    check(os_log_type_enabled(log, OS_LOG_TYPE_ERROR), "os_log_type_enabled(ERROR) is true by default");
    os_release(log);

    // signposts must compile and do nothing
    os_signpost_id_t sid = os_signpost_id_make_with_pointer(OS_LOG_DEFAULT, &g_failures);
    os_signpost_interval_begin(OS_LOG_DEFAULT, sid, "Span");
    os_signpost_interval_end(OS_LOG_DEFAULT, sid, "Span");
    check(sid != 0, "os_signpost macros compile and no-op");
}

// ------------------------------------------------------------- main queue --

static volatile int g_mainDone;
static int g_mainQueueHits;
static pthread_t g_mainThread;
static bool g_onMainThread;

static void *mainPoster(void *)
{
    for (int i = 0; i < 10; i++) {
        dispatch_async(dispatch_get_main_queue(), ^{
            g_mainQueueHits++;
            g_onMainThread = pthread_equal(pthread_self(), g_mainThread) != 0;
        });
        usleep(5 * 1000);
    }
    dispatch_async(dispatch_get_main_queue(), ^{ g_mainDone = 1; });
    return 0;
}

static void testMainQueue()
{
    g_mainThread = pthread_self();

    // dispatch_sync to the main queue from the main thread must run inline.
    __block bool inline_ran = false;
    dispatch_sync(dispatch_get_main_queue(), ^{ inline_ran = true; });
    check(inline_ran, "dispatch_sync onto the main queue from the main thread");

    pthread_t t;
    pthread_create(&t, 0, mainPoster, 0);

    CFAbsoluteTime deadline = CFAbsoluteTimeGetCurrent() + 10.0;
    while (!g_mainDone && CFAbsoluteTimeGetCurrent() < deadline)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false);
    pthread_join(t, 0);

    check(g_mainDone && g_mainQueueHits == 10 && g_onMainThread,
        "10 blocks dispatched from a background thread ran on the main run loop");
}

int main()
{
    printf("libtigerdispatch test\n");
    testOnce();
    testUnfairLock();
    testSerialQueue();
    testConcurrentQueue();
    testGlobalQueue();
    testApplyAndFunctionVariants();
    testAfter();
    testTimerSource();
    testInertSources();
    testData();
    testOSLog();
    testMainQueue();
    printf("%s (%d failures)\n", g_failures ? "FAILED" : "ALL PASS", g_failures);
    return g_failures ? 1 : 0;
}
