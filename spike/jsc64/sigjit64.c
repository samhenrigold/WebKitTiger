/* TIGER64: POSIX signal delivery into JIT memory on 10.4.11, x86_64.
 *
 * HAVE(MACH_EXCEPTIONS) is off for this target (mach_exc.defs is 10.5+), so every
 * trap JSC relies on arrives as a POSIX signal instead: wasm out-of-bounds and
 * null checks (SIGSEGV/SIGBUS), VMTraps' `hlt` breakpoints patched into DFG code,
 * OSR-exit/`int3` breakpoints, and `ud2`.  Three things have to hold and none of
 * them was tested before this probe:
 *
 *   1. the signal is delivered at all when the faulting rip is in an RWX mapping;
 *   2. ucontext->uc_mcontext->ss carries the *64-bit* register set, with an rip
 *      that points at the faulting instruction in that mapping;
 *   3. writes to that register set survive the return from the handler -- which is
 *      how every one of JSC's handlers recovers (it redirects rip at a thunk).
 *
 * It also records *which* signal each trap instruction produces, because that is
 * where 10.4 differs from every later system: see the `hlt` case.
 *
 * Build: toolchain/bin/tiger-clang64 -O1 -o sigjit64 sigjit64.c
 */
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ucontext.h>
#include <unistd.h>

static int failures = 0;
#define CHECK(cond, ...) do { \
    if (cond) printf("PASS  " __VA_ARGS__); else { printf("FAIL  " __VA_ARGS__); failures++; } \
    printf("\n"); \
} while (0)

static sigjmp_buf g_jmp;
static volatile sig_atomic_t g_hits;
static volatile int g_sig, g_code;
static volatile uintptr_t g_rip;
static void *g_redirect;          /* non-null: handler redirects rip here */
uint64_t g_raxSeen;               /* written by recoverStub, must be extern */
static volatile int g_recovered;

void recoverTail(void);
void recoverTail(void)
{
    g_recovered = 1;
    siglongjmp(g_jmp, 2);
}

/* Entered only because the handler rewrote rip in the ucontext.  Proves the write
 * took effect, and carries rax out so a GPR write is proven too. */
extern void recoverStub(void);
__asm__(
    ".text\n"
    ".align 4\n"
    ".globl _recoverStub\n"
    "_recoverStub:\n"
    "    movq %rax, _g_raxSeen(%rip)\n"
    "    jmp  _recoverTail\n");

#define kRedirectRax 0x00c0ffee0badf00dull

static void handler(int sig, siginfo_t *info, void *uap)
{
    ucontext_t *uc = (ucontext_t *)uap;
    g_sig = sig;
    g_code = info ? info->si_code : -1;
    g_rip = (uintptr_t)uc->uc_mcontext->ss.rip;
    g_hits++;
    if (g_redirect && g_hits == 1) {
        uc->uc_mcontext->ss.rip = (uint64_t)(uintptr_t)g_redirect;
        uc->uc_mcontext->ss.rax = kRedirectRax;
        return;                   /* resume at recoverStub */
    }
    siglongjmp(g_jmp, 1);
}

static void install(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    int sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGTRAP, SIGFPE };
    for (unsigned i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++)
        sigaction(sigs[i], &sa, NULL);
}

static const char *signame(int s)
{
    switch (s) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS:  return "SIGBUS";
    case SIGILL:  return "SIGILL";
    case SIGTRAP: return "SIGTRAP";
    case SIGFPE:  return "SIGFPE";
    default:      return "?";
    }
}

static unsigned char *g_jit;
static size_t g_jitUsed;

/* Copy a stub into the RWX arena and hand back a callable pointer. */
static void *emit(const unsigned char *bytes, size_t n)
{
    unsigned char *p = g_jit + g_jitUsed;
    memcpy(p, bytes, n);
    g_jitUsed = (g_jitUsed + n + 15) & ~(size_t)15;
    return p;
}

/* Run `stub(arg)` expecting a fault; report the signal and whether rip landed on
 * the faulting instruction inside the RWX arena. */
static void expectTrap(const char *what, void *stub, void *arg, int ripDelta)
{
    g_hits = 0; g_sig = 0; g_code = 0; g_rip = 0;
    if (sigsetjmp(g_jmp, 1) == 0) {
        ((void (*)(void *))stub)(arg);
        printf("FAIL  %s did not fault\n", what);
        failures++;
        return;
    }
    int inArena = g_rip >= (uintptr_t)g_jit && g_rip < (uintptr_t)g_jit + getpagesize();
    /* Faults report the faulting instruction; int3 is a trap, so rip is already past it. */
    CHECK(inArena && g_rip == (uintptr_t)stub + ripDelta,
          "%-22s -> %s (si_code %d), rip 0x%llx == stub %p%+d in RWX memory",
          what, signame(g_sig), g_code, (unsigned long long)g_rip, stub, ripDelta);
}

int main(void)
{
    size_t page = getpagesize();
    g_jit = mmap(NULL, page, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    unsigned char *guard = mmap(NULL, page, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (g_jit == MAP_FAILED || guard == MAP_FAILED) { printf("FAIL  mmap\n"); return 1; }
    install();

    static const unsigned char kHlt[]   = { 0xf4, 0xc3 };                   /* hlt; ret  -- VMTraps' breakpoint */
    static const unsigned char kUd2[]   = { 0x0f, 0x0b, 0xc3 };             /* ud2; ret */
    static const unsigned char kInt3[]  = { 0xcc, 0xc3 };                   /* int3; ret -- breakpoint traps */
    static const unsigned char kLoad[]  = { 0x48, 0x8b, 0x07, 0xc3 };       /* movq (%rdi),%rax; ret */
    static const unsigned char kStore[] = { 0x48, 0x89, 0x07, 0xc3 };       /* movq %rax,(%rdi); ret */
    static const unsigned char kNull[]  = { 0x48, 0x8b, 0x07, 0xc3 };       /* same, with a null pointer */

    void *hlt = emit(kHlt, sizeof(kHlt));
    void *ud2 = emit(kUd2, sizeof(kUd2));
    void *int3 = emit(kInt3, sizeof(kInt3));
    void *load = emit(kLoad, sizeof(kLoad));
    void *store = emit(kStore, sizeof(kStore));
    void *nullLoad = emit(kNull, sizeof(kNull));

    printf("sigjit64: RWX arena at %p, guard page at %p\n", g_jit, guard);
    expectTrap("hlt (VMTraps halt)", hlt, NULL, 0);
    expectTrap("ud2", ud2, NULL, 0);
    expectTrap("int3 (breakpoint)", int3, NULL, 1);
    expectTrap("load from PROT_NONE", load, guard, 0);
    expectTrap("store to PROT_NONE", store, guard, 0);
    expectTrap("load from null", nullLoad, NULL, 0);

    /* The recovery mechanism every JSC handler uses: rewrite rip (and here a GPR)
     * in the ucontext and return, so execution resumes somewhere else entirely. */
    g_redirect = (void *)recoverStub;
    g_hits = 0; g_recovered = 0; g_raxSeen = 0;
    if (sigsetjmp(g_jmp, 1) == 0) {
        ((void (*)(void *))load)(guard);
        printf("FAIL  redirect case did not fault\n");
        failures++;
    }
    CHECK(g_recovered == 1, "rip rewritten in the ucontext took effect on return (recoverStub ran)");
    CHECK(g_raxSeen == kRedirectRax, "rax written in the ucontext took effect (saw 0x%llx)", (unsigned long long)g_raxSeen);
    g_redirect = NULL;

    printf("%s (%d failure%s)\n", failures ? "FAILURES" : "ALL PASS", failures, failures == 1 ? "" : "s");
    return failures != 0;
}
