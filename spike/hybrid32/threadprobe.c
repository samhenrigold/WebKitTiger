// hybrid32 Q4: two threads in long-mode code at once, and the cost of the
// 64 -> 32 -> 64 far-call thunk that every libSystem call would have to take.
#include "h32common.h"
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>

// 32-bit thunks, called by far call from the guest and returning with lret.
// These live in our own __text and run in our own CS, so they can call
// libSystem normally.
__asm__(
".text\n.align 4\n"
".globl _t32_null\n_t32_null:\n"
"   lret\n"
".globl _t32_cfn\n_t32_cfn:\n"
"   calll _cfn\n"
"   lret\n"
".globl _t32_repair\n_t32_repair:\n"
"   calll _getpid\n"
"   movl %eax, _t32_pid\n"
"   movl $20, %eax\n"
"   int $0x80\n"            // does an int-0x80 round trip undo whatever sysexit did?
"   lret\n"
".globl _t32_getpid2\n_t32_getpid2:\n"
"   calll _getpid\n"
"   movl %eax, _t32_pid\n"      // marker: did the libSystem call itself return?
"   lret\n"
".globl _t32_int80\n_t32_int80:\n"
"   movl $20, %eax\n"      // SYS_getpid the old way, bypassing libSystem's stub
"   int $0x80\n"
"   movl %eax, _t32_pid\n"
"   lret\n"
".globl _t32_getpid\n_t32_getpid:\n"
"   calll _getpid\n"
"   lret\n"
".globl _t32_gtod\n_t32_gtod:\n"
"   pushl $0\n"
"   pushl $_t32_tv\n"
"   calll _gettimeofday\n"
"   addl $8, %esp\n"
"   lret\n"
".globl _t32_write\n_t32_write:\n"
"   pushl $0\n"          // len 0: the syscall happens, nothing is written
"   pushl $_t32_tv\n"
"   pushl $2\n"
"   calll _write\n"
"   addl $12, %esp\n"
"   lret\n"
".data\n.globl _t32_tv\n_t32_tv:\n   .space 32\n"
".globl _t32_pid\n_t32_pid:\n   .long 0\n");
extern void t32_null(void), t32_gtod(void), t32_write(void);
extern char t32_tv[];
extern void t32_cfn(void), t32_getpid(void), t32_int80(void);
extern int t32_pid;
extern void t32_getpid2(void), t32_repair(void);
#include <signal.h>
#include <ucontext.h>
static volatile uint64_t g_done, g_done2;
static void fault(int sig, siginfo_t* si, void* uap) {
    ucontext_t* uc = uap; struct mcontext* mc = (struct mcontext*)uc->uc_mcontext;
    char b[256];
    int n = snprintf(b, sizeof b, "  FAULT sig=%d eip=0x%x cs=0x%x trapno=%u err=%u faultvaddr=0x%x "
                     "esp=0x%x eax=0x%x ecx=0x%x edx=0x%x t32_pid=%d\n",
                     sig, mc->ss.eip, mc->ss.cs, mc->es.trapno, mc->es.err, mc->es.faultvaddr,
                     mc->ss.esp, mc->ss.eax, mc->ss.ecx, mc->ss.edx, t32_pid);
    n += snprintf(b+n, sizeof b - n, "  survived %llu (+%llu on the worker) mode transitions before this\n",
                  (unsigned long long)g_done, (unsigned long long)g_done2);
    write(1, b, n); _exit(4);
}
static void hung(int s){(void)s; char b[64]; int n=snprintf(b,sizeof b,"  HUNG: t32_pid=%d (0 = never returned from getpid; nonzero = lret is the problem)\n",t32_pid); write(1,b,n); _exit(3);}
int cfn_hits;
void cfn(void) { cfn_hits++; }

__asm__(
".text\n.align 4\n"
".globl _r_beg\n_r_beg:\n"
".code64\n"
// block: [0]=count [1]=expected r15 [2]=corruption count
".globl _r_loop\n_r_loop:\n"
"   movl %edi, %r13d\n"
"   movq 8(%r13), %r15\n"
"   movq 0(%r13), %rcx\n"
"1: cmpq 8(%r13), %r15\n"
"   je 2f\n"
"   incq 16(%r13)\n"
"   movq 8(%r13), %r15\n"
"2: decq %rcx\n"
"   jnz 1b\n"
"   xorl %eax, %eax\n   lret\n"
// block: [0]=count, bytes 8..13 = far pointer (off32, sel16) to the 32-bit thunk
".globl _r_trip\n_r_trip:\n"
"   movl %edi, %r13d\n"
"   movq 0(%r13), %rcx\n"
"1: lcall *8(%r13)\n"
"   decq %rcx\n"
"   jnz 1b\n"
"   xorl %eax, %eax\n   lret\n"
".code32\n"
".globl _r_end\n_r_end:\n");
extern const unsigned char r_beg[], r_loop[], r_trip[], r_end[];

static unsigned char* page;
static double now(void) { struct timeval t; gettimeofday(&t, NULL); return t.tv_sec + t.tv_usec/1e6; }

struct arg { uint64_t* blk; double secs; uint64_t iters; };

static void* spin(void* v) {
    struct arg* a = v;
    double t0 = now();
    while (now() - t0 < a->secs) {
        a->blk[0] = 20000000;
        h32_call64(page + (r_loop - r_beg), (uint32_t)(uintptr_t)a->blk);
        a->iters += 20000000;
    }
    return NULL;
}

static uint64_t* tb;
// second thread doing its own mode transitions, with its own parameter block
static void* tripper(void* v) {
    (void)v;
    uint64_t* b = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    b[0] = 1000;
    *(uint32_t*)((char*)b + 8)  = (uint32_t)(uintptr_t)t32_null;
    *(uint16_t*)((char*)b + 12) = h32_cs32_sel;
    for (;;) { h32_call64(page + (r_trip - r_beg), (uint32_t)(uintptr_t)b); g_done2 += 1000; }
}
static double trip(void (*fn)(void), uint64_t n) {
    uint64_t* b = tb ? tb : (tb = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0));
    b[0] = n;
    *(uint32_t*)((char*)b + 8)  = (uint32_t)(uintptr_t)fn;
    *(uint16_t*)((char*)b + 12) = h32_cs32_sel;
    double t0 = now();
    h32_call64(page + (r_trip - r_beg), (uint32_t)(uintptr_t)b);
    double dt = now() - t0;
    return dt * 1e9 / (double)n;
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    h32_cs64();
    page = h32_load(r_beg, r_end, NULL);
    printf("cs64=0x%x cs32=0x%x\n", h32_cs64_sel, h32_cs32_sel);

    if (argc > 1 && !strcmp(argv[1], "mtbf")) {
        // How many 32<->64 mode transitions does the bridge survive?
        struct sigaction sa; memset(&sa,0,sizeof sa); sa.sa_sigaction=fault; sa.sa_flags=SA_SIGINFO;
        sigaction(SIGILL,&sa,0); sigaction(SIGSEGV,&sa,0); sigaction(SIGBUS,&sa,0); sigaction(SIGTRAP,&sa,0);
        g_done = 0;
        int nthread = argc > 2 ? atoi(argv[2]) : 1;
        for (int t = 1; t < nthread; t++) { pthread_t x; pthread_create(&x, NULL, tripper, NULL); }
        void (*fn)(void) = (argc > 3 && !strcmp(argv[3], "int80")) ? t32_int80 : t32_null;
        for (int i = 0; i < 20000; i++) { trip(fn, 1000); g_done += 1000; }
        printf("  survived all %llu transitions (+%llu on the worker)\n", (unsigned long long)g_done, (unsigned long long)g_done2);
        return 0;
    }
    if (argc > 1) {
        // Bind every lazy stub the thunks use BEFORE any far call: dyld's lazy
        // binder is the other thing that could be unhappy about the frame.
        struct timeval pre; gettimeofday(&pre, NULL); write(2, "", 0); getpid();
        printf("lazy stubs pre-bound\n");
    }
    if (argc > 1) {   // one far call at a time, to find where the trip breaks
        printf("1 x null thunk ...\n");  printf("  %.0f ns\n", trip(t32_null, 1));
        printf("10 x null thunk ...\n"); printf("  %.0f ns\n", trip(t32_null, 10));
        printf("1 x C-function thunk ...\n"); printf("  %.0f ns, hits=%d\n", trip(t32_cfn, 1), cfn_hits);
        printf("1 x getpid+int80-repair thunk (3 s watchdog) ...\n");
        signal(SIGALRM, hung); alarm(3);
        printf("  %.0f ns, pid=%d -- REPAIR WORKED\n", trip(t32_repair, 1), t32_pid);
        alarm(0);
        printf("1 x getpid+marker thunk (3 s watchdog) ...\n");
        signal(SIGALRM, hung); alarm(3);
        { struct sigaction sa; memset(&sa,0,sizeof sa); sa.sa_sigaction=fault; sa.sa_flags=SA_SIGINFO;
          sigaction(SIGILL,&sa,0); sigaction(SIGSEGV,&sa,0); sigaction(SIGBUS,&sa,0); sigaction(SIGTRAP,&sa,0); }
        printf("  _getpid stub at %p, thunk at %p\n", (void*)&getpid, (void*)t32_getpid2);
        printf("  %.0f ns, pid=%d\n", trip(t32_getpid2, 1), t32_pid);
        alarm(0);
        printf("1 x int-0x80 thunk ...\n");   printf("  %.0f ns, pid=%d (real %d)\n", trip(t32_int80, 1), t32_pid, (int)getpid());
        printf("1 x getpid thunk ...\n");     printf("  %.0f ns\n", trip(t32_getpid, 1));
        printf("1 x write thunk ...\n");      printf("  %.0f ns\n", trip(t32_write, 1));
        printf("1 x gtod thunk ...\n");       printf("  %.0f ns\n", trip(t32_gtod, 1));
        printf("tv = %lld\n", (long long)*(long*)&t32_tv);
        return 0;
    }
    uint64_t* b1 = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    uint64_t* b2 = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    memset(b1, 0, 4096); memset(b2, 0, 4096);
    b1[1] = 0xC0FFEE000000000FULL;   // distinct sentinels: a swap between the
    b2[1] = 0xC0FFEE000000001FULL;   // two threads would show up as corruption
    struct arg a1 = { b1, 2.0, 0 }, a2 = { b2, 2.0, 0 };
    pthread_t th; pthread_create(&th, NULL, spin, &a2);
    spin(&a1);
    pthread_join(th, NULL);
    printf("two threads, 2 s each in long mode:\n");
    printf("  main   %llu iterations, %llu corruptions\n", (unsigned long long)a1.iters, (unsigned long long)b1[2]);
    printf("  worker %llu iterations, %llu corruptions\n", (unsigned long long)a2.iters, (unsigned long long)b2[2]);

    // Only these two survive the return to long mode: see the README. A thunk
    // that calls a libSystem stub does complete the call and then wedges on the
    // lret, so gettimeofday()/write() cannot be benchmarked this way at all.
    { struct sigaction sa; memset(&sa,0,sizeof sa); sa.sa_sigaction=fault; sa.sa_flags=SA_SIGINFO;
      sigaction(SIGILL,&sa,0); sigaction(SIGSEGV,&sa,0); sigaction(SIGBUS,&sa,0); sigaction(SIGTRAP,&sa,0); }
    printf("64->32->64 round trip, 1M calls in batches of 1000:\n");
    static const struct { const char* n; void (*f)(void); } K[] = {
        {"empty thunk (lret only)", t32_null}, {"local C function", t32_cfn},
        {"raw int $0x80 getpid", t32_int80} };
    for (int k = 0; k < 3; k++) {
        g_done = 0; double tt = now();
        for (int i = 0; i < 1000; i++) { trip(K[k].f, 1000); g_done += 1000; }
        printf("  %-24s %8.1f ns/call over %llu round trips\n", K[k].n,
               (now()-tt)*1e9/1e6, (unsigned long long)g_done);
    }
    struct timeval tv; double t0 = now();
    for (int i = 0; i < 1000000; i++) gettimeofday(&tv, NULL);
    printf("  plain 32-bit gettimeofday %8.1f ns/call (baseline)\n", (now()-t0)*1e9/1e6);
    t0 = now();
    for (int i = 0; i < 1000000; i++) getpid();
    printf("  plain 32-bit getpid       %8.1f ns/call (baseline)\n", (now()-t0)*1e9/1e6);
    return 0;
}
