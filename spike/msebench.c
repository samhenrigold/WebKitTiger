/* msebench - can libavformat demux incrementally appended segments the way
 * MediaSource feeds a SourceBuffer?
 *
 * Two models, selected with -m:
 *
 *   -m stream   One AVFormatContext for the SourceBuffer's whole lifetime, over a
 *               growing append-only buffer behind a custom AVIOContext. The read
 *               callback returns AVERROR(EAGAIN) - not EOF - when the demuxer asks
 *               for bytes that have not been appended yet.
 *
 *   -m reopen   A fresh AVFormatContext per appended media segment, over
 *               (init segment || that segment) as a finite stream. One decoder is
 *               kept alive across all segments, so no keyframes are lost.
 *
 * Findings are in logs/mse-demux.md. The short version: "stream" does not work
 * with the mov demuxer. mov_read_default only arms mov->next_root_atom once it has
 * seen both moov and mdat (mov.c:9549), and mov_switch_root zeroes it and resets
 * found_mdat *before* parsing the next root atom (mov.c:10889). Pump the demuxer
 * once at a fragment boundary before the next moof has been appended - which
 * happens every time, since that is exactly when appends arrive - and the
 * continuation pointer is gone for good: mov_read_packet then returns a permanent
 * AVERROR_EOF at mov.c:11119.
 *
 * Three phases in both modes, matching what a player does:
 *   PLAY    append segments in order, decode as frames become available
 *   SEEK    skip ahead and append a later run of segments (timestamp discontinuity)
 *   REMOVE  evict the front of the buffer (SourceBuffer.remove()) and keep going
 *
 * Usage: msebench [-m stream|reopen] [-f fmt] [-i] [-d N] [-q] <init> <segment>...
 *          -m  append model (default reopen)
 *          -f  force a demuxer by name ("mp4", "matroska"); default is probing
 *          -i  call avformat_find_stream_info() after opening
 *          -d  stream mode: media segments to append before avformat_open_input
 *              (default 1; -d 0 opens on the init segment alone, which mov cannot
 *              resume from at all)
 *          -b  scan for complete top-level boxes/elements before parsing
 *              (what a real SourceBufferPrivate must do; without it a
 *              truncated append parses into garbage packets)
 *          -p N  split every media segment into N appends, to model MSE's
 *                arbitrary-byte-range appends. In reopen mode the bytes are
 *                accumulated and re-parsed until a parse yields packets.
 *          -q  quiet: no per-append lines
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/time.h>

#define AVIO_BUF 32768

typedef struct {
    uint8_t *data;
    size_t   size;          /* bytes held in the window */
    size_t   cap;
    int64_t  window_start;  /* absolute stream offset of data[0]; > 0 after a remove() */
    int64_t  pos;           /* absolute read position */
    int      more_expected; /* 1 => starvation is EAGAIN, not EOF */
    long long starved;      /* EAGAIN returns */
    long long evicted_read; /* reads that fell before the window (must stay 0) */
} AppendBuf;

static double now_s(void) { return av_gettime_relative() / 1e6; }

static int ab_read(void *opaque, uint8_t *buf, int want)
{
    AppendBuf *ab = opaque;
    if (ab->pos < ab->window_start) { ab->evicted_read++; return AVERROR(EIO); }
    int64_t avail = (int64_t)(ab->window_start + ab->size) - ab->pos;
    if (avail <= 0) {
        if (ab->more_expected) { ab->starved++; return AVERROR(EAGAIN); }
        return AVERROR_EOF;
    }
    int n = want < avail ? want : (int)avail;
    memcpy(buf, ab->data + (ab->pos - ab->window_start), n);
    ab->pos += n;
    return n;
}

static int64_t ab_seek(void *opaque, int64_t offset, int whence)
{
    AppendBuf *ab = opaque;
    int64_t end = ab->window_start + ab->size;
    if (whence == AVSEEK_SIZE) return ab->more_expected ? -1 : end;
    if (whence == SEEK_CUR) offset += ab->pos;
    else if (whence == SEEK_END) { if (ab->more_expected) return -1; offset += end; }
    if (offset < ab->window_start) return AVERROR(EIO);
    ab->pos = offset;
    return offset;
}

static void ab_append(AppendBuf *ab, const uint8_t *p, size_t n)
{
    if (ab->size + n > ab->cap) {
        ab->cap = (ab->size + n) * 2 + 1;
        ab->data = realloc(ab->data, ab->cap);
    }
    memcpy(ab->data + ab->size, p, n);
    ab->size += n;
}

/* SourceBuffer.remove(): drop everything before `keep_from` (absolute offset). */
static void ab_evict(AppendBuf *ab, int64_t keep_from)
{
    if (keep_from <= ab->window_start) return;
    size_t drop = (size_t)(keep_from - ab->window_start);
    if (drop > ab->size) drop = ab->size;
    memmove(ab->data, ab->data + drop, ab->size - drop);
    ab->size -= drop;
    ab->window_start += drop;
}

/* ---- completeness scanners ----------------------------------------------
 * MSE appends arbitrary byte ranges, not whole segments, and handing
 * libavformat a truncated fragment is NOT safe: the mov demuxer parses the
 * (complete) moof, builds the sample table from trun, and then happily reads
 * samples out of a short mdat, emitting truncated packets. So the caller must
 * know where the last complete top-level box/element ends and parse only that
 * prefix. These two functions are what SourceBufferPrivateFFmpeg owes; together
 * they are the entire "box parser" this design needs.
 */

/* ISO-BMFF: 32-bit size + 4cc type, size 1 => 64-bit largesize follows.
 * The unit handed to the demuxer is a whole fragment, not a box: a moof without
 * its mdat gives mov a sample table pointing at bytes that are not there, and it
 * emits nothing while the moof is consumed. So the committed prefix ends at the
 * end of the last complete mdat. */
static size_t complete_prefix_mp4(const uint8_t *p, size_t n)
{
    size_t off = 0, frag_end = 0;
    while (off + 8 <= n) {
        uint64_t sz = ((uint64_t)p[off] << 24) | (p[off+1] << 16) | (p[off+2] << 8) | p[off+3];
        size_t hdr = 8;
        if (sz == 1) {
            if (off + 16 > n) break;
            sz = 0;
            for (int i = 0; i < 8; i++) sz = (sz << 8) | p[off + 8 + i];
            hdr = 16;
        } else if (sz == 0) {
            break;                       /* "to end of file": never in a fragment */
        }
        if (sz < hdr || off + sz > n) break;
        off += sz;
        if (!memcmp(p + off - sz + 4, "mdat", 4)) frag_end = off;
    }
    return frag_end;
}

/* EBML: variable-length ID then variable-length size, both leading-1 coded. */
static int ebml_vint(const uint8_t *p, size_t n, size_t off, int keep_marker, uint64_t *val, int *len)
{
    if (off >= n) return 0;
    uint8_t b = p[off];
    int l = 0;
    for (int i = 0; i < 8; i++) if (b & (0x80 >> i)) { l = i + 1; break; }
    if (!l || off + l > n) return 0;
    uint64_t v = keep_marker ? b : (uint8_t)(b & (0xFF >> l));
    for (int i = 1; i < l; i++) v = (v << 8) | p[off + i];
    *val = v; *len = l;
    return 1;
}

static size_t complete_prefix_webm(const uint8_t *p, size_t n)
{
    size_t off = 0;
    for (;;) {
        uint64_t id, sz; int idl, szl;
        if (!ebml_vint(p, n, off, 1, &id, &idl)) break;
        if (!ebml_vint(p, n, off + idl, 0, &sz, &szl)) break;
        uint64_t unknown = (1ULL << (7 * szl)) - 1;
        if (sz == unknown) break;        /* live cluster of unknown length */
        if (off + idl + szl + sz > n) break;
        off += idl + szl + sz;
    }
    return off;
}

static uint8_t *slurp(const char *path, size_t *out)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *p = malloc(n);
    if (fread(p, 1, n, f) != (size_t)n) { fprintf(stderr, "short read %s\n", path); exit(1); }
    fclose(f);
    *out = n;
    return p;
}

/* Per-run tallies. */
static long long g_frames, g_packets, g_gaps;
static double g_demux_s, g_decode_s, g_open_s;
static int g_opens;
static int g_seg_first = 1;
static double g_prev_dts = -1e9;
static double g_first_seg_pts = -1;
static double g_first_pts = -1, g_last_pts = -1, g_max_gap;
static long long g_reorder;

static void note_packet(AVPacket *pkt, AVRational tb)
{
    g_packets++;
    if (pkt->pts != AV_NOPTS_VALUE) {
        double pts = pkt->pts * av_q2d(tb);
        if (g_first_pts < 0) g_first_pts = pts;
        if (g_seg_first) { g_first_seg_pts = pts; g_seg_first = 0; }
        g_last_pts = pts;
    }
    /* Continuity is a DTS property: av_read_frame delivers packets in decode
     * order, so with B-frames the PTS legitimately moves backwards. */
    if (pkt->dts == AV_NOPTS_VALUE) return;
    double dts = pkt->dts * av_q2d(tb);
    if (g_prev_dts > -1e8) {
        double d = dts - g_prev_dts;
        if (d < 0) g_reorder++;
        if (d < 0 || d > 0.5) { g_gaps++; if (d > g_max_gap) g_max_gap = d; }
    }
    g_prev_dts = dts;
}

static void decode(AVCodecContext *dec, AVPacket *pkt, AVFrame *frm)
{
    double t = now_s();
    if (avcodec_send_packet(dec, pkt) >= 0)
        while (avcodec_receive_frame(dec, frm) >= 0) { g_frames++; av_frame_unref(frm); }
    g_decode_s += now_s() - t;
}

/* Drain whatever the demuxer can produce from the bytes appended so far. */
static int pump(AVFormatContext *fmt, AVCodecContext *dec, int sidx, AVPacket *pkt, AVFrame *frm)
{
    for (;;) {
        double t = now_s();
        int ret = av_read_frame(fmt, pkt);
        g_demux_s += now_s() - t;

        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            /* The avio layer latches the condition; clear it or every later read
             * short-circuits without calling the read callback again. */
            fmt->pb->error = 0;
            fmt->pb->eof_reached = 0;
            return ret;
        }
        if (ret < 0) return ret;
        if (pkt->stream_index != sidx) { av_packet_unref(pkt); continue; }
        note_packet(pkt, fmt->streams[sidx]->time_base);
        decode(dec, pkt, frm);
        av_packet_unref(pkt);
    }
}

static AVFormatContext *open_over(AppendBuf *ab, const AVInputFormat *ifmt, int find_info, int *err)
{
    uint8_t *avio_buf = av_malloc(AVIO_BUF);
    AVIOContext *avio = avio_alloc_context(avio_buf, AVIO_BUF, 0, ab, ab_read, NULL, ab_seek);
    avio->seekable = 0;   /* a growing MSE buffer is not a seekable file */

    AVFormatContext *fmt = avformat_alloc_context();
    fmt->pb = avio;
    fmt->flags |= AVFMT_FLAG_CUSTOM_IO | AVFMT_FLAG_NOBUFFER;
    fmt->probesize = 1 << 20;
    fmt->max_analyze_duration = AV_TIME_BASE / 2;

    double t = now_s();
    *err = avformat_open_input(&fmt, NULL, ifmt, NULL);
    if (*err >= 0 && find_info) {
        avformat_find_stream_info(fmt, NULL);
        fmt->pb->error = 0; fmt->pb->eof_reached = 0;
    }
    g_open_s += now_s() - t;
    g_opens++;
    return fmt;
}

static void close_over(AVFormatContext *fmt)
{
    AVIOContext *pb = fmt->pb;
    avformat_close_input(&fmt);
    av_freep(&pb->buffer);
    avio_context_free(&pb);
}

static int pick_stream(AVFormatContext *fmt)
{
    for (unsigned s = 0; s < fmt->nb_streams; s++) {
        enum AVMediaType t = fmt->streams[s]->codecpar->codec_type;
        if (t == AVMEDIA_TYPE_VIDEO || t == AVMEDIA_TYPE_AUDIO) return s;
    }
    return -1;
}

int main(int argc, char **argv)
{
    const char *fmtname = NULL, *mode = "reopen";
    int find_info = 0, quiet = 0, ndefer = 1, nsplit = 1, scan = 0, i;
    const char **files = calloc(argc, sizeof(char *));
    int nfiles = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-f") && i + 1 < argc) fmtname = argv[++i];
        else if (!strcmp(argv[i], "-m") && i + 1 < argc) mode = argv[++i];
        else if (!strcmp(argv[i], "-d") && i + 1 < argc) ndefer = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-i")) find_info = 1;
        else if (!strcmp(argv[i], "-p") && i + 1 < argc) nsplit = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-b")) scan = 1;
        else if (!strcmp(argv[i], "-q")) quiet = 1;
        else files[nfiles++] = argv[i];
    }
    if (nfiles < 2) { fprintf(stderr, "usage: msebench [-m stream|reopen] [-f fmt] [-i] [-d N] [-q] init seg...\n"); return 2; }

    int reopen = !strcmp(mode, "reopen");
    if (nsplit < 1) nsplit = 1;
    long long partial_retries = 0, short_parses = 0;
    AppendBuf pend = { 0 };          /* reopen mode: bytes not yet fully parsed */
    int nsegs = nfiles - 1;
    /* PLAY over the first third, SEEK into the last third, REMOVE partway through. */
    int play_end = nsegs / 3 > 0 ? nsegs / 3 : 1;
    int seek_to  = nsegs - play_end;

    const AVInputFormat *ifmt = fmtname ? av_find_input_format(fmtname) : NULL;
    if (fmtname && !ifmt) { fprintf(stderr, "no demuxer named %s\n", fmtname); return 1; }

    size_t initn; uint8_t *initp = slurp(files[0], &initn);
    printf("MODE=%s\nINIT_SEGMENT=%s BYTES=%zu\n", mode, files[0], initn);

    AppendBuf ab = { 0 };            /* stream mode: lives for the whole run */
    AVFormatContext *fmt = NULL;
    AVCodecContext *dec = NULL;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frm = av_frame_alloc();
    int sidx = -1, ret = 0;

    double append_s = 0;
    long long appended_bytes = 0;
    int nappends = 0, first_seg = 0;
    double phase_pts[3][2] = { { -1, -1 }, { -1, -1 }, { -1, -1 } };
    long long phase_frames[3] = { 0, 0, 0 };
    int phase = 0;

    /* ---- open the decoder from the init segment (plus a prebuffered segment in
     * stream mode, which mov needs in order to arm next_root_atom at all) ---- */
    {
        AppendBuf probe = { 0 };
        ab_append(&probe, initp, initn);
        if (!reopen) ndefer = ndefer < 0 ? 0 : (ndefer > nsegs ? nsegs : ndefer);
        else ndefer = 1;
        long long pre = 0;
        for (i = 0; i < ndefer; i++) {
            size_t n; uint8_t *p = slurp(files[i + 1], &n);
            ab_append(&probe, p, n); free(p); pre += n;
        }
        probe.more_expected = !reopen;
        printf("OPEN_ON=init+%d segment(s) BYTES=%lld\n", ndefer, (long long)initn + pre);

        AVFormatContext *f0 = open_over(&probe, ifmt, find_info, &ret);
        printf("OPEN_INPUT=%s\n", ret >= 0 ? "ok" : av_err2str(ret));
        if (ret < 0) return 1;
        printf("DEMUXER=%s NB_STREAMS=%u\n", f0->iformat->name, f0->nb_streams);
        sidx = pick_stream(f0);
        if (sidx < 0) { printf("VERDICT=no-av-stream\n"); return 1; }

        AVCodecParameters *par = f0->streams[sidx]->codecpar;
        const AVCodec *codec = avcodec_find_decoder(par->codec_id);
        printf("STREAM=%d CODEC=%s EXTRADATA=%d", sidx, codec ? codec->name : "?", par->extradata_size);
        if (par->codec_type == AVMEDIA_TYPE_VIDEO) printf(" SIZE=%dx%d\n", par->width, par->height);
        else printf(" RATE=%d CH=%d\n", par->sample_rate, par->ch_layout.nb_channels);
        if (!codec) { printf("VERDICT=no-decoder\n"); return 1; }

        dec = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(dec, par);
        dec->thread_count = 2;
        dec->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
        if (avcodec_open2(dec, codec, NULL) < 0) { fprintf(stderr, "avcodec_open2 failed\n"); return 1; }

        if (reopen) {
            close_over(f0);
            free(probe.data);
            first_seg = 0;                  /* reopen mode re-reads segment 1 below */
            g_opens = 0; g_open_s = 0;      /* that probe open is not part of the steady state */
        } else {
            fmt = f0;
            ab = probe;                      /* stream mode keeps this buffer */
            nappends = ndefer;
            appended_bytes = (long long)ab.size - initn;
            first_seg = ndefer;
            long long f1 = g_frames;
            pump(fmt, dec, sidx, pkt, frm);
            phase_pts[0][0] = g_first_pts; phase_pts[0][1] = g_last_pts;
            phase_frames[0] += g_frames - f1;
            if (!quiet)
                printf("prebuffered %-25d %7lld B -> %4lld frames, last pts %7.3f\n",
                       ndefer, appended_bytes, g_frames - f1, g_last_pts);
        }
    }

    /* ---- the append loop ---- */
    for (int seg = first_seg; seg < nsegs; ) {
        if (phase == 0 && seg >= play_end) {
            phase = 1;
            printf("-- SEEK: skipping segments %d..%d, next append is segment %d\n",
                   play_end + 1, seek_to, seek_to + 1);
            seg = seek_to;
            continue;
        }
        if (phase == 1 && seg >= seek_to + (nsegs - seek_to) / 2) {
            phase = 2;
            if (reopen) {
                printf("-- REMOVE: nothing to evict; each append owns its bytes\n");
            } else {
                size_t before = ab.size;
                ab_evict(&ab, ab.pos);
                printf("-- REMOVE: evicted %zu of %zu buffered bytes, window now starts at %lld\n",
                       before - ab.size, before, (long long)ab.window_start);
            }
        }

        size_t n; uint8_t *p = slurp(files[seg + 1], &n);
        long long f0 = g_frames;
        if (phase_pts[phase][0] < 0) phase_pts[phase][0] = -2;   /* fill from the first packet below */
        double pts_before = g_last_pts;
        g_seg_first = 1;
        double t = now_s();

        if (reopen) {
            /* One parse context per append: init segment || the media bytes
             * buffered so far, as a finite stream. No starvation, no latched
             * demuxer state. A partial append parses to nothing, so the bytes
             * stay buffered and the next append retries. */
            for (int c = 0; c < nsplit; c++) {
                size_t off = n * c / nsplit, end = n * (c + 1) / nsplit;
                double ta = now_s();
                ab_append(&pend, p + off, end - off);
                append_s += now_s() - ta;

                AppendBuf one = { 0 };
                ab_append(&one, initp, initn);
                ab_append(&one, pend.data, pend.size);
                one.more_expected = 0;

                size_t parse_len = pend.size;
                if (scan) {
                    parse_len = ifmt && !strncmp(ifmt->name, "matroska", 8)
                              ? complete_prefix_webm(pend.data, pend.size)
                              : complete_prefix_mp4(pend.data, pend.size);
                    if (!parse_len) { partial_retries++; continue; }
                    one.size = 0;
                    ab_append(&one, initp, initn);
                    ab_append(&one, pend.data, parse_len);
                }

                long long pk0 = g_packets;
                AVFormatContext *f = open_over(&one, ifmt, find_info, &ret);
                if (ret >= 0) {
                    int si = pick_stream(f);
                    if (si >= 0) ret = pump(f, dec, si, pkt, frm);
                    close_over(f);
                } else {
                    avformat_free_context(f);
                }
                free(one.data);

                if (scan) {
                    memmove(pend.data, pend.data + parse_len, pend.size - parse_len);
                    pend.size -= parse_len;
                } else if (g_packets > pk0) {
                    if (c != nsplit - 1) short_parses++;   /* truncated mdat still emitted packets */
                    pend.size = 0;
                } else {
                    partial_retries++;          /* not parseable yet, keep the bytes */
                }
            }
            ret = AVERROR_EOF;
        } else {
            ab_append(&ab, p, n);
            append_s += now_s() - t;
            ret = pump(fmt, dec, sidx, pkt, frm);
        }
        free(p);
        appended_bytes += n;
        nappends++;

        if (phase_pts[phase][0] == -2) phase_pts[phase][0] = (g_last_pts != pts_before || g_frames != f0) ? g_first_seg_pts : g_last_pts;
        phase_pts[phase][1] = g_last_pts;
        phase_frames[phase] += g_frames - f0;

        if (!quiet) {
            const char *base = strrchr(files[seg + 1], '/');
            printf("append %2d %-28s %7zu B -> %4lld frames, last pts %7.3f, %s\n",
                   nappends, base ? base + 1 : files[seg + 1], n, g_frames - f0, g_last_pts,
                   ret == AVERROR(EAGAIN) ? "EAGAIN" : av_err2str(ret));
        }
        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) { printf("DEMUX_ERROR=%s\n", av_err2str(ret)); break; }
        seg++;
    }

    if (!reopen) {
        ab.more_expected = 0;              /* end of stream: starvation now means EOF */
        pump(fmt, dec, sidx, pkt, frm);
    }
    avcodec_send_packet(dec, NULL);
    while (avcodec_receive_frame(dec, frm) >= 0) { g_frames++; av_frame_unref(frm); }

    printf("\nAPPENDS=%d BYTES=%lld\n", nappends, appended_bytes);
    printf("PACKETS=%lld FRAMES=%lld\n", g_packets, g_frames);
    printf("PHASE_PLAY_PTS=%.3f..%.3f FRAMES=%lld\n", phase_pts[0][0], phase_pts[0][1], phase_frames[0]);
    printf("PHASE_SEEK_PTS=%.3f..%.3f FRAMES=%lld\n", phase_pts[1][0], phase_pts[1][1], phase_frames[1]);
    printf("PHASE_REMOVE_PTS=%.3f..%.3f FRAMES=%lld\n", phase_pts[2][0], phase_pts[2][1], phase_frames[2]);
    printf("DTS_DISCONTINUITIES=%lld MAX_GAP_S=%.3f PTS_REORDERS=%lld\n", g_gaps, g_max_gap, g_reorder);
    printf("STARVATION_EAGAIN=%lld EVICTED_READS=%lld\n", ab.starved, ab.evicted_read);
    printf("SPLIT_PER_SEGMENT=%d PARTIAL_RETRIES=%lld PARTIAL_PARSES_WITH_PACKETS=%lld\n",
           nsplit, partial_retries, short_parses);
    printf("OPENS=%d OPEN_MS_TOTAL=%.2f PER_OPEN=%.3f\n", g_opens, g_open_s * 1000, g_opens ? g_open_s * 1000 / g_opens : 0);
    printf("APPEND_MS_TOTAL=%.2f PER_APPEND=%.3f\n", append_s * 1000, nappends ? append_s * 1000 / nappends : 0);
    printf("DEMUX_MS_TOTAL=%.2f PER_APPEND=%.3f\n", g_demux_s * 1000, nappends ? g_demux_s * 1000 / nappends : 0);
    printf("DECODE_MS_TOTAL=%.2f PER_FRAME=%.3f\n", g_decode_s * 1000, g_frames ? g_decode_s * 1000 / g_frames : 0);
    printf("PARSE_OVERHEAD_MS_PER_APPEND=%.3f\n",
           nappends ? (g_open_s + g_demux_s + append_s) * 1000 / nappends : 0);

    int complete = phase_frames[0] > 0 && phase_frames[1] > 0 && phase_frames[2] > 0;
    printf("VERDICT=%s\n", complete && ab.evicted_read == 0 ? "all-phases-decoded" : "incomplete");

    av_frame_free(&frm); av_packet_free(&pkt);
    avcodec_free_context(&dec);
    if (fmt) close_over(fmt);
    return complete ? 0 : 1;
}
