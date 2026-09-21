// CAHost - host a Core Animation layer tree on screen on Mac OS X 10.4.11.
//
// Tiger has no layer-backed NSViews and its WindowServer speaks no CA protocol,
// so the only path is CARenderer over the CGL context of an NSOpenGLView, driven
// by hand: [CATransaction flush] + beginFrame/addUpdateRect/render/endFrame.
// This is the shape WebKitLegacy's compositing host will take.
//
// Build: see Makefile. MRR, fragile ObjC runtime.

#import <Cocoa/Cocoa.h>

// The Leopard CA headers mark the whole API 10.5+, and we deploy at 10.4.
// The binary is a Darwin 8 build, so the availability annotation is simply
// wrong for us; neuter it. WebKitLegacy will need the same thing.
#include <AvailabilityMacros.h>
#undef AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER
#define AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER
#undef AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER_BUT_DEPRECATED
#define AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER_BUT_DEPRECATED

#import <QuartzCore/CoreAnimation.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>

// ---------------------------------------------------------------- helpers

static CGColorRef makeColor(float r, float g, float b, float a)
{
    static CGColorSpaceRef cs;
    if (!cs)
        cs = CGColorSpaceCreateDeviceRGB();
    float c[4] = { r, g, b, a };
    return CGColorCreate(cs, c);
}

// A 64x64 checkerboard, so we can see whether -contents (a CGImage) composites.
static CGImageRef makeCheckerImage(void)
{
    const size_t n = 64;
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef bmp = CGBitmapContextCreate(NULL, n, n, 8, n * 4, cs,
                                             kCGImageAlphaPremultipliedFirst);
    CGColorSpaceRelease(cs);
    if (!bmp)
        return NULL;
    for (size_t y = 0; y < 8; y++) {
        for (size_t x = 0; x < 8; x++) {
            int on = ((x + y) & 1);
            CGContextSetRGBFillColor(bmp, on ? 1.0 : 0.15, on ? 0.55 : 0.15,
                                     on ? 0.1 : 0.35, 1.0);
            CGContextFillRect(bmp, CGRectMake(x * 8, y * 8, 8, 8));
        }
    }
    CGImageRef img = CGBitmapContextCreateImage(bmp);
    CGContextRelease(bmp);
    return img;
}

// ---------------------------------------------------------------- the view

@interface CAHostView : NSOpenGLView
{
    CARenderer *_renderer;
    CALayer *_root;
    CALayer *_spinner;
    NSTimer *_timer;
    double _accum;          // seconds of render time accumulated
    unsigned _frames;
    unsigned _totalFrames;
    double _totalAccum;
    double _swapAccum;
}
- (void)buildLayerTree;
- (void)drawFrame;
@end

@implementation CAHostView

- (void)dealloc
{
    [_timer invalidate];
    [_renderer release];
    [_root release];
    [_spinner release];
    [super dealloc];
}

- (BOOL)isOpaque { return YES; }

- (void)prepareOpenGL
{
    [super prepareOpenGL];
    GLint swap = 1;
    CGLContextObj cgl = (CGLContextObj)[[self openGLContext] CGLContextObj];
    CGLSetParameter(cgl, kCGLCPSwapInterval, &swap);
    CGLSetCurrentContext(cgl);

    _renderer = [[CARenderer rendererWithCGLContext:cgl options:nil] retain];
    if (!_renderer) {
        fprintf(stderr, "FATAL: no CARenderer\n");
        exit(1);
    }
    fprintf(stderr, "GL_RENDERER = %s\n", glGetString(GL_RENDERER));

    [self buildLayerTree];
    [_renderer setLayer:_root];
    [self reshape];

    // Common modes so animation keeps running while the window is resized.
    _timer = [NSTimer timerWithTimeInterval:1.0 / 60.0 target:self
                                   selector:@selector(drawFrame)
                                   userInfo:nil repeats:YES];
    [[NSRunLoop currentRunLoop] addTimer:_timer
                                 forMode:(NSString *)kCFRunLoopCommonModes];
}

- (void)buildLayerTree
{
    NSRect b = [self bounds];

    _root = [[CALayer layer] retain];
    [_root setBounds:CGRectMake(0, 0, b.size.width, b.size.height)];
    [_root setAnchorPoint:CGPointMake(0, 0)];
    [_root setPosition:CGPointMake(0, 0)];
    CGColorRef bg = makeColor(0.09, 0.10, 0.13, 1);
    [_root setBackgroundColor:bg];
    CGColorRelease(bg);

    // 1. plain solid-colour sublayer with a border and rounded corners
    CALayer *box = [CALayer layer];
    [box setBounds:CGRectMake(0, 0, 160, 120)];
    [box setPosition:CGPointMake(140, 260)];
    CGColorRef c = makeColor(0.20, 0.45, 0.95, 1);
    [box setBackgroundColor:c];
    CGColorRelease(c);
    c = makeColor(1, 1, 1, 0.8);
    [box setBorderColor:c];
    CGColorRelease(c);
    [box setBorderWidth:3];
    [box setCornerRadius:16];
    [_root addSublayer:box];

    // 2. CGImage contents
    CALayer *img = [CALayer layer];
    [img setBounds:CGRectMake(0, 0, 128, 128)];
    [img setPosition:CGPointMake(340, 260)];
    CGImageRef checker = makeCheckerImage();
    if (checker) {
        [img setContents:(id)checker];
        CGImageRelease(checker);
    }
    [img setMagnificationFilter:kCAFilterNearest];
    [_root addSublayer:img];

    // 3. CATextLayer
    CATextLayer *text = [CATextLayer layer];
    [text setBounds:CGRectMake(0, 0, 420, 40)];
    [text setPosition:CGPointMake(240, 100)];
    [text setString:@"Core Animation on Mac OS X 10.4.11"];
    [text setFontSize:22];
    c = makeColor(1, 0.95, 0.6, 1);
    [text setForegroundColor:c];
    CGColorRelease(c);
    [text setAlignmentMode:kCAAlignmentCenter];
    [_root addSublayer:text];

    // 4. the layer we spin in 3D, with a repeating opacity pulse
    _spinner = [[CALayer layer] retain];
    [_spinner setBounds:CGRectMake(0, 0, 120, 120)];
    [_spinner setPosition:CGPointMake(500, 260)];
    c = makeColor(0.95, 0.25, 0.35, 1);
    [_spinner setBackgroundColor:c];
    CGColorRelease(c);
    [_spinner setCornerRadius:12];
    [_root addSublayer:_spinner];

    // repeating position animation on the blue box
    CABasicAnimation *move = [CABasicAnimation animationWithKeyPath:@"position.y"];
    [move setFromValue:[NSNumber numberWithDouble:260]];
    [move setToValue:[NSNumber numberWithDouble:340]];
    [move setDuration:1.0];
    [move setAutoreverses:YES];
    [move setRepeatCount:1e9f];
    [box addAnimation:move forKey:@"bob"];

    // repeating opacity pulse on the image layer
    CABasicAnimation *fade = [CABasicAnimation animationWithKeyPath:@"opacity"];
    [fade setFromValue:[NSNumber numberWithFloat:1.0f]];
    [fade setToValue:[NSNumber numberWithFloat:0.2f]];
    [fade setDuration:0.8];
    [fade setAutoreverses:YES];
    [fade setRepeatCount:1e9f];
    [img addAnimation:fade forKey:@"pulse"];
}

- (void)reshape
{
    NSRect b = [self bounds];
    CGLContextObj cgl = (CGLContextObj)[[self openGLContext] CGLContextObj];
    CGLSetCurrentContext(cgl);
    glViewport(0, 0, (GLsizei)b.size.width, (GLsizei)b.size.height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, b.size.width, 0, b.size.height, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    CGRect r = CGRectMake(0, 0, b.size.width, b.size.height);
    // No implicit animation on a resize.
    [CATransaction begin];
    [CATransaction setValue:(id)kCFBooleanTrue forKey:kCATransactionDisableActions];
    [_root setBounds:r];
    [CATransaction commit];
    [_renderer setBounds:r];
}

- (void)drawFrame
{
    NSRect b = [self bounds];
    CGLContextObj cgl = (CGLContextObj)[[self openGLContext] CGLContextObj];
    CGLSetCurrentContext(cgl);

    CFTimeInterval now = CACurrentMediaTime();

    // 3D rotation driven directly (not by an animation) so we can also prove
    // CATransform3D math works.
    CATransform3D t = CATransform3DMakeRotation(now * 1.5, 0.3, 1.0, 0.15);
    t.m34 = -1.0 / 600.0;
    [_spinner setTransform:t];

    // CA normally commits from a run-loop observer; headless of AppKit's layer
    // support we must flush by hand or the render tree never updates.
    [CATransaction flush];

    CFTimeInterval t0 = CACurrentMediaTime();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    [_renderer beginFrameAtTime:now timeStamp:NULL];
    [_renderer addUpdateRect:CGRectMake(0, 0, b.size.width, b.size.height)];
    [_renderer render];
    [_renderer endFrame];
    CFTimeInterval t1 = CACurrentMediaTime();
    [[self openGLContext] flushBuffer];
    CFTimeInterval t2 = CACurrentMediaTime();
    _accum += t1 - t0;
    _swapAccum += t2 - t1;
    _frames++;
    _totalAccum += t1 - t0;
    _totalFrames++;

    if (_frames >= 60) {
        fprintf(stderr, "CA render %.2f ms + swap %.2f ms avg over %u frames"
                " (%.2f ms render lifetime avg)\n",
                _accum * 1000.0 / _frames, _swapAccum * 1000.0 / _frames, _frames,
                _totalAccum * 1000.0 / _totalFrames);
        _accum = 0;
        _swapAccum = 0;
        _frames = 0;
    }
}

- (void)drawRect:(NSRect)r { [self drawFrame]; }

@end

// ---------------------------------------------------------------- app

@interface CAHostDelegate : NSObject
@end
@implementation CAHostDelegate
+ (void)resizeWindow:(NSTimer *)t
{
    NSWindow *w = [t userInfo];
    [w setContentSize:NSMakeSize(900, 600)];
    fprintf(stderr, "resized to 900x600\n");
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)a { return YES; }
@end

int main(int argc, const char **argv)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    NSApplication *app = [NSApplication sharedApplication];

    // Minimal menu bar; Tiger has no setActivationPolicy:, the bundle Info.plist
    // is what makes this a foreground app.
    NSMenu *menubar = [[[NSMenu alloc] init] autorelease];
    NSMenuItem *appItem = [[[NSMenuItem alloc] init] autorelease];
    [menubar addItem:appItem];
    NSMenu *appMenu = [[[NSMenu alloc] init] autorelease];
    [appMenu addItemWithTitle:@"Quit CAHost" action:@selector(terminate:) keyEquivalent:@"q"];
    [appItem setSubmenu:appMenu];
    [app setMainMenu:menubar];

    NSOpenGLPixelFormatAttribute attrs[] = {
        NSOpenGLPFADoubleBuffer,
        NSOpenGLPFAAccelerated,
        NSOpenGLPFAColorSize, 32,
        NSOpenGLPFADepthSize, 24,
        0
    };
    NSOpenGLPixelFormat *pf =
        [[[NSOpenGLPixelFormat alloc] initWithAttributes:attrs] autorelease];
    if (!pf) {
        fprintf(stderr, "FATAL: no pixel format\n");
        return 1;
    }

    NSRect frame = NSMakeRect(0, 0, 640, 420);
    NSWindow *win = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:(NSTitledWindowMask | NSClosableWindowMask |
                             NSMiniaturizableWindowMask | NSResizableWindowMask)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    [win setTitle:@"CAHost - CARenderer on Tiger"];
    CAHostView *view = [[[CAHostView alloc] initWithFrame:frame
                                              pixelFormat:pf] autorelease];
    [win setContentView:view];
    [win center];
    [win makeKeyAndOrderFront:nil];

    // Check: CAHOST_RESIZE=1 resizes the window after 3s, exercising -reshape
    // (layer bounds + renderer bounds + glViewport/glOrtho) the same way a user
    // drag does. Without the reshape path the tree renders clipped or stretched.
    if (getenv("CAHOST_RESIZE"))
        [NSTimer scheduledTimerWithTimeInterval:3.0
                                         target:[CAHostDelegate class]
                                       selector:@selector(resizeWindow:)
                                       userInfo:win repeats:NO];

    [app setDelegate:[[[CAHostDelegate alloc] init] autorelease]];
    [app activateIgnoringOtherApps:YES];
    [app run];
    [pool release];
    return 0;
}
