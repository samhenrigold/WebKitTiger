// qtrenderertest - probe QTKit 7.6.4's private QTVideoRendererWebKitOnly,
// the class the 2018 WebKit1 MediaPlayerPrivateQTKit backend used for its
// real (non-frameImageAtTime:) playback paint path. logs/qtkit-plan.md found
// this class absent on Tiger's original QuickTime 7.2; the box now has
// QuickTime/QTKit 7.6.4 (from the Safari 4.1.3 install), which DOES export
// it (confirmed by nm: .objc_class_name_QTVideoRendererWebKitOnly and
// _QTVideoRendererWebKitOnlyNewImageAvailableNotification are both defined,
// non-U, symbols in the box's real QTKit.framework). frameImageAtTime: at
// 720p costs ~1.4s/frame (spike/qtkittest.m timing) -- far too slow for
// playback, so this renderer path is the one that actually matters.
//
// Driven per refs/webkit-history/qtkit-mediaplayer/MediaPlayerPrivateQTKit.mm
// (createQTVideoRenderer/destroyQTVideoRenderer, ~line 376-416, and paint(),
// ~line 1266-1292): NSClassFromString(@"QTVideoRendererWebKitOnly"),
// alloc/init, -setMovie:, observe
// QTVideoRendererWebKitOnlyNewImageAvailableNotification on the renderer
// object, and on each notification call -drawInRect: with an NSGraphicsContext
// wrapping a CGContext set current via +[NSGraphicsContext setCurrentContext:].
// Both -setMovie: and -drawInRect: are declared only as an informal protocol
// in that file (WebKitVideoRenderingDetails) -- QTVideoRendererWebKitOnly has
// no public header anywhere, it's Apple-internal SPI.
//
// Build: see NOTES.md-style one-liner in spike/README or just:
//   tiger-clang -fobjc-runtime=macosx-fragile-10.4 -O0 -g -Wall \
//     -o build/qtrenderertest spike/qtrenderertest.m \
//     -framework Cocoa -framework QTKit -framework ApplicationServices
// MRR, fragile ObjC runtime.

#import <Cocoa/Cocoa.h>
#import <QTKit/QTKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include <string.h>
#include <dlfcn.h>

// Real exported data symbol in QTKit.framework (confirmed by nm, see header
// comment) -- but it's private SPI, not in the 10.4u SDK's QTKit stub used
// for link-time symbol resolution, so a plain `extern` fails to link even
// though the on-device dylib genuinely has it. Same problem WebKit's own
// SOFT_LINK_POINTER macro solves; do the minimal version by hand with dlsym
// against the already-loaded (-framework QTKit) image at runtime.
static NSString *getQTVideoRendererWebKitOnlyNewImageAvailableNotification(void)
{
    NSString **ptr = (NSString **)dlsym(RTLD_DEFAULT, "QTVideoRendererWebKitOnlyNewImageAvailableNotification");
    return ptr ? *ptr : nil;
}

// Informal protocol for the two selectors WebKit's own backend used, per
// MediaPlayerPrivateQTKit.mm's WebKitVideoRenderingDetails. QTVideoRendererWebKitOnly
// has no public header to import, so this is how we get static-typed dispatch.
@protocol WebKitVideoRenderingDetails
- (void)setMovie:(id)movie;
- (void)drawInRect:(NSRect)rect;
@end

// -------------------------------------------------------- load-state polling

static BOOL waitForLoadState(QTMovie *movie, QTMovieLoadState target, NSTimeInterval timeout)
{
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:timeout];
    for (;;) {
        long state = [[movie attributeForKey:QTMovieLoadStateAttribute] longValue];
        if (state >= target)
            return YES;
        if (state == QTMovieLoadStateError)
            return NO;
        if ([deadline timeIntervalSinceNow] <= 0)
            return NO;
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                  beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
    }
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

// ----------------------------------------------------------------- probe object

// Owns the notification observer method (needs an ObjC target), the
// CGBitmapContext we draw into, and the running stats.
@interface QTRendererProbe : NSObject
{
@public
    id renderer;               // QTVideoRendererWebKitOnly instance
    CGContextRef bitmapContext;
    unsigned char *bitmapBuffer;
    size_t width, height;
    unsigned notificationCount;
    double drawMinMs, drawMaxMs, drawTotalMs;
    unsigned drawCount;
    unsigned long firstChecksum;   // whole-buffer checksum, first draw
    unsigned long lastChecksum;    // whole-buffer checksum, most recent draw
}
- (void)newImageAvailable:(NSNotification *)note;
@end

@implementation QTRendererProbe

- (void)newImageAvailable:(NSNotification *)note
{
    notificationCount++;

    NSGraphicsContext *newContext = [NSGraphicsContext graphicsContextWithGraphicsPort:bitmapContext flipped:NO];
    [NSGraphicsContext saveGraphicsState];
    [NSGraphicsContext setCurrentContext:newContext];

    NSDate *start = [NSDate date];
    [(id<WebKitVideoRenderingDetails>)renderer drawInRect:NSMakeRect(0, 0, width, height)];
    double ms = -[start timeIntervalSinceNow] * 1000.0;

    [NSGraphicsContext restoreGraphicsState];

    drawCount++;
    drawTotalMs += ms;
    if (drawCount == 1 || ms < drawMinMs) drawMinMs = ms;
    if (ms > drawMaxMs) drawMaxMs = ms;

    // Whole-buffer checksum (cheap additive/rotate hash) rather than a fixed
    // sample point: the test pattern has large static black/white regions
    // (see spike/qtkit-media/frame-720p.png), so a single fixed pixel or
    // corner can easily land somewhere that never changes even though the
    // frame as a whole is animating (the rainbow bar, the countdown digits).
    size_t total = width * height * 4;
    unsigned long checksum = 0;
    size_t i;
    for (i = 0; i < total; i++)
        checksum = (checksum * 31) + bitmapBuffer[i];
    lastChecksum = checksum;
    if (drawCount == 1)
        firstChecksum = checksum;
}

@end

// ------------------------------------------------------------------- probe run

static void probeRenderer(NSString *path, NSTimeInterval runSeconds)
{
    fprintf(stderr, "\n=== %s ===\n", [path UTF8String]);

    Class rendererClass = NSClassFromString(@"QTVideoRendererWebKitOnly");
    if (!rendererClass) {
        fprintf(stderr, "  QTVideoRendererWebKitOnly: NOT FOUND (NSClassFromString returned nil)\n");
        return;
    }
    NSString *notificationName = getQTVideoRendererWebKitOnlyNewImageAvailableNotification();
    if (!notificationName) {
        fprintf(stderr, "  QTVideoRendererWebKitOnlyNewImageAvailableNotification: NOT FOUND (dlsym returned nil)\n");
        return;
    }
    fprintf(stderr, "  QTVideoRendererWebKitOnly class found\n");
    fprintf(stderr, "  class responds to setMovie: = %s\n",
            [rendererClass instancesRespondToSelector:@selector(setMovie:)] ? "YES" : "NO");
    fprintf(stderr, "  class responds to drawInRect: = %s\n",
            [rendererClass instancesRespondToSelector:@selector(drawInRect:)] ? "YES" : "NO");

    NSError *error = nil;
    QTMovie *movie = [QTMovie movieWithFile:path error:&error];
    if (!movie) {
        fprintf(stderr, "  movieWithFile:error: failed: %s\n", [[error description] UTF8String]);
        return;
    }
    if (!waitForLoadState(movie, QTMovieLoadStateComplete, 15.0)) {
        fprintf(stderr, "  failed to reach QTMovieLoadStateComplete\n");
        return;
    }

    NSValue *sizeValue = [movie attributeForKey:QTMovieNaturalSizeAttribute];
    NSSize naturalSize = sizeValue ? [sizeValue sizeValue] : NSZeroSize;
    fprintf(stderr, "  naturalSize: %.0fx%.0f\n", naturalSize.width, naturalSize.height);
    if (naturalSize.width <= 0 || naturalSize.height <= 0) {
        fprintf(stderr, "  no video track, skipping\n");
        return;
    }

    id renderer = [[rendererClass alloc] init];
    if (!renderer) {
        fprintf(stderr, "  [[QTVideoRendererWebKitOnly alloc] init] -> nil\n");
        return;
    }
    fprintf(stderr, "  instance responds to setMovie: = %s\n",
            [renderer respondsToSelector:@selector(setMovie:)] ? "YES" : "NO");
    fprintf(stderr, "  instance responds to drawInRect: = %s\n",
            [renderer respondsToSelector:@selector(drawInRect:)] ? "YES" : "NO");

    [(id<WebKitVideoRenderingDetails>)renderer setMovie:movie];

    QTRendererProbe *probe = [[QTRendererProbe alloc] init];
    probe->renderer = renderer;
    probe->width = (size_t)naturalSize.width;
    probe->height = (size_t)naturalSize.height;

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    size_t bytesPerRow = probe->width * 4;
    probe->bitmapBuffer = calloc(bytesPerRow * probe->height, 1);
    probe->bitmapContext = CGBitmapContextCreate(probe->bitmapBuffer, probe->width, probe->height, 8,
                                                  bytesPerRow, cs, kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(cs);
    if (!probe->bitmapContext) {
        fprintf(stderr, "  CGBitmapContextCreate failed\n");
        [renderer release];
        [probe release];
        return;
    }

    [[NSNotificationCenter defaultCenter] addObserver:probe
                                             selector:@selector(newImageAvailable:)
                                                 name:notificationName
                                               object:renderer];

    fprintf(stderr, "  setRate:1.0, running for %.1fs, counting NewImageAvailable...\n", runSeconds);
    [movie setRate:1.0];
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:runSeconds];
    while ([deadline timeIntervalSinceNow] > 0) {
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                  beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
    }
    [movie stop];

    [[NSNotificationCenter defaultCenter] removeObserver:probe
                                                     name:notificationName
                                                   object:renderer];

    fprintf(stderr, "  NewImageAvailable notifications in %.1fs: %u (%.1f/s effective decoded frame rate)\n",
            runSeconds, probe->notificationCount, probe->notificationCount / runSeconds);

    if (probe->drawCount > 0) {
        fprintf(stderr, "  drawInRect: cost over %u draws: min=%.2fms avg=%.2fms max=%.2fms\n",
                probe->drawCount, probe->drawMinMs, probe->drawTotalMs / probe->drawCount, probe->drawMaxMs);
        BOOL pixelsChanged = probe->firstChecksum != probe->lastChecksum;
        fprintf(stderr, "  pixels changed between first and last draw: %s (checksum first=%lu last=%lu)\n",
                pixelsChanged ? "YES" : "NO (identical checksum, or only one draw happened)",
                probe->firstChecksum, probe->lastChecksum);

        CGImageRef img = CGBitmapContextCreateImage(probe->bitmapContext);
        if (img) {
            NSString *outPath = [NSString stringWithFormat:@"/tmp/qtrenderer-%@.png",
                                  [[path lastPathComponent] stringByDeletingPathExtension]];
            writeCGImageToPNG(img, outPath);
            CGImageRelease(img);
        }
    } else {
        fprintf(stderr, "  no draws happened (no NewImageAvailable notifications received)\n");
    }

    [(id<WebKitVideoRenderingDetails>)renderer setMovie:nil];
    CGContextRelease(probe->bitmapContext);
    free(probe->bitmapBuffer);
    [probe release];
    [renderer release];
}

// ------------------------------------------------------------------------- main

int main(int argc, const char **argv)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    NSString *dir = @"/Users/shg/qtkit-media";
    probeRenderer([dir stringByAppendingPathComponent:@"test.mp4"], 3.0);
    probeRenderer([dir stringByAppendingPathComponent:@"test-720p-main.mp4"], 3.0);

    fprintf(stderr, "\nqtrenderertest: done\n");
    [pool release];
    return 0;
}
