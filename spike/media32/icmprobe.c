// icmprobe -- what QuickTime 7's ICMDecompressionSession (the C API under
// QTKit) decodes per second on Tiger, i386, into BGRA CVPixelBuffers that are
// CGBitmapContext-compatible. This is the decoder the i386 GPU process would
// use for an in-process H.264 path; the numbers go next to spike/decodebench
// (ffmpeg, x86_64) in NOTES.md.
//
// Demux: Movie Toolbox (NewMovieFromProperties, GetMediaSample2 per sample).
// Decode: ICMDecompressionSessionDecodeFrame with non-scheduled display times,
// one SetNonScheduledDisplayTime per sample so every frame is emitted once.
// Output: the tracking callback gets a CVPixelBufferRef; with "copy" as the
// second argument each frame's BGRA plane is memcpy'd out too (what a GPU
// process would pay to hand the frame to CoreGraphics / a GL texture).
//
// Build/run: make -C spike/media32 run CLIP=../media64/h264-480p30.mp4
#include <QuickTime/QuickTime.h>
#include <CoreVideo/CoreVideo.h>
#include <ApplicationServices/ApplicationServices.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double wall(void) { struct timeval tv; gettimeofday(&tv, NULL); return tv.tv_sec + tv.tv_usec / 1e6; }
static double cpu(void) { struct rusage r; getrusage(RUSAGE_SELF, &r);
    return r.ru_utime.tv_sec + r.ru_utime.tv_usec / 1e6 + r.ru_stime.tv_sec + r.ru_stime.tv_usec / 1e6; }

static struct {
    long emitted, decoded, dropped, errors;
    int copy, cgChecked;
    size_t w, h, stride;
    void* copyBuf;
    OSType fmt;
} g;

static void tracking(void* refCon, OSStatus result, ICMDecompressionTrackingFlags flags, CVPixelBufferRef pb,
    TimeValue64 displayTime, TimeValue64 displayDuration, ICMValidTimeFlags validTimeFlags, void* reserved, void* srcRefCon)
{
    if (result != noErr) { g.errors++; return; }
    if (flags & kICMDecompressionTracking_FrameDecoded) g.decoded++;
    if (flags & kICMDecompressionTracking_FrameDropped) g.dropped++;
    if (!(flags & kICMDecompressionTracking_EmittingFrame) || !pb) return;
    g.emitted++;
    CVPixelBufferLockBaseAddress(pb, 0);
    void* base = CVPixelBufferGetBaseAddress(pb);
    g.w = CVPixelBufferGetWidth(pb); g.h = CVPixelBufferGetHeight(pb);
    g.stride = CVPixelBufferGetBytesPerRow(pb); g.fmt = CVPixelBufferGetPixelFormatType(pb);
    if (!g.cgChecked) {
        // Prove the buffer really is CGBitmapContext-compatible: wrap it, draw, read a pixel back.
        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        CGContextRef ctx = CGBitmapContextCreate(base, g.w, g.h, 8, g.stride, cs,
            kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
        uint32_t px = *(uint32_t*)base;
        fprintf(stderr, "first frame: %zux%zu stride %zu fmt '%c%c%c%c' CGBitmapContextCreate=%s pixel[0]=%08x\n",
            g.w, g.h, g.stride, (int)(g.fmt >> 24), (int)(g.fmt >> 16), (int)(g.fmt >> 8), (int)g.fmt,
            ctx ? "ok" : "NULL", px);
        if (ctx) CGContextRelease(ctx);
        CGColorSpaceRelease(cs);
        g.cgChecked = 1;
    }
    if (g.copy) {
        if (!g.copyBuf) g.copyBuf = malloc(g.stride * g.h);
        memcpy(g.copyBuf, base, g.stride * g.h);
    }
    CVPixelBufferUnlockBaseAddress(pb, 0);
}

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: icmprobe file.mp4 [copy]\n"); return 2; }
    g.copy = argc > 2 && !strcmp(argv[2], "copy");
    EnterMovies();

    CFStringRef path = CFStringCreateWithCString(NULL, argv[1], kCFStringEncodingUTF8);
    Boolean yes = true;
    QTNewMoviePropertyElement props[] = {
        { kQTPropertyClass_DataLocation, kQTDataLocationPropertyID_CFStringPosixPath, sizeof(path), &path, 0 },
        { kQTPropertyClass_MovieInstantiation, kQTMovieInstantiationPropertyID_DontAskUnresolvedDataRefs, sizeof(yes), &yes, 0 },
        { kQTPropertyClass_NewMovieProperty, kQTNewMoviePropertyID_Active, sizeof(yes), &yes, 0 },
    };
    Movie movie = NULL;
    OSStatus err = NewMovieFromProperties(3, props, 0, NULL, &movie);
    if (err || !movie) { fprintf(stderr, "NewMovieFromProperties: %d\n", (int)err); return 1; }

    Track track = GetMovieIndTrackType(movie, 1, VideoMediaType, movieTrackMediaType);
    if (!track) { fprintf(stderr, "no video track\n"); return 1; }
    Media media = GetTrackMedia(track);
    TimeScale ts = GetMediaTimeScale(media);
    long sampleCount = GetMediaSampleCount(media);
    ImageDescriptionHandle desc = (ImageDescriptionHandle)NewHandle(0);
    GetMediaSampleDescription(media, 1, (SampleDescriptionHandle)desc);
    fprintf(stderr, "codec '%c%c%c%c' %dx%d depth %d, %ld samples, timescale %ld\n",
        (int)((**desc).cType >> 24), (int)((**desc).cType >> 16), (int)((**desc).cType >> 8), (int)(**desc).cType,
        (**desc).width, (**desc).height, (**desc).depth, sampleCount, (long)ts);

    // BGRA, CG-compatible destination. Same keys the 2018 WebKit QTKit backend and
    // QTPixelBufferContext use.
    OSType bgra = k32BGRAPixelFormat;
    CFNumberRef fmtNum = CFNumberCreate(NULL, kCFNumberSInt32Type, &bgra);
    const void* keys[] = { kCVPixelBufferPixelFormatTypeKey, kCVPixelBufferCGBitmapContextCompatibilityKey };
    const void* vals[] = { fmtNum, kCFBooleanTrue };
    CFDictionaryRef attrs = CFDictionaryCreate(NULL, keys, vals, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

    ICMDecompressionTrackingCallbackRecord cb = { tracking, NULL };
    ICMDecompressionSessionRef session = NULL;
    err = ICMDecompressionSessionCreate(NULL, desc, NULL, attrs, &cb, &session);
    if (err) { fprintf(stderr, "ICMDecompressionSessionCreate: %d\n", (int)err); return 1; }

    ByteCount cap = 4 << 20;
    UInt8* buf = malloc(cap);
    TimeValue64 decodeTime = 0;
    long fed = 0;
    double w0 = wall(), c0 = cpu();
    for (;;) {
        ByteCount size = 0; TimeValue64 sampleDecodeTime = 0, dur = 0, displayOffset = 0;
        ItemCount n = 0; MediaSampleFlags sflags = 0;
        err = GetMediaSample2(media, buf, cap, &size, decodeTime, &sampleDecodeTime, &dur, &displayOffset,
            NULL, NULL, 1, &n, &sflags);
        if (err || n == 0) break;
        ICMFrameTimeRecord ft;
        memset(&ft, 0, sizeof ft);
        ft.recordSize = sizeof ft;
        ft.scale = ts;
        ft.rate = fixed1;
        ft.flags = icmFrameTimeIsNonScheduledDisplayTime | icmFrameTimeHasDecodeTime;
        ft.decodeTime = sampleDecodeTime;
        TimeValue64 display = sampleDecodeTime + displayOffset;
        ft.value.hi = (SInt32)(display >> 32); ft.value.lo = (UInt32)display;
        ft.duration = (long)dur;
        err = ICMDecompressionSessionDecodeFrame(session, buf, size, NULL, &ft, NULL);
        if (err) { fprintf(stderr, "DecodeFrame %ld: %d\n", fed, (int)err); g.errors++; }
        ICMDecompressionSessionSetNonScheduledDisplayTime(session, display, ts, 0);
        fed++;
        decodeTime = sampleDecodeTime + dur;
    }
    ICMDecompressionSessionFlush(session);
    double w1 = wall(), c1 = cpu();
    ICMDecompressionSessionRelease(session);

    double secs = w1 - w0;
    printf("%s: fed %ld emitted %ld (decoded-cb %ld dropped %ld errors %ld) %zux%zu %s\n",
        argv[1], fed, g.emitted, g.decoded, g.dropped, g.errors, g.w, g.h, g.copy ? "with copy" : "no copy");
    printf("  %.2f s wall, %.2f s cpu (%.0f%% of one core), %.1f fps, %.1f ms/frame\n",
        secs, c1 - c0, 100 * (c1 - c0) / secs, g.emitted / secs, 1000 * secs / (g.emitted ? g.emitted : 1));
    DisposeMovie(movie);
    return g.emitted ? 0 : 1;
}
