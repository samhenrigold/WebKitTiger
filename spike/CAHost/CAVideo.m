// CAVideo - QuickTime's OpenGL visual context feeding the CARenderer host.
//
// spike/qtrenderertest.m established that QTVideoRendererWebKitOnly decodes
// 320x240 in real time but only manages ~2 fps at 720p, because that path pulls
// every frame through CoreGraphics on the CPU. QuickTime's accelerated route is
// a visual context: QTOpenGLTextureContextCreate over our own CGL context hands
// back CVOpenGLTextures that the decoder has already put on the GPU.
//
// This program is the CAHost GL host with that pipeline underneath: the video
// quad is drawn straight into the GL view, then CARenderer composites a small
// layer tree on top, proving the two can share one context and one frame.
//
// Build: make -f Makefile CAVideo. MRR, fragile ObjC runtime.
//
//   CAVIDEO_MEDIA=/path/to.mp4   clip to play (default /Users/shg/qtkit-media/test.mp4)
//   CAVIDEO_OPENPLAYBACK=1       pass QTMovieOpenForPlaybackAttribute when opening
//   CAVIDEO_SECONDS=5            measurement window, then print a summary

// QTKit.h pulls in QuartzCore, so this has to come before every other import:
// the Leopard CA headers mark the whole API 10.5+ and we deploy at 10.4.
#include <AvailabilityMacros.h>
#undef AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER
#define AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER
#undef AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER_BUT_DEPRECATED
#define AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER_BUT_DEPRECATED

#import <Cocoa/Cocoa.h>
#import <QuickTime/QuickTime.h>


#ifndef CAVIDEO_NO_CA
#import <QuartzCore/CoreAnimation.h>
#endif
#import <CoreVideo/CoreVideo.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>
#import <OpenGL/glext.h>
#include <dlfcn.h>

// QuickTime 7.6.4 exports all of this (162 QTVisualContext* symbols are in the
// 10.4u SDK stub too, so it links), but the 10.4u SDK declares none of it: there
// is no QTVisualContext.h in either the 10.4u or the 10.5 SDK. Declare it here.
typedef struct OpaqueQTVisualContext *QTVisualContextRef;

extern OSStatus QTOpenGLTextureContextCreate(CFAllocatorRef allocator,
                                             CGLContextObj cglContext,
                                             CGLPixelFormatObj cglPixelFormat,
                                             CFDictionaryRef attributes,
                                             QTVisualContextRef *newContext);
extern Boolean QTVisualContextIsNewImageAvailable(QTVisualContextRef ctx,
                                                  const CVTimeStamp *ts);
extern OSStatus QTVisualContextCopyImageForTime(QTVisualContextRef ctx,
                                                CFAllocatorRef allocator,
                                                const CVTimeStamp *ts,
                                                CVImageBufferRef *newImage);
extern void QTVisualContextTask(QTVisualContextRef ctx);
extern void QTVisualContextRelease(QTVisualContextRef ctx);

// QuickTime 7's own entry points. Exported by the shipping framework and by the
// 10.4u SDK stub, but the SDK's Movies.h is QuickTime 6 vintage and declares
// neither, so declare them here. Opening the movie this way keeps QTKit out of
// the process entirely, which is the point: QTKit's movie setup reaches for
// +[CIFilter filterWithName:], and the Apple TV QuartzCore in this process has
// already claimed the CIFilter class name with a build that has no such method.
extern OSStatus SetMovieVisualContext(Movie movie, QTVisualContextRef ctx);


extern OSStatus NewMovieFromProperties(ItemCount inputPropertyCount,
                                       QTNewMoviePropertyElement *inputProperties,
                                       ItemCount outputPropertyCount,
                                       QTNewMoviePropertyElement *outputProperties,
                                       Movie *newMovie);


static double nowSeconds(void) { return (double)CFAbsoluteTimeGetCurrent(); }

// ---------------------------------------------------------------- the view

@interface CAVideoView : NSOpenGLView
{
#ifndef CAVIDEO_NO_CA
    CARenderer *_renderer;
    CALayer *_root;
    CALayer *_badge;
#endif
    Movie _movie;
    QTVisualContextRef _visualContext;
    CVOpenGLTextureRef _currentFrame;
    NSTimer *_timer;

    NSString *_moviePath;
    NSSize _movieSize;
    double _started;
    BOOL _summarised;

    unsigned _newImages, _newImagesTotal;
    unsigned _frames;
    double _quadAccum;          // GPU time for the video quad
    double _caAccum;            // CARenderer time
    double _windowStart;
}
- (void)setMoviePath:(NSString *)path;
- (void)drawFrame;
@end

@implementation CAVideoView

- (void)setMoviePath:(NSString *)path { _moviePath = [path copy]; }
- (BOOL)isOpaque { return YES; }

// ---- QuickTime

- (BOOL)openMovieAtPath:(NSString *)path
{
    EnterMovies();

    CFStringRef cfPath = (CFStringRef)path;
    Boolean active = true;
    QTNewMoviePropertyElement props[3];
    ItemCount n = 0;

    props[n].propClass = kQTPropertyClass_DataLocation;
    props[n].propID = kQTDataLocationPropertyID_CFStringNativePath;
    props[n].propValueSize = sizeof(cfPath);
    props[n].propValueAddress = &cfPath;
    props[n].propStatus = 0;
    n++;

    props[n].propClass = kQTPropertyClass_NewMovieProperty;
    props[n].propID = kQTNewMoviePropertyID_Active;
    props[n].propValueSize = sizeof(active);
    props[n].propValueAddress = &active;
    props[n].propStatus = 0;
    n++;

    // kQTContextPropertyID_VisualContext as a creation property returns paramErr
    // (-50) on QuickTime 7.6.4; attach the context after the movie exists.
    OSStatus err = NewMovieFromProperties(n, props, 0, NULL, &_movie);
    if (err != noErr || !_movie) {
        fprintf(stderr, "FATAL: NewMovieFromProperties err=%d\n", (int)err);
        return NO;
    }

    Rect box;
    GetMovieBox(_movie, &box);
    _movieSize = NSMakeSize(box.right - box.left, box.bottom - box.top);
    OSStatus serr = SetMovieVisualContext(_movie, _visualContext);
    fprintf(stderr, "movie: %s, box %.0fx%.0f, SetMovieVisualContext err=%d\n",
            [[path lastPathComponent] UTF8String],
            _movieSize.width, _movieSize.height, (int)serr);
    return serr == noErr;
}

- (BOOL)attachVisualContext
{
    CGLContextObj cgl = (CGLContextObj)[[self openGLContext] CGLContextObj];
    CGLPixelFormatObj pf = (CGLPixelFormatObj)[[self pixelFormat] CGLPixelFormatObj];

    CFDictionaryRef ctxAttrs = NULL;
    if (getenv("CAVIDEO_HWDECODE")) {
        // In the shipping QuickTime but not in the 10.4u SDK stub, so dlsym it.
        CFStringRef *keyp = (CFStringRef *)dlsym(RTLD_DEFAULT,
                                "kQTVisualContextAllowHardwareDecode");
        if (!keyp) {
            fprintf(stderr, "no kQTVisualContextAllowHardwareDecode symbol\n");
            goto noattrs;
        }
        const void *k = *keyp;
        const void *v = kCFBooleanTrue;
        ctxAttrs = CFDictionaryCreate(kCFAllocatorDefault, &k, &v, 1,
                                      &kCFTypeDictionaryKeyCallBacks,
                                      &kCFTypeDictionaryValueCallBacks);
        fprintf(stderr, "context attrs: AllowHardwareDecode = YES\n");
    }
noattrs:;
    {
        // Which image won the CIFilter class name? The Apple TV QuartzCore
        // duplicates 207 CI* class names from Tiger's own.
        Class ci = NSClassFromString(@"CIFilter");
        Dl_info info;
        const char *image = (ci && dladdr((const void *)ci, &info)) ? info.dli_fname : "?";
        fprintf(stderr, "CIFilter class %p from %s, +filterWithName: %d\n", ci, image,
                ci ? [ci respondsToSelector:sel_registerName("filterWithName:")] : -1);
    }

    OSStatus err = QTOpenGLTextureContextCreate(kCFAllocatorDefault, cgl, pf,
                                                ctxAttrs, &_visualContext);
    if (ctxAttrs)
        CFRelease(ctxAttrs);
    fprintf(stderr, "QTOpenGLTextureContextCreate -> err=%d ctx=%p\n",
            (int)err, _visualContext);
    if (err != noErr || !_visualContext)
        return NO;

    return YES;
}

// ---- GL / CA host

- (void)prepareOpenGL
{
    [super prepareOpenGL];
    GLint swap = 1;
    CGLContextObj cgl = (CGLContextObj)[[self openGLContext] CGLContextObj];
    CGLSetParameter(cgl, kCGLCPSwapInterval, &swap);
    CGLSetCurrentContext(cgl);
    fprintf(stderr, "GL_RENDERER = %s\n", glGetString(GL_RENDERER));

#ifndef CAVIDEO_NO_CA
    _renderer = [[CARenderer rendererWithCGLContext:cgl options:nil] retain];
    if (!_renderer) {
        fprintf(stderr, "FATAL: no CARenderer\n");
        exit(1);
    }

    // A transparent root with two small overlays, so we can see that CA still
    // composites on top of the video in the same frame.
    _root = [[CALayer layer] retain];
    [_root setName:@"root"];
    [_root setAnchorPoint:CGPointMake(0, 0)];
    [_root setPosition:CGPointMake(0, 0)];
    [_root setBounds:CGRectMake(0, 0, [self bounds].size.width,
                                [self bounds].size.height)];

    _badge = [[CALayer layer] retain];
    [_badge setName:@"badge"];
    [_badge setAnchorPoint:CGPointMake(0, 0)];
    [_badge setBounds:CGRectMake(0, 0, 140, 60)];
    [_badge setPosition:CGPointMake(20, 20)];
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    float red[4] = { 0.95, 0.25, 0.35, 0.85 };
    CGColorRef c = CGColorCreate(cs, red);
    [_badge setBackgroundColor:c];
    CGColorRelease(c);
    [_badge setCornerRadius:12];
    [_root addSublayer:_badge];

    CATextLayer *label = [CATextLayer layer];
    [label setAnchorPoint:CGPointMake(0, 0)];
    [label setBounds:CGRectMake(0, 0, 420, 28)];
    [label setPosition:CGPointMake(20, 96)];
    [label setString:@"CARenderer over a QuickTime OpenGL visual context"];
    [label setFontSize:16];
    float white[4] = { 1, 1, 1, 1 };
    c = CGColorCreate(cs, white);
    [label setForegroundColor:c];
    CGColorRelease(c);
    CGColorSpaceRelease(cs);
    [_root addSublayer:label];

    CABasicAnimation *fade = [CABasicAnimation animationWithKeyPath:@"opacity"];
    [fade setFromValue:[NSNumber numberWithFloat:1.0f]];
    [fade setToValue:[NSNumber numberWithFloat:0.2f]];
    [fade setDuration:0.7];
    [fade setAutoreverses:YES];
    [fade setRepeatCount:1e9f];
    [_badge addAnimation:fade forKey:@"pulse"];

    [_renderer setLayer:_root];
#endif
    [self reshape];

    if (![self attachVisualContext])
        exit(1);
    if (![self openMovieAtPath:_moviePath])
        exit(1);

    GoToBeginningOfMovie(_movie);
    PrerollMovie(_movie, 0, 1 << 16);
    SetMovieRate(_movie, 1 << 16);
    fprintf(stderr, "playing, rate=%ld\n", (long)GetMovieRate(_movie));

    _started = nowSeconds();
    _windowStart = _started;
    _timer = [NSTimer timerWithTimeInterval:1.0 / 60.0 target:self
                                   selector:@selector(drawFrame)
                                   userInfo:nil repeats:YES];
    [[NSRunLoop currentRunLoop] addTimer:_timer
                                 forMode:(NSString *)kCFRunLoopCommonModes];
}

- (void)reshape
{
    NSRect b = [self bounds];
    CGLSetCurrentContext((CGLContextObj)[[self openGLContext] CGLContextObj]);
    glViewport(0, 0, (GLsizei)b.size.width, (GLsizei)b.size.height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, b.size.width, 0, b.size.height, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

#ifndef CAVIDEO_NO_CA
    CGRect r = CGRectMake(0, 0, b.size.width, b.size.height);
    [CATransaction begin];
    [CATransaction setValue:(id)kCFBooleanTrue forKey:kCATransactionDisableActions];
    [_root setBounds:r];
    [CATransaction commit];
    [_renderer setBounds:r];
#endif
}

// Letterbox the movie into the view.
- (CGRect)videoRect
{
    NSRect b = [self bounds];
    if (_movieSize.width <= 0 || _movieSize.height <= 0)
        return CGRectMake(0, 0, b.size.width, b.size.height);
    double scale = MIN(b.size.width / _movieSize.width,
                       b.size.height / _movieSize.height);
    double w = _movieSize.width * scale, h = _movieSize.height * scale;
    return CGRectMake((b.size.width - w) / 2, (b.size.height - h) / 2, w, h);
}

- (void)drawVideoQuad
{
    if (!_currentFrame)
        return;
    GLenum target = CVOpenGLTextureGetTarget(_currentFrame);
    GLuint name = CVOpenGLTextureGetName(_currentFrame);
    GLfloat bl[2], br[2], tr[2], tl[2];
    CVOpenGLTextureGetCleanTexCoords(_currentFrame, bl, br, tr, tl);

    CGRect r = [self videoRect];
    glEnable(target);
    glBindTexture(target, name);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
        glTexCoord2f(bl[0], bl[1]); glVertex2f(r.origin.x, r.origin.y);
        glTexCoord2f(br[0], br[1]); glVertex2f(CGRectGetMaxX(r), r.origin.y);
        glTexCoord2f(tr[0], tr[1]); glVertex2f(CGRectGetMaxX(r), CGRectGetMaxY(r));
        glTexCoord2f(tl[0], tl[1]); glVertex2f(r.origin.x, CGRectGetMaxY(r));
    glEnd();
    // CARenderer's contract: every GL state except viewport/projection/modelview
    // must be back at its default before -render.
    glBindTexture(target, 0);
    glDisable(target);
}

- (void)drawFrame
{
    CGLContextObj cgl = (CGLContextObj)[[self openGLContext] CGLContextObj];
    CGLSetCurrentContext(cgl);

    // Pull any decoded frame the visual context has ready.
    if (_visualContext && QTVisualContextIsNewImageAvailable(_visualContext, NULL)) {
        CVImageBufferRef img = NULL;
        OSStatus err = QTVisualContextCopyImageForTime(_visualContext,
                                                       kCFAllocatorDefault,
                                                       NULL, &img);
        if (err == noErr && img) {
            if (_currentFrame)
                CVOpenGLTextureRelease(_currentFrame);
            _currentFrame = (CVOpenGLTextureRef)img;
            _newImages++;
            _newImagesTotal++;
        } else if (err != noErr)
            fprintf(stderr, "QTVisualContextCopyImageForTime err=%d\n", (int)err);
    }

    glClearColor(0.05, 0.05, 0.07, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    double t0 = nowSeconds();
    [self drawVideoQuad];
    glFinish();                       // so the number below is GPU time, not queue time
    double t1 = nowSeconds();

#ifndef CAVIDEO_NO_CA
    NSRect b = [self bounds];
    [CATransaction flush];
    [_renderer beginFrameAtTime:nowSeconds() timeStamp:NULL];
    [_renderer addUpdateRect:CGRectMake(0, 0, b.size.width, b.size.height)];
    [_renderer render];
    [_renderer endFrame];
#endif
    double t2 = nowSeconds();

    [[self openGLContext] flushBuffer];

    // Give QuickTime its decode time on the main thread.
    MoviesTask(_movie, 0);
    if (IsMovieDone(_movie)) {
        GoToBeginningOfMovie(_movie);
        SetMovieRate(_movie, 1 << 16);
    }
    if (_visualContext)
        QTVisualContextTask(_visualContext);

    _quadAccum += t1 - t0;
    _caAccum += t2 - t1;
    _frames++;

    double now = nowSeconds();
    if (now - _windowStart >= 1.0) {
        fprintf(stderr, "%5.1fs  new images %2u/s | quad %.2f ms | CA %.2f ms |"
                        " %u frames/s | movie t=%.2f rate=%.1f\n",
                now - _started, _newImages,
                _frames ? _quadAccum * 1000.0 / _frames : 0.0,
                _frames ? _caAccum * 1000.0 / _frames : 0.0,
                _frames,
                (double)GetMovieTime(_movie, NULL) /
                    (double)GetMovieTimeScale(_movie),
                GetMovieRate(_movie) / 65536.0);
        _newImages = 0;
        _frames = 0;
        _quadAccum = _caAccum = 0;
        _windowStart = now;
    }

    double window = getenv("CAVIDEO_SECONDS") ? atof(getenv("CAVIDEO_SECONDS")) : 5.0;
    if (!_summarised && now - _started >= window) {
        _summarised = YES;
        fprintf(stderr, "SUMMARY %.0fx%.0f: %.1f new images/s over %.0f s (%u total)\n",
                _movieSize.width, _movieSize.height,
                _newImagesTotal / (now - _started), now - _started,
                _newImagesTotal);
    }
}

- (void)drawRect:(NSRect)r { [self drawFrame]; }

@end

// ---------------------------------------------------------------- app

@interface CAVideoDelegate : NSObject
@end
@implementation CAVideoDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)a { return YES; }
@end

int main(int argc, const char **argv)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    NSApplication *app = [NSApplication sharedApplication];

    NSMenu *menubar = [[[NSMenu alloc] init] autorelease];
    NSMenuItem *appItem = [[[NSMenuItem alloc] init] autorelease];
    [menubar addItem:appItem];
    NSMenu *appMenu = [[[NSMenu alloc] init] autorelease];
    [appMenu addItemWithTitle:@"Quit CAVideo" action:@selector(terminate:) keyEquivalent:@"q"];
    [appItem setSubmenu:appMenu];
    [app setMainMenu:menubar];

    NSString *path = getenv("CAVIDEO_MEDIA")
        ? [NSString stringWithUTF8String:getenv("CAVIDEO_MEDIA")]
        : @"/Users/shg/qtkit-media/test.mp4";

    NSOpenGLPixelFormatAttribute attrs[] = {
        NSOpenGLPFADoubleBuffer, NSOpenGLPFAAccelerated,
        NSOpenGLPFAColorSize, 32, NSOpenGLPFADepthSize, 24, 0
    };
    NSOpenGLPixelFormat *pf =
        [[[NSOpenGLPixelFormat alloc] initWithAttributes:attrs] autorelease];
    if (!pf) {
        fprintf(stderr, "FATAL: no pixel format\n");
        return 1;
    }

    NSRect frame = NSMakeRect(0, 0, 800, 500);
    NSWindow *win = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:(NSTitledWindowMask | NSClosableWindowMask |
                             NSMiniaturizableWindowMask | NSResizableWindowMask)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    [win setTitle:@"CAVideo - QuickTime visual context under CARenderer"];

    CAVideoView *view = [[[CAVideoView alloc] initWithFrame:frame
                                               pixelFormat:pf] autorelease];
    [view setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [view setMoviePath:path];
    [win setContentView:view];
    [win center];
    [win makeKeyAndOrderFront:nil];

    [app setDelegate:[[[CAVideoDelegate alloc] init] autorelease]];
    [app activateIgnoringOtherApps:YES];
    [app run];
    [pool release];
    return 0;
}
