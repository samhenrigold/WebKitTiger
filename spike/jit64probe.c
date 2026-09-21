/* Does Tiger's x86_64 libSystem give a JIT what it needs?
   Executable mappings, W^X flipping, the mach_vm family, large thread stacks,
   and how much address space a 64-bit process can actually get. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>

static int ok, fail;
#define CHECK(c, m) do { if (c) { ok++; printf("ok   %s\n", m); } else { fail++; printf("FAIL %s\n", m); } } while (0)

/* mov eax, 0x2A ; ret  -> returns 42 */
static const unsigned char kCode[] = { 0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3 };
typedef int (*fn_t)(void);

static void *thread_probe(void *a) { (void)a; char x[1024]; memset(x, 0, sizeof x); return NULL; }

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("pointer width %d\n\n", (int)sizeof(void *));

    /* 1. RWX in one step */
    void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    CHECK(p != MAP_FAILED, "mmap PROT_READ|WRITE|EXEC");
    if (p != MAP_FAILED) {
        memcpy(p, kCode, sizeof kCode);
        CHECK(((fn_t)p)() == 42, "execute code written into an RWX page");
    }

    /* 2. W^X: map RW, write, then flip to RX */
    void *q = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    CHECK(q != MAP_FAILED, "mmap PROT_READ|WRITE");
    if (q != MAP_FAILED) {
        memcpy(q, kCode, sizeof kCode);
        CHECK(mprotect(q, 4096, PROT_READ | PROT_EXEC) == 0, "mprotect RW -> RX");
        CHECK(((fn_t)q)() == 42, "execute after the W^X flip");
    }

    /* 3. the mach_vm family a JIT's allocator tends to use */
    mach_vm_address_t a = 0;
    kern_return_t kr = mach_vm_allocate(mach_task_self(), &a, 1 << 20, VM_FLAGS_ANYWHERE);
    CHECK(kr == KERN_SUCCESS, "mach_vm_allocate 1 MB");
    if (kr == KERN_SUCCESS) {
        CHECK(mach_vm_protect(mach_task_self(), a, 1 << 20, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE) == KERN_SUCCESS,
              "mach_vm_protect to RWX");
        CHECK(mach_vm_deallocate(mach_task_self(), a, 1 << 20) == KERN_SUCCESS, "mach_vm_deallocate");
    }

    /* 4. large thread stacks */
    pthread_attr_t at; pthread_attr_init(&at);
    CHECK(pthread_attr_setstacksize(&at, 16 << 20) == 0, "pthread_attr_setstacksize 16 MB");
    pthread_t th;
    CHECK(pthread_create(&th, &at, thread_probe, NULL) == 0, "pthread_create with a 16 MB stack");
    pthread_join(th, NULL);

    /* 5. how much address space is actually reachable */
    size_t step = (size_t)1 << 30, got = 0;
    void *keep[64]; int n = 0;
    while (n < 64) {
        void *r = mmap(NULL, step, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
        if (r == MAP_FAILED) break;
        keep[n++] = r; got += step;
    }
    printf("\nreserved %zu GB of address space in 1 GB chunks (%d mappings)\n", got >> 30, n);
    for (int i = 0; i < n; i++) munmap(keep[i], step);
    CHECK(got >= (size_t)4 << 30, "at least 4 GB of address space reservable");

    printf("\n%d passed, %d failed\n", ok, fail);
    return fail != 0;
}
