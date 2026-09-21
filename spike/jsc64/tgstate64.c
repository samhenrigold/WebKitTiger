/* TIGER64: does thread_get_state(x86_THREAD_STATE64) tell the truth on 10.4.11?
 *
 * xnu-792 is a 32-bit kernel (RELEASE_I386) hosting 64-bit tasks, and this project
 * already caught pthread_get_stackaddr_np() answering a 64-bit process with 32-bit
 * values.  Conservative GC (MachineStackMarker -> Thread::getRegisters) and the
 * sampling profiler both stake everything on this call, so it gets a lie detector
 * rather than a "did it return KERN_SUCCESS" smoke test:
 *
 *   - the victim thread parks with five known sentinels in the callee-saved
 *     registers (rbx, r12-r15); every one must come back verbatim.
 *   - rsp must land inside that thread's own stack, not the main thread's and not
 *     a 32-bit-looking address.
 *   - rip must land in the code the thread is actually executing, tested twice:
 *     once in ordinary __TEXT, once in a plain RWX mmap standing in for JIT memory.
 *   - the in/out count must come back as x86_THREAD_STATE64_COUNT.
 *
 * Build: toolchain/bin/tiger-clang64 -O1 -o tgstate64 tgstate64.c   (-O0 is fine too)
 */
#include <mach/mach.h>
#include <mach/thread_act.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static int failures = 0;
#define CHECK(cond, ...) do { \
    if (cond) printf("PASS  " __VA_ARGS__); \
    else { printf("FAIL  " __VA_ARGS__); failures++; } \
    printf("\n"); \
} while (0)

static const uint64_t kSentinel[5] = {
    0x1234deadbeef0012ull, /* r12 */
    0x1234deadbeef0013ull, /* r13 */
    0x1234deadbeef0014ull, /* r14 */
    0x1234deadbeef0015ull, /* r15 */
    0x1234deadbeef00bbull, /* rbx */
};

static volatile int g_go;          /* victim spins until this is set */
static volatile int g_parked;      /* victim has its sentinels loaded */
static uintptr_t g_victimLocal;    /* an address on the victim's stack */
static uintptr_t g_victimStackLo, g_victimStackHi;
static void (*g_jitSpin)(volatile int *go, volatile int *parked);

/* Park with the sentinels live in callee-saved registers.  The clobber list keeps
 * the compiler from handing us one of those registers for the operands. */
static void park(void)
{
    __asm__ volatile (
        "movq  0(%0), %%r12\n\t"
        "movq  8(%0), %%r13\n\t"
        "movq 16(%0), %%r14\n\t"
        "movq 24(%0), %%r15\n\t"
        "movq 32(%0), %%rbx\n\t"
        "movl $1, (%2)\n\t"
        "1:\n\t"
        "pause\n\t"
        "cmpl $0, (%1)\n\t"
        "je 1b\n\t"
        : : "r"(kSentinel), "r"(&g_go), "r"(&g_parked)
        : "r12", "r13", "r14", "r15", "rbx", "memory", "cc");
}

static void *victim(void *unused)
{
    (void)unused;
    int local = 0;
    g_victimLocal = (uintptr_t)&local;
    void *addr = pthread_get_stackaddr_np(pthread_self());
    size_t size = pthread_get_stacksize_np(pthread_self());
    g_victimStackHi = (uintptr_t)addr;
    g_victimStackLo = g_victimStackHi - size;
    park();
    return NULL;
}

/* Same spin loop, but assembled by hand and executed out of an RWX mapping, which
 * is what JSC's executable allocator hands the JIT on Tiger (no MAP_JIT, no W^X). */
static void *jitVictim(void *unused)
{
    (void)unused;
    g_jitSpin(&g_go, &g_parked);
    return NULL;
}

static int sample(pthread_t thread, x86_thread_state64_t *out, mach_msg_type_number_t *outCount)
{
    mach_port_t port = pthread_mach_thread_np(thread);
    if (thread_suspend(port) != KERN_SUCCESS) { printf("FAIL  thread_suspend\n"); failures++; return 0; }
    /* Give the suspend a moment to take effect the way MachineStackMarker does not
     * have to: thread_suspend is synchronous, this is belt and braces on one core. */
    mach_msg_type_number_t count = x86_THREAD_STATE64_COUNT;
    memset(out, 0, sizeof(*out));
    kern_return_t kr = thread_get_state(port, x86_THREAD_STATE64, (thread_state_t)out, &count);
    thread_resume(port);
    if (kr != KERN_SUCCESS) {
        printf("FAIL  thread_get_state(x86_THREAD_STATE64) -> %d (%s)\n", kr, mach_error_string(kr));
        failures++;
        return 0;
    }
    *outCount = count;
    return 1;
}

static void waitParked(void)
{
    while (!g_parked) usleep(1000);
}

int main(void)
{
    printf("tgstate64: pointer size %zu, x86_THREAD_STATE64_COUNT %u, sizeof(x86_thread_state64_t) %zu\n",
           sizeof(void *), (unsigned)x86_THREAD_STATE64_COUNT, sizeof(x86_thread_state64_t));

    /* ---- 1. ordinary __TEXT victim ---- */
    pthread_t t;
    g_go = g_parked = 0;
    pthread_create(&t, NULL, victim, NULL);
    waitParked();

    x86_thread_state64_t st;
    mach_msg_type_number_t count = 0;
    if (sample(t, &st, &count)) {
        CHECK(count == x86_THREAD_STATE64_COUNT, "count returned %u (want %u)", (unsigned)count, (unsigned)x86_THREAD_STATE64_COUNT);
        CHECK(st.r12 == kSentinel[0], "r12 = 0x%llx", (unsigned long long)st.r12);
        CHECK(st.r13 == kSentinel[1], "r13 = 0x%llx", (unsigned long long)st.r13);
        CHECK(st.r14 == kSentinel[2], "r14 = 0x%llx", (unsigned long long)st.r14);
        CHECK(st.r15 == kSentinel[3], "r15 = 0x%llx", (unsigned long long)st.r15);
        CHECK(st.rbx == kSentinel[4], "rbx = 0x%llx", (unsigned long long)st.rbx);
        CHECK(st.rsp > 0xffffffffull, "rsp 0x%llx is a 64-bit address (not a 32-bit-looking one)", (unsigned long long)st.rsp);
        CHECK(st.rsp >= g_victimStackLo && st.rsp <= g_victimStackHi,
              "rsp 0x%llx inside the victim stack [0x%llx,0x%llx)",
              (unsigned long long)st.rsp, (unsigned long long)g_victimStackLo, (unsigned long long)g_victimStackHi);
        {
            uintptr_t d = st.rip > (uintptr_t)park ? st.rip - (uintptr_t)park : (uintptr_t)park - st.rip;
            CHECK(d < 0x1000, "rip 0x%llx is inside park() at %p (delta %llu)",
                  (unsigned long long)st.rip, (void *)park, (unsigned long long)d);
        }
        /* The conservative scan walks [rsp, stack top).  A local of the victim has to
         * be in that window or the GC would miss every root the victim is holding. */
        CHECK(g_victimLocal >= st.rsp && g_victimLocal < g_victimStackHi,
              "victim local 0x%llx is within [rsp, stack top)", (unsigned long long)g_victimLocal);
    }
    g_go = 1;
    pthread_join(t, NULL);

    /* ---- 2. victim executing from RWX (JIT) memory ---- */
    /* void spin(volatile int *go /rdi/, volatile int *parked /rsi/):
     *   movl $1,(%rsi); 1: pause; cmpl $0,(%rdi); je 1b; ret        */
    static const unsigned char kSpin[] = {
        0xc7, 0x06, 0x01, 0x00, 0x00, 0x00,  /* movl $1, (%rsi)   */
        0xf3, 0x90,                          /* pause             */
        0x83, 0x3f, 0x00,                    /* cmpl $0, (%rdi)   */
        0x74, 0xf9,                          /* je -7             */
        0xc3,                                /* ret               */
    };
    size_t page = getpagesize();
    void *jit = mmap(NULL, page, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (jit == MAP_FAILED) {
        printf("FAIL  RWX mmap for the JIT-memory case\n");
        failures++;
    } else {
        memcpy(jit, kSpin, sizeof(kSpin));
        memcpy(&g_jitSpin, &jit, sizeof(jit));
        g_go = g_parked = 0;
        pthread_create(&t, NULL, jitVictim, NULL);
        waitParked();
        if (sample(t, &st, &count)) {
            CHECK(st.rip >= (uintptr_t)jit && st.rip < (uintptr_t)jit + sizeof(kSpin),
                  "rip 0x%llx is inside the RWX mapping at %p (JIT-code rip is reported)",
                  (unsigned long long)st.rip, jit);
        }
        g_go = 1;
        pthread_join(t, NULL);
    }

    /* ---- 3. repeat suspend/get_state/resume, the way a GC or profiler does ---- */
    g_go = g_parked = 0;
    pthread_create(&t, NULL, victim, NULL);
    waitParked();
    int bad = 0;
    for (int i = 0; i < 2000; i++) {
        if (!sample(t, &st, &count)) { bad++; break; }
        if (count != x86_THREAD_STATE64_COUNT || st.r12 != kSentinel[0] || st.rbx != kSentinel[4])
            bad++;
    }
    CHECK(bad == 0, "2000 suspend/get_state/resume rounds, %d bad", bad);
    g_go = 1;
    pthread_join(t, NULL);

    printf("%s (%d failure%s)\n", failures ? "FAILURES" : "ALL PASS", failures, failures == 1 ? "" : "s");
    return failures != 0;
}
