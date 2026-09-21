/* msescantest - tests for spike/msescan.c.
 *
 * Part 1: synthetic unit tests, including the two adversarial cases - a moof
 *         whose mdat never arrives, and an mdat declared with size 0.
 * Part 2: the real append pipeline over DASH segments, cut at a large sample of
 *         byte positions (every offset near a box boundary, every offset in the
 *         first 128 bytes, plus a stride across the whole segment). Every cut
 *         must still yield exactly the same packet count, and a few also decode
 *         to the full frame count.
 *
 * Usage: msescantest [-u] <fmp4-dir> <webm-dir>
 *          -u  unit tests only (no segments needed, runs anywhere)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include "msescan.h"

static int failures;

#define CHECK(cond, ...) do { \
    if (cond) { printf("  ok   " __VA_ARGS__); putchar('\n'); } \
    else { failures++; printf("  FAIL " __VA_ARGS__); putchar('\n'); } \
} while (0)

/* ---- synthetic buffer construction ------------------------------------- */

static uint8_t buf[4096];
static size_t blen;

static void reset(void) { blen = 0; }

/* Append a top-level box with a 32-bit size. payload bytes of zeros. */
static size_t box32(const char *type, size_t payload)
{
    size_t at = blen, size = 8 + payload;
    buf[blen++] = (size >> 24) & 0xFF; buf[blen++] = (size >> 16) & 0xFF;
    buf[blen++] = (size >> 8) & 0xFF;  buf[blen++] = size & 0xFF;
    memcpy(buf + blen, type, 4); blen += 4;
    memset(buf + blen, 0, payload); blen += payload;
    return at;
}

/* Box with size == 1 and a 64-bit largesize. */
static size_t box64(const char *type, size_t payload)
{
    size_t at = blen, size = 16 + payload;
    buf[blen++] = 0; buf[blen++] = 0; buf[blen++] = 0; buf[blen++] = 1;
    memcpy(buf + blen, type, 4); blen += 4;
    for (int i = 7; i >= 0; i--) buf[blen++] = (size >> (i * 8)) & 0xFF;
    memset(buf + blen, 0, payload); blen += payload;
    return at;
}

/* Box with size == 0, meaning "to end of file". */
static void box_to_eof(const char *type, size_t payload)
{
    buf[blen++] = 0; buf[blen++] = 0; buf[blen++] = 0; buf[blen++] = 0;
    memcpy(buf + blen, type, 4); blen += 4;
    memset(buf + blen, 0, payload); blen += payload;
}

/* EBML element with a 4-byte ID and a 4-byte size. */
static void ebml(uint32_t id, size_t payload)
{
    for (int i = 3; i >= 0; i--) buf[blen++] = (id >> (i * 8)) & 0xFF;
    buf[blen++] = 0x10 | ((payload >> 24) & 0x0F);   /* 4-byte VINT marker */
    buf[blen++] = (payload >> 16) & 0xFF;
    buf[blen++] = (payload >> 8) & 0xFF;
    buf[blen++] = payload & 0xFF;
    memset(buf + blen, 0, payload); blen += payload;
}

static void ebml_unknown_size(uint32_t id)
{
    for (int i = 3; i >= 0; i--) buf[blen++] = (id >> (i * 8)) & 0xFF;
    buf[blen++] = 0xFF;                               /* 1-byte VINT, all ones */
}

#define ID_CLUSTER 0x1F43B675u
#define ID_CUES    0x1C53BB6Bu

static void unit_tests(void)
{
    printf("ISO-BMFF:\n");

    reset();
    CHECK(mse_scan_mp4(buf, 0) == 0, "empty buffer -> 0");

    reset(); box32("moof", 40);
    CHECK(mse_scan_mp4(buf, blen) == 0, "moof alone (mdat never arrived) -> 0");

    reset(); box32("moof", 40); size_t full = blen; box32("mdat", 100); full = blen;
    CHECK(mse_scan_mp4(buf, blen) == full, "moof+mdat -> %zu", full);

    reset(); box32("moof", 40); box32("mdat", 100);
    CHECK(mse_scan_mp4(buf, blen - 1) == 0, "moof+mdat short by one byte -> 0");
    CHECK(mse_scan_mp4(buf, 8) == 0, "only the moof header -> 0");
    CHECK(mse_scan_mp4(buf, 7) == 0, "less than one box header -> 0");

    reset(); box32("moof", 40); box32("mdat", 100); size_t one = blen;
    box32("moof", 40);
    CHECK(mse_scan_mp4(buf, blen) == one, "fragment then a bare moof -> %zu", one);
    box32("mdat", 60); size_t two = blen;
    CHECK(mse_scan_mp4(buf, blen) == two, "two whole fragments -> %zu", two);

    reset(); box32("styp", 16); box32("sidx", 32); box32("moof", 40); box32("mdat", 64);
    CHECK(mse_scan_mp4(buf, blen) == blen, "styp+sidx+moof+mdat -> whole buffer");

    reset(); box32("moof", 40); box64("mdat", 100);
    CHECK(mse_scan_mp4(buf, blen) == blen, "64-bit largesize mdat -> whole buffer");
    CHECK(mse_scan_mp4(buf, blen - 1) == 0, "64-bit largesize mdat short by one -> 0");
    reset(); box32("moof", 40); box64("mdat", 100);
    CHECK(mse_scan_mp4(buf, 8 + 40 + 12) == 0, "64-bit header itself truncated -> 0");

    reset(); box32("moof", 40); box32("mdat", 64); size_t before_eof = blen;
    box32("moof", 40); box_to_eof("mdat", 200);
    CHECK(mse_scan_mp4(buf, blen) == before_eof,
          "trailing size-0 mdat is never complete -> %zu", before_eof);

    reset(); box_to_eof("mdat", 200);
    CHECK(mse_scan_mp4(buf, blen) == 0, "size-0 mdat alone -> 0");

    reset();
    buf[0] = 0; buf[1] = 0; buf[2] = 0; buf[3] = 4;   /* size 4, smaller than a header */
    memcpy(buf + 4, "mdat", 4); blen = 8;
    CHECK(mse_scan_mp4(buf, blen) == 0, "size smaller than the box header -> 0");

    reset(); box32("moof", 40); box32("mdat", 100); size_t good = blen;
    buf[blen++] = 0xFF; buf[blen++] = 0xFF; buf[blen++] = 0xFF; buf[blen++] = 0xF0;
    memcpy(buf + blen, "mdat", 4); blen += 4;          /* absurd size, not here yet */
    CHECK(mse_scan_mp4(buf, blen) == good, "absurd trailing size -> %zu", good);

    printf("EBML/WebM:\n");

    reset();
    CHECK(mse_scan_webm(buf, 0) == 0, "empty buffer -> 0");

    reset(); ebml(ID_CLUSTER, 64);
    CHECK(mse_scan_webm(buf, blen) == blen, "one Cluster -> whole buffer");
    CHECK(mse_scan_webm(buf, blen - 1) == 0, "Cluster short by one byte -> 0");
    CHECK(mse_scan_webm(buf, 4) == 0, "only the element ID -> 0");

    reset(); ebml(ID_CLUSTER, 64); size_t c1 = blen; ebml(ID_CLUSTER, 32);
    CHECK(mse_scan_webm(buf, blen) == blen, "two Clusters -> whole buffer");
    CHECK(mse_scan_webm(buf, blen - 1) == c1, "second Cluster incomplete -> %zu", c1);

    reset(); ebml(ID_CLUSTER, 64); size_t only_cluster = blen; ebml(ID_CUES, 32);
    CHECK(mse_scan_webm(buf, blen) == only_cluster,
          "trailing Cues does not extend the prefix -> %zu", only_cluster);

    reset(); ebml(ID_CLUSTER, 64); size_t before_unknown = blen; ebml_unknown_size(ID_CLUSTER);
    CHECK(mse_scan_webm(buf, blen) == before_unknown,
          "unknown-size Cluster is never complete -> %zu", before_unknown);

    reset(); buf[0] = 0x00; blen = 1;
    CHECK(mse_scan_webm(buf, blen) == 0, "invalid leading byte 0x00 -> 0");
}

/* ---- the real append pipeline ------------------------------------------ */

typedef struct { const uint8_t *data; size_t size; size_t pos; } Ro;

static int ro_read(void *o, uint8_t *b, int want)
{
    Ro *r = o;
    size_t avail = r->size - r->pos;
    if (!avail) return AVERROR_EOF;
    int n = want < (int)avail ? want : (int)avail;
    memcpy(b, r->data + r->pos, n);
    r->pos += n;
    return n;
}

static int64_t ro_seek(void *o, int64_t off, int whence)
{
    Ro *r = o;
    if (whence == AVSEEK_SIZE) return r->size;
    if (whence == SEEK_CUR) off += r->pos;
    else if (whence == SEEK_END) off += r->size;
    if (off < 0 || (size_t)off > r->size) return AVERROR(EIO);
    r->pos = off;
    return off;
}

static uint8_t *slurp(const char *path, size_t *out)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *p = malloc(n ? n : 1);
    if (fread(p, 1, n, f) != (size_t)n) { fprintf(stderr, "short read %s\n", path); exit(1); }
    fclose(f);
    *out = n;
    return p;
}

/* Parse init||bytes as a finite stream. Counts packets; decodes if dec != NULL. */
static void parse_prefix(const uint8_t *joined, size_t n, const AVInputFormat *ifmt,
                         AVCodecContext *dec, long long *packets, long long *frames)
{
    Ro ro = { joined, n, 0 };
    uint8_t *ab = av_malloc(32768);
    AVIOContext *pb = avio_alloc_context(ab, 32768, 0, &ro, ro_read, NULL, ro_seek);
    pb->seekable = 0;

    AVFormatContext *fmt = avformat_alloc_context();
    fmt->pb = pb;
    fmt->flags |= AVFMT_FLAG_CUSTOM_IO | AVFMT_FLAG_NOBUFFER;
    fmt->probesize = 1 << 20;
    fmt->max_analyze_duration = AV_TIME_BASE / 2;

    if (avformat_open_input(&fmt, NULL, ifmt, NULL) < 0) {
        avformat_free_context(fmt);
        av_freep(&pb->buffer); avio_context_free(&pb);
        return;
    }

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frm = dec ? av_frame_alloc() : NULL;
    while (av_read_frame(fmt, pkt) >= 0) {
        (*packets)++;
        if (dec && avcodec_send_packet(dec, pkt) >= 0)
            while (avcodec_receive_frame(dec, frm) >= 0) { (*frames)++; av_frame_unref(frm); }
        av_packet_unref(pkt);
    }
    if (frm) av_frame_free(&frm);
    av_packet_free(&pkt);

    pb = fmt->pb;
    avformat_close_input(&fmt);
    av_freep(&pb->buffer);
    avio_context_free(&pb);
}

/* Feed every segment in two appends split at absolute offset `cut`, scanning for
 * complete fragments exactly as SourceBufferPrivateFFmpeg would. */
static void run_pipeline(const char *dir, const char *initname, const char **segs, int nsegs,
                         const AVInputFormat *ifmt, int is_mp4, size_t cut,
                         AVCodecContext *dec, long long *packets, long long *frames,
                         long long *retries)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, initname);
    size_t initn; uint8_t *initp = slurp(path, &initn);

    uint8_t *pend = NULL; size_t plen = 0, pcap = 0;
    uint8_t *joined = NULL; size_t jcap = 0;

    for (int s = 0; s < nsegs; s++) {
        snprintf(path, sizeof path, "%s/%s", dir, segs[s]);
        size_t n; uint8_t *p = slurp(path, &n);
        size_t k = cut < n ? cut : n;
        size_t pieces[2][2] = { { 0, k }, { k, n } };

        for (int i = 0; i < 2; i++) {
            size_t off = pieces[i][0], end = pieces[i][1];
            if (end <= off) continue;
            if (plen + (end - off) > pcap) {
                pcap = (plen + (end - off)) * 2 + 1;
                pend = realloc(pend, pcap);
            }
            memcpy(pend + plen, p + off, end - off);
            plen += end - off;

            size_t take = is_mp4 ? mse_scan_mp4(pend, plen) : mse_scan_webm(pend, plen);
            if (!take) { (*retries)++; continue; }

            if (initn + take > jcap) { jcap = (initn + take) * 2 + 1; joined = realloc(joined, jcap); }
            memcpy(joined, initp, initn);
            memcpy(joined + initn, pend, take);
            parse_prefix(joined, initn + take, ifmt, dec, packets, frames);

            memmove(pend, pend + take, plen - take);
            plen -= take;
        }
        free(p);
    }
    if (dec) {
        AVFrame *frm = av_frame_alloc();
        avcodec_send_packet(dec, NULL);
        while (avcodec_receive_frame(dec, frm) >= 0) { (*frames)++; av_frame_unref(frm); }
        av_frame_free(&frm);
        avcodec_flush_buffers(dec);
    }
    free(pend); free(joined); free(initp);
}

/* Top-level box/element boundaries of one segment, for cut positions that land
 * exactly on and around structural edges. */
static int boundaries(const uint8_t *p, size_t n, int is_mp4, size_t *out, int max)
{
    int c = 0; size_t off = 0;
    while (off + 8 <= n && c < max) {
        uint64_t size;
        if (is_mp4) {
            size = ((uint64_t)p[off] << 24) | (p[off+1] << 16) | (p[off+2] << 8) | p[off+3];
            if (size == 1 || size == 0 || size < 8) break;
        } else {
            uint64_t id, sz; int idl = 0, szl = 0;
            uint8_t b = p[off];
            for (int i = 0; i < 8; i++) if (b & (0x80 >> i)) { idl = i + 1; break; }
            if (!idl || off + idl >= n) break;
            id = 0; for (int i = 0; i < idl; i++) id = (id << 8) | p[off + i];
            b = p[off + idl];
            for (int i = 0; i < 8; i++) if (b & (0x80 >> i)) { szl = i + 1; break; }
            if (!szl || off + idl + szl > n) break;
            sz = (uint64_t)(b & (0xFF >> szl));
            for (int i = 1; i < szl; i++) sz = (sz << 8) | p[off + idl + i];
            size = idl + szl + sz;
            (void)id;
        }
        off += (size_t)size;
        if (off > n) break;
        out[c++] = off;
    }
    return c;
}

static int cmp_size(const void *a, const void *b)
{
    size_t x = *(const size_t *)a, y = *(const size_t *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

/* Build the decoder the way SourceBufferPrivateFFmpeg would: from the init
 * segment plus the first complete fragment. The init segment alone is not
 * enough - matroska will not open without a Cluster, and mov opens but reports
 * no usable continuation. */
static AVCodecContext *make_decoder(const char *dir, const char *initname, const char *seg0name,
                                    const AVInputFormat *ifmt, int is_mp4)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, initname);
    size_t initn; uint8_t *initp = slurp(path, &initn);
    snprintf(path, sizeof path, "%s/%s", dir, seg0name);
    size_t segn; uint8_t *segp = slurp(path, &segn);

    size_t take = is_mp4 ? mse_scan_mp4(segp, segn) : mse_scan_webm(segp, segn);
    if (!take) { free(initp); free(segp); return NULL; }

    uint8_t *joined = malloc(initn + take);
    memcpy(joined, initp, initn);
    memcpy(joined + initn, segp, take);

    Ro ro = { joined, initn + take, 0 };
    uint8_t *ab = av_malloc(32768);
    AVIOContext *pb = avio_alloc_context(ab, 32768, 0, &ro, ro_read, NULL, ro_seek);
    pb->seekable = 0;
    AVFormatContext *fmt = avformat_alloc_context();
    fmt->pb = pb;
    fmt->flags |= AVFMT_FLAG_CUSTOM_IO | AVFMT_FLAG_NOBUFFER;

    AVCodecContext *dec = NULL;
    if (avformat_open_input(&fmt, NULL, ifmt, NULL) >= 0) {
        if (fmt->nb_streams) {
            AVCodecParameters *par = fmt->streams[0]->codecpar;
            const AVCodec *codec = avcodec_find_decoder(par->codec_id);
            if (codec) {
                dec = avcodec_alloc_context3(codec);
                avcodec_parameters_to_context(dec, par);
                dec->thread_count = 2;
                dec->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
                if (avcodec_open2(dec, codec, NULL) < 0) avcodec_free_context(&dec);
            }
        }
        pb = fmt->pb;
        avformat_close_input(&fmt);
    } else {
        avformat_free_context(fmt);
    }
    av_freep(&pb->buffer);
    avio_context_free(&pb);
    free(joined); free(initp); free(segp);
    return dec;
}

static void pipeline_tests(const char *dir, const char *initname, const char **segs, int nsegs,
                           const char *fmtname, int is_mp4, const char *label)
{
    const AVInputFormat *ifmt = av_find_input_format(fmtname);
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, segs[0]);
    size_t seg0n; uint8_t *seg0 = slurp(path, &seg0n);

    /* Reference: whole-segment appends. */
    long long ref_packets = 0, ref_frames = 0, retries = 0;
    run_pipeline(dir, initname, segs, nsegs, ifmt, is_mp4, seg0n + 1, NULL,
                 &ref_packets, &ref_frames, &retries);
    printf("%s: reference (whole-segment appends) = %lld packets\n", label, ref_packets);
    if (!ref_packets) { failures++; printf("  FAIL no packets at all\n"); free(seg0); return; }

    /* Cut positions: every offset in the first 128 bytes, +-3 around every
     * top-level boundary of segment 1, and a stride across the segment. */
    size_t bnd[256];
    int nb = boundaries(seg0, seg0n, is_mp4, bnd, 256);
    size_t cuts[1024]; int nc = 0;
    for (size_t k = 0; k <= 128 && nc < 1000; k++) cuts[nc++] = k;
    for (int i = 0; i < nb && nc < 1000; i++)
        for (int d = -3; d <= 3 && nc < 1000; d++) {
            long long v = (long long)bnd[i] + d;
            if (v > 0 && (size_t)v <= seg0n) cuts[nc++] = (size_t)v;
        }
    for (int i = 1; i < 40 && nc < 1000; i++) cuts[nc++] = seg0n * i / 40;
    qsort(cuts, nc, sizeof cuts[0], cmp_size);

    printf("%s: %d top-level boundaries in segment 1, testing %d cut positions\n", label, nb, nc);

    int bad = 0; size_t worst = 0;
    long long total_retries = 0;
    for (int i = 0; i < nc; i++) {
        if (i && cuts[i] == cuts[i - 1]) continue;
        long long pk = 0, fr = 0, rt = 0;
        run_pipeline(dir, initname, segs, nsegs, ifmt, is_mp4, cuts[i], NULL, &pk, &fr, &rt);
        total_retries += rt;
        if (pk != ref_packets) { if (!bad) worst = cuts[i]; bad++; }
    }
    CHECK(bad == 0, "%s: all %d cut positions yield %lld packets%s",
          label, nc, ref_packets, bad ? " (first bad cut noted below)" : "");
    if (bad) printf("       %d cut positions differed, first at offset %zu\n", bad, worst);
    printf("%s: %lld appends had no complete fragment yet and were buffered\n", label, total_retries);

    /* Decode for a few cuts, including ones landing inside the last moof and
     * inside its mdat, to prove the frames really come out. */
    size_t deep[3];
    int ndeep = 0;
    deep[ndeep++] = seg0n + 1;                       /* whole-segment appends */
    if (nb >= 2) deep[ndeep++] = bnd[nb - 2] + (bnd[nb - 1] - bnd[nb - 2]) / 2;  /* inside the mdat */
    if (nb >= 3) deep[ndeep++] = bnd[nb - 3] + (bnd[nb - 2] - bnd[nb - 3]) / 2;  /* inside the moof */
    else if (nb >= 1) deep[ndeep++] = bnd[0] / 2;

    for (int i = 0; i < ndeep; i++) {
        AVCodecContext *dec = make_decoder(dir, initname, segs[0], ifmt, is_mp4);
        if (!dec) { failures++; printf("  FAIL %s: no decoder from init + first fragment\n", label); break; }
        long long pk = 0, fr = 0, rt = 0;
        run_pipeline(dir, initname, segs, nsegs, ifmt, is_mp4, deep[i], dec, &pk, &fr, &rt);
        CHECK(pk == ref_packets && fr == ref_packets,
              "%s: cut at %zu decodes %lld/%lld frames", label, deep[i], fr, ref_packets);
        avcodec_free_context(&dec);
    }
    free(seg0);
}

/* Collect chunk-streamN-*.EXT from a directory listing we already know the shape of. */
static int collect(const char *dir, const char *prefix, const char *ext, const char **out, int max)
{
    int c = 0;
    for (int i = 1; i <= max; i++) {
        static char names[64][64];
        snprintf(names[c], sizeof names[0], "%s-%05d.%s", prefix, i, ext);
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, names[c]);
        FILE *f = fopen(path, "rb");
        if (!f) break;
        fclose(f);
        out[c] = names[c];
        c++;
    }
    return c;
}

int main(int argc, char **argv)
{
    int units_only = 0, i;
    const char *fmp4dir = NULL, *webmdir = NULL;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-u")) units_only = 1;
        else if (!fmp4dir) fmp4dir = argv[i];
        else webmdir = argv[i];
    }

    printf("=== unit tests ===\n");
    unit_tests();

    if (!units_only && fmp4dir) {
        av_log_set_level(AV_LOG_FATAL);
        const char *segs[64];
        int n;

        printf("\n=== pipeline: fragmented MP4 ===\n");
        n = collect(fmp4dir, "chunk-stream0", "m4s", segs, 60);
        if (n) pipeline_tests(fmp4dir, "init-stream0.m4s", segs, n, "mp4", 1, "fMP4 H.264");
        else { failures++; printf("  FAIL no segments in %s\n", fmp4dir); }

        if (webmdir) {
            printf("\n=== pipeline: WebM ===\n");
            n = collect(webmdir, "chunk-stream0", "webm", segs, 60);
            if (n) pipeline_tests(webmdir, "init-stream0.webm", segs, n, "matroska", 0, "WebM VP9");
            else { failures++; printf("  FAIL no segments in %s\n", webmdir); }
        }
    }

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
