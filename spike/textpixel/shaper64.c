/* The 64-bit side: pick fonts from the manifest, shape with HarfBuzz, send
 * concrete (face handle, glyphs, positions) over the wire.
 *
 * This is the recording process of the split design, so **every fallback
 * decision is made here**. The rasteriser draws exactly what it is told and has
 * no way to ask CoreText what it would have chosen, which is the constraint
 * logs/n1-briefs.md calls out as where missing-glyph bugs will come from. So
 * coverage is tested here, per character, with HarfBuzz, and a run that the
 * requested family cannot draw is re-pointed at a face that can before it is
 * sent.
 *
 * A 64-bit process on Tiger can link libSystem, libstdc++ and libz and nothing
 * else: no CoreFoundation, no CoreText, no ATS. So the manifest is parsed here
 * by hand rather than with CFPropertyList, and font coverage is HarfBuzz's
 * answer rather than CoreText's.
 */

#include "common.h"
#include <hb.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#define MAXFACES 256

struct face {
    char psName[128];
    char family[128];
    char path[TP_PATH];
    int  faceIndex;
    int  bold, italic;
};

static struct face g_faces[MAXFACES];
static unsigned g_faceCount;

/* ---- a small reader for our own manifest -------------------------------- */

static const char* skipTo(const char* p, const char* end, char c)
{
    while (p < end && *p != c) ++p;
    return p;
}

/* Value of "key": "..." or "key": N, searched only inside [p,end). */
static int readString(const char* p, const char* end, const char* key, char* out, size_t cap)
{
    char pattern[64];
    const char* q;
    size_t n;

    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    n = strlen(pattern);
    for (q = p; q + n < end; ++q) {
        if (memcmp(q, pattern, n))
            continue;
        q = skipTo(q + n, end, '"');
        if (q >= end) return 0;
        ++q;
        {
            const char* s = q;
            size_t len;
            while (q < end && *q != '"') ++q;
            len = (size_t)(q - s);
            if (len >= cap) len = cap - 1;
            memcpy(out, s, len);
            out[len] = 0;
            return 1;
        }
    }
    return 0;
}

static int readInt(const char* p, const char* end, const char* key, int* out)
{
    char pattern[64];
    const char* q;
    size_t n;

    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    n = strlen(pattern);
    for (q = p; q + n < end; ++q) {
        if (memcmp(q, pattern, n))
            continue;
        *out = (int)strtol(q + n, NULL, 10);
        return 1;
    }
    return 0;
}

static int readBool(const char* p, const char* end, const char* key)
{
    char pattern[64];
    const char* q;
    size_t n;

    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    n = strlen(pattern);
    for (q = p; q + n < end; ++q) {
        if (memcmp(q, pattern, n))
            continue;
        q += n;
        while (q < end && (*q == ' ' || *q == '\t')) ++q;
        return q < end && *q == 't';
    }
    return 0;
}

static int loadManifest(const char* path)
{
    int fd = open(path, O_RDONLY);
    char* buf;
    off_t size;
    const char* p;
    const char* end;

    if (fd < 0) return 0;
    size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    buf = (char*)malloc((size_t)size + 1);
    if (read(fd, buf, (size_t)size) != size) { close(fd); free(buf); return 0; }
    close(fd);
    buf[size] = 0;
    end = buf + size;

    p = strstr(buf, "\"faces\"");
    if (!p) { free(buf); return 0; }
    while (p < end && g_faceCount < MAXFACES) {
        const char* objStart = skipTo(p, end, '{');
        const char* objEnd;
        struct face* f;

        if (objStart >= end) break;
        objEnd = skipTo(objStart, end, '}');
        if (objEnd >= end) break;
        f = &g_faces[g_faceCount];
        memset(f, 0, sizeof(*f));
        if (readString(objStart, objEnd, "postScriptName", f->psName, sizeof(f->psName))
            && readString(objStart, objEnd, "path", f->path, sizeof(f->path))
            && readInt(objStart, objEnd, "faceIndex", &f->faceIndex)
            && f->path[0] && f->faceIndex >= 0) {
            readString(objStart, objEnd, "familyName", f->family, sizeof(f->family));
            f->bold = readBool(objStart, objEnd, "bold");
            f->italic = readBool(objStart, objEnd, "italic");
            ++g_faceCount;
        }
        p = objEnd + 1;
    }
    free(buf);
    return g_faceCount != 0;
}

/* ---- HarfBuzz faces, cached ---------------------------------------------- */

static hb_face_t* g_hbFace[MAXFACES];

static hb_face_t* hbFaceFor(unsigned i)
{
    int fd;
    off_t size;
    char* bytes;
    hb_blob_t* blob;

    if (i >= g_faceCount) return NULL;
    if (g_hbFace[i]) return g_hbFace[i];
    fd = open(g_faces[i].path, O_RDONLY);
    if (fd < 0) return NULL;
    size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    bytes = (char*)malloc((size_t)size);
    if (read(fd, bytes, (size_t)size) != size) { close(fd); free(bytes); return NULL; }
    close(fd);
    blob = hb_blob_create(bytes, (unsigned)size, HB_MEMORY_MODE_WRITABLE, bytes, free);
    g_hbFace[i] = hb_face_create(blob, (unsigned)g_faces[i].faceIndex);
    hb_blob_destroy(blob);
    return g_hbFace[i];
}

/* ---- Apple-format kern, applied the way CoreText applies it -------------
 *
 * HarfBuzz and CoreText both read the same `kern` table and both arrive at the
 * same total advance, but they distribute an Apple-format pair value
 * differently: CoreText subtracts the whole thing from the leading glyph,
 * HarfBuzz splits it across the pair. Measured in logs/hb-raster.md, that moves
 * the second glyph of a kerned pair by up to 0.99 pt, which is visible.
 *
 * CoreText's distribution is the one the table means: for AV at 16 pt the table
 * holds -151 units and CoreText applies exactly -1.1797 pt to the A. So for a
 * face whose kern table is Apple-format, shape with kerning off and apply the
 * pair values here, to the leading glyph. 66 of the box's 176 faces are in this
 * group, including Helvetica and Courier; the system font is not.
 *
 * Microsoft-format kern and GPOS are left alone: HarfBuzz already agrees with
 * CoreText there to 0.008 pt. */
struct kernPair { uint16_t left, right; int16_t value; };

struct kernTable {
    struct kernPair* pairs;
    unsigned count;
    unsigned upem;
    int isApple;
};

static uint16_t rd16(const unsigned char* p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t rd32(const unsigned char* p)
{ return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static void loadAppleKern(hb_face_t* face, struct kernTable* k)
{
    hb_blob_t* blob = hb_face_reference_table(face, HB_TAG('k','e','r','n'));
    unsigned len = 0;
    const unsigned char* d = (const unsigned char*)hb_blob_get_data(blob, &len);
    unsigned nTables, t, off;

    memset(k, 0, sizeof(*k));
    k->upem = hb_face_get_upem(face);
    if (!d || len < 8 || rd16(d) == 0) { hb_blob_destroy(blob); return; }  /* absent or MS format */

    k->isApple = 1;
    nTables = rd32(d + 4);
    off = 8;
    for (t = 0; t < nTables && off + 8 <= len; ++t) {
        unsigned subLen = rd32(d + off);
        unsigned coverage = rd16(d + off + 4);
        const unsigned char* body = d + off + 8;

        /* Horizontal, not cross-stream, format 0: an ordered list of pairs. */
        if ((coverage & 0xFF) == 0 && !(coverage & 0x8000) && !(coverage & 0x4000)
            && off + subLen <= len && subLen >= 16) {
            unsigned nPairs = rd16(body);
            unsigned i;
            if (8 + nPairs * 6 <= subLen) {
                k->pairs = (struct kernPair*)malloc(nPairs * sizeof(struct kernPair));
                for (i = 0; i < nPairs; ++i) {
                    const unsigned char* e = body + 8 + i * 6;
                    k->pairs[i].left = rd16(e);
                    k->pairs[i].right = rd16(e + 2);
                    k->pairs[i].value = (int16_t)rd16(e + 4);
                }
                k->count = nPairs;
            }
            break;
        }
        if (!subLen) break;
        off += subLen;
    }
    hb_blob_destroy(blob);
}

static int kernFor(const struct kernTable* k, uint16_t left, uint16_t right)
{
    unsigned i;
    for (i = 0; i < k->count; ++i)
        if (k->pairs[i].left == left && k->pairs[i].right == right)
            return k->pairs[i].value;
    return 0;
}

/* ---- the font cache's job, in miniature ---------------------------------- */

static int matchFamily(const char* family, int bold, int italic)
{
    unsigned i;
    int loose = -1;

    for (i = 0; i < g_faceCount; ++i) {
        if (strcasecmp(g_faces[i].family, family))
            continue;
        if (g_faces[i].bold == bold && g_faces[i].italic == italic)
            return (int)i;
        if (loose < 0)
            loose = (int)i;
    }
    return loose;
}

static int faceCovers(unsigned i, const uint16_t* text, unsigned n)
{
    hb_face_t* face = hbFaceFor(i);
    hb_font_t* font;
    unsigned k;
    int ok = 1;

    if (!face) return 0;
    font = hb_font_create(face);
    for (k = 0; k < n; ++k) {
        hb_codepoint_t g = 0;
        if (!hb_font_get_nominal_glyph(font, text[k], &g) || !g) { ok = 0; break; }
    }
    hb_font_destroy(font);
    return ok;
}

/* The fallback decision, made entirely here. Ask every face in the manifest
 * until one covers the text; the rasteriser never gets a choice. */
static int fallbackFace(const uint16_t* text, unsigned n)
{
    unsigned i;
    for (i = 0; i < g_faceCount; ++i)
        if (faceCovers(i, text, n))
            return (int)i;
    return -1;
}

/* ---- the wire ------------------------------------------------------------ */

static mach_port_t g_peer, g_self;
static uint32_t g_seq;

static int sendRun(const char* label, int faceIdx, int dataId, const char* dataPath,
    float size, int rtl, const uint16_t* text, unsigned textLen, int fellBack)
{
    TPRunMsg m;
    hb_face_t* face;
    hb_font_t* font;
    hb_buffer_t* buf;
    hb_glyph_info_t* info;
    hb_glyph_position_t* pos;
    unsigned count = 0, i;
    double penX = 0, penY = 0;
    struct kernTable kern;

    memset(&m, 0, sizeof(m));
    if (dataId >= 0) {
        int fd = open(dataPath, O_RDONLY);
        off_t sz;
        char* bytes;
        hb_blob_t* blob;
        if (fd < 0) return 0;
        sz = lseek(fd, 0, SEEK_END); lseek(fd, 0, SEEK_SET);
        bytes = (char*)malloc((size_t)sz);
        if (read(fd, bytes, (size_t)sz) != sz) { close(fd); free(bytes); return 0; }
        close(fd);
        blob = hb_blob_create(bytes, (unsigned)sz, HB_MEMORY_MODE_WRITABLE, bytes, free);
        face = hb_face_create(blob, 0);
        hb_blob_destroy(blob);
        m.dataId = dataId;
        m.faceIndex = 0;
    } else {
        face = hbFaceFor((unsigned)faceIdx);
        m.dataId = -1;
        m.faceIndex = g_faces[faceIdx].faceIndex;
        strncpy(m.path, g_faces[faceIdx].path, TP_PATH - 1);
    }
    if (!face) return 0;

    font = hb_font_create(face);
    /* Scale, and why it is not the obvious size * 64.
     *
     * HarfBuzz returns positions as integers in whatever unit the scale sets.
     * The conventional size * 64 gives 26.6 fixed point, a quantum of 1/64 pt,
     * and each glyph's advance is rounded into it. Rounding once is invisible;
     * the error *accumulates along the run*. Measured on Helvetica at 16 pt it
     * is 0.0078 pt per glyph, which is 0.14 pt by the eighteenth glyph and
     * would be near half a point across a full line of body text.
     *
     * Scaling by 1024 instead makes the quantum 1/1024 pt and the accumulated
     * drift disappears below the tolerance. CoreText computes in float and does
     * not accumulate at all, so this is HarfBuzz's precision to choose, not a
     * disagreement to reconcile. */
    hb_font_set_scale(font, (int)(size * 1024), (int)(size * 1024));
    hb_font_set_ppem(font, 0, 0);
    loadAppleKern(face, &kern);
    buf = hb_buffer_create();
    for (i = 0; i < textLen; ++i)
        hb_buffer_add(buf, text[i], i);
    hb_buffer_set_content_type(buf, HB_BUFFER_CONTENT_TYPE_UNICODE);
    hb_buffer_set_direction(buf, rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
    hb_buffer_guess_segment_properties(buf);
    if (kern.isApple && kern.count) {
        hb_feature_t noKern;
        hb_feature_from_string("-kern", -1, &noKern);
        hb_shape(font, buf, &noKern, 1);
    } else
        hb_shape(font, buf, NULL, 0);

    info = hb_buffer_get_glyph_infos(buf, &count);
    pos = hb_buffer_get_glyph_positions(buf, &count);
    if (count > TP_MAX_GLYPHS) count = TP_MAX_GLYPHS;

    for (i = 0; i < count; ++i) {
        double advance = pos[i].x_advance / 1024.0;
        m.glyphs[i] = (uint16_t)info[i].codepoint;
        m.posX[i] = (float)(penX + pos[i].x_offset / 1024.0);
        m.posY[i] = (float)(penY + pos[i].y_offset / 1024.0);
        if (kern.isApple && kern.count && i + 1 < count && kern.upem) {
            /* CoreText's distribution: the whole pair value comes off the
             * leading glyph, so the next glyph starts where CoreText puts it. */
            int units = kernFor(&kern, (uint16_t)info[i].codepoint,
                (uint16_t)info[i + 1].codepoint);
            advance += units * size / (double)kern.upem;
        }
        penX += advance;
        penY += pos[i].y_advance / 1024.0;
    }
    free(kern.pairs);
    m.glyphCount = count;
    m.textLength = textLen < TP_MAX_TEXT ? textLen : TP_MAX_TEXT;
    memcpy(m.text, text, m.textLength * 2);
    m.size = size;
    m.rtl = (uint32_t)rtl;
    m.fellBack = (uint32_t)fellBack;
    strncpy(m.label, label, TP_LABEL - 1);
    m.op = TP_MSG_RUN;
    m.seq = ++g_seq;

    hb_buffer_destroy(buf);
    hb_font_destroy(font);
    if (dataId >= 0) hb_face_destroy(face);

    m.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
    m.header.msgh_size = sizeof(m);
    m.header.msgh_remote_port = g_peer;
    m.header.msgh_local_port = MACH_PORT_NULL;
    m.header.msgh_id = TP_MSG_RUN;
    return mach_msg(&m.header, MACH_SEND_MSG, sizeof(m), 0, MACH_PORT_NULL,
        MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL) == MACH_MSG_SUCCESS;
}

static int sendFontData(const char* path, int dataId)
{
    int fd = open(path, O_RDONLY);
    off_t size;
    uint32_t off = 0;
    unsigned char* bytes;

    if (fd < 0) return 0;
    size = lseek(fd, 0, SEEK_END); lseek(fd, 0, SEEK_SET);
    bytes = (unsigned char*)malloc((size_t)size);
    if (read(fd, bytes, (size_t)size) != size) { close(fd); free(bytes); return 0; }
    close(fd);

    while (off < (uint32_t)size) {
        TPDataMsg m;
        uint32_t n = (uint32_t)size - off;
        if (n > TP_CHUNK) n = TP_CHUNK;
        memset(&m, 0, sizeof(m));
        m.op = TP_MSG_FONTDATA;
        m.seq = ++g_seq;
        m.dataId = (uint32_t)dataId;
        m.totalBytes = (uint32_t)size;
        m.offset = off;
        m.length = n;
        memcpy(m.bytes, bytes + off, n);
        m.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
        m.header.msgh_size = sizeof(m);
        m.header.msgh_remote_port = g_peer;
        m.header.msgh_local_port = MACH_PORT_NULL;
        m.header.msgh_id = TP_MSG_FONTDATA;
        if (mach_msg(&m.header, MACH_SEND_MSG, sizeof(m), 0, MACH_PORT_NULL,
                MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL) != MACH_MSG_SUCCESS) {
            free(bytes);
            return 0;
        }
        off += n;
    }
    free(bytes);
    fprintf(stderr, "[64] web font %s: %u bytes in %u chunks\n",
        path, (uint32_t)size, (uint32_t)((size + TP_CHUNK - 1) / TP_CHUNK));
    return 1;
}

static unsigned utf16(const char* ascii, uint16_t* out)
{
    unsigned n = 0;
    while (*ascii && n < TP_MAX_TEXT) out[n++] = (uint16_t)(unsigned char)*ascii++;
    return n;
}

static void request(const char* label, const char* family, int bold, int italic,
    const uint16_t* text, unsigned n, float size, int rtl)
{
    int idx = matchFamily(family, bold, italic);
    int fellBack = 0;

    /* The cascade, such as it is: the requested family first, then anything in
     * the manifest that covers the text. Deciding this here is the whole point;
     * the rasteriser cannot. */
    if (idx < 0 || !faceCovers((unsigned)idx, text, n)) {
        int alt = fallbackFace(text, n);
        if (alt >= 0) { idx = alt; fellBack = 1; }
    }
    if (idx < 0) { fprintf(stderr, "[64] %s: nothing covers it\n", label); return; }
    fprintf(stderr, "[64] %-22s -> %s%s\n", label, g_faces[idx].psName,
        fellBack ? "  (fell back)" : "");
    sendRun(label, idx, -1, NULL, size, rtl, text, n, fellBack);
}

int main(int argc, char** argv)
{
    uint16_t text[TP_MAX_TEXT];
    unsigned n;

    if (argc < 4) { fprintf(stderr, "usage: shaper64 <service> <manifest> <webfont>\n"); return 2; }
    if (bootstrap_look_up(bootstrap_port, argv[1], &g_peer) != KERN_SUCCESS) {
        fprintf(stderr, "[64] bootstrap_look_up failed\n"); return 1;
    }
    mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &g_self);
    if (!loadManifest(argv[2])) { fprintf(stderr, "[64] cannot read manifest\n"); return 1; }
    fprintf(stderr, "[64] manifest: %u usable faces\n", g_faceCount);

    n = utf16("Waving AV To fluffy", text);
    request("latin kerned", "Helvetica", 0, 0, text, n, 16, 0);
    request("system font", "Lucida Grande", 0, 0, text, n, 13, 0);
    request("bold request", "Helvetica", 1, 0, text, n, 16, 0);

    /* Arabic and CJK through a Latin family: the request cannot be honoured, so
     * the fallback has to happen here or the rasteriser draws .notdef. */
    { uint16_t a[] = { 0x0627, 0x0628, 0x062C, 0x062F };
      request("arabic via Helvetica", "Helvetica", 0, 0, a, 4, 16, 1); }
    { uint16_t c[] = { 0x4E2D, 0x6587, 0x6E2C, 0x8A66 };
      request("cjk via Helvetica", "Helvetica", 0, 0, c, 4, 16, 0); }

    /* A web font: bytes over the wire once, then a run that names it.
     *
     * The same coverage discipline applies to a downloaded face as to an
     * installed one. Asking this one for Latin produced a run of .notdef and
     * the rasteriser flagged it, which is the check working: a face that cannot
     * draw the text has to be rejected *here*, because the other side will
     * faithfully draw whatever it is handed. */
    if (sendFontData(argv[3], 0)) {
        uint16_t arabic[] = { 0x0627, 0x0628, 0x062C, 0x062F };
        hb_face_t* webFace;
        hb_font_t* webFont;
        int covers = 1;
        unsigned k;
        {
            int fd = open(argv[3], O_RDONLY);
            off_t sz = lseek(fd, 0, SEEK_END);
            char* bytes = (char*)malloc((size_t)sz);
            hb_blob_t* blob;
            lseek(fd, 0, SEEK_SET);
            if (read(fd, bytes, (size_t)sz) != sz) { close(fd); free(bytes); return 1; }
            close(fd);
            blob = hb_blob_create(bytes, (unsigned)sz, HB_MEMORY_MODE_WRITABLE, bytes, free);
            webFace = hb_face_create(blob, 0);
            hb_blob_destroy(blob);
        }
        webFont = hb_font_create(webFace);
        for (k = 0; k < 4; ++k) {
            hb_codepoint_t g = 0;
            if (!hb_font_get_nominal_glyph(webFont, arabic[k], &g) || !g) covers = 0;
        }
        hb_font_destroy(webFont);
        hb_face_destroy(webFace);

        if (covers) {
            fprintf(stderr, "[64] web font covers the text, sending it\n");
            sendRun("web font from bytes", -1, 0, argv[3], 16, 1, arabic, 4, 0);
        } else {
            int alt = fallbackFace(arabic, 4);
            fprintf(stderr, "[64] web font does not cover the text, falling back\n");
            if (alt >= 0)
                sendRun("web font, fell back", alt, -1, NULL, 16, 1, arabic, 4, 1);
        }

        /* And the case that must not reach the rasteriser: Latin through an
         * Arabic-only web font. The shaper rejects it and falls back. */
        n = utf16("Web font AV", text);
        {
            int alt = matchFamily("Helvetica", 0, 0);
            fprintf(stderr, "[64] latin via an arabic web font: rejected here, using %s\n",
                alt >= 0 ? g_faces[alt].psName : "(nothing)");
            if (alt >= 0)
                sendRun("latin rejected by shaper", alt, -1, NULL, 16, 0, text, n, 1);
        }
    }

    {
        TPSimpleMsg d;
        memset(&d, 0, sizeof(d));
        d.op = TP_MSG_DONE;
        d.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
        d.header.msgh_size = sizeof(d);
        d.header.msgh_remote_port = g_peer;
        d.header.msgh_id = TP_MSG_DONE;
        mach_msg(&d.header, MACH_SEND_MSG, sizeof(d), 0, MACH_PORT_NULL,
            MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    }
    return 0;
}
