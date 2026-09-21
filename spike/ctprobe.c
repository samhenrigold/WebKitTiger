/*
 * ctprobe.c - behavioural oracle diff for the CoreText functions WebCore calls
 * that Tiger exports directly (no ctcompat adapter).
 *
 * The static screens already say these link and take the right arguments. What
 * they cannot say is whether Tiger's implementation *answers* the same. So this
 * calls each one with fixed inputs against a font bundled beside it, and prints
 * a canonical dump. Build it twice - natively here against modern CoreText, and
 * with tiger-clang against the box - and diff the two dumps.
 *
 * The font travels with the test so both sides measure identical bytes. Loading
 * it goes through CTFontManagerCreateFontDescriptorFromData, which is native
 * here and ctcompat's ATS activation on Tiger; that is a helper, not a
 * subject, and a divergence there is ctcompat's.
 *
 * Everything printed must be reproducible. No pointers, no addresses, no type
 * ids (they are assigned per process), no timings.
 *
 * Build:
 *   cc -O1 -o build/ctprobe-mac spike/ctprobe.c -framework CoreText \
 *      -framework CoreFoundation -framework CoreGraphics
 *   toolchain/bin/tiger-clang -O1 -o build/ctprobe-tiger spike/ctprobe.c \
 *      -Icompat/include -Fcompat/sdk-overlay \
 *      -Fsdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks \
 *      -framework ApplicationServices -framework CoreText -ltigercompat
 * Run: ctprobe <path-to-DejaVuSans.ttf>
 */

#include <CoreFoundation/CoreFoundation.h>
#include <CoreText/CoreText.h>
#include <CoreGraphics/CoreGraphics.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- canonical output helpers ---------- */

static int g_section;

static void section(const char *name) {
    printf("\n## %02d %s\n", ++g_section, name);
}

/* Metrics are compared with a tolerance, so print generously but stably. */
static void kv_f(const char *key, double v) {
    if (v != v) { printf("%-52s NaN\n", key); return; }        /* NaN prints stably */
    if (v == 0) v = 0;                                          /* fold -0.0 */
    printf("%-52s %.6f\n", key, v);
}
static void kv_i(const char *key, long v)          { printf("%-52s %ld\n", key, v); }
static void kv_x(const char *key, unsigned long v) { printf("%-52s 0x%lx\n", key, v); }
static void kv_s(const char *key, const char *v)   { printf("%-52s \"%s\"\n", key, v ? v : "(null)"); }
static void kv_b(const char *key, int v)           { printf("%-52s %s\n", key, v ? "true" : "false"); }
static void kv_null(const char *key)               { printf("%-52s (null)\n", key); }

static const char *cfstr(CFStringRef s, char *buf, size_t n) {
    if (!s) return NULL;
    buf[0] = 0;
    if (CFGetTypeID(s) != CFStringGetTypeID()) { snprintf(buf, n, "(not-a-string)"); return buf; }
    if (!CFStringGetCString(s, buf, (CFIndex)n, kCFStringEncodingUTF8)) snprintf(buf, n, "(unencodable)");
    return buf;
}

static void kv_cfstr(const char *key, CFStringRef s) {
    char b[512];
    if (!s) { kv_null(key); return; }
    kv_s(key, cfstr(s, b, sizeof b));
}

/* A CFTypeRef of unknown class, rendered by class rather than by address. */
static void kv_cftype(const char *key, CFTypeRef v) {
    char b[512];
    if (!v) { kv_null(key); return; }
    CFTypeID t = CFGetTypeID(v);
    if (t == CFStringGetTypeID())      { kv_cfstr(key, (CFStringRef)v); return; }
    if (t == CFNumberGetTypeID())      { double d = 0; CFNumberGetValue((CFNumberRef)v, kCFNumberDoubleType, &d); kv_f(key, d); return; }
    if (t == CFBooleanGetTypeID())     { kv_b(key, CFBooleanGetValue((CFBooleanRef)v)); return; }
    if (t == CFArrayGetTypeID())       { printf("%-52s array[%ld]\n", key, (long)CFArrayGetCount((CFArrayRef)v)); return; }
    if (t == CFDictionaryGetTypeID())  { printf("%-52s dict[%ld]\n", key, (long)CFDictionaryGetCount((CFDictionaryRef)v)); return; }
    if (t == CFDataGetTypeID())        { printf("%-52s data[%ld]\n", key, (long)CFDataGetLength((CFDataRef)v)); return; }
    (void)cfstr(NULL, b, sizeof b);
    printf("%-52s (other-cftype)\n", key);
}

/* Dictionary keys, sorted, so hash order never shows through. */
static CFComparisonResult cmp_str(const void *a, const void *b, void *ctx) {
    (void)ctx; return CFStringCompare((CFStringRef)a, (CFStringRef)b, 0);
}
static void kv_dict_keys(const char *key, CFDictionaryRef d) {
    if (!d) { kv_null(key); return; }
    CFIndex n = CFDictionaryGetCount(d);
    const void **keys = (const void **)calloc((size_t)(n ? n : 1), sizeof(void *));
    CFDictionaryGetKeysAndValues(d, keys, NULL);
    CFMutableArrayRef a = CFArrayCreateMutable(NULL, n, &kCFTypeArrayCallBacks);
    for (CFIndex i = 0; i < n; i++)
        if (CFGetTypeID(keys[i]) == CFStringGetTypeID()) CFArrayAppendValue(a, keys[i]);
    CFArraySortValues(a, CFRangeMake(0, CFArrayGetCount(a)), cmp_str, NULL);
    printf("%-52s [%ld]", key, (long)n);
    char b[256];
    for (CFIndex i = 0; i < CFArrayGetCount(a); i++)
        printf(" %s", cfstr((CFStringRef)CFArrayGetValueAtIndex(a, i), b, sizeof b));
    printf("\n");
    CFRelease(a); free(keys);
}

/* ---------- fixed inputs ---------- */

/* Latin, Arabic and CJK. Arabic is "arabic" (needs joining); CJK is a
   Han string DejaVu almost certainly lacks, which is itself worth measuring. */
static const UniChar kLatin[]  = { 'A','V','A', ' ', 'f','i', ' ', 'W','a','v','e' };
static const UniChar kArabic[] = { 0x0639,0x0631,0x0628,0x064A,0x0629 };
static const UniChar kCJK[]    = { 0x4E00,0x4E8C,0x4E09 };
static const UniChar kMixed[]  = { 'a','b', 0x0410, 0x03B1, 0x0628, 0x4E00 };

static const double kSizes[] = { 12.0, 16.0, 24.0 };

static CFStringRef mkstr(const UniChar *u, CFIndex n) { return CFStringCreateWithCharacters(NULL, u, n); }

static CFAttributedStringRef mkattr(CFStringRef s, CTFontRef font) {
    CFStringRef keys[1]; CFTypeRef vals[1];
    keys[0] = kCTFontAttributeName; vals[0] = font;
    CFDictionaryRef d = CFDictionaryCreate(NULL, (const void **)keys, (const void **)vals, 1,
                                           &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef a = CFAttributedStringCreate(NULL, s, d);
    CFRelease(d);
    return a;
}

/* ---------- probes ---------- */

static void probe_metrics(CTFontRef f, const char *tag) {
    char k[160];
    #define M(fn) do { snprintf(k, sizeof k, "%s.%s", tag, #fn); kv_f(k, (double)fn(f)); } while (0)
    M(CTFontGetSize); M(CTFontGetAscent); M(CTFontGetDescent); M(CTFontGetLeading);
    M(CTFontGetCapHeight); M(CTFontGetXHeight);
    M(CTFontGetUnderlinePosition); M(CTFontGetUnderlineThickness);
    #undef M
    snprintf(k, sizeof k, "%s.CTFontGetUnitsPerEm", tag);        kv_i(k, (long)CTFontGetUnitsPerEm(f));
    snprintf(k, sizeof k, "%s.CTFontGetSymbolicTraits", tag);    kv_x(k, (unsigned long)CTFontGetSymbolicTraits(f));
    CGAffineTransform m = CTFontGetMatrix(f);
    snprintf(k, sizeof k, "%s.CTFontGetMatrix", tag);
    printf("%-52s [%.6f %.6f %.6f %.6f %.6f %.6f]\n", k,
           (double)m.a, (double)m.b, (double)m.c, (double)m.d, (double)m.tx, (double)m.ty);
}

static void probe_glyphs(CTFontRef f, const char *tag, const UniChar *u, CFIndex n, const char *label) {
    CGGlyph g[16];
    char k[160];
    memset(g, 0, sizeof g);
    Boolean all = CTFontGetGlyphsForCharacters(f, u, g, n);
    snprintf(k, sizeof k, "%s.glyphs.%s.allMapped", tag, label);
    kv_b(k, all);
    snprintf(k, sizeof k, "%s.glyphs.%s.ids", tag, label);
    printf("%-52s", k);
    for (CFIndex i = 0; i < n; i++) printf(" %u", (unsigned)g[i]);
    printf("\n");
}

static void probe_charset(CTFontRef f, const char *tag) {
    static const struct { const char *name; UniChar c; } pts[] = {
        { "U+0041", 0x0041 }, { "U+007A", 0x007A }, { "U+00E9", 0x00E9 },
        { "U+0410", 0x0410 }, { "U+03B1", 0x03B1 }, { "U+0628", 0x0628 },
        { "U+4E00", 0x4E00 }, { "U+3042", 0x3042 }, { "U+20AC", 0x20AC },
    };
    char k[160];
    CFCharacterSetRef cs = CTFontCopyCharacterSet(f);
    snprintf(k, sizeof k, "%s.CTFontCopyCharacterSet", tag);
    if (!cs) { kv_null(k); return; }
    printf("%-52s", k);
    for (unsigned i = 0; i < sizeof pts / sizeof pts[0]; i++)
        printf(" %s=%c", pts[i].name, CFCharacterSetIsCharacterMember(cs, pts[i].c) ? 'y' : 'n');
    printf("\n");
    CFRelease(cs);
}

static void probe_line(CTFontRef font, const UniChar *u, CFIndex n, const char *label) {
    char k[160];
    CFStringRef s = mkstr(u, n);
    CFAttributedStringRef as = mkattr(s, font);
    CTLineRef line = CTLineCreateWithAttributedString(as);

    snprintf(k, sizeof k, "line.%s.created", label); kv_b(k, line != NULL);
    if (!line) { CFRelease(as); CFRelease(s); return; }

    CFRange sr = CTLineGetStringRange(line);
    snprintf(k, sizeof k, "line.%s.CTLineGetStringRange", label);
    printf("%-52s {%ld, %ld}\n", k, (long)sr.location, (long)sr.length);

    CFArrayRef runs = CTLineGetGlyphRuns(line);
    CFIndex nruns = runs ? CFArrayGetCount(runs) : -1;
    snprintf(k, sizeof k, "line.%s.CTLineGetGlyphRuns.count", label); kv_i(k, (long)nruns);

    for (CFIndex r = 0; r < nruns; r++) {
        CTRunRef run = (CTRunRef)CFArrayGetValueAtIndex(runs, r);
        CFIndex gc = CTRunGetGlyphCount(run);
        snprintf(k, sizeof k, "line.%s.run%ld.CTRunGetGlyphCount", label, (long)r); kv_i(k, (long)gc);
        snprintf(k, sizeof k, "line.%s.run%ld.CTRunGetStatus", label, (long)r);
        kv_x(k, (unsigned long)CTRunGetStatus(run));
        CFRange rr = CTRunGetStringRange(run);
        snprintf(k, sizeof k, "line.%s.run%ld.CTRunGetStringRange", label, (long)r);
        printf("%-52s {%ld, %ld}\n", k, (long)rr.location, (long)rr.length);
        snprintf(k, sizeof k, "line.%s.run%ld.CTRunGetAttributes", label, (long)r);
        kv_dict_keys(k, CTRunGetAttributes(run));

        const CGGlyph *gp = CTRunGetGlyphsPtr(run);
        snprintf(k, sizeof k, "line.%s.run%ld.CTRunGetGlyphsPtr", label, (long)r);
        if (!gp) kv_null(k);
        else { printf("%-52s", k); for (CFIndex i = 0; i < gc && i < 12; i++) printf(" %u", (unsigned)gp[i]); printf("\n"); }

        const CGSize *ap = CTRunGetAdvancesPtr(run);
        snprintf(k, sizeof k, "line.%s.run%ld.CTRunGetAdvancesPtr", label, (long)r);
        if (!ap) kv_null(k);
        else { printf("%-52s", k); for (CFIndex i = 0; i < gc && i < 12; i++) printf(" %.4f", (double)ap[i].width); printf("\n"); }

        const CFIndex *ip = CTRunGetStringIndicesPtr(run);
        snprintf(k, sizeof k, "line.%s.run%ld.CTRunGetStringIndicesPtr", label, (long)r);
        if (!ip) kv_null(k);
        else { printf("%-52s", k); for (CFIndex i = 0; i < gc && i < 12; i++) printf(" %ld", (long)ip[i]); printf("\n"); }
    }

    /* truncation: ask for a line narrower than the text */
    CTLineRef trunc = CTLineCreateTruncatedLine(line, 20.0, kCTLineTruncationEnd, NULL);
    snprintf(k, sizeof k, "line.%s.CTLineCreateTruncatedLine.created", label); kv_b(k, trunc != NULL);
    if (trunc) {
        CFRange tr = CTLineGetStringRange(trunc);
        snprintf(k, sizeof k, "line.%s.CTLineCreateTruncatedLine.range", label);
        printf("%-52s {%ld, %ld}\n", k, (long)tr.location, (long)tr.length);
        CFArrayRef trs = CTLineGetGlyphRuns(trunc);
        snprintf(k, sizeof k, "line.%s.CTLineCreateTruncatedLine.runs", label);
        kv_i(k, trs ? (long)CFArrayGetCount(trs) : -1);
        CFRelease(trunc);
    }

    CFRelease(line); CFRelease(as); CFRelease(s);
}

/* CTLineCreateWithUniCharProvider needs a provider pair. */
static const UniChar *provider_get(CFIndex idx, CFIndex *count, CFDictionaryRef *attrs, void *info) {
    (void)idx;
    CFDictionaryRef d = (CFDictionaryRef)info;
    static const UniChar chunk[] = { 'P','r','o','v','i','d','e','d' };
    if (idx >= (CFIndex)(sizeof chunk / sizeof chunk[0])) { *count = 0; return NULL; }
    *count = (CFIndex)(sizeof chunk / sizeof chunk[0]) - idx;
    if (attrs) *attrs = d;
    return chunk + idx;
}
static void provider_dispose(void *info) { (void)info; }

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "spike/ctprobe-data/DejaVuSans.ttf";
    char b[512];

    printf("# ctprobe\n");
#if defined(__LP64__) && __LP64__
    printf("cgfloat_is_double                                    true\n");
#else
    printf("cgfloat_is_double                                    false\n");
#endif

    /* ---- load the bundled font ---- */
    section("font load (helper: CTFontManagerCreateFontDescriptorFromData)");
    CFDataRef data = NULL;
    {
        FILE *fp = fopen(path, "rb");
        if (!fp) { printf("FATAL cannot open %s\n", path); return 2; }
        fseek(fp, 0, SEEK_END); long len = ftell(fp); fseek(fp, 0, SEEK_SET);
        void *buf = malloc((size_t)len);
        if (fread(buf, 1, (size_t)len, fp) != (size_t)len) { printf("FATAL short read\n"); return 2; }
        fclose(fp);
        data = CFDataCreate(NULL, (const UInt8 *)buf, len);
        free(buf);
        kv_i("font.bytes", len);
    }
    CTFontDescriptorRef fd = CTFontManagerCreateFontDescriptorFromData(data);
    kv_b("font.descriptorFromData", fd != NULL);
    if (!fd) { printf("FATAL no descriptor; nothing further is comparable\n"); return 3; }

    /* ---- descriptor attributes ---- */
    section("CTFontDescriptorCopyAttribute / CopyAttributes");
    static const struct { const char *name; CFStringRef *key; } dkeys[] = {
        { "Name",       (CFStringRef *)&kCTFontNameAttribute },
        { "Family",     (CFStringRef *)&kCTFontFamilyNameAttribute },
        { "Style",      (CFStringRef *)&kCTFontStyleNameAttribute },
        { "Size",       (CFStringRef *)&kCTFontSizeAttribute },
        { "Traits",     (CFStringRef *)&kCTFontTraitsAttribute },
    };
    for (unsigned i = 0; i < sizeof dkeys / sizeof dkeys[0]; i++) {
        snprintf(b, sizeof b, "desc.CTFontDescriptorCopyAttribute.%s", dkeys[i].name);
        CFTypeRef v = CTFontDescriptorCopyAttribute(fd, *dkeys[i].key);
        kv_cftype(b, v);
        if (v) CFRelease(v);
    }
    kv_dict_keys("desc.CTFontDescriptorCopyAttributes", CTFontDescriptorCopyAttributes(fd));

    /* ---- fonts at each size ---- */
    for (unsigned si = 0; si < sizeof kSizes / sizeof kSizes[0]; si++) {
        double sz = kSizes[si];
        snprintf(b, sizeof b, "font at size %.0f", sz);
        section(b);
        CTFontRef f = CTFontCreateWithFontDescriptor(fd, sz, NULL);
        snprintf(b, sizeof b, "size%.0f.CTFontCreateWithFontDescriptor", sz);
        kv_b(b, f != NULL);
        if (!f) continue;

        snprintf(b, sizeof b, "size%.0f", sz);
        probe_metrics(f, b);

        snprintf(b, sizeof b, "size%.0f.CTFontCopyFamilyName", sz);       kv_cfstr(b, CTFontCopyFamilyName(f));
        snprintf(b, sizeof b, "size%.0f.CTFontCopyPostScriptName", sz);   kv_cfstr(b, CTFontCopyPostScriptName(f));
        snprintf(b, sizeof b, "size%.0f.CTFontCopyAttribute.Family", sz);
        { CFTypeRef v = CTFontCopyAttribute(f, kCTFontFamilyNameAttribute); kv_cftype(b, v); if (v) CFRelease(v); }
        snprintf(b, sizeof b, "size%.0f.CTFontCopyAttribute.Size", sz);
        { CFTypeRef v = CTFontCopyAttribute(f, kCTFontSizeAttribute); kv_cftype(b, v); if (v) CFRelease(v); }

        snprintf(b, sizeof b, "size%.0f", sz);
        probe_glyphs(f, b, kLatin, 11, "latin");
        probe_glyphs(f, b, kArabic, 5, "arabic");
        probe_glyphs(f, b, kCJK, 3, "cjk");
        probe_glyphs(f, b, kMixed, 6, "mixed");
        probe_charset(f, b);
        CFRelease(f);
    }

    /* the size-16 font is the subject for everything structural below */
    CTFontRef font = CTFontCreateWithFontDescriptor(fd, 16.0, NULL);
    if (!font) { printf("FATAL no size-16 font\n"); return 4; }

    /* ---- descriptor round trips ---- */
    section("descriptor round trips");
    {
        CTFontDescriptorRef back = CTFontCopyFontDescriptor(font);
        kv_b("CTFontCopyFontDescriptor", back != NULL);
        if (back) {
            kv_dict_keys("CTFontCopyFontDescriptor.attributes", CTFontDescriptorCopyAttributes(back));
            for (unsigned i = 0; i < sizeof dkeys / sizeof dkeys[0]; i++) {
                snprintf(b, sizeof b, "CTFontCopyFontDescriptor.%s", dkeys[i].name);
                CFTypeRef v = CTFontDescriptorCopyAttribute(back, *dkeys[i].key);
                kv_cftype(b, v);
                if (v) CFRelease(v);
            }
            CFRelease(back);
        }

        CFStringRef ps = CTFontCopyPostScriptName(font);
        if (ps) {
            CTFontDescriptorRef byName = CTFontDescriptorCreateWithNameAndSize(ps, 18.0);
            kv_b("CTFontDescriptorCreateWithNameAndSize", byName != NULL);
            if (byName) {
                CTFontRef f2 = CTFontCreateWithFontDescriptor(byName, 0.0, NULL);
                kv_b("  -> CTFontCreateWithFontDescriptor(size 0)", f2 != NULL);
                if (f2) { kv_f("  -> size carried from descriptor", (double)CTFontGetSize(f2));
                          kv_cfstr("  -> family", CTFontCopyFamilyName(f2)); CFRelease(f2); }
                CFRelease(byName);
            }

            CFStringRef keys[2]; CFTypeRef vals[2];
            double eighteen = 18.0;
            keys[0] = kCTFontNameAttribute; vals[0] = ps;
            keys[1] = kCTFontSizeAttribute; vals[1] = CFNumberCreate(NULL, kCFNumberDoubleType, &eighteen);
            CFDictionaryRef at = CFDictionaryCreate(NULL, (const void **)keys, (const void **)vals, 2,
                                                    &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
            CTFontDescriptorRef made = CTFontDescriptorCreateWithAttributes(at);
            kv_b("CTFontDescriptorCreateWithAttributes", made != NULL);
            if (made) {
                CTFontRef f3 = CTFontCreateWithFontDescriptor(made, 0.0, NULL);
                kv_b("  -> font", f3 != NULL);
                if (f3) { kv_f("  -> size", (double)CTFontGetSize(f3));
                          kv_cfstr("  -> postScriptName", CTFontCopyPostScriptName(f3)); CFRelease(f3); }
                CFRelease(made);
            }
            CFRelease(vals[1]); CFRelease(at); CFRelease(ps);
        }

        CTFontRef copy = CTFontCreateCopyWithAttributes(font, 30.0, NULL, NULL);
        kv_b("CTFontCreateCopyWithAttributes", copy != NULL);
        if (copy) { kv_f("  -> size", (double)CTFontGetSize(copy));
                    kv_f("  -> ascent", (double)CTFontGetAscent(copy));
                    kv_cfstr("  -> family", CTFontCopyFamilyName(copy)); CFRelease(copy); }
    }

    /* ---- CTFontCreateWithName by family, and by PostScript name ---- */
    section("CTFontCreateWithName");
    {
        CFStringRef fam = CTFontCopyFamilyName(font);
        CTFontRef byFam = CTFontCreateWithName(fam ? fam : CFSTR("DejaVu Sans"), 20.0, NULL);
        kv_b("CTFontCreateWithName(family, 20)", byFam != NULL);
        if (byFam) { kv_f("  -> size", (double)CTFontGetSize(byFam));
                     kv_cfstr("  -> family", CTFontCopyFamilyName(byFam));
                     kv_f("  -> ascent", (double)CTFontGetAscent(byFam));
                     CFRelease(byFam); }
        if (fam) CFRelease(fam);
        /* a name nothing can resolve: real CoreText substitutes rather than failing */
        CTFontRef bogus = CTFontCreateWithName(CFSTR("NoSuchFontFamily-Regular"), 14.0, NULL);
        kv_b("CTFontCreateWithName(bogus, 14)", bogus != NULL);
        if (bogus) { kv_cfstr("  -> substituted family", CTFontCopyFamilyName(bogus));
                     kv_f("  -> size", (double)CTFontGetSize(bogus)); CFRelease(bogus); }
    }

    /* ---- CTFontCreateWithGraphicsFont ---- */
    section("CTFontCreateWithGraphicsFont");
    {
        CGFontRef cg = CTFontCopyGraphicsFont(font, NULL);   /* helper on Tiger */
        kv_b("helper CTFontCopyGraphicsFont", cg != NULL);
        if (cg) {
            CTFontRef f = CTFontCreateWithGraphicsFont(cg, 22.0, NULL, NULL);
            kv_b("CTFontCreateWithGraphicsFont", f != NULL);
            if (f) {
                kv_f("  -> size", (double)CTFontGetSize(f));
                kv_f("  -> ascent", (double)CTFontGetAscent(f));
                kv_i("  -> unitsPerEm", (long)CTFontGetUnitsPerEm(f));
                kv_cfstr("  -> postScriptName", CTFontCopyPostScriptName(f));
                probe_glyphs(f, "cgfont", kLatin, 11, "latin");
                CFRelease(f);
            }
            CGFontRelease(cg);
        }
    }

    /* ---- features and variation axes ---- */
    section("CTFontCopyFeatures / CTFontCopyVariationAxes");
    {
        CFArrayRef feats = CTFontCopyFeatures(font);
        if (!feats) kv_null("CTFontCopyFeatures");
        else {
            kv_i("CTFontCopyFeatures.count", (long)CFArrayGetCount(feats));
            for (CFIndex i = 0; i < CFArrayGetCount(feats) && i < 8; i++) {
                CFDictionaryRef d = (CFDictionaryRef)CFArrayGetValueAtIndex(feats, i);
                snprintf(b, sizeof b, "CTFontCopyFeatures[%ld].keys", (long)i);
                kv_dict_keys(b, d);
            }
            CFRelease(feats);
        }
        CFArrayRef axes = CTFontCopyVariationAxes(font);
        if (!axes) kv_null("CTFontCopyVariationAxes");
        else { kv_i("CTFontCopyVariationAxes.count", (long)CFArrayGetCount(axes)); CFRelease(axes); }
    }

    /* ---- line layout ---- */
    section("line layout at size 16");
    probe_line(font, kLatin, 11, "latin");
    probe_line(font, kArabic, 5, "arabic");
    probe_line(font, kCJK, 3, "cjk");
    probe_line(font, kMixed, 6, "mixed");

    /* ---- typesetter ---- */
    section("CTTypesetterCreateLine");
    {
        CFStringRef s = mkstr(kLatin, 11);
        CFAttributedStringRef as = mkattr(s, font);
        CTTypesetterRef ts = CTTypesetterCreateWithAttributedString(as);   /* helper */
        kv_b("helper CTTypesetterCreateWithAttributedString", ts != NULL);
        if (ts) {
            CTLineRef l = CTTypesetterCreateLine(ts, CFRangeMake(0, 5));
            kv_b("CTTypesetterCreateLine{0,5}", l != NULL);
            if (l) {
                CFRange r = CTLineGetStringRange(l);
                printf("%-52s {%ld, %ld}\n", "  -> stringRange", (long)r.location, (long)r.length);
                CFArrayRef rs = CTLineGetGlyphRuns(l);
                kv_i("  -> runs", rs ? (long)CFArrayGetCount(rs) : -1);
                if (rs && CFArrayGetCount(rs))
                    kv_i("  -> run0 glyphs", (long)CTRunGetGlyphCount((CTRunRef)CFArrayGetValueAtIndex(rs, 0)));
                CFRelease(l);
            }
            CFRelease(ts);
        }
        CFRelease(as); CFRelease(s);
    }

    /* ---- uniChar provider ---- */
    section("CTLineCreateWithUniCharProvider");
    {
        CFStringRef keys[1]; CFTypeRef vals[1];
        keys[0] = kCTFontAttributeName; vals[0] = font;
        CFDictionaryRef d = CFDictionaryCreate(NULL, (const void **)keys, (const void **)vals, 1,
                                               &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CTLineRef l = CTLineCreateWithUniCharProvider(provider_get, provider_dispose, (void *)d);
        kv_b("CTLineCreateWithUniCharProvider", l != NULL);
        if (l) {
            CFArrayRef rs = CTLineGetGlyphRuns(l);
            kv_i("  -> runs", rs ? (long)CFArrayGetCount(rs) : -1);
            if (rs && CFArrayGetCount(rs)) {
                CTRunRef run = (CTRunRef)CFArrayGetValueAtIndex(rs, 0);
                kv_i("  -> run0 glyphs", (long)CTRunGetGlyphCount(run));
                const CGGlyph *gp = CTRunGetGlyphsPtr(run);
                if (!gp) kv_null("  -> run0 glyphsPtr");
                else { printf("%-52s", "  -> run0 glyphsPtr");
                       for (CFIndex i = 0; i < CTRunGetGlyphCount(run) && i < 12; i++) printf(" %u", (unsigned)gp[i]);
                       printf("\n"); }
            }
            CFRelease(l);
        }
        CFRelease(d);
    }

    /* ---- paragraph style ---- */
    section("CTParagraphStyleCreate");
    {
        CTTextAlignment align = kCTTextAlignmentCenter;
        double firstIndent = 7.5;
        CTParagraphStyleSetting set[2];
        set[0].spec = kCTParagraphStyleSpecifierAlignment;
        set[0].valueSize = sizeof align; set[0].value = &align;
        CGFloat fi = (CGFloat)firstIndent;
        set[1].spec = kCTParagraphStyleSpecifierFirstLineHeadIndent;
        set[1].valueSize = sizeof fi; set[1].value = &fi;
        CTParagraphStyleRef ps = CTParagraphStyleCreate(set, 2);
        kv_b("CTParagraphStyleCreate", ps != NULL);
        if (ps) {
            CTTextAlignment got = (CTTextAlignment)0; CGFloat gotIndent = -1;
            Boolean okA = CTParagraphStyleGetValueForSpecifier(ps, kCTParagraphStyleSpecifierAlignment, sizeof got, &got);
            Boolean okI = CTParagraphStyleGetValueForSpecifier(ps, kCTParagraphStyleSpecifierFirstLineHeadIndent, sizeof gotIndent, &gotIndent);
            kv_b("  -> alignment readback ok", okA);
            kv_i("  -> alignment value", (long)got);
            kv_b("  -> firstLineHeadIndent readback ok", okI);
            kv_f("  -> firstLineHeadIndent value", (double)gotIndent);
            CFRelease(ps);
        }
    }

    /* ---- framesetter ---- */
    section("framesetter");
    {
        CFStringRef s = mkstr(kLatin, 11);
        CFMutableStringRef big = CFStringCreateMutableCopy(NULL, 0, s);
        for (int i = 0; i < 6; i++) CFStringAppend(big, s);       /* enough to wrap */
        CFAttributedStringRef as = mkattr(big, font);
        CTFramesetterRef fs = CTFramesetterCreateWithAttributedString(as);
        kv_b("CTFramesetterCreateWithAttributedString", fs != NULL);
        if (fs) {
            CGMutablePathRef p = CGPathCreateMutable();
            CGPathAddRect(p, NULL, CGRectMake(0, 0, 120, 200));
            CTFrameRef fr = CTFramesetterCreateFrame(fs, CFRangeMake(0, 0), p, NULL);
            kv_b("CTFramesetterCreateFrame", fr != NULL);
            if (fr) {
                CFArrayRef lines = CTFrameGetLines(fr);
                kv_i("CTFrameGetLines.count", lines ? (long)CFArrayGetCount(lines) : -1);
                if (lines) for (CFIndex i = 0; i < CFArrayGetCount(lines) && i < 8; i++) {
                    CTLineRef l = (CTLineRef)CFArrayGetValueAtIndex(lines, i);
                    CFRange r = CTLineGetStringRange(l);
                    snprintf(b, sizeof b, "CTFrameGetLines[%ld].stringRange", (long)i);
                    printf("%-52s {%ld, %ld}\n", b, (long)r.location, (long)r.length);
                }
                CFRelease(fr);
            }
            CGPathRelease(p);
            CFRelease(fs);
        }
        CFRelease(as); CFRelease(big); CFRelease(s);
    }

    section("end");
    kv_s("status", "complete");
    CFRelease(font); CFRelease(fd); CFRelease(data);
    return 0;
}
