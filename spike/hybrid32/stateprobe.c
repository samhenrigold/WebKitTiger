// hybrid32 Q2: can a 32-bit task see and edit the 64-bit register state of one
// of its own threads that is parked inside long-mode code?  This is what JSC's
// conservative GC (scan r8-r15 + rsp of suspended threads) and the sampling
// profiler (read rip) need.
#include "h32common.h"
#include <pthread.h>
#include <unistd.h>
#include <mach/mach.h>

__asm__(
".text\n.align 4\n"
".globl _s_beg\n_s_beg:\n"
".code64\n"
// rdi = shared block. Fill sentinels, spin until [r13+0] != 0, then publish r15
// and lret. Guest never touches rbx/rbp.
".globl _s_park\n_s_park:\n"
"   movl %edi, %r13d\n"
"   movabsq $0xC0FFEE0000000008, %r8\n"
"   movabsq $0xC0FFEE0000000009, %r9\n"
"   movabsq $0xC0FFEE000000000A, %r10\n"
"   movabsq $0xC0FFEE000000000B, %r11\n"
"   movabsq $0xC0FFEE000000000C, %r12\n"
"   movabsq $0xC0FFEE000000000E, %r14\n"
"   movabsq $0xC0FFEE000000000F, %r15\n"
".globl _s_spin\n_s_spin:\n"
"1: cmpl $0, 0(%r13)\n"
"   je 1b\n"
"   movq %r15, 8(%r13)\n"
"   movq %r8,  16(%r13)\n"
"   movq %rsp, 24(%r13)\n"
"   xorl %eax, %eax\n"
"   lret\n"
".code32\n"
".globl _s_end\n_s_end:\n");
extern const unsigned char s_beg[], s_park[], s_spin[], s_end[];

static unsigned char* page;
static volatile uint64_t* blk;
static volatile mach_port_t tport;

static void* worker(void* arg) {
    tport = pthread_mach_thread_np(pthread_self());
    printf("worker: tport=0x%x entering guest\n", tport);
    h32_call64(page + (s_park - s_beg), (uint32_t)(uintptr_t)blk);
    return arg;
}

static void dump64(const char* tag, x86_thread_state64_t* t) {
    printf("  %s rip=0x%llx rsp=0x%llx cs=0x%llx\n", tag, t->rip, t->rsp, t->cs);
    printf("  %s rax=0x%llx rdi=0x%llx r8=0x%llx r12=0x%llx r13=0x%llx r15=0x%llx\n",
           tag, t->rax, t->rdi, t->r8, t->r12, t->r13, t->r15);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    h32_cs64();
    page = h32_load(s_beg, s_end, NULL);
    blk = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    memset((void*)blk, 0, 4096);
    uint32_t spin_at = (uint32_t)(uintptr_t)(page + (s_spin - s_beg));
    printf("guest page=%p spin loop at 0x%x, cs64=0x%x\n", page, spin_at, h32_cs64_sel);

    pthread_t th; pthread_create(&th, NULL, worker, NULL);
    while (!tport) usleep(1000);
    usleep(200000);
    printf("main: suspending\n");
    kern_return_t kr = thread_suspend(tport);
    printf("thread_suspend -> %d\n", kr);

    x86_thread_state32_t s32; mach_msg_type_number_t c = x86_THREAD_STATE32_COUNT;
    kr = thread_get_state(tport, x86_THREAD_STATE32, (thread_state_t)&s32, &c);
    printf("x86_THREAD_STATE32 (count in %u) -> kr=%d count out=%u\n", x86_THREAD_STATE32_COUNT, kr, c);
    if (kr == 0) printf("  eip=0x%x esp=0x%x cs=0x%x eax=0x%x edi=0x%x (spin=0x%x -> %s)\n",
                        s32.eip, s32.esp, s32.cs, s32.eax, s32.edi, spin_at,
                        (s32.eip >= spin_at && s32.eip < spin_at + 8) ? "IN GUEST" : "elsewhere");

    x86_thread_state64_t s64; memset(&s64, 0xEE, sizeof s64);
    c = x86_THREAD_STATE64_COUNT;
    kr = thread_get_state(tport, x86_THREAD_STATE64, (thread_state_t)&s64, &c);
    printf("x86_THREAD_STATE64 (count in %u) -> kr=%d count out=%u\n", x86_THREAD_STATE64_COUNT, kr, c);
    if (kr == 0) dump64("  64:", &s64);

    x86_thread_state_t sx; memset(&sx, 0xEE, sizeof sx);
    c = x86_THREAD_STATE_COUNT;
    kr = thread_get_state(tport, x86_THREAD_STATE, (thread_state_t)&sx, &c);
    printf("x86_THREAD_STATE   (count in %u) -> kr=%d count out=%u hdr.flavor=%d hdr.count=%d\n",
           x86_THREAD_STATE_COUNT, kr, c, sx.tsh.flavor, sx.tsh.count);
    if (kr == 0 && sx.tsh.flavor == x86_THREAD_STATE64) dump64("  gen:", &sx.uts.ts64);
    else if (kr == 0) printf("  gen: eip=0x%x cs=0x%x edi=0x%x\n", sx.uts.ts32.eip, sx.uts.ts32.cs, sx.uts.ts32.edi);

    // write back: try to change r15 through the 64-bit flavor
    if (kr == 0) {
        s64.r15 = 0xFEEDFACE12345678ULL;
        c = x86_THREAD_STATE64_COUNT;
        kern_return_t kw = thread_set_state(tport, x86_THREAD_STATE64, (thread_state_t)&s64, c);
        printf("thread_set_state(STATE64, r15=0xFEEDFACE12345678) -> kr=%d\n", kw);
    }
    thread_resume(tport);
    blk[0] = 1;
    pthread_join(th, NULL);
    printf("guest published: r15=0x%llx r8=0x%llx rsp=0x%llx\n",
           (unsigned long long)blk[1], (unsigned long long)blk[2], (unsigned long long)blk[3]);
    printf("r15 write-back %s\n", blk[1] == 0xFEEDFACE12345678ULL ? "TOOK EFFECT" : "ignored");
    return 0;
}
