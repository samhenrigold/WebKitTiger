/* Tiger-only experiment: render from a separate 32-bit process into an owned
 * Cocoa window. Private ABI and SecondaryOwner protocol were checked against
 * Tiger 10.4.11 libXplugin's xp_export_surface / xp_attach_gl_context. This is
 * not an engine implementation or a video playback benchmark. */
#import <Cocoa/Cocoa.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>
#import <OpenGL/glext.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dlfcn.h>

#if SURFACE_PROBE_CA
#undef AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER
#define AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER
#undef AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER_BUT_DEPRECATED
#define AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER_BUT_DEPRECATED
#import <QuartzCore/CoreAnimation.h>
#import <QuartzCore/CARenderer.h>
#include "PixelBufferPool.h"

@interface CALayer (TigerCAPrivate)
- (void)setGeometryFlipped:(BOOL)flipped;
@end
#endif

extern int CGSMainConnectionID(void);
extern int CGSAddSurface(int, int, int*);
extern int CGSRemoveSurface(int, int, int);
extern int CGSSetSurfaceBounds(int, int, int, CGRect);
extern int CGSOrderSurface(int, int, int, int, int);
extern int CGSSetWindowProperty(int, int, CFStringRef, CFTypeRef);
extern int CGSSetWindowSharingState(int, int, int);
extern CGLError CGLSetSurface(CGLContextObj, int, int, int);

struct Surface {
    int window, surface, width, height;
    double programStart, measurementDeadline;
};

static void requireOK(const char* operation, int error)
{
    fprintf(stderr, "SURFACE %s=%d\n", operation, error);
    if (error)
        exit(1);
}

static void transfer(int fd, void* bytes, size_t count, int writing)
{
    while (count) {
        ssize_t n = writing ? write(fd, bytes, count) : read(fd, bytes, count);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            fprintf(stderr, "SURFACE protocol failed: %s\n", strerror(errno));
            exit(2);
        }
        bytes = (char*)bytes + n;
        count -= n;
    }
}

static int compareDouble(const void* a, const void* b)
{
    double x = *(const double*)a, y = *(const double*)b;
    return (x > y) - (x < y);
}

struct FrameTiming {
    double allocation, copy, image, transaction, render, flush, total;
};

#if SURFACE_PROBE_CA
static CALayer* createLayer(void)
{
    CALayer* layer = [[CALayer alloc] init];
    NSDictionary* actions = [NSDictionary dictionaryWithObjectsAndKeys:
        [NSNull null], @"contents", [NSNull null], @"position",
        [NSNull null], @"bounds", [NSNull null], @"sublayers",
        [NSNull null], @"backgroundColor", [NSNull null], @"onOrderIn",
        [NSNull null], @"onOrderOut", nil];
    [layer setActions:actions];
    [layer setAnchorPoint:CGPointMake(0, 0)];
    return layer;
}

// Match WCSceneCA's owned snapshot and BGRA CGImage representation. CA is free
// to retain the image: it never references the mutable producer pixels below.
static CGImageRef copyFrameImage(const unsigned char* pixels, const struct Surface* target, CGColorSpaceRef colorSpace,
    struct PixelBufferPool* pool, struct FrameTiming* timing)
{
    size_t bytes = (size_t)target->width * target->height * 4;
    double start = CFAbsoluteTimeGetCurrent();
    void* info = NULL;
    void* owned = pool ? pixelPoolAcquire(pool, bytes, &info) : malloc(bytes);
    double allocated = CFAbsoluteTimeGetCurrent();
    if (!owned)
        return NULL;
    memcpy(owned, pixels, bytes);
    double copied = CFAbsoluteTimeGetCurrent();
    CGDataProviderRef provider = CGDataProviderCreateWithData(info, owned, bytes, pixelPoolRelease);
    if (!provider) {
        pixelPoolRelease(info, owned, bytes);
        return NULL;
    }
    CGImageRef image = CGImageCreate(target->width, target->height, 8, 32, target->width * 4,
        colorSpace, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little,
        provider, NULL, false, kCGRenderingIntentDefault);
    CGDataProviderRelease(provider);
    timing->allocation = (allocated - start) * 1000;
    timing->copy = (copied - allocated) * 1000;
    timing->image = (CFAbsoluteTimeGetCurrent() - copied) * 1000;
    return image;
}
#endif

static int renderChild(int fd, int useCA, int usePool)
{
    int connection = CGSMainConnectionID();
    if (!connection)
        return 2;
    transfer(fd, &connection, sizeof(connection), 1);
    struct Surface target;
    transfer(fd, &target, sizeof(target), 0);
    close(fd);
    if (target.width != 1280 || target.height != 720 || target.window <= 0 || target.surface <= 0)
        return 2;
    CGLPixelFormatAttribute attributes[] = { kCGLPFAAccelerated, kCGLPFANoRecovery,
        kCGLPFADoubleBuffer, kCGLPFAColorSize, 32, kCGLPFAAlphaSize, 8,
        kCGLPFADepthSize, useCA ? 24 : 0, 0 };
    CGLPixelFormatObj format = NULL;
    CGLContextObj context = NULL;
    GLint count = 0;
    requireOK("choose-format", CGLChoosePixelFormat(attributes, &format, &count));
    if (!format || !count)
        return 2;
    requireOK("create-context", CGLCreateContext(format, NULL, &context));
    CGLDestroyPixelFormat(format);
    requireOK("attach-other-process-surface", CGLSetSurface(context, connection, target.window, target.surface));
    requireOK("current-context", CGLSetCurrentContext(context));
    GLint interval = 1;
    requireOK("swap-interval", CGLSetParameter(context, kCGLCPSwapInterval, &interval));
    fprintf(stderr, "SURFACE renderer=%s\n", glGetString(GL_RENDERER));
    fprintf(stderr, "SURFACE mode=%s\n", useCA ? "ca" : "gl");
    fprintf(stderr, "SURFACE frame_storage=%s\n", usePool ? "pool" : "malloc");
    NSAutoreleasePool* renderPool = [[NSAutoreleasePool alloc] init];

    size_t bytes = (size_t)target.width * target.height * 4;
    unsigned char* pixels = malloc(bytes);
    if (!pixels)
        return 2;
    for (int y = 0; y < target.height; ++y) {
        for (int x = 0; x < target.width; ++x) {
            size_t p = ((size_t)y * target.width + x) * 4;
            pixels[p] = (x / 32 & 1) ? 224 : 48;
            pixels[p + 1] = (y / 32 & 1) ? 200 : 64;
            pixels[p + 2] = 80;
            pixels[p + 3] = 255;
        }
    }
    GLuint texture = 0;
#if SURFACE_PROBE_CA
    CARenderer* renderer = nil;
    CALayer *host = nil, *video = nil, *overlay = nil;
    CGColorSpaceRef colorSpace = NULL;
    struct PixelBufferPool* pixelPool = usePool ? pixelPoolCreate() : NULL;
    if (usePool && !pixelPool)
        return 2;
    if (useCA) {
        Dl_info framework;
        if (!dladdr((void*)CACurrentMediaTime, &framework) || !framework.dli_fname)
            return 2;
        fprintf(stderr, "SURFACE quartzcore_path=%s\n", framework.dli_fname);
        renderer = [[CARenderer rendererWithCGLContext:context options:nil] retain];
        if (!renderer)
            return 2;
        colorSpace = CGColorSpaceCreateDeviceRGB();
        if (!colorSpace)
            return 2;
        [CATransaction begin];
        [CATransaction setValue:(id)kCFBooleanTrue forKey:kCATransactionDisableActions];
        host = createLayer();
        [host setGeometryFlipped:YES];
        [host setBounds:CGRectMake(0, 0, target.width, target.height)];
        video = createLayer();
        [video setBounds:CGRectMake(0, 0, target.width, target.height)];
        [host addSublayer:video];
        overlay = createLayer();
        [overlay setPosition:CGPointMake(40, 40)];
        [overlay setBounds:CGRectMake(0, 0, 200, 72)];
        CGFloat pink[] = { 1, 0, 1, 1 };
        CGColorRef color = CGColorCreate(colorSpace, pink);
        [overlay setBackgroundColor:color];
        CGColorRelease(color);
        [host addSublayer:overlay];
        [renderer setLayer:host];
        [renderer setBounds:CGRectMake(0, 0, target.width, target.height)];
        [CATransaction commit];
        [CATransaction flush];
        fprintf(stderr, "SURFACE overlay=opaque-magenta rect=40,40,200,72 above=fresh-video-image\n");
    } else
#endif
    {
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_RECTANGLE_EXT, texture);
        glTexParameteri(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_RECTANGLE_EXT, 0, GL_RGBA, target.width, target.height, 0,
            GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pixels);
        glEnable(GL_TEXTURE_RECTANGLE_EXT);
    }
    glViewport(0, 0, target.width, target.height);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0, target.width, 0, target.height, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();

    // Finish measurement before the runner's screenshot at 10 s. Capturing the
    // screen can stall Tiger's WindowServer and must not contaminate this sample.
    enum { frameCount = 240 };
    double start = CFAbsoluteTimeGetCurrent(), costs[frameCount], sum = 0;
    double copySum = 0, renderSum = 0, flushSum = 0;
    struct FrameTiming timings[frameCount] = { { 0 } };
    int completed = 0;
    int slowestFrame = -1;
    double slowestCost = 0;
    for (int frame = 0; frame < frameCount; ++frame) {
        double due = start + frame / 30.0, now = CFAbsoluteTimeGetCurrent();
        if (due > now)
            usleep((useconds_t)((due - now) * 1e6));
        if (CFAbsoluteTimeGetCurrent() >= target.measurementDeadline)
            break;
        double before = CFAbsoluteTimeGetCurrent();
        NSAutoreleasePool* framePool = [[NSAutoreleasePool alloc] init];
        // Upload an entire changing 720p BGRA buffer, as a video texture would.
        int y = frame % target.height;
        memset(pixels + (size_t)y * target.width * 4, 255, target.width * 4);
        double copied = before;
#if SURFACE_PROBE_CA
        if (useCA) {
            CGImageRef image = copyFrameImage(pixels, &target, colorSpace, pixelPool, &timings[frame]);
            if (!image)
                return 2;
            copied = CFAbsoluteTimeGetCurrent();
            [CATransaction begin];
            [CATransaction setValue:(id)kCFBooleanTrue forKey:kCATransactionDisableActions];
            [video setContents:(id)image];
            CGImageRelease(image);
            [CATransaction commit];
            [CATransaction flush];
            double committed = CFAbsoluteTimeGetCurrent();
            timings[frame].transaction = (committed - copied) * 1000;
            // Unlike the persistent pbuffer, swapped drawable contents are not
            // assumed preserved: redraw the entire scene on every frame.
            glViewport(0, 0, target.width, target.height);
            glMatrixMode(GL_PROJECTION); glLoadIdentity();
            glOrtho(0, target.width, 0, target.height, -1, 1);
            glMatrixMode(GL_MODELVIEW); glLoadIdentity();
            glDisable(GL_SCISSOR_TEST);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            [renderer beginFrameAtTime:CACurrentMediaTime() timeStamp:NULL];
            [renderer addUpdateRect:CGRectMake(0, 0, target.width, target.height)];
            [renderer render];
            [renderer endFrame];
            timings[frame].render = (CFAbsoluteTimeGetCurrent() - committed) * 1000;
        } else
#endif
        {
            glTexSubImage2D(GL_TEXTURE_RECTANGLE_EXT, 0, 0, 0, target.width, target.height,
                GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pixels);
            glBegin(GL_QUADS);
            glTexCoord2f(0, 0); glVertex2f(0, 0);
            glTexCoord2f(target.width, 0); glVertex2f(target.width, 0);
            glTexCoord2f(target.width, target.height); glVertex2f(target.width, target.height);
            glTexCoord2f(0, target.height); glVertex2f(0, target.height);
            glEnd();
        }
        double rendered = CFAbsoluteTimeGetCurrent();
        CGLError flushError = CGLFlushDrawable(context);
        if (flushError)
            requireOK("flush", flushError);
        GLenum error = glGetError();
        if (error)
            requireOK("gl-error", error);
        double flushed = CFAbsoluteTimeGetCurrent();
        [framePool release];
        costs[frame] = (CFAbsoluteTimeGetCurrent() - before) * 1000;
        timings[frame].flush = (flushed - rendered) * 1000;
        timings[frame].total = costs[frame];
        sum += costs[frame];
        copySum += (copied - before) * 1000;
        renderSum += (rendered - copied) * 1000;
        flushSum += (flushed - rendered) * 1000;
        ++completed;
        if (costs[frame] > slowestCost) {
            slowestCost = costs[frame];
            slowestFrame = frame;
        }
    }
    double elapsed = CFAbsoluteTimeGetCurrent() - start;
    if (!completed)
        return 2;
    qsort(costs, completed, sizeof(double), compareDouble);
    int percentile = (completed * 95 + 99) / 100 - 1;
    fprintf(stderr, "SURFACE RESULT mode=%s uploads=%d pixels=1280x720 elapsed=%.3f rate=%.3f work_mean_ms=%.3f work_p95_ms=%.3f work_max_ms=%.3f slowest_frame=%d copy_mean_ms=%.3f render_mean_ms=%.3f flush_mean_ms=%.3f completed_at=%.3f deadline=%.3f (flushes, not scanout)\n",
        useCA ? "ca" : "gl", completed, elapsed, completed / elapsed, sum / completed, costs[percentile], slowestCost, slowestFrame,
        copySum / completed, renderSum / completed, flushSum / completed,
        CFAbsoluteTimeGetCurrent() - target.programStart, target.measurementDeadline - target.programStart);
    // Emit only after measurement; file logging cannot affect the frame timings.
    if (useCA) {
        for (int frame = 0; frame < completed; ++frame) {
            struct FrameTiming* t = &timings[frame];
            fprintf(stderr, "SURFACE FRAME id=%d allocation_ms=%.3f memcpy_ms=%.3f image_ms=%.3f transaction_ms=%.3f render_ms=%.3f flush_ms=%.3f total_ms=%.3f\n",
                frame, t->allocation, t->copy, t->image, t->transaction, t->render, t->flush, t->total);
        }
    }
#if SURFACE_PROBE_CA
    if (pixelPool) {
        struct PixelBufferPoolStats stats = pixelPoolStats(pixelPool);
        fprintf(stderr, "SURFACE POOL allocations=%u reuses=%u fallbacks=%u in_use=%u peak_in_use=%u slots=%d\n",
            stats.allocations, stats.reuses, stats.fallbacks, stats.inUse, stats.peakInUse, PIXEL_POOL_SLOTS);
    }
#endif
    sleep(4); // Keep the rendered surface alive for the later screenshot.
    if (texture)
        glDeleteTextures(1, &texture);
#if SURFACE_PROBE_CA
    if (useCA) {
        [renderer setLayer:nil];
        [video setContents:nil];
        [CATransaction flush];
        [overlay release];
        [video release];
        [host release];
        [renderer release];
        CGColorSpaceRelease(colorSpace);
    }
#endif
    free(pixels);
    [renderPool release];
#if SURFACE_PROBE_CA
    if (pixelPool)
        pixelPoolClose(pixelPool);
#endif
    requireOK("detach-surface", CGLClearDrawable(context));
    CGLSetCurrentContext(NULL);
    CGLDestroyContext(context);
    return completed == frameCount ? 0 : 3;
}

int main(int argc, char** argv)
{
    alarm(20);
    double programStart = CFAbsoluteTimeGetCurrent();
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    if (argc == 4 && !strcmp(argv[1], "--child")) {
        int result = renderChild(atoi(argv[2]), strcmp(argv[3], "gl") != 0, !strcmp(argv[3], "ca-pool"));
        [pool release];
        return result;
    }
    int usePool = argc == 2 && !strcmp(argv[1], "--ca-pool");
    int useCA = usePool || (argc == 2 && !strcmp(argv[1], "--ca"));
    if (argc != 1 && !useCA)
        return 2;
#if !SURFACE_PROBE_CA
    if (useCA)
        return 2;
#endif
    int channel[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, channel))
        return 2;
    pid_t child = fork();
    if (child < 0)
        return 2;
    if (!child) {
        close(channel[0]);
        char descriptor[20];
        snprintf(descriptor, sizeof(descriptor), "%d", channel[1]);
        execl(argv[0], argv[0], "--child", descriptor, usePool ? "ca-pool" : (useCA ? "ca" : "gl"), NULL);
        _exit(2);
    }
    close(channel[1]);
    [NSApplication sharedApplication];
    NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(100, 100, 1280, 720)
        styleMask:NSTitledWindowMask backing:NSBackingStoreBuffered defer:NO];
    [window setTitle:@"Tiger cross-process GPU surface probe"];
    [window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    [window display];
    int connection = CGSMainConnectionID(), childConnection = 0;
    transfer(channel[0], &childConnection, sizeof(childConnection), 0);
    struct Surface target = { [window windowNumber], 0, 1280, 720, programStart, programStart + 8.5 };
    requireOK("add-owned-surface", CGSAddSurface(connection, target.window, &target.surface));
    requireOK("surface-bounds", CGSSetSurfaceBounds(connection, target.window, target.surface, CGRectMake(0, 22, 1280, 720)));
    requireOK("order-surface", CGSOrderSurface(connection, target.window, target.surface, 1, 0));
    CFNumberRef secondaryOwner = CFNumberCreate(NULL, kCFNumberIntType, &childConnection);
    requireOK("secondary-owner", CGSSetWindowProperty(connection, target.window, CFSTR("SecondaryOwner"), secondaryOwner));
    CFRelease(secondaryOwner);
    requireOK("share-owned-window", CGSSetWindowSharingState(connection, target.window, 2));
    fprintf(stderr, "SURFACE parent=%d child=%d window=%d surface=%d\n", connection, childConnection, target.window, target.surface);
    transfer(channel[0], &target, sizeof(target), 1);
    close(channel[0]);
    double end = CFAbsoluteTimeGetCurrent() + 14;
    while (CFAbsoluteTimeGetCurrent() < end) {
        NSEvent* event = [NSApp nextEventMatchingMask:NSAnyEventMask untilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]
            inMode:NSDefaultRunLoopMode dequeue:YES];
        if (event)
            [NSApp sendEvent:event];
        [NSApp updateWindows];
    }
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited != child) {
        fprintf(stderr, "SURFACE waitpid failed: %s\n", strerror(errno));
        return 1;
    }
    requireOK("remove-owned-surface", CGSRemoveSurface(connection, target.window, target.surface));
    [window orderOut:nil];
    [window release];
    [pool release];
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
