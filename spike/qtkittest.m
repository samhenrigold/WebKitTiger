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
    fprintf(stderr, "\n=== %s ===\n", [path UTF8String]);

    NSError *error = nil;
    QTMovie *movie = [QTMovie movieWithFile:path error:&error];
    if (!movie) {
        fprintf(stderr, "  movieWithFile:error: failed: %s\n", [[error description] UTF8String]);
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
                    CFRelease(result);
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
            CFRelease(result);
        }
    }
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

    fprintf(stderr, "\nqtkittest: done\n");
    [pool release];
    return 0;
}
