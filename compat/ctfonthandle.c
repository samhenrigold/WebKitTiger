/* TIGER: font resolution by (path, face index), for the split-process port.
 *
 * Rationale and the shape of the handle are in
 * compat/include/TigerCompat/CTFontHandle.h. This file is the activation and
 * the caching.
 *
 * The route is ATS, because it is the only one Tiger has:
 *   path -> FSRef -> FSSpec -> ATSFontActivateFromFileSpecification
 *        -> ATSFontFindFromContainer -> CGFontCreateWithPlatformFont
 *        -> CTFontCreateWithGraphicsFont
 * CGFontCreateWithDataProvider is not a shortcut here: it is exported by Tiger
 * and returns NULL for .ttf and .dfont alike, which CT-SURVEY.md records.
 *
 * Two caches, for two different costs. Activation is expensive and, worse,
 * irreversible in practice, so each file is activated at most once. Font
 * creation is cheap but happens per size, so those are kept in a small LRU.
 */

/* Tiger's real ABI first: CTFontCreateWithGraphicsFont takes a by-value
 * double here, not a CGFloat. */
#include <TigerCompat/CTCompat.h>
#include <TigerCompat/CTFontHandle.h>

#include <CoreServices/CoreServices.h>
#include <stdlib.h>
#include <string.h>

/* ---- activation cache: one entry per file, never evicted ---------------- */

struct activation {
    struct activation* next;
    char* path;                 /* NULL for a data activation */
    CFDataRef data;             /* retained, and retained forever: ATS reads it */
    ATSFontContainerRef container;
    ATSFontRef* faces;
    ItemCount faceCount;
};

static struct activation* g_activations;
static unsigned g_activationCount;

/* Every face in a container, in the order ATS reports them. That order is what
 * faceIndex means, and it is the same order the manifest generator records. */
static int collectFaces(struct activation* a)
{
    ItemCount count = 0;

    if (ATSFontFindFromContainer(a->container, kATSOptionFlagsDefault, 0, NULL, &count) != noErr
        || !count)
        return 0;
    a->faces = (ATSFontRef*)calloc((size_t)count, sizeof(ATSFontRef));
    if (!a->faces)
        return 0;
    if (ATSFontFindFromContainer(a->container, kATSOptionFlagsDefault, count, a->faces, &count) != noErr) {
        free(a->faces);
        a->faces = NULL;
        return 0;
    }
    a->faceCount = count;
    return 1;
}

static struct activation* activateFile(const char* path)
{
    struct activation* a;
    FSRef ref;
    FSSpec spec;
    ATSFontContainerRef container = 0;

    for (a = g_activations; a; a = a->next) {
        if (a->path && !strcmp(a->path, path))
            return a;
    }
    if (FSPathMakeRef((const UInt8*)path, &ref, NULL) != noErr)
        return NULL;
    /* ATS on Tiger only takes an FSSpec; FSGetCatalogInfo is the supported way
     * to get one from an FSRef, since FSSpec-by-path is long deprecated. */
    if (FSGetCatalogInfo(&ref, kFSCatInfoNone, NULL, NULL, &spec, NULL) != noErr)
        return NULL;
    if (ATSFontActivateFromFileSpecification(&spec, kATSFontContextLocal,
            kATSFontFormatUnspecified, NULL, kATSOptionFlagsDefault, &container) != noErr
        || !container)
        return NULL;

    a = (struct activation*)calloc(1, sizeof(*a));
    if (!a)
        return NULL;
    a->path = strdup(path);
    a->container = container;
    if (!collectFaces(a)) {
        free(a->path);
        free(a);
        return NULL;
    }
    a->next = g_activations;
    g_activations = a;
    ++g_activationCount;
    return a;
}

static struct activation* activateData(CFDataRef data)
{
    struct activation* a;
    ATSFontContainerRef container = 0;

    if (!data || !CFDataGetLength(data))
        return NULL;
    for (a = g_activations; a; a = a->next) {
        if (a->data && CFEqual(a->data, data))
            return a;
    }
    /* Retained for the process lifetime: ATS reads the caller's memory for as
     * long as the container lives, and there is no safe point to free it. */
    CFRetain(data);
    if (ATSFontActivateFromMemory((void*)CFDataGetBytePtr(data), (ByteCount)CFDataGetLength(data),
            kATSFontContextLocal, kATSFontFormatUnspecified, NULL, kATSOptionFlagsDefault,
            &container) != noErr || !container) {
        CFRelease(data);
        return NULL;
    }
    a = (struct activation*)calloc(1, sizeof(*a));
    if (!a) {
        CFRelease(data);
        return NULL;
    }
    a->data = data;
    a->container = container;
    if (!collectFaces(a)) {
        CFRelease(data);
        free(a);
        return NULL;
    }
    a->next = g_activations;
    g_activations = a;
    ++g_activationCount;
    return a;
}

/* ---- font cache: small LRU over (activation, face, size, flags) --------- */

#define kFontCacheSize 32

struct fontEntry {
    struct activation* activation;
    int faceIndex;
    CGFloat size;
    unsigned flags;
    CTFontRef font;             /* retained */
    unsigned stamp;
};

static struct fontEntry g_fonts[kFontCacheSize];
static unsigned g_clock;

static CTFontRef lookUp(struct activation* a, int faceIndex, CGFloat size, unsigned flags)
{
    unsigned i;
    for (i = 0; i < kFontCacheSize; ++i) {
        struct fontEntry* e = &g_fonts[i];
        if (e->font && e->activation == a && e->faceIndex == faceIndex
            && e->size == size && e->flags == flags) {
            e->stamp = ++g_clock;
            return e->font;
        }
    }
    return NULL;
}

static void insert(struct activation* a, int faceIndex, CGFloat size, unsigned flags, CTFontRef font)
{
    unsigned i, victim = 0;
    unsigned oldest = 0xFFFFFFFFu;

    for (i = 0; i < kFontCacheSize; ++i) {
        if (!g_fonts[i].font) { victim = i; break; }
        if (g_fonts[i].stamp < oldest) { oldest = g_fonts[i].stamp; victim = i; }
    }
    if (g_fonts[victim].font)
        CFRelease(g_fonts[victim].font);
    g_fonts[victim].activation = a;
    g_fonts[victim].faceIndex = faceIndex;
    g_fonts[victim].size = size;
    g_fonts[victim].flags = flags;
    g_fonts[victim].font = (CTFontRef)CFRetain(font);
    g_fonts[victim].stamp = ++g_clock;
}

/* ---- the resolver ------------------------------------------------------- */

static OSStatus fontFromActivation(struct activation* a, int faceIndex, CGFloat size,
    unsigned flags, CTFontRef* out)
{
    CGFontRef cgFont;
    CTFontRef font;
    ATSFontRef ats;
    CGAffineTransform matrix;
    const CGAffineTransform* matrixPtr = NULL;
    CTFontRef cached;

    *out = NULL;
    if (!a)
        return fnfErr;
    if (faceIndex < 0 || (ItemCount)faceIndex >= a->faceCount)
        return paramErr;

    cached = lookUp(a, faceIndex, size, flags);
    if (cached) {
        *out = (CTFontRef)CFRetain(cached);
        return noErr;
    }

    ats = a->faces[faceIndex];
    cgFont = CGFontCreateWithPlatformFont(&ats);
    if (!cgFont)
        return fnfErr;

    if (flags & kTigerCTFontSyntheticOblique) {
        /* The conventional synthetic italic: a 1-in-4 shear, which is what
         * WebCore uses on every other port. */
        matrix = CGAffineTransformMake(1, 0, 0.25f, 1, 0, 0);
        matrixPtr = &matrix;
    }
    font = CTFontCreateWithGraphicsFont(cgFont, size, matrixPtr, NULL);
    CGFontRelease(cgFont);
    if (!font)
        return fnfErr;

    insert(a, faceIndex, size, flags, font);
    *out = font;   /* the caller's reference; the cache holds its own */
    return noErr;
}

OSStatus TigerCTFontForHandle(const char* path, int faceIndex, CGFloat size,
    unsigned flags, CTFontRef* out)
{
    if (!out)
        return paramErr;
    *out = NULL;
    if (!path || !(size > 0))
        return paramErr;
    return fontFromActivation(activateFile(path), faceIndex, size, flags, out);
}

OSStatus TigerCTFontForData(CFDataRef data, int faceIndex, CGFloat size,
    unsigned flags, CTFontRef* out)
{
    if (!out)
        return paramErr;
    *out = NULL;
    if (!data || !(size > 0))
        return paramErr;
    return fontFromActivation(activateData(data), faceIndex, size, flags, out);
}

void TigerCTFontHandleFlushCache(void)
{
    unsigned i;
    for (i = 0; i < kFontCacheSize; ++i) {
        if (g_fonts[i].font) {
            CFRelease(g_fonts[i].font);
            g_fonts[i].font = NULL;
        }
    }
    /* Activations deliberately survive: ATSFontDeactivate would pull the face
     * out from under any CTFont still holding it, and we cannot know. */
}

void TigerCTFontHandleCacheStats(unsigned* activations, unsigned* fonts)
{
    unsigned i, n = 0;
    for (i = 0; i < kFontCacheSize; ++i)
        if (g_fonts[i].font) ++n;
    if (activations) *activations = g_activationCount;
    if (fonts) *fonts = n;
}
