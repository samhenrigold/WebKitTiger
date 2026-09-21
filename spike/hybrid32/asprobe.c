// hybrid32 Q3: how much address space does a 32-bit task get, does small-code-model
// 64-bit code survive addresses >= 2 GB, and what does `syscall` do from long mode?
#include "h32common.h"
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#define HI 0x80000000u          // first address whose disp32 sign-extends negative
#define MAGIC 0x5EEDFACEu

__asm__(
".text\n.align 4\n"
".globl _a_beg\n_a_beg:\n"
".code64\n"
// rdi = result slot. Absolute disp32 addressing: the assembler emits
// `mov 0x80000000, %eax`, which in long mode means [0xffffffff80000000].
".globl _a_abs\n_a_abs:\n"
"   movl %edi, %r13d\n"
"   movl 0x80000000, %eax\n"
"   movl %eax, (%r13)\n"
"   xorl %eax, %eax\n   lret\n"
// The same page reached through a full 64-bit address in a register.
".globl _a_movabs\n_a_movabs:\n"
"   movl %edi, %r13d\n"
"   movabsq $0x80000000, %rcx\n"
"   movl (%rcx), %eax\n"
"   movl %eax, (%r13)\n"
"   xorl %eax, %eax\n   lret\n"
// rip-relative load of a datum inside this very blob -- the blob itself is
// loaded above 2 GB by the host for this case.
".globl _a_rip\n_a_rip:\n"
"   movl %edi, %r13d\n"
"   movl _a_ripdata(%rip), %eax\n"
"   movl %eax, (%r13)\n"
"   leaq _a_ripdata(%rip), %rax\n"
"   movq %rax, 8(%r13)\n"
"   xorl %eax, %eax\n   lret\n"
".globl _a_ripdata\n_a_ripdata:\n   .long 0x5EEDFACE\n"
// SYS_getpid through the long-mode syscall instruction.
".globl _a_sys\n_a_sys:\n"
"   movl %edi, %r13d\n"
"   movl $0x2000014, %eax\n"
"   syscall\n"
"   movl %eax, (%r13)\n"
"   xorl %eax, %eax\n   lret\n"
".code32\n"
".globl _a_end\n_a_end:\n");
extern const unsigned char a_beg[], a_abs[], a_movabs[], a_rip[], a_sys[], a_end[], a_ripdata[];

// Hints only: MAP_FIXED over 0x90000000 unmaps the dyld shared region and kills
// the process on the spot.  For each hint, take the mapping, write and read it
// back, then give it up.
static void survey(void) {
    static const uint32_t hints[] = {
        0x40000000u, 0x80000000u, 0x90000000u, 0xa0000000u, 0xb0000000u,
        0xc0000000u, 0xe0000000u, 0xf0000000u, 0xff000000u, 0xfff00000u,
        0xffffe000u, 0u };
    printf("-- mmap reach in a 32-bit task (MAP_ANON, hint, no MAP_FIXED) --\n");
    for (int i = 0; hints[i]; i++) {
        volatile uint32_t* h = mmap((void*)hints[i], 4096, PROT_READ|PROT_WRITE,
                                    MAP_PRIVATE|MAP_ANON, -1, 0);
        if (h == MAP_FAILED) { printf("  hint 0x%08x -> %s\n", hints[i], strerror(errno)); continue; }
        *h = 0x1234abcd;
        printf("  hint 0x%08x -> %p  %s  rw=%s\n", hints[i], (void*)h,
               (uint32_t)(uintptr_t)h == hints[i] ? "honoured" : "moved   ",
               *h == 0x1234abcd ? "yes" : "NO");
        munmap((void*)h, 4096);
    }
    void* big = mmap(NULL, 512u<<20, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    printf("  unhinted 512 MB -> %p (top 0x%lx)\n", big,
           big == MAP_FAILED ? 0ul : (unsigned long)big + (512ul<<20));
    if (big != MAP_FAILED) munmap(big, 512u<<20);
    void* huge = mmap(NULL, 2048u<<20, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    printf("  unhinted 2 GB   -> %p %s\n", huge, huge == MAP_FAILED ? strerror(errno) : "");
    if (huge != MAP_FAILED) munmap(huge, 2048u<<20);
}

static pid_t child;
static void wd(int s) { (void)s; if (child) kill(child, SIGKILL); }

static int runcase(const char* self, const char* name) {
    fflush(stdout);
    child = fork();
    if (child == 0) { execl(self, self, name, (char*)0); _exit(127); }
    signal(SIGALRM, wd); alarm(6);
    int st = 0; waitpid(child, &st, 0); alarm(0);
    if (WIFSIGNALED(st)) printf("  -> died on signal %d%s\n", WTERMSIG(st),
                                WTERMSIG(st) == SIGKILL ? " (watchdog: hung)" : "");
    else printf("  -> exit %d\n", WEXITSTATUS(st));
    return 0;
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc == 1 || !strcmp(argv[1], "--danger-syscall")) {
        printf("survey:\n"); runcase(argv[0], "survey");
        printf("-- 64-bit addressing above 2 GB (each in a forked child) --\n");
        printf("abs (disp32, sign-extended):\n");    runcase(argv[0], "abs");
        printf("movabs + [reg]:\n");                 runcase(argv[0], "movabs");
        printf("rip-relative, blob loaded at 0x%x:\n", HI + 0x10000u); runcase(argv[0], "rip");
        // DO NOT enable casually: `syscall` in long mode from a 32-bit task
        // panicked the box hard (no output, no reboot, power cycle needed).
        if (argc > 1) { printf("syscall from long mode:\n"); runcase(argv[0], "syscall"); }
        else printf("syscall from long mode: SKIPPED (kernel panic -- pass any argument to force)\n");
        return 0;
    }
    if (!strcmp(argv[1], "survey")) { survey(); return 0; }
    h32_cs64();
    uint32_t* hi = mmap((void*)HI, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON|MAP_FIXED, -1, 0);
    if (hi == MAP_FAILED) { printf("  cannot map 0x%x: %s\n", HI, strerror(errno)); return 3; }
    *hi = MAGIC;
    uint64_t* out = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    memset(out, 0, 4096);

    int rip = !strcmp(argv[1], "rip");
    unsigned char* p = h32_load(a_beg, a_end, rip ? (void*)(uintptr_t)(HI + 0x10000u) : NULL);
    printf("  blob at %p, magic at 0x%x = 0x%x\n", p, HI, *hi);
    const unsigned char* ent =
        !strcmp(argv[1], "abs")    ? a_abs :
        !strcmp(argv[1], "movabs") ? a_movabs :
        rip                        ? a_rip : a_sys;
    h32_call64(p + (ent - a_beg), (uint32_t)(uintptr_t)out);
    printf("  result = 0x%x (magic=0x%x, getpid=%d)", (uint32_t)out[0], MAGIC, (int)getpid());
    if (rip) printf("  ripdata addr seen by guest = 0x%llx (expected 0x%lx)",
                    (unsigned long long)out[1], (unsigned long)(p + (a_ripdata - a_beg)));
    printf("\n");
    return 0;
}
