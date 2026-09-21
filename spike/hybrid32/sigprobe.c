// hybrid32 Q1: what does a 32-bit signal handler see when the fault happened in
// long-mode code, and can it resume that code?
//
// Each case runs in a forked child: 64-bit guest fills r8..r15 and the upper
// halves of rax..rdi with sentinels, faults (ud2 / hlt / load from PROT_NONE /
// int3), and -- if the handler manages to resume it -- writes every register
// back to a shared buffer so the host can see which 64-bit state survived.
#include "h32common.h"
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>

#define BADPAGE 0x30000000u

// ---- 64-bit guest -----------------------------------------------------------
#define FILL \
"   movabsq $0xC0FFEE0000000008, %r8\n"\
"   movabsq $0xC0FFEE0000000009, %r9\n"\
"   movabsq $0xC0FFEE000000000A, %r10\n"\
"   movabsq $0xC0FFEE000000000B, %r11\n"\
"   movabsq $0xC0FFEE000000000C, %r12\n"\
"   movabsq $0xC0FFEE000000000E, %r14\n"\
"   movabsq $0xC0FFEE000000000F, %r15\n"\
"   movabsq $0x1111000100000000, %rax\n"\
"   movabsq $0x1111000200000000, %rcx\n"\
"   movabsq $0x1111000300000000, %rdx\n"\
"   movabsq $0x1111000400000000, %rsi\n"\
"   movabsq $0x1111000500000000, %rdi\n"

#define STORE \
"   movq %r8,   0(%r13)\n"\
"   movq %r9,   8(%r13)\n"\
"   movq %r10, 16(%r13)\n"\
"   movq %r11, 24(%r13)\n"\
"   movq %r12, 32(%r13)\n"\
"   movq %r14, 40(%r13)\n"\
"   movq %r15, 48(%r13)\n"\
"   movq %rax, 56(%r13)\n"\
"   movq %rcx, 64(%r13)\n"\
"   movq %rdx, 72(%r13)\n"\
"   movq %rsi, 80(%r13)\n"\
"   movq %rdi, 88(%r13)\n"\
"   movq $0x5A5A5A5A, 96(%r13)\n"

__asm__(
".text\n.align 4\n"
".globl _g_beg\n_g_beg:\n"
".code64\n"
".globl _g_ud2\n_g_ud2:\n"
"   movl %edi, %r13d\n" FILL
".globl _g_ud2_at\n_g_ud2_at:\n   ud2\n"
".globl _g_ud2_after\n_g_ud2_after:\n" STORE "   xorl %eax,%eax\n   lret\n"

".globl _g_hlt\n_g_hlt:\n"
"   movl %edi, %r13d\n" FILL
".globl _g_hlt_at\n_g_hlt_at:\n   hlt\n"
".globl _g_hlt_after\n_g_hlt_after:\n" STORE "   xorl %eax,%eax\n   lret\n"

".globl _g_segv\n_g_segv:\n"
"   movl %edi, %r13d\n" FILL
".globl _g_segv_at\n_g_segv_at:\n   movl 0x30000000, %eax\n"
".globl _g_segv_after\n_g_segv_after:\n" STORE "   xorl %eax,%eax\n   lret\n"

".globl _g_int3\n_g_int3:\n"
"   movl %edi, %r13d\n" FILL
".globl _g_int3_at\n_g_int3_at:\n   int3\n"
".globl _g_int3_after\n_g_int3_after:\n" STORE "   xorl %eax,%eax\n   lret\n"

// wake test: spin in long mode until the SIGALRM handler sets out[18], then
// record the CPU mode. `movl %eax, 0x30001000` has the same encoding and
// meaning in both modes, so the marker survives even if we got demoted.
".globl _g_wake\n_g_wake:\n"
"   movl %edi, %r13d\n" FILL
"1: cmpl $0, 144(%r13)\n"
"   je 1b\n"
"   movl $1, %eax\n"
"   incq %rax\n"                        // 2 = still long mode, 1 = 32-bit decode
"   movl %eax, 0x30001000\n"
"   movq %r15, 0x30001008\n"
"   movq %rdi, 0x30001010\n"
"   lret\n"

// spin loop: rcx = iteration count from 104(%r13); every iteration re-checks
// r15 against 112(%r13) and rdi against 128(%r13), counting mismatches.
".globl _g_loop\n_g_loop:\n"
"   movl %edi, %r13d\n" FILL
"   movq 104(%r13), %rcx\n"
"1:\n"
"   cmpq 112(%r13), %r15\n"
"   je 2f\n"
"   incl 120(%r13)\n"
"   movabsq $0xC0FFEE000000000F, %r15\n"
"2:\n"
"   cmpq 128(%r13), %rdi\n"
"   je 3f\n"
"   incl 136(%r13)\n"
"   movabsq $0x1111000500000000, %rdi\n"
"3:\n"
"   decq %rcx\n"
"   jnz 1b\n"
STORE
"   movl 120(%r13), %eax\n"
"   addl 136(%r13), %eax\n"
"   lret\n"
".code32\n"
".globl _g_end\n_g_end:\n");

extern const unsigned char g_beg[], g_end[];
extern const unsigned char g_ud2[], g_ud2_at[], g_ud2_after[];
extern const unsigned char g_hlt[], g_hlt_at[], g_hlt_after[];
extern const unsigned char g_segv[], g_segv_at[], g_segv_after[];
extern const unsigned char g_int3[], g_int3_at[], g_int3_after[];
extern const unsigned char g_loop[], g_wake[];

// ---- host -------------------------------------------------------------------
static unsigned char* page;
static uint32_t fault_at, fault_after;
static volatile int nsig, nalrm, resumed;
static uint64_t* out;

static const char* rname[13] = {"r8","r9","r10","r11","r12","r14","r15",
                                "rax","rcx","rdx","rsi","rdi","marker"};
static const uint64_t expect[13] = {
    0xC0FFEE0000000008ULL,0xC0FFEE0000000009ULL,0xC0FFEE000000000AULL,
    0xC0FFEE000000000BULL,0xC0FFEE000000000CULL,0xC0FFEE000000000EULL,
    0xC0FFEE000000000FULL,0x1111000100000000ULL,0x1111000200000000ULL,
    0x1111000300000000ULL,0x1111000400000000ULL,0x1111000500000000ULL,
    0x5A5A5A5AULL };

static int setcs, tramp;
// Trampoline escape: instead of sigreturn (which always comes back in 32-bit
// mode), the handler restores the interrupted esp and far-jumps into the guest
// itself. r8-r15 are invisible to 32-bit code, so if the kernel left them alone
// the guest's 64-bit state may still be live in the CPU.
uint32_t tramp_esp;
struct { uint32_t off; uint16_t seg; } __attribute__((packed)) tramp_fp;
static volatile uint32_t* wake_flag;
// Look for the 64-bit sentinels anywhere in the frame the kernel handed us.
static void scan(const char* what, const void* base, size_t n) {
    const uint32_t* w = (const uint32_t*)base;
    int hi = 0, up = 0, firsthi = -1;
    for (size_t i = 0; i < n/4; i++) {
        if (w[i] == 0xC0FFEE00u) { if (!hi) firsthi = (int)(i*4); hi++; }
        if ((w[i] & 0xFFFF0000u) == 0x11110000u) up++;
    }
    printf("  scan %-24s %4lu bytes: r8-r15 sentinel x%d (first @+%d), rax-rdi upper x%d\n",
           what, (unsigned long)n, hi, firsthi, up);
}

static void handler(int sig, siginfo_t* si, void* uap) {
    ucontext_t* uc = (ucontext_t*)uap;
    struct mcontext* mc = (struct mcontext*)uc->uc_mcontext;
    nsig++;
    if (nsig > 4) { _exit(90); }   // fault repeating: handler could not skip it
    printf("  [sig %d] uc_mcsize=%lu (i386 mcontext=%lu) si_addr=%p si_code=%d\n",
           sig, (unsigned long)uc->uc_mcsize, (unsigned long)(I386_MCONTEXT_SIZE), si->si_addr, si->si_code);
    printf("  [sig %d] trapno=%u err=%u faultvaddr=0x%x | eip=0x%x cs=0x%x ss=0x%x esp=0x%x eflags=0x%x\n",
           sig, mc->es.trapno, mc->es.err, mc->es.faultvaddr,
           mc->ss.eip, mc->ss.cs, mc->ss.ss, mc->ss.esp, mc->ss.eflags);
    printf("  [sig %d] eax=0x%x ecx=0x%x edx=0x%x esi=0x%x edi=0x%x ebx=0x%x ebp=0x%x\n",
           sig, mc->ss.eax, mc->ss.ecx, mc->ss.edx, mc->ss.esi, mc->ss.edi, mc->ss.ebx, mc->ss.ebp);
    printf("  [sig %d] expected fault eip=0x%x -> %s ; cs matches cs64(0x%x)=%s\n",
           sig, fault_at, mc->ss.eip == fault_at ? "MATCH" : "no",
           h32_cs64_sel, mc->ss.cs == h32_cs64_sel ? "yes" : "no");
    scan("mcontext", mc, uc->uc_mcsize);
    scan("ucontext", uc, sizeof *uc);
    scan("sigframe stack below uc", (const char*)uc - 2048, 2048);
    if (!fault_after) {   // alarm mode: a fault here is fatal, just report it
        printf("  FAULT after %d SIGALRMs: sig=%d eip=0x%x cs=0x%x faultvaddr=0x%x mode-marker=%u\n",
               nalrm, sig, mc->ss.eip, mc->ss.cs, mc->es.faultvaddr, *(volatile uint32_t*)0x30001000u);
        _exit(92);
    }
    resumed = 1;
    if (tramp) {
        tramp_esp = mc->ss.esp;
        tramp_fp.off = fault_after; tramp_fp.seg = h32_cs64_sel;
        printf("  trampolining back to 0x%x:0x%x with esp=0x%x\n",
               tramp_fp.seg, tramp_fp.off, tramp_esp);
        __asm__ volatile ("movl _tramp_esp, %esp\n\tljmp *_tramp_fp");
    }
    mc->ss.eip = fault_after;   // skip the faulting instruction
    if (setcs) mc->ss.cs = h32_cs64_sel;   // try to resume in long mode
}

static void alrm(int sig, siginfo_t* si, void* uap) {
    ucontext_t* uc = (ucontext_t*)uap;
    struct mcontext* mc = (struct mcontext*)uc->uc_mcontext;
    if (nalrm == 0)
        printf("  [alrm] uc_mcsize=%lu eip=0x%x cs=0x%x (cs64=0x%x, in-guest=%s)\n",
               (unsigned long)uc->uc_mcsize, mc->ss.eip, mc->ss.cs, h32_cs64_sel,
               mc->ss.cs == h32_cs64_sel ? "yes" : "no");
    nalrm++;
    if (wake_flag) *wake_flag = 1;
}

static void install(int sig, void (*fn)(int, siginfo_t*, void*), int extra) {
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = fn;
    sa.sa_flags = SA_SIGINFO | extra;
    sigemptyset(&sa.sa_mask);
    if (sigaction(sig, &sa, NULL) < 0) { printf("  sigaction(%d, flags=0x%x): %s\n", sig, sa.sa_flags, strerror(errno)); _exit(91); }
}

static int report(void) {
    int bad = 0;
    for (int i = 0; i < 13; i++)
        if (out[i] != expect[i]) { printf("  CORRUPT %-4s = 0x%016llx (want 0x%016llx)\n", rname[i], out[i], expect[i]); bad++; }
    printf("  resumed=%d signals=%d registers-intact=%d/13\n", resumed, nsig, 13 - bad);
    return bad;
}

static pid_t g_child;
static const char* g_mode = "-";
static void wd(int s) { (void)s; if (g_child) kill(g_child, SIGKILL); }

// Run every case in a forked child under a 8 s watchdog: a fault the kernel
// cannot deliver as a signal shows up as an endless re-execution loop.
static int run_all(const char* self) {
    static const char* cases[] = {"ud2","hlt","segv","int3","alarm","wake","blocked"};
    for (int i = 0; i < 7; i++) for (int rs = 0; rs < 2; rs++) {
        fflush(stdout);
        g_child = fork();
        if (g_child == 0) { execl(self, self, cases[i], rs ? "64" : "32", g_mode, (char*)0); _exit(127); }
        signal(SIGALRM, wd); alarm(8);
        int st = 0; waitpid(g_child, &st, 0); alarm(0);
        if (WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL)
            printf("  WATCHDOG: killed after 8 s -- never returned from the fault\n");
        else if (WIFSIGNALED(st)) printf("  child died on signal %d\n", WTERMSIG(st));
        else printf("  child exit=%d\n", WEXITSTATUS(st));
        printf("\n");
    }
    return 0;
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1 && !strcmp(argv[1], "all")) { if (argc > 2) g_mode = argv[2]; return run_all(argv[0]); }
    const char* which = argc > 1 ? argv[1] : "";
    int regset64 = argc > 2 && !strcmp(argv[2], "64");
    setcs = argc > 3 && !strcmp(argv[3], "setcs");
    tramp = argc > 3 && !strcmp(argv[3], "tramp");
    h32_cs64();
    page = h32_load(g_beg, g_end, NULL);
    out = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    memset(out, 0, 4096);
    if (mmap((void*)(BADPAGE+0x1000), 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON|MAP_FIXED, -1, 0) == MAP_FAILED)
        { perror("marker page"); return 2; }
    if (mmap((void*)BADPAGE, 4096, PROT_NONE, MAP_PRIVATE|MAP_ANON|MAP_FIXED, -1, 0) == MAP_FAILED)
        { perror("PROT_NONE page"); return 2; }

    int extra = regset64 ? 0x0200 /*SA_64REGSET*/ : 0;
    printf("== %s%s%s%s ==\n", which, regset64 ? " +SA_64REGSET" : "", setcs ? " +restore-cs" : "", tramp ? " +trampoline" : "");

    const unsigned char *ent, *at, *aft;
    if (!strcmp(which, "ud2"))       { ent=g_ud2; at=g_ud2_at; aft=g_ud2_after; }
    else if (!strcmp(which, "hlt"))  { ent=g_hlt; at=g_hlt_at; aft=g_hlt_after; }
    else if (!strcmp(which, "segv")) { ent=g_segv; at=g_segv_at; aft=g_segv_after; }
    else if (!strcmp(which, "int3")) { ent=g_int3; at=g_int3_at; aft=g_int3_after; }
    else if (!strcmp(which, "alarm")) {
        install(SIGALRM, alrm, extra);
        install(SIGSEGV, handler, extra); install(SIGBUS, handler, extra);
        install(SIGILL, handler, extra);  install(SIGTRAP, handler, extra);
        out[13] = 200000000ULL;                 // iterations
        out[14] = 0xC0FFEE000000000FULL;        // expected r15
        out[16] = 0x1111000500000000ULL;        // expected rdi
        struct itimerval it = {{0, 10000}, {0, 10000}};
        setitimer(ITIMER_REAL, &it, NULL);
        struct timeval t0, t1; gettimeofday(&t0, NULL);
        uint32_t corrupt = h32_call64(page + (g_loop - g_beg), (uint32_t)(uintptr_t)out);
        gettimeofday(&t1, NULL);
        setitimer(ITIMER_REAL, &(struct itimerval){{0,0},{0,0}}, NULL);
        double ms = (t1.tv_sec-t0.tv_sec)*1e3 + (t1.tv_usec-t0.tv_usec)/1e3;
        printf("  loop returned: %u corrupt iterations of %llu in %.0f ms, %d SIGALRMs delivered\n",
               corrupt, (unsigned long long)out[13], ms, nalrm);
        printf("  r15 mismatches=%u rdi mismatches=%u\n", (uint32_t)out[15], (uint32_t)out[17]);
        return report() != 0 || corrupt != 0;
    } else if (!strcmp(which, "blocked")) {
        // The only plausible mitigation: keep every async signal masked on the
        // threads that run guest code, and handle signals on a 32-bit-only
        // thread. Does masking really keep the guest in long mode?
        install(SIGALRM, alrm, extra);
        install(SIGSEGV, handler, extra); install(SIGBUS, handler, extra);
        install(SIGILL, handler, extra);  install(SIGTRAP, handler, extra);
        sigset_t all; sigfillset(&all);
        sigprocmask(SIG_BLOCK, &all, NULL);
        out[13] = 200000000ULL;
        out[14] = 0xC0FFEE000000000FULL;
        out[16] = 0x1111000500000000ULL;
        setitimer(ITIMER_REAL, &(struct itimerval){{0,10000},{0,10000}}, NULL);
        struct timeval t0, t1; gettimeofday(&t0, NULL);
        uint32_t corrupt = h32_call64(page + (g_loop - g_beg), (uint32_t)(uintptr_t)out);
        gettimeofday(&t1, NULL);
        setitimer(ITIMER_REAL, &(struct itimerval){{0,0},{0,0}}, NULL);
        double ms = (t1.tv_sec-t0.tv_sec)*1e3 + (t1.tv_usec-t0.tv_usec)/1e3;
        printf("  with all signals blocked: %u corrupt of %llu iterations in %.0f ms, %d handler runs\n",
               corrupt, (unsigned long long)out[13], ms, nalrm);
        sigprocmask(SIG_UNBLOCK, &all, NULL);
        printf("  after unblocking, %d pending SIGALRM ran\n", nalrm);
        return report() != 0 || corrupt != 0;
    } else if (!strcmp(which, "wake")) {
        install(SIGALRM, alrm, extra);
        install(SIGSEGV, handler, extra); install(SIGBUS, handler, extra);
        install(SIGILL, handler, extra);  install(SIGTRAP, handler, extra);
        volatile uint32_t* mark = (volatile uint32_t*)0x30001000u;
        wake_flag = (volatile uint32_t*)&out[18];
        mark[0] = 0xdead; mark[2] = mark[3] = mark[4] = mark[5] = 0;
        setitimer(ITIMER_REAL, &(struct itimerval){{0,0},{0,50000}}, NULL);
        uint32_t r = h32_call64(page + (g_wake - g_beg), (uint32_t)(uintptr_t)out);
        printf("  returned r=%u after %d SIGALRM; mode-marker=%u (2=still long mode, 1=demoted to 32-bit)\n",
               r, nalrm, mark[0]);
        printf("  r15=0x%llx rdi=0x%llx\n", *(uint64_t*)&mark[2], *(uint64_t*)&mark[4]);
        return mark[0] != 2;
    } else { printf("usage: sigprobe ud2|hlt|segv|int3|alarm|wake [64] [setcs]\n"); return 1; }

    fault_at    = (uint32_t)(uintptr_t)(page + (at  - g_beg));
    fault_after = (uint32_t)(uintptr_t)(page + (aft - g_beg));
    printf("  guest page=%p entry=+%d fault@0x%x len=%d\n", page, (int)(ent-g_beg),
           fault_at, (int)(aft - at));
    install(SIGILL, handler, extra);  install(SIGSEGV, handler, extra);
    install(SIGBUS, handler, extra);  install(SIGTRAP, handler, extra);
    install(SIGFPE, handler, extra);
    uint32_t r = h32_call64(page + (ent - g_beg), (uint32_t)(uintptr_t)out);
    printf("  guest returned eax=%u\n", r);
    return report() != 0;
}
