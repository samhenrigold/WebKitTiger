/* TIGER: does HarfBuzz agree with Tiger's CoreText for the same font bytes?
 *
 * The candidate architecture lays out and shapes in a 64-bit process with
 * HarfBuzz and rasterises in a 32-bit process with Tiger's CoreText through
 * ctcompat's adapters. That only works if the two agree on which glyphs to draw
 * and how far apart. This measures the agreement.
 *
 * Both sides load identical bytes: HarfBuzz from the file, CoreText from the
 * same buffer through CTFontManagerCreateFontDescriptorFromData, which is
 * ctcompat's ATS activation. Naming a font instead of supplying it would
 * compare two font files as well as two implementations.
 *
 * Build (bash, repo root):
 *   toolchain/bin/tiger-clang++ -O1 -g -x c++ spike/hbvsct.c -o build/hbvsct \
 *     -I toolchain/sysroot-i386/usr/include/harfbuzz \
 *     -nostdinc++ -isystem toolchain/sysroot-i386/usr/include/c++/v1 -stdlib=libc++ \
 *     -Fcompat/sdk-overlay \
 *     -F sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks \
 *     -lharfbuzz -lc++ -lc++abi -lunwind -ltigercompat \
 *     -framework CoreFoundation -framework ApplicationServices \
 *     -Wl,build/builtins-i386/libclang_rt.builtins-i386.a
 * Run on the box: hbvsct <font.ttf> [more fonts...]
 */

#include <CoreText/CoreText.h>
#include <hb.h>
#include <hb-ot.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAXG 64

struct sample { const char* name; const char* script; unsigned n; hb_codepoint_t cp[12]; };

/* Latin ligatures and kerned pairs exercise GSUB/morx and GPOS/kern. The Arabic
 * and CJK samples only mean anything for the fonts that cover them. */
static const struct sample samples[] = {
    { "plain",      "Latn", 5, { 'H','a','m','b','u' } },
    { "lig fi",     "Latn", 2, { 'f','i' } },
    { "lig ffl",    "Latn", 3, { 'f','f','l' } },
    { "kern AV",    "Latn", 2, { 'A','V' } },
    { "kern To",    "Latn", 2, { 'T','o' } },
    { "arabic",     "Arab", 3, { 0x0627, 0x0628, 0x062C } },
    { "cjk",        "Hani", 2, { 0x4E2D, 0x6587 } }
};
static const unsigned kSampleCount = sizeof(samples) / sizeof(samples[0]);
static const double sizes[] = { 12, 16, 24 };

static double maxGlyphDelta, maxAdvDelta, sumAdvDelta;
static double maxTotalDelta, sumTotalDelta;
static long advComparisons, glyphComparisons, glyphMismatches, totalComparisons;

static CFDataRef loadData(const char* path)
{
    FILE* f = fopen(path, "rb");
    long n;
    unsigned char* b;
    CFDataRef d;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = (unsigned char*)malloc(n);
    if (fread(b, 1, n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
    fclose(f);
    d = CFDataCreate(NULL, b, n);
    free(b);
    return d;
}

/* ---- CoreText side: shape through CTLine, read back with the adapters ---- */

static unsigned ctShape(CTFontRef font, const hb_codepoint_t* cp, unsigned n,
    CGGlyph* glyphs, double* advances)
{
    UniChar u[24];
    unsigned i, k = 0, out = 0;
    CFStringRef s;
    CFStringRef key = kCTFontAttributeName;
    CFDictionaryRef attrs;
    CFAttributedStringRef as;
    CTLineRef line;
    CFArrayRef runs;

    for (i = 0; i < n; ++i) {
        if (cp[i] < 0x10000)
            u[k++] = (UniChar)cp[i];
    }
    s = CFStringCreateWithCharacters(NULL, u, k);
    attrs = CFDictionaryCreate(NULL, (const void**)&key, (const void**)&font, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    as = CFAttributedStringCreate(NULL, s, attrs);
    line = CTLineCreateWithAttributedString(as);
    runs = line ? CTLineGetGlyphRuns(line) : NULL;

    if (runs) {
        CFIndex r;
        for (r = 0; r < CFArrayGetCount(runs) && out < MAXG; ++r) {
            CTRunRef run = (CTRunRef)CFArrayGetValueAtIndex(runs, r);
            CFIndex c = CTRunGetGlyphCount(run), j;
            CGGlyph* g = (CGGlyph*)calloc((size_t)c, sizeof(CGGlyph));
            CGSize* a = (CGSize*)calloc((size_t)c, sizeof(CGSize));

            CTRunGetGlyphs(run, CFRangeMake(0, 0), g);
            CTRunGetAdvances(run, CFRangeMake(0, 0), a);
            for (j = 0; j < c && out < MAXG; ++j) {
                glyphs[out] = g[j];
                advances[out] = a[j].width;
                ++out;
            }
            free(g); free(a);
        }
    }
    if (line) CFRelease(line);
    CFRelease(as); CFRelease(attrs); CFRelease(s);
    return out;
}

/* ---- HarfBuzz side ------------------------------------------------------ */

static unsigned hbShape(hb_font_t* font, const struct sample* sm, double size,
    const char* const* shaperList, CGGlyph* glyphs, double* advances, int* ran,
    int disableKerning)
{
    hb_buffer_t* buf = hb_buffer_create();
    hb_feature_t noKern;
    unsigned n = 0, i;
    hb_glyph_info_t* info;
    hb_glyph_position_t* pos;

    hb_buffer_add_codepoints(buf, sm->cp, (int)sm->n, 0, (int)sm->n);
    hb_buffer_set_direction(buf, strcmp(sm->script, "Arab") ? HB_DIRECTION_LTR : HB_DIRECTION_RTL);
    hb_buffer_set_script(buf, hb_script_from_string(sm->script, -1));
    hb_buffer_set_language(buf, hb_language_from_string("en", -1));

    hb_feature_from_string("-kern", -1, &noKern);
    *ran = hb_shape_full(font, buf, disableKerning ? &noKern : NULL, disableKerning ? 1 : 0,
        shaperList);
    if (!*ran) { hb_buffer_destroy(buf); return 0; }

    info = hb_buffer_get_glyph_infos(buf, &n);
    pos = hb_buffer_get_glyph_positions(buf, &n);
    if (n > MAXG) n = MAXG;
    for (i = 0; i < n; ++i) {
        glyphs[i] = (CGGlyph)info[i].codepoint;
        /* scale is size*64, so positions are 26.6 of points */
        advances[i] = pos[i].x_advance / 64.0;
    }
    hb_buffer_destroy(buf);
    (void)size;
    return n;
}

static void compareFont(const char* path)
{
    CFDataRef data = loadData(path);
    hb_blob_t* blob;
    hb_face_t* face;
    CTFontDescriptorRef desc;
    const char* base = path;
    const char* p;
    unsigned si, mi;
    static const char* aatOnly[] = { "aat", NULL };
    static const char* otOnly[] = { "ot", NULL };
    CGGlyph hg[MAXG], cg[MAXG];
    double ha[MAXG], ca[MAXG];
    int ran;

    for (p = path; *p; ++p) if (*p == '/') base = p + 1;
    if (!data) { printf("%s: cannot read\n", base); return; }
    desc = CTFontManagerCreateFontDescriptorFromData(data);
    if (!desc) { printf("%s: CoreText will not load it\n", base); CFRelease(data); return; }

    blob = hb_blob_create((const char*)CFDataGetBytePtr(data), (unsigned)CFDataGetLength(data),
        HB_MEMORY_MODE_READONLY, NULL, NULL);
    face = hb_face_create(blob, 0);

    printf("\n=== %s ===\n", base);
    {   /* HarfBuzz has no separate "aat" shaper: morx and kerx are handled
         * inside the "ot" shaper, which prefers them when the font has them.
         * So the useful report is which shapers exist and that "ot" ran. */
        hb_font_t* f = hb_font_create(face);
        int ranOt = 0;
        const char** all = (const char**)hb_shape_list_shapers();
        unsigned i;
        hb_font_set_scale(f, (int)(16 * 64), (int)(16 * 64));
        hb_font_set_ppem(f, 0, 0);
        hbShape(f, &samples[0], 16, otOnly, hg, ha, &ranOt, 0);
        printf("  harfbuzz shapers:");
        for (i = 0; all && all[i]; ++i) printf(" %s", all[i]);
        printf("   ot ran: %s   (AAT morx/kerx live inside \"ot\")\n", ranOt ? "yes" : "no");
        (void)aatOnly;
        hb_font_destroy(f);
    }

    for (si = 0; si < 3; ++si) {
        double size = sizes[si];
        hb_font_t* hbf = hb_font_create(face);
        CTFontRef ctf = CTFontCreateWithFontDescriptor(desc, size, NULL);

        hb_font_set_scale(hbf, (int)(size * 64), (int)(size * 64));
        hb_font_set_ppem(hbf, 0, 0);   /* unhinted, to match CoreText's linear advances */

        for (mi = 0; mi < kSampleCount; ++mi) {
            unsigned hn = hbShape(hbf, &samples[mi], size, NULL, hg, ha, &ran, 0);
            unsigned cn = ctShape(ctf, samples[mi].cp, samples[mi].n, cg, ca);
            unsigned i, lim = hn < cn ? hn : cn;
            int glyphsSame = (hn == cn);
            int allNotdef = 1;
            double worst = 0;

            if (!hn && !cn) continue;
            for (i = 0; i < hn; ++i) if (hg[i]) allNotdef = 0;
            if (allNotdef) {
                /* The font does not cover this sample. CTLine silently falls
                 * back to another font; hb_shape only ever uses the font it was
                 * given. Comparing them here would measure font fallback, not
                 * shaping, so there is nothing to compare. */
                printf("  %-10s %2.0fpt  not covered by this font (CoreText fell back)\n",
                    samples[mi].name, size);
                continue;
            }
            for (i = 0; i < lim; ++i) {
                ++glyphComparisons;
                if (hg[i] != cg[i]) { glyphsSame = 0; ++glyphMismatches; }
                {
                    double d = fabs(ha[i] - ca[i]);
                    if (d > worst) worst = d;
                    if (d > maxAdvDelta) maxAdvDelta = d;
                    sumAdvDelta += d; ++advComparisons;
                }
            }
            {
                /* The total is what layout actually depends on. Per-glyph
                 * advances can differ while the run is the same width, which is
                 * exactly what AAT kerning does here: CoreText puts the whole
                 * kern on the leading glyph, HarfBuzz splits it across the pair. */
                double ht = 0, ct2 = 0, td;
                for (i = 0; i < hn; ++i) ht += ha[i];
                for (i = 0; i < cn; ++i) ct2 += ca[i];
                td = fabs(ht - ct2);
                if (td > maxTotalDelta) maxTotalDelta = td;
                sumTotalDelta += td; ++totalComparisons;
                printf("  %-10s %2.0fpt  hb=%u ct=%u  %-8s  per-glyph %.4f  total %.4f pt\n",
                    samples[mi].name, size, hn, cn,
                    glyphsSame ? "same" : "DIFFER", worst, td);
            }
            if (!glyphsSame) {
                printf("       hb:"); for (i = 0; i < hn; ++i) printf(" %u", hg[i]);
                printf("\n       ct:"); for (i = 0; i < cn; ++i) printf(" %u", cg[i]);
                printf("\n");
            }
            /* When the advances disagree but the glyphs do not, the usual cause
             * is one side applying kerning the other did not. Re-shape with
             * kerning off and say which side that matches. */
            if (glyphsSame && worst > 0.05) {
                CGGlyph ng[MAXG]; double na[MAXG]; int r2 = 0;
                unsigned nn = hbShape(hbf, &samples[mi], size, NULL, ng, na, &r2, 1);
                double worstNoKern = 0;
                for (i = 0; i < nn && i < cn; ++i) {
                    double d = fabs(na[i] - ca[i]);
                    if (d > worstNoKern) worstNoKern = d;
                }
                printf("       kerning: hb-with %.4f  hb-without %.4f  -> CoreText matches %s\n",
                    worst, worstNoKern, worstNoKern < worst ? "UNKERNED" : "kerned");
            }
        }

        if (si == 1) {  /* metrics once, at 16pt */
            hb_font_extents_t ext;
            hb_font_get_h_extents(hbf, &ext);
            printf("  metrics 16pt  hb asc %.3f desc %.3f gap %.3f | ct asc %.3f desc %.3f lead %.3f\n",
                ext.ascender / 64.0, -ext.descender / 64.0, ext.line_gap / 64.0,
                (double)CTFontGetAscent(ctf), (double)CTFontGetDescent(ctf),
                (double)CTFontGetLeading(ctf));
            printf("  cap/x   16pt  ct cap %.3f x %.3f\n",
                (double)CTFontGetCapHeight(ctf), (double)CTFontGetXHeight(ctf));
        }

        if (ctf) CFRelease(ctf);
        hb_font_destroy(hbf);
    }

    hb_face_destroy(face);
    hb_blob_destroy(blob);
    CFRelease(desc);
    CFRelease(data);
    (void)maxGlyphDelta;
}

int main(int argc, char** argv)
{
    int i;
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("HarfBuzz %s vs Tiger CoreText, identical font bytes\n", hb_version_string());
    for (i = 1; i < argc; ++i)
        compareFont(argv[i]);
    printf("\n--- totals ---\n");
    printf("glyph comparisons %ld, mismatches %ld\n", glyphComparisons, glyphMismatches);
    printf("per-glyph advance: %ld comparisons, mean %.5f pt, max %.5f pt\n",
        advComparisons, advComparisons ? sumAdvDelta / advComparisons : 0.0, maxAdvDelta);
    printf("run total advance: %ld comparisons, mean %.5f pt, max %.5f pt\n",
        totalComparisons, totalComparisons ? sumTotalDelta / totalComparisons : 0.0, maxTotalDelta);
    return glyphMismatches ? 1 : 0;
}
