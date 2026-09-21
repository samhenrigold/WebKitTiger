// qtkittest - probe Tiger's real QTKit 7.2: load an H.264/AAC MP4 and .mov,
// pull a decoded frame via -[QTMovie frameImageAtTime:withAttributes:error:],
// confirm the runtime return type, play for 1s, and check audio-only files
// (mp3/m4a) advance currentTime under -setRate:.
//
// Background: logs/qtkit-plan.md section 4 establishes that Tiger's QTKit has
// neither QTMovieLayer nor QTVideoRendererWebKitOnly (both missing from the
// box's real QTKit.framework), so a WebKitLegacy <video> backend would have to
// paint via frameImageAtTime:withAttributes:error: with
// QTMovieFrameImageTypeCGImageRef. Header says the method returns (void *);
// this spike nails down what actually comes back.
//
// Build: see Makefile. MRR, fragile ObjC runtime.

#import <Cocoa/Cocoa.h>

// The QTMovieFrameImage* constants/method are annotated 10.5+ in the SDK
// header, but logs/qtkit-plan.md section 2 confirms Tiger's real QuickTime
// 7.2 dylib exports them as defined data symbols regardless (the annotation
// describes when Apple put it in the public SDK, not this QuickTime update's
// actual capability). Neuter the availability macro before importing, same
// trick as spike/CAHost/CAHost.m uses for the CoreAnimation headers.
#include <AvailabilityMacros.h>
#undef AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER
#define AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER
#undef AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER_BUT_DEPRECATED
#define AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER_BUT_DEPRECATED

#import <QTKit/QTKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include <limits.h>

// QTMovieOpenForPlaybackAttribute is a 7.6.3+ addition, not in the 10.4u SDK's
// QTMovie.h at all (checked: grep finds nothing). Declare the key locally —
// per Apple's QTKit release notes the string value is exactly
// "QTMovieOpenForPlaybackAttribute" (an NSNumber BOOL), same family as
// QTMovieOpenAsyncOKAttribute (which the SDK header does have).
static NSString * const QTMovieOpenForPlaybackAttributeLocal = @"QTMovieOpenForPlaybackAttribute";

// -------------------------------------------------------- load-state polling

static BOOL waitForLoadState(QTMovie *movie, QTMovieLoadState target, NSTimeInterval timeout)
{
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:timeout];
    for (;;) {
        long state = [[movie attributeForKey:QTMovieLoadStateAttribute] longValue];
        fprintf(stderr, "  loadState=%ld\n", state);
        if (state >= target)
            return YES;
        if (state == QTMovieLoadStateError)
            return NO;
        if ([deadline timeIntervalSinceNow] <= 0)
            return NO;
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                  beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }
}

// ------------------------------------------------------------- pixel sample

static void describeCGImage(CGImageRef img)
{
    size_t w = CGImageGetWidth(img);
    size_t h = CGImageGetHeight(img);
    fprintf(stderr, "  CGImageRef %zux%zu, bitsPerPixel=%zu, bytesPerRow=%zu\n",
            w, h, CGImageGetBitsPerPixel(img), CGImageGetBytesPerRow(img));

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    size_t sw = 4, sh = 4;
    unsigned char *buf = calloc(sw * sh * 4, 1);
    CGContextRef ctx = CGBitmapContextCreate(buf, sw, sh, 8, sw * 4, cs,
                                              kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(cs);
    CGContextDrawImage(ctx, CGRectMake(0, 0, sw, sh), img);
    fprintf(stderr, "  sample pixel (0,0) RGBA = %d %d %d %d\n", buf[0], buf[1], buf[2], buf[3]);
    CGContextRelease(ctx);
    free(buf);
}

static void writeCGImageToPNG(CGImageRef img, NSString *path)
{
    CFURLRef url = (CFURLRef)[NSURL fileURLWithPath:path];
    CGImageDestinationRef dest = CGImageDestinationCreateWithURL(url, kUTTypePNG, 1, NULL);
    if (!dest) {
        fprintf(stderr, "  CGImageDestinationCreateWithURL failed\n");
        return;
    }
    CGImageDestinationAddImage(dest, img, NULL);
    BOOL ok = CGImageDestinationFinalize(dest);
    fprintf(stderr, "  wrote PNG to %s: %s\n", [path UTF8String], ok ? "ok" : "FAILED");
    CFRelease(dest);
}

// ------------------------------------------------------------- movie probe

static void probeMovie(NSString *path)
{
    // Own autorelease pool per file: on 7.6.4 (unlike 7.2), draining a single
    // top-level pool holding four still-live QTMovies at once triggers a
    // reproducible EXC_BAD_ACCESS in objc_msgSend during final pool release
    // (see README "quirks" — crash log points at QuickTimeComponents'
    // background reader/decode threads). Draining per-file, with an explicit
    // -invalidate first, keeps each movie's teardown isolated.
    NSAutoreleasePool *filePool = [[NSAutoreleasePool alloc] init];

    fprintf(stderr, "\n=== %s ===\n", [path UTF8String]);

    NSError *error = nil;
    QTMovie *movie = [QTMovie movieWithFile:path error:&error];
    if (!movie) {
        fprintf(stderr, "  movieWithFile:error: failed: %s\n", [[error description] UTF8String]);
        [filePool release];
        return;
    }

    fprintf(stderr, "  waiting for QTMovieLoadStateComplete...\n");
    BOOL loaded = waitForLoadState(movie, QTMovieLoadStateComplete, 15.0);
    fprintf(stderr, "  loaded=%s\n", loaded ? "YES" : "NO (timed out or error)");

    QTTime duration = [movie duration];
    fprintf(stderr, "  duration: %lld/%ld (%.3fs)\n",
            duration.timeValue, (long)duration.timeScale,
            duration.timeScale ? (double)duration.timeValue / duration.timeScale : 0.0);

    NSValue *sizeValue = [movie attributeForKey:QTMovieNaturalSizeAttribute];
    NSSize naturalSize = sizeValue ? [sizeValue sizeValue] : NSZeroSize;
    fprintf(stderr, "  naturalSize: %.0fx%.0f\n", naturalSize.width, naturalSize.height);

    NSArray *tracks = [movie tracks];
    fprintf(stderr, "  tracks: %lu\n", (unsigned long)[tracks count]);
    NSUInteger i;
    for (i = 0; i < [tracks count]; i++) {
        QTTrack *track = [tracks objectAtIndex:i];
        QTMedia *media = [track media];
        NSString *mediaType = [media attributeForKey:QTMediaTypeAttribute];
        fprintf(stderr, "    track %lu: mediaType=%s\n", (unsigned long)i, [mediaType UTF8String]);
    }

    BOOL hasVideo = naturalSize.width > 0 && naturalSize.height > 0;
    if (!hasVideo) {
        fprintf(stderr, "  (no video track / zero natural size, skipping frameImageAtTime:)\n");
    } else {
        QTTime t = QTMakeTimeWithTimeInterval(0.5);

        // Control: legacy -frameImageAtTime: (NSImage, no attributes dict).
        NSImage *legacyImage = [movie frameImageAtTime:t];
        fprintf(stderr, "  -frameImageAtTime: (legacy) -> class %s, size %.0fx%.0f\n",
                legacyImage ? [NSStringFromClass([legacyImage class]) UTF8String] : "(nil)",
                [legacyImage size].width, [legacyImage size].height);

        // QTMovieFrameImageTypeCGImageRef
        {
            NSDictionary *attrs = [NSDictionary dictionaryWithObject:QTMovieFrameImageTypeCGImageRef
                                                                forKey:QTMovieFrameImageType];
            NSError *frameError = nil;
            void *result = [movie frameImageAtTime:t withAttributes:attrs error:&frameError];
            if (!result) {
                fprintf(stderr, "  frameImageAtTime:CGImageRef -> nil, error: %s\n",
                        [[frameError description] UTF8String]);
            } else {
                CFTypeID typeID = CFGetTypeID(result);
                fprintf(stderr, "  frameImageAtTime:CGImageRef -> CFTypeID=%lu (CGImageGetTypeID()=%lu) match=%s\n",
                        (unsigned long)typeID, (unsigned long)CGImageGetTypeID(),
                        typeID == CGImageGetTypeID() ? "YES" : "NO");
                if (typeID == CGImageGetTypeID()) {
                    CGImageRef img = (CGImageRef)result;
                    describeCGImage(img);
                    NSString *outPath = [NSString stringWithFormat:@"/tmp/qtkit-frame-%@.png",
                                          [[path lastPathComponent] stringByDeletingPathExtension]];
                    writeCGImageToPNG(img, outPath);
                    // NOTE: do NOT CFRelease(result) here. Despite the toll-free CGImageRef
                    // payload, this came back from an ObjC method whose name doesn't start
                    // with alloc/new/copy/mutableCopy, so by the Cocoa ownership convention
                    // it is autoreleased, not owned by the caller. CFRelease-ing it anyway
                    // (an earlier version of this file did) double-frees it once the
                    // enclosing autorelease pool drains -- corrupts the heap and crashes
                    // with "-[NSCFType ]: selector not recognized" on a garbage self
                    // pointer, confirmed by removing this exact call. See README quirks.
                } else {
                    fprintf(stderr, "  unexpected type, runtime class (if ObjC) = %s\n",
                            object_getClassName((id)result));
                }
            }
        }

        // Control: QTMovieFrameImageTypeNSImage
        {
            NSDictionary *attrs = [NSDictionary dictionaryWithObject:QTMovieFrameImageTypeNSImage
                                                                forKey:QTMovieFrameImageType];
            NSError *frameError = nil;
            void *result = [movie frameImageAtTime:t withAttributes:attrs error:&frameError];
            if (!result) {
                fprintf(stderr, "  frameImageAtTime:NSImage -> nil, error: %s\n",
                        [[frameError description] UTF8String]);
            } else {
                id obj = (id)result;
                fprintf(stderr, "  frameImageAtTime:NSImage -> class %s, isKindOfClass:NSImage=%s, size %.0fx%.0f\n",
                        [NSStringFromClass([obj class]) UTF8String],
                        [obj isKindOfClass:[NSImage class]] ? "YES" : "NO",
                        [obj isKindOfClass:[NSImage class]] ? [(NSImage *)obj size].width : -1.0,
                        [obj isKindOfClass:[NSImage class]] ? [(NSImage *)obj size].height : -1.0);
            }
        }
    }

    // Playback: run for 1s at rate 1.0, confirm currentTime advances (this is
    // the audio-path check too: mp3/m4a files have no video track but should
    // still advance currentTime while playing).
    QTTime before = [movie currentTime];
    fprintf(stderr, "  currentTime before play: %lld/%ld\n", before.timeValue, (long)before.timeScale);
    [movie setRate:1.0];
    NSDate *playDeadline = [NSDate dateWithTimeIntervalSinceNow:1.0];
    while ([playDeadline timeIntervalSinceNow] > 0) {
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                  beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
    }
    QTTime after = [movie currentTime];
    [movie stop];
    fprintf(stderr, "  currentTime after ~1s play: %lld/%ld (rate was %.2f)\n",
            after.timeValue, (long)after.timeScale, [movie rate]);
    fprintf(stderr, "  advanced: %s\n",
            (after.timeScale && before.timeScale &&
             (double)after.timeValue / after.timeScale > (double)before.timeValue / before.timeScale)
                ? "YES" : "NO");

    if (hasVideo) {
        QTTime t2 = [movie currentTime];
        NSDictionary *attrs = [NSDictionary dictionaryWithObject:QTMovieFrameImageTypeCGImageRef
                                                            forKey:QTMovieFrameImageType];
        NSError *frameError = nil;
        void *result = [movie frameImageAtTime:t2 withAttributes:attrs error:&frameError];
        if (result && CFGetTypeID(result) == CGImageGetTypeID()) {
            NSString *outPath = [NSString stringWithFormat:@"/tmp/qtkit-playing-%@.png",
                                  [[path lastPathComponent] stringByDeletingPathExtension]];
            writeCGImageToPNG((CGImageRef)result, outPath);
            // See the "do NOT CFRelease" note above -- same autoreleased object.
        }
    }

    [filePool release];
}

// ------------------------------------------------ playback-mode open (7.6.3+)

// Opens with QTMovieOpenForPlaybackAttribute=YES (playback-optimized; Apple's
// docs say a movie opened this way may not support frameImageAtTime: for
// arbitrary times) and QTMovieOpenAsyncOKAttribute=YES (async open allowed).
// Reports load-state progression and whether frameImageAtTime: still works.
static void probePlaybackModeOpen(NSString *path)
{
    NSAutoreleasePool *filePool = [[NSAutoreleasePool alloc] init];
    fprintf(stderr, "\n=== %s (QTMovieOpenForPlaybackAttribute=YES, OpenAsyncOKAttribute=YES) ===\n",
            [path UTF8String]);

    NSDictionary *attrs = [NSDictionary dictionaryWithObjectsAndKeys:
                            path, QTMovieFileNameAttribute,
                            [NSNumber numberWithBool:YES], QTMovieOpenForPlaybackAttributeLocal,
                            [NSNumber numberWithBool:YES], QTMovieOpenAsyncOKAttribute,
                            nil];
    NSError *error = nil;
    QTMovie *movie = [QTMovie movieWithAttributes:attrs error:&error];
    if (!movie) {
        fprintf(stderr, "  movieWithAttributes:error: failed: %s\n", [[error description] UTF8String]);
        [filePool release];
        return;
    }

    fprintf(stderr, "  waiting for QTMovieLoadStateComplete (logging every state seen)...\n");
    NSMutableArray *statesSeen = [NSMutableArray array];
    long lastState = LONG_MIN;
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:15.0];
    for (;;) {
        long state = [[movie attributeForKey:QTMovieLoadStateAttribute] longValue];
        if (state != lastState) {
            [statesSeen addObject:[NSNumber numberWithLong:state]];
            fprintf(stderr, "  loadState -> %ld\n", state);
            lastState = state;
        }
        if (state >= QTMovieLoadStateComplete || state == QTMovieLoadStateError)
            break;
        if ([deadline timeIntervalSinceNow] <= 0) {
            fprintf(stderr, "  TIMED OUT waiting for load state\n");
            break;
        }
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                  beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
    }
    fprintf(stderr, "  distinct load states seen (in order): %s\n", [[statesSeen description] UTF8String]);

    NSValue *sizeValue = [movie attributeForKey:QTMovieNaturalSizeAttribute];
    NSSize naturalSize = sizeValue ? [sizeValue sizeValue] : NSZeroSize;
    fprintf(stderr, "  naturalSize: %.0fx%.0f\n", naturalSize.width, naturalSize.height);

    QTTime t = QTMakeTimeWithTimeInterval(0.5);
    NSDictionary *frameAttrs = [NSDictionary dictionaryWithObject:QTMovieFrameImageTypeCGImageRef
                                                             forKey:QTMovieFrameImageType];
    NSError *frameError = nil;
    void *result = [movie frameImageAtTime:t withAttributes:frameAttrs error:&frameError];
    if (!result) {
        fprintf(stderr, "  frameImageAtTime:CGImageRef in playback mode -> nil, error: %s\n",
                [[frameError description] UTF8String]);
    } else {
        CFTypeID typeID = CFGetTypeID(result);
        BOOL isCGImage = (typeID == CGImageGetTypeID());
        fprintf(stderr, "  frameImageAtTime:CGImageRef in playback mode -> non-nil, isCGImageRef=%s\n",
                isCGImage ? "YES" : "NO");
        if (isCGImage) {
            describeCGImage((CGImageRef)result);
            // Autoreleased, not owned -- do not CFRelease.
        }
    }

    [filePool release];
}

// ---------------------------------------------------------- per-frame timing

// Measures wall-clock cost of frameImageAtTime:withAttributes:error: with
// QTMovieFrameImageTypeCGImageRef across `sampleCount` evenly spaced times in
// the movie, reporting min/avg/max milliseconds per call.
static void measureFrameImageCost(NSString *path, int sampleCount)
{
    NSAutoreleasePool *filePool = [[NSAutoreleasePool alloc] init];
    fprintf(stderr, "\n=== timing: %s ===\n", [path UTF8String]);

    NSError *error = nil;
    QTMovie *movie = [QTMovie movieWithFile:path error:&error];
    if (!movie) {
        fprintf(stderr, "  movieWithFile:error: failed: %s\n", [[error description] UTF8String]);
        [filePool release];
        return;
    }
    if (!waitForLoadState(movie, QTMovieLoadStateComplete, 15.0)) {
        fprintf(stderr, "  failed to reach QTMovieLoadStateComplete\n");
        [filePool release];
        return;
    }

    NSValue *sizeValue = [movie attributeForKey:QTMovieNaturalSizeAttribute];
    NSSize naturalSize = sizeValue ? [sizeValue sizeValue] : NSZeroSize;
    QTTime duration = [movie duration];
    double durationSeconds = duration.timeScale ? (double)duration.timeValue / duration.timeScale : 0.0;
    fprintf(stderr, "  naturalSize=%.0fx%.0f duration=%.3fs samples=%d\n",
            naturalSize.width, naturalSize.height, durationSeconds, sampleCount);

    NSDictionary *attrs = [NSDictionary dictionaryWithObject:QTMovieFrameImageTypeCGImageRef
                                                        forKey:QTMovieFrameImageType];
    double minMs = 1e9, maxMs = 0, totalMs = 0;
    int ok = 0;
    int i;
    for (i = 0; i < sampleCount; i++) {
        double t = durationSeconds * (i + 0.5) / sampleCount;
        QTTime qt = QTMakeTimeWithTimeInterval(t);
        NSDate *start = [NSDate date];
        NSError *frameError = nil;
        void *result = [movie frameImageAtTime:qt withAttributes:attrs error:&frameError];
        double ms = -[start timeIntervalSinceNow] * 1000.0;
        if (result && CFGetTypeID(result) == CGImageGetTypeID()) {
            ok++;
            if (ms < minMs) minMs = ms;
            if (ms > maxMs) maxMs = ms;
            totalMs += ms;
            // Autoreleased, not owned -- do not CFRelease (accumulates in filePool
            // until this function's pool drains, fine for sampleCount this small).
        } else {
            fprintf(stderr, "  sample %d at t=%.3fs FAILED: %s\n", i, t,
                    frameError ? [[frameError description] UTF8String] : "(nil, no error)");
        }
    }
    if (ok > 0) {
        fprintf(stderr, "  %d/%d frames ok: min=%.1fms avg=%.1fms max=%.1fms\n",
                ok, sampleCount, minMs, totalMs / ok, maxMs);
    }

    [filePool release];
}

// ------------------------------------------------------------------------- main

int main(int argc, const char **argv)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    BOOL withApp = (argc > 1 && strcmp(argv[1], "--app") == 0);
    fprintf(stderr, "qtkittest: %s NSApplication\n", withApp ? "WITH" : "WITHOUT");
    if (withApp) {
        [NSApplication sharedApplication];
    }

    NSString *dir = @"/Users/shg/qtkit-media";
    probeMovie([dir stringByAppendingPathComponent:@"test.mp4"]);
    probeMovie([dir stringByAppendingPathComponent:@"test.mov"]);
    probeMovie([dir stringByAppendingPathComponent:@"test.mp3"]);
    probeMovie([dir stringByAppendingPathComponent:@"test.m4a"]);

    // 7.6.3+ playback-optimized open path.
    probePlaybackModeOpen([dir stringByAppendingPathComponent:@"test.mp4"]);

    // What does 7.6.4 actually decode: High profile, and a larger Main-profile clip.
    probeMovie([dir stringByAppendingPathComponent:@"test-high.mp4"]);
    probeMovie([dir stringByAppendingPathComponent:@"test-720p-main.mp4"]);

    // Per-frame decode+frameImage cost, small vs. large frame.
    measureFrameImageCost([dir stringByAppendingPathComponent:@"test.mp4"], 10);
    measureFrameImageCost([dir stringByAppendingPathComponent:@"test-720p-main.mp4"], 10);

    fprintf(stderr, "\nqtkittest: done\n");
    [pool release];
    return 0;
}
