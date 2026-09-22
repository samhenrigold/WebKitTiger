// Headless check of WCSceneCA's read-back path: on a secondary thread, a
// CATransaction, a layer tree under a geometryFlipped host (one tile with a
// CGImage whose top row is red, one solid blue box), CARenderer over a CGL
// pbuffer, glReadPixels reversed into a top-down buffer, written as PNG.
// Expect: red at the top of the tile at (10,10), blue box at (200,50).
#import <Foundation/Foundation.h>
#import <ApplicationServices/ApplicationServices.h>
#undef AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER
#define AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER
#undef AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER_BUT_DEPRECATED
#define AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER_BUT_DEPRECATED
#import <QuartzCore/CoreAnimation.h>
#import <QuartzCore/CARenderer.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

@interface CALayer (TigerCAPrivate)
- (void)setGeometryFlipped:(BOOL)b;
@end

#include <signal.h>
#include <dlfcn.h>
#include <sys/ucontext.h>

static const int W = 400, H = 300;

// Tiger's gdb cannot read this binary's load commands, so symbolize by hand:
// eip and a frame-pointer walk, each through dladdr.
static void where(const char* tag, uintptr_t pc)
{
    Dl_info info;
    if (dladdr((void*)pc, &info) && info.dli_sname)
        fprintf(stderr, "  %s %#lx %s+%#lx (%s)\n", tag, (unsigned long)pc, info.dli_sname, (unsigned long)(pc - (uintptr_t)info.dli_saddr), info.dli_fname ? strrchr(info.dli_fname, '/') + 1 : "?");
    else
        fprintf(stderr, "  %s %#lx ?\n", tag, (unsigned long)pc);
}
static void onCrash(int sig, siginfo_t* si, void* uap)
{
    ucontext_t* uc = (ucontext_t*)uap;
    fprintf(stderr, "CRASH signal %d addr %p\n", sig, si->si_addr);
    where("pc", uc->uc_mcontext->ss.eip);
    uintptr_t* fp = (uintptr_t*)uc->uc_mcontext->ss.ebp;
    for (int i = 0; i < 20 && fp && (uintptr_t)fp > 0x1000; i++) {
        where("fr", fp[1]);
        uintptr_t* next = (uintptr_t*)fp[0];
        if (next <= fp) break;
        fp = next;
    }
    _exit(70);
}

static void* run(void*)
{
    fprintf(stderr, "thread start\n");
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    fprintf(stderr, "pool ok; key ptr %p\n", kCATransactionDisableActions);
    fprintf(stderr, "key %s\n", [kCATransactionDisableActions UTF8String]);
    [CATransaction begin];
    [CATransaction setValue:(id)kCFBooleanTrue forKey:kCATransactionDisableActions];
    fprintf(stderr, "transaction ok\n");

    CALayer *host = [CALayer layer];
    fprintf(stderr, "layer ok %p\n", host);
    [host setAnchorPoint:CGPointMake(0, 0)];
    [host setGeometryFlipped:YES];
    [host setBounds:CGRectMake(0, 0, W, H)];
    fprintf(stderr, "host props ok\n");

    // Tile: 100x100 top-down BGRA, top half red, bottom half green.
    static uint32_t px[100 * 100];
    for (int y = 0; y < 100; y++) for (int x = 0; x < 100; x++) px[y * 100 + x] = y < 50 ? 0xffff0000 : 0xff00ff00;
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef dp = CGDataProviderCreateWithData(NULL, px, sizeof px, NULL);
    CGImageRef img = CGImageCreate(100, 100, 8, 32, 400, cs, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little, dp, NULL, false, kCGRenderingIntentDefault);
    CALayer *tile = [CALayer layer];
    [tile setAnchorPoint:CGPointMake(0, 0)];
    [tile setBounds:CGRectMake(0, 0, 100, 100)];
    [tile setPosition:CGPointMake(10, 10)];
    fprintf(stderr, "tile geometry ok\n");
    [tile setContents:(id)img];
    fprintf(stderr, "tile contents ok\n");
    [host addSublayer:tile];

    CALayer *box = [CALayer layer];
    [box setAnchorPoint:CGPointMake(0, 0)];
    [box setBounds:CGRectMake(0, 0, 80, 60)];
    [box setPosition:CGPointMake(200, 50)];
    CGFloat blue[] = { 0, 0, 1, 1 };
    CGColorRef c = CGColorCreate(cs, blue);
    [box setBackgroundColor:c];
    [host addSublayer:box];
    fprintf(stderr, "box ok\n");
    [CATransaction commit];
    fprintf(stderr, "commit ok\n");
    [CATransaction flush];
    fprintf(stderr, "tree ok\n");

    CGLPixelFormatAttribute attrs[] = { kCGLPFAPBuffer, kCGLPFAAccelerated, kCGLPFANoRecovery,
        kCGLPFAColorSize, (CGLPixelFormatAttribute)32, kCGLPFAAlphaSize, (CGLPixelFormatAttribute)8,
        kCGLPFADepthSize, (CGLPixelFormatAttribute)24, (CGLPixelFormatAttribute)0 };
    CGLPixelFormatObj pf = NULL; long n = 0;
    CGLError e = CGLChoosePixelFormat(attrs, &pf, &n);
    fprintf(stderr, "pixel format %d n=%ld pf=%p\n", e, n, pf);
    if (!pf) exit(2);
    CGLContextObj ctx = NULL;
    e = CGLCreateContext(pf, NULL, &ctx);
    fprintf(stderr, "context %d %p\n", e, ctx);
    if (!ctx) exit(3);
    CGLPBufferObj pb = NULL;
    e = CGLCreatePBuffer(W, H, GL_TEXTURE_RECTANGLE_EXT, GL_RGBA, 0, &pb);
    fprintf(stderr, "pbuffer %d %p\n", e, pb);
    if (!pb) exit(4);
    long screen = 0; CGLGetVirtualScreen(ctx, &screen);
    e = CGLSetPBuffer(ctx, pb, 0, 0, screen);
    fprintf(stderr, "setpbuffer %d screen %ld\n", e, screen);
    CGLSetCurrentContext(ctx);

    CARenderer *r = [CARenderer rendererWithCGLContext:ctx options:nil];
    fprintf(stderr, "renderer %p\n", r);
    if (!r) exit(5);
    [r setLayer:host];
    [r setBounds:CGRectMake(0, 0, W, H)];
    // The render tree only sees the layers after a flush that follows the
    // attachment; a flush before setLayer: renders nothing.
    [CATransaction flush];

    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, W, 0, H, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glClearColor(1, 1, 1, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    [r beginFrameAtTime:CACurrentMediaTime() timeStamp:NULL];
    [r addUpdateRect:CGRectMake(0, 0, W, H)];
    [r render];
    [r endFrame];
    fprintf(stderr, "rendered\n");

    static uint32_t scratch[W * H], out[W * H];
    glReadPixels(0, 0, W, H, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, scratch);
    for (int y = 0; y < H; y++) memcpy(out + y * W, scratch + (H - 1 - y) * W, W * 4);
    fprintf(stderr, "probe (20,20) top of tile = %08x (want ffff0000 red)\n", out[20 * W + 20]);
    fprintf(stderr, "probe (20,90) bottom of tile = %08x (want ff00ff00 green)\n", out[90 * W + 20]);
    fprintf(stderr, "probe (240,80) box = %08x (want ff0000ff blue)\n", out[80 * W + 240]);
    fprintf(stderr, "probe (5,5) bg = %08x (want ffffffff)\n", out[5 * W + 5]);

    CGDataProviderRef odp = CGDataProviderCreateWithData(NULL, out, sizeof out, NULL);
    CGImageRef oimg = CGImageCreate(W, H, 8, 32, W * 4, cs, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little, odp, NULL, false, kCGRenderingIntentDefault);
    CFURLRef url = CFURLCreateWithFileSystemPath(NULL, CFSTR("/tmp/caoffscreen.png"), kCFURLPOSIXPathStyle, false);
    CGImageDestinationRef dest = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, NULL);
    CGImageDestinationAddImage(dest, oimg, NULL);
    CGImageDestinationFinalize(dest);
    fprintf(stderr, "wrote /tmp/caoffscreen.png\n");
    [pool release];
    exit(0);
}

int main()
{
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = onCrash; sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL); sigaction(SIGILL, &sa, NULL);
    if (getenv("CAOFFSCREEN_MAIN"))
        run(NULL);
    pthread_t t;
    pthread_create(&t, NULL, run, NULL);
    // A main run loop, as the GPU process has, while the worker renders. (A timer
    // keeps it alive: an empty run loop returns at once.)
    [NSTimer scheduledTimerWithTimeInterval:1 target:[NSDate class] selector:@selector(date) userInfo:nil repeats:YES];
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:20]];
    fprintf(stderr, "timed out\n");
    return 1;
}
