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

extern int CGSMainConnectionID(void);
extern int CGSAddSurface(int, int, int*);
extern int CGSRemoveSurface(int, int, int);
extern int CGSSetSurfaceBounds(int, int, int, CGRect);
extern int CGSOrderSurface(int, int, int, int, int);
extern int CGSSetWindowProperty(int, int, CFStringRef, CFTypeRef);
extern int CGSSetWindowSharingState(int, int, int);
extern CGLError CGLSetSurface(CGLContextObj, int, int, int);

struct Surface { int window, surface, width, height; };

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

static int renderChild(int fd)
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
        kCGLPFADoubleBuffer, kCGLPFAColorSize, 32, kCGLPFAAlphaSize, 8, 0 };
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
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_RECTANGLE_EXT, texture);
    glTexParameteri(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_RECTANGLE_EXT, 0, GL_RGBA, target.width, target.height, 0,
        GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pixels);
    glViewport(0, 0, target.width, target.height);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0, target.width, 0, target.height, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glEnable(GL_TEXTURE_RECTANGLE_EXT);

    // Finish measurement before the runner's screenshot at 10 s. Capturing the
    // screen can stall Tiger's WindowServer and must not contaminate this sample.
    enum { frameCount = 240 };
    double start = CFAbsoluteTimeGetCurrent(), costs[frameCount], sum = 0;
    int slowestFrame = -1;
    double slowestCost = 0;
    for (int frame = 0; frame < frameCount; ++frame) {
        double due = start + frame / 30.0, now = CFAbsoluteTimeGetCurrent();
        if (due > now)
            usleep((useconds_t)((due - now) * 1e6));
        double before = CFAbsoluteTimeGetCurrent();
        // Upload an entire changing 720p BGRA buffer, as a video texture would.
        int y = frame % target.height;
        memset(pixels + (size_t)y * target.width * 4, 255, target.width * 4);
        glTexSubImage2D(GL_TEXTURE_RECTANGLE_EXT, 0, 0, 0, target.width, target.height,
            GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pixels);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex2f(0, 0);
        glTexCoord2f(target.width, 0); glVertex2f(target.width, 0);
        glTexCoord2f(target.width, target.height); glVertex2f(target.width, target.height);
        glTexCoord2f(0, target.height); glVertex2f(0, target.height);
        glEnd();
        CGLError flushError = CGLFlushDrawable(context);
        if (flushError)
            requireOK("flush", flushError);
        GLenum error = glGetError();
        if (error)
            requireOK("gl-error", error);
        costs[frame] = (CFAbsoluteTimeGetCurrent() - before) * 1000;
        sum += costs[frame];
        if (costs[frame] > slowestCost) {
            slowestCost = costs[frame];
            slowestFrame = frame;
        }
    }
    double elapsed = CFAbsoluteTimeGetCurrent() - start;
    qsort(costs, frameCount, sizeof(double), compareDouble);
    fprintf(stderr, "SURFACE RESULT uploads=%d pixels=1280x720 elapsed=%.3f rate=%.3f work_mean_ms=%.3f work_p95_ms=%.3f work_max_ms=%.3f slowest_frame=%d (flushes, not scanout)\n",
        frameCount, elapsed, frameCount / elapsed, sum / frameCount, costs[frameCount * 95 / 100 - 1], slowestCost, slowestFrame);
    sleep(4); // Keep the rendered surface alive for the later screenshot.
    glDeleteTextures(1, &texture);
    free(pixels);
    CGLSetCurrentContext(NULL);
    CGLDestroyContext(context);
    return 0;
}

int main(int argc, char** argv)
{
    alarm(20);
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    if (argc == 3 && !strcmp(argv[1], "--child")) {
        int result = renderChild(atoi(argv[2]));
        [pool release];
        return result;
    }
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
        execl(argv[0], argv[0], "--child", descriptor, NULL);
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
    struct Surface target = { [window windowNumber], 0, 1280, 720 };
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
