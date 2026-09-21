/*
 * ctshape.c - does Tiger shape AAT fonts the way modern CoreText does?
 *
 * ctprobe.c established that Tiger applies no shaping to an OpenType-only font
 * (DejaVu Sans): Arabic came back as the raw cmap glyphs in visual order. That
 * left the question of whether Tiger's own shaper works at all, or only its
 * OpenType path is missing. Tiger's engine is ATSUI, which reads AAT `morx`
 * and `mort`, so a font carrying those should shape.
 *
 * For each font and string this prints, side by side:
 *   - the raw cmap glyphs from CTFontGetGlyphsForCharacters, which is what the
 *     characters map to with no substitution at all;
 *   - the glyphs CTLine actually produced.
 * When those differ, shaping happened. Comparing the shaped sequence across the
 * two machines then says whether it happened the same way.
 *
 * Glyph access prefers CTRunGetGlyphsPtr and falls back to the copying
 * CTRunGetGlyphs, which is what ComplexTextControllerCoreText.mm does. Modern
 * CoreText returns NULL from the Ptr form, Tiger returns a pointer, so the two
 * machines take different paths here by design; the line reports which.
 *
 * Build: see logs/ct-probe.md. Run: ctshape <font.ttf> [<font.ttf> ...]
 */

#include <CoreFoundation/CoreFoundation.h>
#include <CoreText/CoreText.h>
#include <CoreGraphics/CoreGraphics.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* Keys are prefixed with the font so the same sample name under two fonts
   never collides when the dumps are diffed. */
static const char *g_font = "";

static void kv(const char *k, const char *fmt, ...) {
    va_list ap;
    char full[320];
    snprintf(full, sizeof full, "%s/%s", g_font, k);
    printf("%-58s ", full);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

/* ---- the strings, chosen so each exercises a different substitution ---- */
typedef struct { const char *name; const UniChar *u; CFIndex n; } Sample;

static const UniChar sLigFi[]  = { 'f','i' };
static const UniChar sLigFfl[] = { 'f','f','l' };
static const UniChar sLatin[]  = { 'A','V','A','T','a','r' };          /* kerning pairs */
static const UniChar sArabic[] = { 0x0639,0x0631,0x0628,0x064A,0x0629 }; /* arabiyya */
static const UniChar sArabic2[]= { 0x0645,0x062D,0x0645,0x062F };        /* muhammad */
static const UniChar sCJK[]    = { 0x65E5,0x672C,0x8A9E };               /* nihongo */
static const UniChar sKana[]   = { 0x3042,0x3044,0x3046 };

static const Sample kSamples[] = {
    { "lig.fi",   sLigFi,   2 },
    { "lig.ffl",  sLigFfl,  3 },
    { "latin",    sLatin,   6 },
    { "arabic1",  sArabic,  5 },
    { "arabic2",  sArabic2, 4 },
    { "cjk",      sCJK,     3 },
    { "kana",     sKana,    3 },
};

static CFDataRef readfile(const char *p) {
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    void *b = malloc((size_t)n);
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
    fclose(f);
    CFDataRef d = CFDataCreate(NULL, (const UInt8 *)b, n);
    free(b);
    return d;
}

static const char *base(const char *p) {
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static void run_font(const char *path) {
    char k[256], line[1024], *w;
    CFDataRef data = readfile(path);
    g_font = base(path);
    printf("\n## font %s\n", g_font);
    if (!data) { kv("load", "FAILED to read"); return; }
    kv("bytes", "%ld", (long)CFDataGetLength(data));

    CTFontDescriptorRef fd = CTFontManagerCreateFontDescriptorFromData(data);
    if (!fd) { kv("descriptorFromData", "NULL - nothing comparable"); CFRelease(data); return; }
    CTFontRef font = CTFontCreateWithFontDescriptor(fd, 24.0, NULL);
    if (!font) { kv("font", "NULL"); CFRelease(fd); CFRelease(data); return; }

    CFStringRef ps = CTFontCopyPostScriptName(font);
    char nb[256] = {0};
    if (ps) CFStringGetCString(ps, nb, sizeof nb, kCFStringEncodingUTF8);
    kv("postScriptName", "\"%s\"", nb);
    kv("unitsPerEm", "%ld", (long)CTFontGetUnitsPerEm(font));

    for (unsigned s = 0; s < sizeof kSamples / sizeof kSamples[0]; s++) {
        const Sample *sm = &kSamples[s];

        /* 1. raw cmap mapping, no substitution */
        CGGlyph raw[16];
        memset(raw, 0, sizeof raw);
        Boolean all = CTFontGetGlyphsForCharacters(font, sm->u, raw, sm->n);
        w = line; *w = 0;
        for (CFIndex i = 0; i < sm->n; i++) w += sprintf(w, " %u", (unsigned)raw[i]);
        snprintf(k, sizeof k, "%s.cmapGlyphs", sm->name);
        kv(k, "%s%s", line, all ? "" : "   (not all mapped)");

        /* 2. what the line actually produced */
        CFStringRef str = CFStringCreateWithCharacters(NULL, sm->u, sm->n);
        CFStringRef ak[1]; CFTypeRef av[1];
        ak[0] = kCTFontAttributeName; av[0] = font;
        CFDictionaryRef at = CFDictionaryCreate(NULL, (const void **)ak, (const void **)av, 1,
                                                &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFAttributedStringRef as = CFAttributedStringCreate(NULL, str, at);
        CTLineRef ln = CTLineCreateWithAttributedString(as);
        snprintf(k, sizeof k, "%s.line", sm->name);
        if (!ln) { kv(k, "NULL"); goto next; }

        CFArrayRef runs = CTLineGetGlyphRuns(ln);
        CFIndex nruns = runs ? CFArrayGetCount(runs) : 0;
        CFIndex total = 0;
        for (CFIndex r = 0; r < nruns; r++)
            total += CTRunGetGlyphCount((CTRunRef)CFArrayGetValueAtIndex(runs, r));
        kv(k, "runs=%ld glyphs=%ld", (long)nruns, (long)total);

        for (CFIndex r = 0; r < nruns; r++) {
            CTRunRef run = (CTRunRef)CFArrayGetValueAtIndex(runs, r);
            CFIndex gc = CTRunGetGlyphCount(run);
            if (gc > 16) gc = 16;

            /* the font this run actually used, which fallback may have changed */
            CFDictionaryRef ra = CTRunGetAttributes(run);
            CTFontRef rf = ra ? (CTFontRef)CFDictionaryGetValue(ra, kCTFontAttributeName) : NULL;
            char rn[256] = {0};
            if (rf) { CFStringRef rp = CTFontCopyPostScriptName(rf);
                      if (rp) { CFStringGetCString(rp, rn, sizeof rn, kCFStringEncodingUTF8); CFRelease(rp); } }

            CGGlyph gbuf[16];
            const CGGlyph *gp = CTRunGetGlyphsPtr(run);
            const char *via = "ptr";
            if (!gp) { CTRunGetGlyphs(run, CFRangeMake(0, gc), gbuf); gp = gbuf; via = "copy"; }

            w = line; *w = 0;
            for (CFIndex i = 0; i < gc; i++) w += sprintf(w, " %u", (unsigned)gp[i]);
            snprintf(k, sizeof k, "%s.run%ld.glyphs[%s]", sm->name, (long)r, via);
            kv(k, "%s", line);

            CGSize abuf[16];
            const CGSize *ap = CTRunGetAdvancesPtr(run);
            if (!ap) { CTRunGetAdvances(run, CFRangeMake(0, gc), abuf); ap = abuf; }
            w = line; *w = 0;
            for (CFIndex i = 0; i < gc; i++) w += sprintf(w, " %.3f", (double)ap[i].width);
            snprintf(k, sizeof k, "%s.run%ld.advances", sm->name, (long)r);
            kv(k, "%s", line);

            snprintf(k, sizeof k, "%s.run%ld.status", sm->name, (long)r);
            kv(k, "0x%lx", (unsigned long)CTRunGetStatus(run));
            /* Identify the run's font by its own properties rather than by
               pointer identity: an equivalent font is not always the same object,
               and a same-named font from another source is the hazard here. */
            snprintf(k, sizeof k, "%s.run%ld.font", sm->name, (long)r);
            if (!rf) { kv(k, "(none)"); }
            else {
                CGGlyph probe = 0;
                UniChar first = sm->u[0];
                CTFontGetGlyphsForCharacters(rf, &first, &probe, 1);
                kv(k, "\"%s\" upem=%ld glyphs=%ld size=%.1f firstCharGlyph=%u%s",
                   rn, (long)CTFontGetUnitsPerEm(rf), (long)CTFontGetGlyphCount(rf),
                   (double)CTFontGetSize(rf), (unsigned)probe,
                   (rf == font) ? "  (the font we loaded)" : "");
            }
        }

        /* 3. the headline: did anything get substituted? */
        {
            int substituted = (total != sm->n);
            if (!substituted && nruns == 1) {
                CTRunRef run = (CTRunRef)CFArrayGetValueAtIndex(runs, 0);
                CGGlyph gbuf[16];
                const CGGlyph *gp = CTRunGetGlyphsPtr(run);
                if (!gp) { CTRunGetGlyphs(run, CFRangeMake(0, sm->n), gbuf); gp = gbuf; }
                for (CFIndex i = 0; i < sm->n; i++) {
                    int found = 0;
                    for (CFIndex j = 0; j < sm->n; j++) if (gp[i] == raw[j]) { found = 1; break; }
                    if (!found) { substituted = 1; break; }
                }
            }
            snprintf(k, sizeof k, "%s.shaped", sm->name);
            kv(k, "%s", substituted ? "yes - glyphs differ from the raw cmap" : "no - raw cmap glyphs only");
        }
        CFRelease(ln);
    next:
        CFRelease(as); CFRelease(at); CFRelease(str);
    }
    if (ps) CFRelease(ps);
    CFRelease(font); CFRelease(fd); CFRelease(data);
}

int main(int argc, char **argv) {
    printf("# ctshape\n");
    if (argc < 2) { printf("usage: ctshape <font> [<font> ...]\n"); return 2; }
    for (int i = 1; i < argc; i++) run_font(argv[i]);
    printf("\n## end\n");
    return 0;
}
