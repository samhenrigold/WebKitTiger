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
".data\n.globl _t32_tv\n_t32_tv:\n   .space 32\n");
extern void t32_null(void), t32_gtod(void), t32_write(void);

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

static double trip(void (*fn)(void), uint64_t n) {
    uint64_t* b = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    b[0] = n;
    *(uint32_t*)((char*)b + 8)  = (uint32_t)(uintptr_t)fn;
    *(uint16_t*)((char*)b + 12) = h32_cs32_sel;
    double t0 = now();
    h32_call64(page + (r_trip - r_beg), (uint32_t)(uintptr_t)b);
    double dt = now() - t0;
    munmap(b, 4096);
    return dt * 1e9 / (double)n;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    h32_cs64();
    page = h32_load(r_beg, r_end, NULL);
    printf("cs64=0x%x cs32=0x%x\n", h32_cs64_sel, h32_cs32_sel);

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

    printf("64->32->64 round trip, 1M calls:\n");
    printf("  empty thunk (lret only)   %8.1f ns/call\n", trip(t32_null, 1000000));
    printf("  thunk + gettimeofday()    %8.1f ns/call\n", trip(t32_gtod, 1000000));
    printf("  thunk + write(2,_,0)      %8.1f ns/call\n", trip(t32_write, 1000000));
    struct timeval tv; double t0 = now();
    for (int i = 0; i < 1000000; i++) gettimeofday(&tv, NULL);
    printf("  plain 32-bit gettimeofday %8.1f ns/call (baseline)\n", (now()-t0)*1e9/1e6);
    t0 = now();
    for (int i = 0; i < 1000000; i++) write(2, &tv, 0);
    printf("  plain 32-bit write        %8.1f ns/call (baseline)\n", (now()-t0)*1e9/1e6);
    return 0;
}
