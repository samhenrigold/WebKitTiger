/* TIGER: write the font manifest the 64-bit web process builds its font
 * database from.
 *
 * The split port picks fonts and shapes in a 64-bit process with HarfBuzz over
 * font *files*, and rasterises in a 32-bit process with Tiger's CoreText. For
 * the two to agree they must name the same faces by the same handles, so this
 * runs on the box, enumerates every installed face through ATS, and records the
 * handle plus everything the web process needs to match on without opening the
 * file itself.
 *
 * The handle is (path, face index). A PostScript name is not one: two files can
 * carry the same name and activation order decides who wins. The face index is
 * the position within a .dfont suitcase or .ttc collection, resolved here by
 * parsing the container and matching PostScript names, which is the same order
 * TigerCTFontForHandle uses.
 *
 * Metrics are read out of the sfnt rather than asked of CoreText, because the
 * web process will read them the same way and agreement should be by
 * construction rather than by luck. The one exception is the cap-height and
 * x-height pair, where CT-SURVEY.md records that Tiger quantises and ctcompat
 * synthesises; both are emitted so a consumer can see the difference.
 *
 * Build:
 *   toolchain/bin/tiger-clang -O1 -o build/fontmanifest spike/fontmanifest.c \
 *     -ltigercompat -Fcompat/sdk-overlay \
 *     -F sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks \
 *     -framework CoreFoundation -framework CoreServices -framework ApplicationServices
 * Run on the box: fontmanifest > fonts.json
 */

#include <CoreText/CoreText.h>
#include <TigerCompat/CTFontHandle.h>
#include <CoreServices/CoreServices.h>
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- sfnt reading ------------------------------------------------------ */

static unsigned be16(const unsigned char* p) { return (unsigned)((p[0] << 8) | p[1]); }
static unsigned long be32(const unsigned char* p)
{ return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | p[3]; }

/* A face inside a container. `offset` is where its sfnt header sits.
 * `tableOrigin` is what its table directory's offsets are measured from, which
 * is NOT always the same thing: a .ttc shares one file and its faces' table
 * offsets are absolute from the start of the file, while a .dfont resource is a
 * self-contained sfnt whose offsets are relative to the resource. Getting this
 * wrong reads plausible-looking garbage rather than failing. */
struct sfnt { const unsigned char* base; size_t length; size_t offset; size_t tableOrigin; };

static int sfntTable(const struct sfnt* f, const char* tag, size_t* off, size_t* len)
{
    const unsigned char* d = f->base + f->offset;
    unsigned n, i;

    if (f->offset + 12 > f->length)
        return 0;
    n = be16(d + 4);
    if (n > 512)                     /* not a table directory */
        return 0;
    for (i = 0; i < n; ++i) {
        const unsigned char* rec = d + 12 + i * 16;
        if (f->offset + 12 + (size_t)(i + 1) * 16 > f->length)
            return 0;
        if (!memcmp(rec, tag, 4)) {
            *off = f->tableOrigin + (size_t)be32(rec + 8);
            *len = (size_t)be32(rec + 12);
            return *off <= f->length && *len <= f->length - *off;
        }
    }
    return 0;
}


struct metrics {
    unsigned unitsPerEm;
    int ascent, descent, lineGap;   /* hhea, font units */
    int capHeight, xHeight;         /* OS/2 v2+, 0 when absent */
    int hasOS2Heights;
};

static void sfntMetrics(const struct sfnt* f, struct metrics* m)
{
    size_t off, len;

    memset(m, 0, sizeof(*m));
    if (sfntTable(f, "head", &off, &len) && len >= 54)
        m->unitsPerEm = be16(f->base + off + 18);
    if (sfntTable(f, "hhea", &off, &len) && len >= 36) {
        m->ascent = (short)be16(f->base + off + 4);
        m->descent = (short)be16(f->base + off + 6);
        m->lineGap = (short)be16(f->base + off + 8);
    }
    if (sfntTable(f, "OS/2", &off, &len) && len >= 90) {
        unsigned version = be16(f->base + off);
        if (version >= 2) {
            m->xHeight = (short)be16(f->base + off + 86);
            m->capHeight = (short)be16(f->base + off + 88);
            m->hasOS2Heights = 1;
        }
    }
}

/* ---- containers: plain sfnt, .ttc collection, .dfont suitcase ----------- */

#define kMaxFaces 64

struct container {
    unsigned char* bytes;
    size_t length;
    size_t faceOffset[kMaxFaces];
    int tableOriginIsFace;      /* dfont: yes. ttc and plain sfnt: no. */
    unsigned faceCount;
};

/* Plenty of Tiger's fonts keep their sfnt in the *resource fork* and leave the
 * data fork empty: everything in /Library/Fonts without an extension, and the
 * multiple-master faces in /System/Library/Fonts. On Mac OS X the resource fork
 * is readable as a named fork, so try that when the data fork has nothing.
 * Missing this silently loses 76 of 176 faces, all of them with a perfectly
 * good path. */
static void scanContainer(struct container* c);

static int readFork(const char* path, struct container* c)
{
    FILE* f = fopen(path, "rb");
    long n;

    memset(c, 0, sizeof(*c));
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return 0; }
    c->bytes = (unsigned char*)malloc((size_t)n);
    if (!c->bytes) { fclose(f); return 0; }
    if (fread(c->bytes, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(c->bytes); return 0; }
    fclose(f);
    c->length = (size_t)n;
    return 1;
}

static int readFile(const char* path, struct container* c)
{
    char forked[1200];

    if (readFork(path, c)) {
        scanContainer(c);
        if (c->faceCount)
            return 1;
        free(c->bytes);
    }
    snprintf(forked, sizeof(forked), "%s/..namedfork/rsrc", path);
    if (!readFork(forked, c))
        return 0;
    scanContainer(c);
    return c->faceCount != 0;
}

/* A .dfont keeps its faces as `sfnt` resources in a classic resource fork that
 * lives in the data fork. The order here is resource order, which is the order
 * ATS reports them in, which is what the face index means. */
static void scanResourceFork(struct container* c)
{
    const unsigned char* d = c->bytes;
    unsigned long dataOff, mapOff;
    unsigned typeListOff, typeCount, i;

    if (c->length < 16)
        return;
    dataOff = be32(d);
    mapOff = be32(d + 4);
    if (mapOff + 30 > c->length)
        return;
    typeListOff = be16(d + mapOff + 24);
    if (mapOff + typeListOff + 2 > c->length)
        return;
    typeCount = be16(d + mapOff + typeListOff) + 1;
    for (i = 0; i < typeCount; ++i) {
        const unsigned char* e = d + mapOff + typeListOff + 2 + i * 8;
        unsigned count, refOff, j;

        if ((size_t)(e - d) + 8 > c->length || memcmp(e, "sfnt", 4))
            continue;
        count = be16(e + 4) + 1;
        refOff = be16(e + 6);
        c->tableOriginIsFace = 1;
        for (j = 0; j < count && c->faceCount < kMaxFaces; ++j) {
            const unsigned char* r = d + mapOff + typeListOff + refOff + j * 12;
            unsigned long off;
            if ((size_t)(r - d) + 12 > c->length)
                break;
            off = be32(r + 4) & 0x00FFFFFFul;
            if (dataOff + off + 4 <= c->length)
                c->faceOffset[c->faceCount++] = dataOff + off + 4;
        }
        return;
    }
}

static void scanContainer(struct container* c)
{
    unsigned long tag;

    if (c->length < 12)
        return;
    tag = be32(c->bytes);
    if (tag == 0x74746366ul) {           /* 'ttcf' */
        unsigned long n = be32(c->bytes + 8), i;
        for (i = 0; i < n && c->faceCount < kMaxFaces; ++i)
            c->faceOffset[c->faceCount++] = (size_t)be32(c->bytes + 12 + i * 4);
        return;
    }
    if (tag == 0x00010000ul || tag == 0x4F54544Ful || tag == 0x74727565ul) {
        c->faceOffset[c->faceCount++] = 0;   /* plain sfnt: 'true', OTTO or 1.0 */
        return;
    }
    scanResourceFork(c);
}

/* ---- JSON ---------------------------------------------------------------- */

static void jsonString(const char* s)
{
    putchar('"');
    for (; *s; ++s) {
        if (*s == '"' || *s == '\\') { putchar('\\'); putchar(*s); }
        else if ((unsigned char)*s < 0x20) printf("\\u%04x", (unsigned char)*s);
        else putchar(*s);
    }
    putchar('"');
}

static void cfToUTF8(CFStringRef s, char* out, size_t cap)
{
    out[0] = 0;
    if (s)
        CFStringGetCString(s, out, (CFIndex)cap, kCFStringEncodingUTF8);
}

/* ---- the faces ----------------------------------------------------------- */

/* The face's position within its own container, straight from ATS.
 *
 * Matching by PostScript name instead looks obvious and is wrong for the old
 * suitcases: ATS reports StoneSansITCTT-Semi where the sfnt's own name table
 * says StoneSansSemITCTTSemi, because ATS synthesises the name from the FOND
 * rather than reading name id 6. Eleven faces resolved to no index that way. */
/* The face's position within its own container.
 *
 * Two approaches do not work. Matching the sfnt's own PostScript name fails for
 * the old suitcases, because ATS synthesises its name from the FOND: it reports
 * StoneSansITCTT-Semi where the name table says StoneSansSemITCTTSemi, which
 * cost eleven faces. And ATSFontGetContainer, which looks like the direct
 * answer, returns error 8 with a null container for anything this process did
 * not activate itself, so it cannot speak about installed fonts.
 *
 * What does work is doing exactly what TigerCTFontForHandle does: activate the
 * file in the local context and read the faces back in ATS's own order. Both
 * sides then get their names from ATS, so they agree, and the ordering is the
 * one the resolver will use. The handle is correct by construction rather than
 * by a match that can drift. */
struct fileFaces {
    struct fileFaces* next;
    char* path;
    ATSFontRef faces[64];
    char names[64][128];
    unsigned count;
};

static struct fileFaces* g_files;

static struct fileFaces* facesForPath(const char* path)
{
    struct fileFaces* e;
    FSRef ref;
    FSSpec spec;
    ATSFontContainerRef container = 0;
    ATSFontRef found[64];
    ItemCount n = 0, i;

    for (e = g_files; e; e = e->next)
        if (!strcmp(e->path, path))
            return e;

    if (FSPathMakeRef((const UInt8*)path, &ref, NULL) != noErr)
        return NULL;
    if (FSGetCatalogInfo(&ref, kFSCatInfoNone, NULL, NULL, &spec, NULL) != noErr)
        return NULL;
    if (ATSFontActivateFromFileSpecification(&spec, kATSFontContextLocal,
            kATSFontFormatUnspecified, NULL, kATSOptionFlagsDefault, &container) != noErr
        || !container)
        return NULL;
    if (ATSFontFindFromContainer(container, kATSOptionFlagsDefault, 64, found, &n) != noErr || !n)
        return NULL;

    e = (struct fileFaces*)calloc(1, sizeof(*e));
    if (!e)
        return NULL;
    e->path = strdup(path);
    e->count = (unsigned)(n < 64 ? n : 64);
    for (i = 0; i < e->count; ++i) {
        CFStringRef ps = NULL;
        e->faces[i] = found[i];
        if (ATSFontGetPostScriptName(found[i], kATSOptionFlagsDefault, &ps) == noErr && ps) {
            CFStringGetCString(ps, e->names[i], sizeof(e->names[i]), kCFStringEncodingUTF8);
            CFRelease(ps);
        }
    }
    e->next = g_files;
    g_files = e;
    return e;
}

static int faceIndexForPath(const char* path, const char* psName)
{
    struct fileFaces* e = facesForPath(path);
    unsigned i;

    if (!e)
        return -1;
    for (i = 0; i < e->count; ++i)
        if (!strcmp(e->names[i], psName))
            return (int)i;
    return e->count == 1 ? 0 : -1;
}

static long totalFaces, resolvedFaces, unresolvedFaces;

/* Every handle this manifest hands out, so the self-check can prove each one
 * resolves back to the face it names. */
static struct { char path[1024]; int index; char psName[128]; } g_handles[512];
static unsigned g_handleCount;

static void emitFace(ATSFontRef ats, int first)
{
    CFStringRef ps = NULL, full = NULL, family = NULL;
    char psName[256] = "", fullName[256] = "", familyName[256] = "", path[1024] = "";
    FSSpec spec;
    FSRef ref;
    struct container c;
    int index = -1;
    struct metrics m;
    CTFontRef ct = NULL;
    CTFontSymbolicTraits traits = 0;

    ++totalFaces;
    if (getenv("FM_TRACE")) fprintf(stderr, "[face %ld]\n", totalFaces);
    if (ATSFontGetPostScriptName(ats, kATSOptionFlagsDefault, &ps) == noErr && ps)
        cfToUTF8(ps, psName, sizeof(psName));
    if (!psName[0]) { if (ps) CFRelease(ps); ++unresolvedFaces; return; }

    if (ATSFontGetName(ats, kATSOptionFlagsDefault, &full) == noErr && full)
        cfToUTF8(full, fullName, sizeof(fullName));

    if (ATSFontGetFileSpecification(ats, &spec) == noErr
        && FSpMakeFSRef(&spec, &ref) == noErr
        && FSRefMakePath(&ref, (UInt8*)path, sizeof(path)) == noErr
        && readFile(path, &c)) {
        index = faceIndexForPath(path, psName);
        if (index >= 0 && (unsigned)index < c.faceCount) {
            struct sfnt f;
            f.base = c.bytes; f.length = c.length; f.offset = c.faceOffset[index];
            f.tableOrigin = c.tableOriginIsFace ? c.faceOffset[index] : 0;
            sfntMetrics(&f, &m);
        } else
            memset(&m, 0, sizeof(m));
        free(c.bytes);
    } else {
        index = path[0] ? faceIndexForPath(path, psName) : -1;
        /* Keep whatever path we did resolve: a face the web process cannot
         * index is still more useful than one it cannot find at all. */
        memset(&m, 0, sizeof(m));
    }

    if (ps) {
        ct = CTFontCreateWithName(ps, 16.0, NULL);
        if (ct) {
            traits = CTFontGetSymbolicTraits(ct);
            family = CTFontCopyFamilyName(ct);
            cfToUTF8(family, familyName, sizeof(familyName));
        }
    }

    if (index >= 0 && path[0]) {
        ++resolvedFaces;
        if (g_handleCount < 512) {
            strncpy(g_handles[g_handleCount].path, path, sizeof(g_handles[0].path) - 1);
            strncpy(g_handles[g_handleCount].psName, psName, sizeof(g_handles[0].psName) - 1);
            g_handles[g_handleCount].index = index;
            ++g_handleCount;
        }
    } else
        ++unresolvedFaces;

    printf("%s\n    {", first ? "" : ",");
    printf("\"postScriptName\": "); jsonString(psName);
    printf(", \"familyName\": "); jsonString(familyName);
    printf(", \"fullName\": "); jsonString(fullName);
    printf(",\n     \"path\": "); jsonString(path);
    printf(", \"faceIndex\": %d", index);
    printf(",\n     \"traits\": {\"bold\": %s, \"italic\": %s, \"monospace\": %s}",
        (traits & kCTFontTraitBold) ? "true" : "false",
        (traits & kCTFontTraitItalic) ? "true" : "false",
        (traits & kCTFontTraitMonoSpace) ? "true" : "false");
    printf(",\n     \"unitsPerEm\": %u, \"hheaAscent\": %d, \"hheaDescent\": %d, \"hheaLineGap\": %d",
        m.unitsPerEm, m.ascent, m.descent, m.lineGap);
    if (m.hasOS2Heights)
        printf(",\n     \"os2CapHeight\": %d, \"os2XHeight\": %d", m.capHeight, m.xHeight);
    if (ct)
        printf(",\n     \"ctCapHeight16\": %.4f, \"ctXHeight16\": %.4f",
            (double)CTFontGetCapHeight(ct), (double)CTFontGetXHeight(ct));
    printf("}");

    if (ct) CFRelease(ct);
    if (ps) CFRelease(ps);
    if (full) CFRelease(full);
    if (family) CFRelease(family);
}

/* ---- system UI fonts and the CSS generic map ----------------------------- */

static void emitUIFonts(void)
{
    /* The NSControlSize triple, from the table ctcompat decodes out of 10.5.8's
     * CoreText and verifies against Tiger. See CT-SURVEY.md. */
    static const struct { const char* key; int uiType; } ui[] = {
        { "regular", 2 }, { "small", 4 }, { "mini", 6 },
        { "emphasizedRegular", 3 }, { "emphasizedSmall", 5 }, { "emphasizedMini", 7 },
        { "menuItem", 12 }, { "label", 10 }, { "toolbar", 21 }, { "message", 23 }
    };
    unsigned i;

    printf("  \"systemFonts\": {");
    for (i = 0; i < sizeof(ui) / sizeof(ui[0]); ++i) {
        CTFontRef f = CTFontCreateUIFontForLanguage((CTFontUIFontType)ui[i].uiType, 0, NULL);
        CFStringRef ps = f ? CTFontCopyPostScriptName(f) : NULL;
        char name[256] = "";
        cfToUTF8(ps, name, sizeof(name));
        printf("%s\n    \"%s\": {\"postScriptName\": ", i ? "," : "", ui[i].key);
        jsonString(name);
        printf(", \"size\": %.1f}", f ? (double)CTFontGetSize(f) : 0.0);
        if (ps) CFRelease(ps);
        if (f) CFRelease(f);
    }
    printf("\n  },\n");
}

static void emitGenericFamilies(void)
{
    /* Tier 1: Tiger's own DefaultFontFallbacks.plist, read through
     * CTFontDescriptorCreateForCSSFamily. Per language, because the table is. */
    static const CFStringRef* keys[] = {
        &kCTFontCSSFamilySerif, &kCTFontCSSFamilySansSerif, &kCTFontCSSFamilyMonospace,
        &kCTFontCSSFamilyCursive, &kCTFontCSSFamilyFantasy, &kCTFontCSSFamilySystemUI
    };
    static const char* names[] = { "serif", "sans-serif", "monospace", "cursive", "fantasy", "system-ui" };
    static const char* languages[] = { "en", "ja", "zh-Hans", "zh-Hant", "ko", "ar" };
    unsigned k, l;

    printf("  \"genericFamilies\": {");
    for (l = 0; l < sizeof(languages) / sizeof(languages[0]); ++l) {
        CFStringRef lang = CFStringCreateWithCString(NULL, languages[l], kCFStringEncodingUTF8);
        printf("%s\n    \"%s\": {", l ? "," : "", languages[l]);
        for (k = 0; k < sizeof(keys) / sizeof(keys[0]); ++k) {
            CTFontDescriptorRef d = CTFontDescriptorCreateForCSSFamily(*keys[k], lang);
            CTFontRef f = d ? CTFontCreateWithFontDescriptor(d, 12.0, NULL) : NULL;
            CFStringRef ps = f ? CTFontCopyPostScriptName(f) : NULL;
            char name[256] = "";
            cfToUTF8(ps, name, sizeof(name));
            printf("%s\"%s\": ", k ? ", " : "", names[k]);
            jsonString(name);
            if (ps) CFRelease(ps);
            if (f) CFRelease(f);
            if (d) CFRelease(d);
        }
        printf("}");
        CFRelease(lang);
    }
    printf("\n  },\n");
}

int main(void)
{
    ATSFontIterator it = NULL;
    ATSFontRef f;
    int first = 1;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    printf("{\n");
    printf("  \"generator\": \"spike/fontmanifest.c\",\n");
    printf("  \"host\": \"Mac OS X 10.4.11 i386\",\n");
    emitUIFonts();
    emitGenericFamilies();
    printf("  \"faces\": [");

    /* Two passes. Collect every face first, then resolve and emit.
     *
     * The reason is not tidiness: resolving a face index activates its file,
     * and activating anything invalidates a live ATSFontIterator, which then
     * stops early. Interleaving the two silently reported 12 faces instead of
     * 176, with no error anywhere. */
    {
        static ATSFontRef all[1024];
        unsigned n = 0, i;

        if (ATSFontIteratorCreate(kATSFontContextGlobal, NULL, NULL,
                kATSOptionFlagsDefault, &it) == noErr) {
            while (n < 1024 && ATSFontIteratorNext(it, &f) == noErr)
                all[n++] = f;
            ATSFontIteratorRelease(&it);
        }
        for (i = 0; i < n; ++i) {
            emitFace(all[i], first);
            first = 0;
        }
    }
    printf("\n  ],\n");
    printf("  \"faceCount\": %ld, \"withHandle\": %ld, \"withoutHandle\": %ld\n",
        totalFaces, resolvedFaces, unresolvedFaces);
    printf("}\n");
    fprintf(stderr, "faces %ld, resolvable to (path, index) %ld, not %ld\n",
        totalFaces, resolvedFaces, unresolvedFaces);

    /* Self-check: every handle emitted must resolve back through the resolver
     * the render process will use, to the face it names. Without this the
     * manifest is a plausible-looking list rather than a set of usable handles. */
    {
        unsigned i, ok = 0, bad = 0;
        for (i = 0; i < g_handleCount; ++i) {
            CTFontRef font = NULL;
            char got[128] = "";
            if (TigerCTFontForHandle(g_handles[i].path, g_handles[i].index, 16.0, 0, &font) == noErr
                && font) {
                CFStringRef ps = CTFontCopyPostScriptName(font);
                if (ps) { CFStringGetCString(ps, got, sizeof(got), kCFStringEncodingUTF8); CFRelease(ps); }
                CFRelease(font);
            }
            if (!strcmp(got, g_handles[i].psName))
                ++ok;
            else {
                ++bad;
                if (bad <= 8)
                    fprintf(stderr, "  handle mismatch: %s#%d gave \"%s\", expected \"%s\"\n",
                        g_handles[i].path, g_handles[i].index, got, g_handles[i].psName);
            }
        }
        fprintf(stderr, "self-check: %u handles resolve to the named face, %u do not\n", ok, bad);
        return bad ? 1 : 0;
    }
}
