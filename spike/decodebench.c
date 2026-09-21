/* decodebench - software decode throughput for the 64-bit Tiger content process.
 *
 * Decodes one stream of a file as fast as possible (no display, no sync) and
 * prints decoded frames/s and ms/frame. With -s it also converts every video
 * frame yuv420p -> BGRA with libswscale and times that separately, since frames
 * must become RGBA before they can be composited.
 *
 * Build (host): see spike/build-decodebench.sh
 * Usage: decodebench [-t threads] [-a] [-s] [-n maxframes] file
 *          -t N  decoder threads (default 2)
 *          -a    decode the audio stream instead of the video stream
 *          -s    also time yuv420p -> BGRA conversion (video only)
 *          -n N  stop after N frames
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>

static double now_s(void) { return av_gettime_relative() / 1e6; }

int main(int argc, char **argv)
{
    int threads = 2, want_audio = 0, do_scale = 0, maxframes = 0, i;
    const char *path = NULL;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-t") && i + 1 < argc) threads = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-n") && i + 1 < argc) maxframes = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-a")) want_audio = 1;
        else if (!strcmp(argv[i], "-s")) do_scale = 1;
        else path = argv[i];
    }
    if (!path) { fprintf(stderr, "usage: decodebench [-t N] [-a] [-s] [-n N] file\n"); return 2; }

    enum AVMediaType type = want_audio ? AVMEDIA_TYPE_AUDIO : AVMEDIA_TYPE_VIDEO;

    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, path, NULL, NULL) < 0) { fprintf(stderr, "open failed: %s\n", path); return 1; }
    if (avformat_find_stream_info(fmt, NULL) < 0) { fprintf(stderr, "no stream info\n"); return 1; }

    const AVCodec *codec = NULL;
    int sidx = av_find_best_stream(fmt, type, -1, -1, &codec, 0);
    if (sidx < 0 || !codec) { fprintf(stderr, "no %s stream / no decoder\n", want_audio ? "audio" : "video"); return 1; }

    AVCodecContext *dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec, fmt->streams[sidx]->codecpar);
    dec->thread_count = threads;
    dec->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    if (avcodec_open2(dec, codec, NULL) < 0) { fprintf(stderr, "avcodec_open2 failed\n"); return 1; }

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frm = av_frame_alloc();

    struct SwsContext *sws = NULL;
    uint8_t *bgra[4] = { 0 }; int bgra_ls[4] = { 0 };
    double scale_s = 0.0;
    long long nframes = 0, nsamples = 0;
    double t0 = now_s(), decode_s = 0.0;
    int eof = 0, draining = 0;

    while (!eof) {
        int ret;
        if (!draining) {
            ret = av_read_frame(fmt, pkt);
            if (ret < 0) { draining = 1; avcodec_send_packet(dec, NULL); }
            else if (pkt->stream_index != sidx) { av_packet_unref(pkt); continue; }
            else {
                double s = now_s();
                ret = avcodec_send_packet(dec, pkt);
                decode_s += now_s() - s;
                av_packet_unref(pkt);
                if (ret < 0 && ret != AVERROR(EAGAIN)) break;
            }
        }
        for (;;) {
            double s = now_s();
            ret = avcodec_receive_frame(dec, frm);
            decode_s += now_s() - s;
            if (ret == AVERROR(EAGAIN)) break;
            if (ret == AVERROR_EOF) { eof = 1; break; }
            if (ret < 0) { eof = 1; break; }
            nframes++;
            if (want_audio) nsamples += frm->nb_samples;
            else if (do_scale) {
                if (!sws) {
                    sws = sws_getContext(frm->width, frm->height, frm->format,
                                         frm->width, frm->height, AV_PIX_FMT_BGRA,
                                         SWS_POINT, NULL, NULL, NULL);
                    if (!sws || av_image_alloc(bgra, bgra_ls, frm->width, frm->height, AV_PIX_FMT_BGRA, 16) < 0) {
                        fprintf(stderr, "swscale setup failed\n"); return 1;
                    }
                }
                double ss = now_s();
                sws_scale(sws, (const uint8_t * const *)frm->data, frm->linesize, 0, frm->height, bgra, bgra_ls);
                scale_s += now_s() - ss;
            }
            av_frame_unref(frm);
            if (maxframes && nframes >= maxframes) { eof = 1; break; }
        }
    }
    double wall = now_s() - t0;

    printf("FILE=%s\n", path);
    printf("CODEC=%s\n", codec->name);
    printf("THREADS=%d\n", threads);
    if (!want_audio) {
        printf("SIZE=%dx%d\n", dec->width, dec->height);
        printf("FRAMES=%lld\n", nframes);
        printf("WALL_S=%.3f\n", wall);
        printf("FPS=%.2f\n", nframes / (wall > 0 ? wall : 1));
        printf("MS_PER_FRAME=%.3f\n", nframes ? wall * 1000.0 / nframes : 0.0);
        if (do_scale) {
            printf("SWS_MS_PER_FRAME=%.3f\n", nframes ? scale_s * 1000.0 / nframes : 0.0);
            printf("SWS_SHARE_PCT=%.1f\n", wall > 0 ? 100.0 * scale_s / wall : 0.0);
        }
    } else {
        double dur = dec->sample_rate ? (double)nsamples / dec->sample_rate : 0.0;
        printf("RATE=%d CHANNELS=%d\n", dec->sample_rate, dec->ch_layout.nb_channels);
        printf("PACKETS=%lld SAMPLES=%lld AUDIO_S=%.3f\n", nframes, nsamples, dur);
        printf("WALL_S=%.3f\n", wall);
        printf("REALTIME_X=%.1f\n", wall > 0 ? dur / wall : 0.0);
    }

    if (sws) { sws_freeContext(sws); av_freep(&bgra[0]); }
    av_frame_free(&frm);
    av_packet_free(&pkt);
    avcodec_free_context(&dec);
    avformat_close_input(&fmt);
    return nframes > 0 ? 0 : 1;
}
