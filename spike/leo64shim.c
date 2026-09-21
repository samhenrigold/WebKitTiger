/*
 * leo64shim.c - the libSystem symbols Leopard 10.5.0's x86_64 frameworks import
 * that Tiger 10.4.11's x86_64 libSystem does not export.
 *
 * Tiger ships x86_64 slices of libSystem and dyld even though 10.4 was never
 * advertised as 64-bit on Intel, and 64-bit processes do run on this hardware.
 * That makes a 64-bit content process possible in principle: Tiger has no
 * x86_64 CoreFoundation, CoreGraphics or Cocoa at all, so unlike the 32-bit
 * case there is nothing already loaded to collide with, and the whole Leopard
 * userland can be brought in privately.
 *
 * 37 symbols stand between that and a load. Most are trivial C99 or renames.
 * The interesting one is the last.
 *
 * Build:
 *   toolchain/llvm-tiger/bin/clang -target x86_64-apple-macosx10.4 \
 *     -isysroot sdk/MacOSX10.4u.sdk -mmacosx-version-min=10.4 -fno-stack-protector \
 *     --ld-path=toolchain/cctools/bin/i386-apple-darwin8-ld -dynamiclib \
 *     -o leo64shim.dylib spike/leo64shim.c
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <errno.h>
#include <dlfcn.h>
#include <stdio.h>

/* Set LEO64_TRACE to see which of these the Leopard stack actually reaches. */
static void trace(const char *n) {
    static int on = -1;
    if (on < 0) on = getenv("LEO64_TRACE") ? 1 : 0;
    if (on) { fprintf(stderr, "[leo64shim] %s\n", n); fflush(stderr); }
}

/* ---- C99 math Tiger's x86_64 libm does not export ---- */
double fmax(double a, double b) { return (a != a) ? b : (b != b) ? a : (a > b ? a : b); }
double fmin(double a, double b) { return (a != a) ? b : (b != b) ? a : (a < b ? a : b); }
double exp2(double x) { return pow(2.0, x); }
int flsl(long m) { int b = 0; if (!m) return 0; while (m) { m >>= 1; b++; } return b; }

/* ---- pointer-width atomics: on x86_64 these are the 64-bit ones ---- */
extern int OSAtomicCompareAndSwap64(int64_t, int64_t, volatile int64_t *);
extern int OSAtomicCompareAndSwap64Barrier(int64_t, int64_t, volatile int64_t *);
int OSAtomicCompareAndSwapPtr(void *o, void *n, void *volatile *p) {
    return OSAtomicCompareAndSwap64((int64_t)o, (int64_t)n, (volatile int64_t *)p);
}
int OSAtomicCompareAndSwapPtrBarrier(void *o, void *n, void *volatile *p) {
    return OSAtomicCompareAndSwap64Barrier((int64_t)o, (int64_t)n, (volatile int64_t *)p);
}
/* long is 64-bit on x86_64 too; libauto's collector wants this one. */
int OSAtomicCompareAndSwapLong(long o, long n, volatile long *p) {
    return OSAtomicCompareAndSwap64((int64_t)o, (int64_t)n, (volatile int64_t *)p);
}

/* ---- conformance aliases: Tiger has only the plain names ---- */
int realpath$DARWIN_EXTSN(const char *p, char *r) { extern char *realpath(const char *, char *); return realpath(p, r) ? 0 : -1; }
int select$DARWIN_EXTSN(int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *t) { return select(n, r, w, e, t); }
int select$1050(int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *t) { return select(n, r, w, e, t); }
/* On x86_64 stat is already 64-bit, so the $INODE64-era names are the same call. */
int stat64(const char *p, struct stat *b) { return stat(p, b); }
int fstat64(int fd, struct stat *b) { return fstat(fd, b); }

/* ---- Leopard additions with no Tiger equivalent: honest failures ---- */
int gethostuuid(unsigned char *u, const void *t) { if (u) memset(u, 0, 16); return 0; }
/* Reporting failure here made CoreGraphics unhappy; a purgeable request that
   cannot be honoured is better answered as "not purgeable, but fine". */
int mach_vm_purgable_control(mach_port_t t, uint64_t a, int c, int *s) { (void)t; (void)a; (void)c; trace("mach_vm_purgable_control"); if (s) *s = 0; return KERN_SUCCESS; }
/* launchd's key/value swap. CoreGraphics asks twice while building a bitmap
   context; answering "error" faults it, so answer success with a zero value. */
int vproc_swap_integer(void *v, int k, int64_t *in, int64_t *out) { trace("vproc_swap_integer"); (void)v; (void)k; (void)in; if (out) *out = 0; return 0; }

/* launchd bootstrap: Leopard's *2 forms over Tiger's originals */
extern kern_return_t bootstrap_register(mach_port_t, const char *, mach_port_t);
kern_return_t bootstrap_look_up(mach_port_t, const char *, mach_port_t *);
kern_return_t bootstrap_look_up2(mach_port_t bp, const char *name, mach_port_t *sp, mach_port_t target, uint64_t flags) {
    (void)target; (void)flags; return bootstrap_look_up(bp, name, sp);
}
kern_return_t bootstrap_register2(mach_port_t bp, const char *name, mach_port_t sp, uint64_t flags) {
    (void)flags; return bootstrap_register(bp, name, sp);
}
const char *bootstrap_strerror(kern_return_t r) { return r == KERN_SUCCESS ? "success" : "bootstrap error"; }

/* Deny the window server, on purpose.
 *
 * CGBitmapContextCreate reaches CGSServerPort by way of CGRenderingStateCreate ->
 * CGFontDefaultAllowsFontSmoothing -> get_font_rendering_defaults -> CGSGetDisplayIsLCD:
 * creating any bitmap context asks the window server whether the display is an LCD, to
 * decide font smoothing. Tiger's WindowServer does answer a bootstrap lookup, but it is a
 * 32-bit Tiger-era server and never replies to Leopard's message, so the process blocks in
 * mach_msg forever behind a pthread_once.
 *
 * Failing the lookup instead gives CoreGraphics the chance to take its no-server path.
 * Opt-in, because it is a lie to the framework: set LEO64_NO_WINDOWSERVER.
 */
#define BOOTSTRAP_UNKNOWN_SERVICE 1102
kern_return_t bootstrap_look_up(mach_port_t bp, const char *name, mach_port_t *sp) {
    static kern_return_t (*real)(mach_port_t, const char *, mach_port_t *);
    static int deny = -1;
    if (deny < 0) deny = getenv("LEO64_NO_WINDOWSERVER") ? 1 : 0;
    if (deny && name && (strstr(name, "coregraphics") || strstr(name, "CoreGraphics"))) {
        trace("bootstrap_look_up denied (window server)");
        if (sp) *sp = 0;
        return BOOTSTRAP_UNKNOWN_SERVICE;
    }
    if (!real) real = (kern_return_t (*)(mach_port_t, const char *, mach_port_t *))dlsym(RTLD_NEXT, "bootstrap_look_up");
    return real ? real(bp, name, sp) : BOOTSTRAP_UNKNOWN_SERVICE;
}

/* CommonCrypto arrived in 10.5; CoreGraphics uses it only for encrypted PDF. */
int CCCryptorCreate(uint32_t op, uint32_t alg, uint32_t opts, const void *k, size_t kl,
                    const void *iv, void **out) { (void)op;(void)alg;(void)opts;(void)k;(void)kl;(void)iv; if (out) *out = 0; return -4305; }
int CCCryptorRelease(void *c) { (void)c; return -4305; }
int CCCryptorUpdate(void *c, const void *i, size_t il, void *o, size_t ol, size_t *m) {
    (void)c;(void)i;(void)il;(void)o;(void)ol; if (m) *m = 0; return -4305; }

/* removefile(3) is Leopard-era; recursive delete is not on any path we exercise. */
void *removefile_state_alloc(void) { return calloc(1, 8); }
int removefile_state_free(void *s) { free(s); return 0; }
int removefile_state_get(void *s, uint32_t k, void *d) { (void)s;(void)k;(void)d; return -1; }
int removefile_state_set(void *s, uint32_t k, const void *d) { (void)s;(void)k;(void)d; return -1; }
int removefile(const char *p, void *s, int f) { (void)s; (void)f; return unlink(p); }

/* The workqueue SPI predates GCD and is optional; failing makes callers thread. */
int pthread_workqueue_create_np(void **wq, const void *a) { (void)a; if (wq) *wq = 0; return ENOTSUP; }
int pthread_workqueue_destroy_np(void *wq) { (void)wq; return ENOTSUP; }
int pthread_workqueue_additem_np(void *wq, void *f, void *a, void *i, void *g) { (void)wq;(void)f;(void)a;(void)i;(void)g; return ENOTSUP; }
int pthread_workqueue_removeitem_np(void *wq, void *i) { (void)wq;(void)i; return ENOTSUP; }
int pthread_workqueue_suspend_np(void *wq) { (void)wq; return ENOTSUP; }
int pthread_workqueue_resume_np(void *wq) { (void)wq; return ENOTSUP; }
int pthread_workqueue_attr_init_np(void *a) { (void)a; return ENOTSUP; }
int pthread_workqueue_attr_setqueuepriority_np(void *a, int p) { (void)a;(void)p; return ENOTSUP; }

int backtrace(void **buf, int sz) { trace("backtrace"); (void)buf; (void)sz; return 0; }
void backtrace_symbols_fd(void *const *b, int n, int fd) { (void)b; (void)n; (void)fd; }

/* dlopen_preflight(3) asks "would this load". Answering yes is wrong only for a
   library that would fail, and the callers treat false as "skip". */
int dlopen_preflight(const char *path) { trace("dlopen_preflight"); (void)path; return 1; }

/* ---- the one that matters ----
 * objc4 learns about images through Leopard's dyld_register_image_state_change_handler,
 * which Tiger's dyld does not have. Tiger has _dyld_register_func_for_add_image, which
 * replays every already-loaded image at registration and then fires once per new one.
 *
 * The batch flag is not decoration. When objc4 registers with batch=true it expects one
 * call carrying every image loaded so far, because it resolves cross-image superclass
 * references within a batch. Feeding it one image at a time instead leaves a class whose
 * superclass lives in a later image unlinked, and the runtime later dereferences that null
 * superclass while flushing caches. That was a real crash here, inside libobjc at +0xfca3,
 * reached from CGBitmapContextCreate.
 *
 * So: at registration, report every current image in a single batch, then report each
 * genuinely new one as it arrives. Several handlers can be registered for different
 * states, so they are kept in a small table rather than a single slot.
 */
struct dyld_image_info {
    const struct mach_header *imageLoadAddress;
    const char               *imageFilePath;
    uintptr_t                 imageFileModDate;
};
typedef const char *(*state_handler)(int state, uint32_t count, const struct dyld_image_info info[]);

#define MAX_HANDLERS 8
#define MAX_SEEN     512
static struct { state_handler fn; int state; } g_handlers[MAX_HANDLERS];
static int g_nhandlers;
static const struct mach_header *g_seen[MAX_SEEN];
static int g_nseen;

static int already_seen(const struct mach_header *mh) {
    for (int i = 0; i < g_nseen; i++) if (g_seen[i] == mh) return 1;
    if (g_nseen < MAX_SEEN) g_seen[g_nseen++] = mh;
    return 0;
}

static void fill(struct dyld_image_info *d, const struct mach_header *mh, const char *name) {
    d->imageLoadAddress = mh;
    d->imageFilePath = name ? name : "";
    d->imageFileModDate = 0;
}

static void on_add_image(const struct mach_header *mh, intptr_t slide) {
    (void)slide;
    if (already_seen(mh)) return;          /* the registration batch covered it */
    const char *name = "";
    for (uint32_t i = 0; i < _dyld_image_count(); i++)
        if (_dyld_get_image_header(i) == mh) { name = _dyld_get_image_name(i); break; }
    struct dyld_image_info one;
    fill(&one, mh, name);
    trace("image added");
    for (int i = 0; i < g_nhandlers; i++)
        g_handlers[i].fn(g_handlers[i].state, 1, &one);
}

void dyld_register_image_state_change_handler(int state, int batch, state_handler handler) {
    static int registered_with_dyld;
    if (g_nhandlers >= MAX_HANDLERS || !handler) return;
    g_handlers[g_nhandlers].fn = handler;
    g_handlers[g_nhandlers].state = state;
    g_nhandlers++;
    trace("dyld_register_image_state_change_handler");

    uint32_t n = _dyld_image_count();
    if (n > MAX_SEEN) n = MAX_SEEN;
    struct dyld_image_info *all = (struct dyld_image_info *)malloc(n * sizeof *all);
    if (all) {
        for (uint32_t i = 0; i < n; i++) {
            const struct mach_header *mh = _dyld_get_image_header(i);
            fill(&all[i], mh, _dyld_get_image_name(i));
            already_seen(mh);              /* record so on_add_image does not repeat it */
        }
        handler(state, n, all);            /* one batch, as a batch registration asks for */
        free(all);
    }
    if (!registered_with_dyld) {
        registered_with_dyld = 1;
        _dyld_register_func_for_add_image(on_add_image);
    }
}
