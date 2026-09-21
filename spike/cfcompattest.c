/* cfcompattest.c -- the compat/cfcompat.c surface, Tiger against modern.
 *
 * Covers what the Foundation probe did not reach: the execinfo, dyld and
 * CoreFoundation entry points cfcompat implements. Dual-build as usual, so the
 * modern column is measured rather than remembered.
 *
 * Tiger:  see spike/run-cfcompattest.sh
 * Host:   clang -o build/cfcompattest-host spike/cfcompattest.c -framework CoreFoundation
 */
#include <CoreFoundation/CoreFoundation.h>
#include <execinfo.h>
#include <mach-o/dyld.h>
#include <malloc/malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uuid/uuid.h>

/* SPI on BOTH ends: these live in <mach-o/dyld_priv.h> on modern and in
   compat/include/sdk-fill/ on Tiger, so declare them here rather than depending
   on either private header. The public modern spelling of the first one is
   _dyld_get_image_header_containing_address; cfcompat implements the SPI name
   because that is what WebKit calls. */
extern const struct mach_header *dyld_image_header_containing_address(const void *);
extern int _dyld_get_image_uuid(const struct mach_header *, uuid_t);
extern uint32_t dyld_get_program_sdk_version(void);
#if TIGER
extern CFArrayRef CFLocaleCopyPreferredLanguages(void);
extern size_t malloc_zone_pressure_relief(malloc_zone_t *, size_t);
#endif

static void kvs(const char *k, const char *v) { printf("%s=%s\n", k, v); }
static void kvi(const char *k, long long v) { printf("%s=%lld\n", k, v); }

static void kvcfstr(const char *k, CFStringRef s)
{
    if (!s) { printf("%s=nil\n", k); return; }
    char buf[256];
    if (!CFStringGetCString(s, buf, sizeof buf, kCFStringEncodingUTF8)) { printf("%s=?\n", k); return; }
    printf("%s=%s\n", k, buf);
}

/* Two nested frames, so the walker has something real to climb. */
static int __attribute__((noinline)) depth2(void **buf, int n) { return backtrace(buf, n); }
static int __attribute__((noinline)) depth1(void **buf, int n) { return depth2(buf, n); }

static void probeBacktrace(void)
{
    void *frames[32];
    int n = depth1(frames, 32);
    kvi("bt.count.positive", n > 0 ? 1 : 0);
    kvi("bt.count.atleast3", n >= 3 ? 1 : 0);
    kvi("bt.count.within", n <= 32 ? 1 : 0);
    /* Every returned address must land in a real image, which is the property
       that matters: a bad frame walk produces garbage that dladdr rejects. */
    int resolvable = 0;
    for (int i = 0; i < n; ++i)
        if (dyld_image_header_containing_address(frames[i]))
            ++resolvable;
    kvi("bt.all.resolvable", (n > 0 && resolvable == n) ? 1 : 0);
    kvi("bt.frames", n);
    kvi("bt.unresolvable", n - resolvable);

    char **syms = backtrace_symbols(frames, n);
    kvi("bt.symbols.nonnil", syms ? 1 : 0);
    if (syms) {
        int named = 0;
        for (int i = 0; i < n; ++i)
            if (syms[i] && strstr(syms[i], "depth"))
                ++named;
        /* depth1 and depth2 should both appear by name. */
        /* depth1/depth2 are static, so whether dladdr can name them is a property
           of the symbol table, not of the frame walk. Counted, not asserted. */
        kvi("bt.symbols.named", named);
        kvi("bt.symbols.firstnonempty", (syms[0] && syms[0][0]) ? 1 : 0);
        free(syms);   /* single free for the whole table, per the contract */
        kvs("bt.symbols.singlefree", "ok");
    }
    /* A zero-size request must not write anything or crash. */
    kvi("bt.zerosize", backtrace(frames, 0));
}

static void probeDyld(void)
{
    /* The header containing a function in this very image. */
    const struct mach_header *mh = dyld_image_header_containing_address((const void *)&probeDyld);
    kvi("dyld.header.nonnil", mh ? 1 : 0);
    kvi("dyld.header.ismacho",
        (mh && (mh->magic == 0xfeedface || mh->magic == 0xfeedfacf)) ? 1 : 0);
    /* It must be the same header dyld itself reports for image 0 ranges. */
    int matchesSomeImage = 0;
    uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; ++i)
        if (_dyld_get_image_header(i) == mh) { matchesSomeImage = 1; break; }
    kvi("dyld.header.isloadedimage", matchesSomeImage);
    kvi("dyld.header.null", dyld_image_header_containing_address(NULL) ? 0 : 1);

    uuid_t u;
    memset(u, 0, sizeof u);
    int got = _dyld_get_image_uuid(mh, u);
    kvi("dyld.uuid.returned", got ? 1 : 0);
    int nonzero = 0;
    for (size_t i = 0; i < sizeof u; ++i) if (u[i]) { nonzero = 1; break; }
    kvi("dyld.uuid.nonzero", nonzero);
    /* Asking twice must give the same answer. */
    uuid_t u2;
    memset(u2, 0, sizeof u2);
    _dyld_get_image_uuid(mh, u2);
    kvi("dyld.uuid.stable", memcmp(u, u2, sizeof u) == 0 ? 1 : 0);
    /* cfcompat guards a NULL header and returns 0. Modern's real one dereferences
       it and segfaults, so only Tiger can be asked. The shim being the more
       defensive of the two is not a defect. */
#if TIGER
    kvi("dyld.uuid.nullheader", _dyld_get_image_uuid(NULL, u2) ? 1 : 0);
#else
    kvs("dyld.uuid.nullheader", "crashes-on-modern-not-asked");
#endif

    /* Packed 10.4.0. Printed rather than asserted: the host reports its own SDK. */
    printf("dyld.sdkversion=0x%08x\n", dyld_get_program_sdk_version());
}

static void probeMallocZone(void)
{
    /* The shim reports nothing released; modern actually releases something or
       nothing depending on the heap, so only the shape is comparable. */
    size_t freed = malloc_zone_pressure_relief(malloc_default_zone(), 0);
    kvi("malloc.pressure.returns", freed == 0 ? 0 : 1);
    kvs("malloc.pressure.nocrash", "ok");
}

static void probeLocale(void)
{
    CFArrayRef langs = CFLocaleCopyPreferredLanguages();
    kvi("cflocale.nonnil", langs ? 1 : 0);
    if (!langs) return;
    CFIndex n = CFArrayGetCount(langs);
    kvi("cflocale.count.positive", n > 0 ? 1 : 0);
    /* Every element must be a CFString. CF-550 filters non-strings out; a shim
       that returns the raw preference does not. */
    int allStrings = 1;
    for (CFIndex i = 0; i < n; ++i)
        if (CFGetTypeID(CFArrayGetValueAtIndex(langs, i)) != CFStringGetTypeID())
            allStrings = 0;
    kvi("cflocale.allstrings", allStrings);
    if (n > 0 && allStrings) {
        CFStringRef first = (CFStringRef)CFArrayGetValueAtIndex(langs, 0);
        kvcfstr("cflocale.first", first);
        /* CF-550 canonicalises each identifier before returning it, so the
           result must already equal its own canonical form. */
        CFStringRef canon = CFLocaleCreateCanonicalLanguageIdentifierFromString(
            kCFAllocatorDefault, first);
        kvcfstr("cflocale.first.canonical", canon);
        kvi("cflocale.first.iscanonical",
            (canon && CFEqual(canon, first)) ? 1 : 0);
        if (canon) CFRelease(canon);
    }
    /* The caller owns the result: a Copy function must hand back a +1 array. */
    kvi("cflocale.retaincount.positive", CFGetRetainCount(langs) > 0 ? 1 : 0);
    CFRelease(langs);
    kvs("cflocale.released", "ok");

    /* Canonicalisation of a hyphenated and a legacy tag, to show what the
       difference between raw and canonical actually looks like here. */
    CFStringRef a = CFLocaleCreateCanonicalLanguageIdentifierFromString(
        kCFAllocatorDefault, CFSTR("en-US"));
    kvcfstr("canon.enUS", a);
    if (a) CFRelease(a);
    CFStringRef b = CFLocaleCreateCanonicalLanguageIdentifierFromString(
        kCFAllocatorDefault, CFSTR("English"));
    kvcfstr("canon.English", b);
    if (b) CFRelease(b);
}

int main(void)
{
#if TIGER
    kvs("platform", "tiger");
#else
    kvs("platform", "host");
#endif
    setbuf(stdout, NULL);
    probeBacktrace();
    probeDyld();
    probeMallocZone();
    probeLocale();
    return 0;
}
