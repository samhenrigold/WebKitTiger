/* posix_memalign on Tiger (compat/libcompat.c), including alignments above the page size,
   which go through our registered malloc zone so that plain free() still works.
   Build: spike/run.sh spike/memaligntest.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <malloc/malloc.h>
#include <pthread.h>

static int failures;
static void expect(const char *name, int ok)
{
    printf("%-52s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok)
        ++failures;
}

/* Write a recognisable pattern over the whole block and read it back, so a block that
   overlaps another allocation or runs off the end of its mapping shows up. */
static int fillAndCheck(unsigned char *p, size_t size, unsigned char seed)
{
    size_t i;
    for (i = 0; i < size; ++i)
        p[i] = (unsigned char)(seed + (i & 0x7f));
    for (i = 0; i < size; ++i)
        if (p[i] != (unsigned char)(seed + (i & 0x7f)))
            return 0;
    return 1;
}

static volatile int stressErrors;

static void *stressThread(void *arg)
{
    unsigned char tag = (unsigned char)(uintptr_t)arg;
    int i;
    for (i = 0; i < 200; ++i) {
        void *p = NULL;
        size_t size = 16384 + (size_t)(i % 7) * 4096;
        if (posix_memalign(&p, 16384, size) != 0 || !p || ((uintptr_t)p & 16383)) {
            ++stressErrors;
            return NULL;
        }
        memset(p, tag, size);
        if (((unsigned char *)p)[size - 1] != tag)
            ++stressErrors;
        if (malloc_size(p) < size)
            ++stressErrors;
        free(p);
    }
    return NULL;
}

int tigerRunThreadedStress(void)
{
    pthread_t t[8];
    int i;
    stressErrors = 0;
    for (i = 0; i < 8; ++i)
        if (pthread_create(&t[i], NULL, stressThread, (void *)(uintptr_t)(i + 1)) != 0)
            return -1;
    for (i = 0; i < 8; ++i)
        pthread_join(t[i], NULL);
    return stressErrors;
}

int main(void)
{
    static const size_t alignments[] = { 16, 64, 4096, 16384, 65536, 1024 * 1024 };
    static const size_t sizes[] = { 1, 15, 16, 100, 4095, 4096, 4097, 16384, 65536,
                                    1024 * 1024, 3 * 1024 * 1024 };
    size_t ai, si;
    char name[128];

    setbuf(stdout, NULL);
    printf("page size %d\n\n", getpagesize());

    /* Bad arguments, per POSIX and Apple's libmalloc. */
    {
        void *p = (void *)0x1234;
        expect("alignment 0 is EINVAL", posix_memalign(&p, 0, 16) == EINVAL);
        expect("alignment 3 (not a power of two) is EINVAL", posix_memalign(&p, 3, 16) == EINVAL);
        expect("alignment 2 (below sizeof(void*)) is EINVAL", posix_memalign(&p, 2, 16) == EINVAL);
        expect("memptr untouched on error", p == (void *)0x1234);
    }

    /* Every alignment against every size: aligned, writable end to end, freeable through
       plain free(), and malloc_size() answers something that covers the request. */
    for (ai = 0; ai < sizeof alignments / sizeof *alignments; ++ai) {
        for (si = 0; si < sizeof sizes / sizeof *sizes; ++si) {
            size_t a = alignments[ai], s = sizes[si];
            void *p = NULL;
            int rc = posix_memalign(&p, a, s);
            snprintf(name, sizeof name, "align %-8lu size %-9lu alloc",
                     (unsigned long)a, (unsigned long)s);
            if (rc != 0 || !p) { expect(name, 0); continue; }
            if ((uintptr_t)p & (a - 1)) {
                snprintf(name, sizeof name, "align %-8lu size %-9lu ALIGNED (got %p)",
                         (unsigned long)a, (unsigned long)s, p);
                expect(name, 0);
                free(p);
                continue;
            }
            if (!fillAndCheck((unsigned char *)p, s, (unsigned char)(ai * 31 + si))) {
                snprintf(name, sizeof name, "align %-8lu size %-9lu readback",
                         (unsigned long)a, (unsigned long)s);
                expect(name, 0);
                free(p);
                continue;
            }
            if (malloc_size(p) < s) {
                snprintf(name, sizeof name, "align %-8lu size %-9lu malloc_size >= size",
                         (unsigned long)a, (unsigned long)s);
                expect(name, 0);
                free(p);
                continue;
            }
            free(p);
            expect(name, 1);
        }
    }

    /* size 0 must still give a unique, freeable pointer. */
    {
        void *a = NULL, *b = NULL;
        int ok = posix_memalign(&a, 65536, 0) == 0 && posix_memalign(&b, 65536, 0) == 0;
        expect("size 0 succeeds", ok && a && b);
        expect("size 0 pointers are distinct", a != b);
        expect("size 0 pointers are aligned", !((uintptr_t)a & 65535) && !((uintptr_t)b & 65535));
        free(a);
        free(b);
    }

    /* Many live blocks at once, interleaved with ordinary malloc, freed out of order.
       This is the case that catches a broken lookup table or a bad munmap length. */
    {
        enum { N = 64 };
        void *big[N], *small[N];
        int i, ok = 1;
        for (i = 0; i < N; ++i) {
            if (posix_memalign(&big[i], 16384, 16384) != 0) { ok = 0; break; }
            small[i] = malloc(1000);
            if (!small[i]) { ok = 0; break; }
            memset(big[i], i, 16384);
            memset(small[i], ~i, 1000);
        }
        expect("64 interleaved 16 KB blocks + mallocs", ok);
        for (i = 0; i < N && ok; ++i) {
            if (((uintptr_t)big[i] & 16383) || *(unsigned char *)big[i] != (unsigned char)i)
                ok = 0;
            if (*(unsigned char *)small[i] != (unsigned char)~i)
                ok = 0;
        }
        expect("no corruption across 64 blocks", ok);
        for (i = N - 1; i >= 0; i -= 2) free(big[i]);      /* out of order, odd first */
        for (i = 0; i < N; i += 2) free(big[i]);
        for (i = 0; i < N; ++i) free(small[i]);
        expect("out-of-order free of all blocks", 1);
    }

    /* Reuse: the address space a freed block occupied has to come back. */
    {
        int i, ok = 1;
        for (i = 0; i < 200; ++i) {
            void *p = NULL;
            if (posix_memalign(&p, 65536, 65536) != 0 || ((uintptr_t)p & 65535)) { ok = 0; break; }
            memset(p, 0xa5, 65536);
            free(p);
        }
        expect("200 alloc/free cycles at 64 KB", ok);
    }

    /* realloc of an over-aligned block. Libc short-circuits the shrink itself; growing
       goes through the zone, which keeps the alignment and copies the old contents. */
    {
        void *p = NULL;
        int ok = posix_memalign(&p, 16384, 16384) == 0;
        if (ok) {
            memset(p, 0x5a, 16384);
            void *shrunk = realloc(p, 8192);
            ok = shrunk == p;                       /* Libc returns old_ptr when it fits */
            expect("realloc shrink returns the same pointer", ok);
            void *grown = realloc(shrunk, 100000);
            ok = grown && !((uintptr_t)grown & 16383);
            expect("realloc grow keeps the alignment", ok);
            if (ok) {
                int i, same = 1;
                for (i = 0; i < 8192; ++i)
                    if (((unsigned char *)grown)[i] != 0x5a) { same = 0; break; }
                expect("realloc grow preserves the contents", same);
            }
            free(grown);
        } else {
            expect("realloc shrink returns the same pointer", 0);
        }
    }

    /* The zone must not claim pointers that are not ours. */
    {
        void *m = malloc(4096);
        void *v = valloc(4096);
        expect("malloc'd pointer still reports a default-zone size", malloc_size(m) >= 4096);
        expect("valloc'd pointer still reports a default-zone size", malloc_size(v) >= 4096);
        free(m);
        free(v);
        expect("malloc_size of a stack address is 0", malloc_size(&failures) == 0);
    }

    /* aligned_alloc is what bmalloc's SystemHeap::memalign actually calls on this port
       (tigerprelude.h defines it over posix_memalign), and it frees through the same path. */
    {
        void *p = aligned_alloc(16384, 16384);
        expect("aligned_alloc(16384) is aligned", p && !((uintptr_t)p & 16383));
        if (p) { memset(p, 1, 16384); free(p); }
    }

    /* MarkedBlock allocation is not single-threaded, and the zone's lookup table is shared,
       so hammer it from several threads at once. */
    {

        expect("8 threads x 200 16 KB alloc/free", tigerRunThreadedStress() == 0);
    }

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
