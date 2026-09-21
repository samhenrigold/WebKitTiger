// CAHost - host a Core Animation layer tree on screen on Mac OS X 10.4.11.
//
// Tiger has no layer-backed NSViews and its WindowServer speaks no CA protocol,
// so the only path is CARenderer over the CGL context of an NSOpenGLView, driven
// by hand: [CATransaction flush] + beginFrame/addUpdateRect/render/endFrame.
//
// Phase 2 models WebKitLegacy's shape, following the 2009 WebKit1 hosting code in
// refs/webkit-history/webkit1-layer-hosting-2009:
//
//   viewport CALayer          masksToBounds, geometryFlipped -> AppKit-style top-left
//     page   CATiledLayer     software content painted by -drawLayer:inContext:
//       pulse CALayer         composited overlay, opacity animation, page coords
//       spin  CALayer         composited overlay, CATransform3D, page coords
//
// Scrolling moves the viewport's bounds origin; no tile is repainted.
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
#import <QuartzCore/CATiledLayer.h>

@interface CATransformLayer : CALayer
@end
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>
#include <mach/mach.h>
#include <objc/objc-runtime.h>

// Present in this QuartzCore binary but absent from the Leopard headers
// (geometryFlipped is 10.6 public; shouldDrawOnMainThread was always private,
// and 2009's WebTiledLayer overrode it for exactly this reason).
@interface CALayer (TigerCAPrivate)
- (void)setGeometryFlipped:(BOOL)b;
- (BOOL)geometryFlipped;
- (BOOL)contentsAreFlipped;
@end

#define PAGE_WIDTH  620.0
#define PAGE_HEIGHT 4000.0
#define TILE_SIZE   256.0

// ---------------------------------------------------------------- helpers

static CGColorSpaceRef deviceRGB(void)
{
    static CGColorSpaceRef cs;
    if (!cs)
        cs = CGColorSpaceCreateDeviceRGB();
    return cs;
}

static CGColorRef makeColor(float r, float g, float b, float a)
{
    float c[4] = { r, g, b, a };
    return CGColorCreate(deviceRGB(), c);
}

static CGImageRef makeCheckerImage(void)
{
    const size_t n = 64;
    CGContextRef bmp = CGBitmapContextCreate(NULL, n, n, 8, n * 4, deviceRGB(),
                                             kCGImageAlphaPremultipliedFirst);
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

static size_t residentBytes(void)
{
    struct task_basic_info info;
    mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&info, &count)
        != KERN_SUCCESS)
        return 0;
    return info.resident_size;
}

// ------------------------------------------------- the "page" software paint
//
// Stands in for WebCore's -paintGraphicsLayerContents. Draws in a top-left
// origin coordinate system over the whole 620x4000 page; the caller has already
// clipped and translated so only the requested tile's pixels are touched.

static CGImageRef gChecker;

static void paintPage(CGContextRef ctx, CGRect dirty)
{
    // Page background.
    CGContextSetRGBFillColor(ctx, 1.0, 0.99, 0.96, 1.0);
    CGContextFillRect(ctx, dirty);

    CGContextSelectFont(ctx, "Helvetica", 15, kCGEncodingMacRoman);
    CGContextSetTextMatrix(ctx, CGAffineTransformMakeScale(1.0, -1.0));

    // 40 "paragraphs" down the page, 100 px apart. Only those intersecting the
    // dirty rect cost anything.
    for (int i = 0; i < 40; i++) {
        CGFloat y = 20 + i * 100.0;
        CGRect row = CGRectMake(0, y - 20, PAGE_WIDTH, 100);
        if (!CGRectIntersectsRect(row, dirty))
            continue;

        // A heading rule in a hue that varies down the page, so scroll position
        // is obvious in a screenshot.
        CGContextSetRGBFillColor(ctx, 0.2 + 0.02 * (i % 10), 0.35, 0.8, 1.0);
        CGContextFillRect(ctx, CGRectMake(24, y, 360, 22));

        char text[64];
        snprintf(text, sizeof(text), "Section %d  -  page y = %.0f", i, y);
        CGContextSetRGBFillColor(ctx, 1, 1, 1, 1);
        CGContextShowTextAtPoint(ctx, 32, y + 17, text, strlen(text));

        CGContextSetRGBFillColor(ctx, 0.25, 0.25, 0.3, 1.0);
        CGContextShowTextAtPoint(ctx, 24, y + 46,
            "Software content painted into the layer by -drawLayer:inContext:.",
            65);
        CGContextShowTextAtPoint(ctx, 24, y + 66,
            "One CGBitmapContext tile at a time, only where the renderer asks.", 65);

        // An image every fourth section.
        if (gChecker && (i % 4) == 0)
            CGContextDrawImage(ctx, CGRectMake(420, y, 64, 64), gChecker);
    }
}

// ---------------------------------------------------------------- tiled layer
//
// 2009's WebTiledLayer overrode exactly these two class methods: no fade-in,
// and draw on the main thread so AppKit/WebCore painting stays single-threaded.

@interface TigerTiledLayer : CATiledLayer
@end

@implementation TigerTiledLayer
+ (CFTimeInterval)fadeDuration { return 0; }
+ (BOOL)shouldDrawOnMainThread { return YES; }
- (id<CAAction>)actionForKey:(NSString *)key { return nil; }   // no implicit animations
@end

// ---------------------------------------------------------------- the view

@interface CAHostView : NSOpenGLView
{
    CARenderer *_renderer;
    CALayer *_viewport;
    CALayer *_page;
    CALayer *_pulse;
    CALayer *_spin;
    NSScroller *_scroller;
    NSTimer *_timer;
    NSMutableArray *_tileLayers;   // manual tile grid, nil in the other modes
    int _tileCols, _tileRows;
    int _liveTiles;

    double _scrollY;
    double _autoTarget;
    BOOL _autoScrolling;
    CFTimeInterval _started;

    // frame stats
    double _accum, _swapAccum;
    unsigned _frames;
    // scroll-only stats
    double _scrollAccum;
    unsigned _scrollFrames;
    // tile stats
    double _tileAccum;
    unsigned _tiles;
}
- (void)setScroller:(NSScroller *)s;
- (void)setScrollY:(double)y;
- (void)drawFrame;
- (void)runHitTests;
@end

@implementation CAHostView

- (void)dealloc
{
    [_timer invalidate];
    [_renderer release];
    [_viewport release];
    [_page release];
    [_pulse release];
    [_spin release];
    [_tileLayers release];
    [super dealloc];
}

- (BOOL)isOpaque { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)setScroller:(NSScroller *)s { _scroller = s; }

// ---- layer tree

- (void)buildLayerTree
{
    NSRect b = [self bounds];

    _viewport = [[CALayer layer] retain];
    [_viewport setName:@"viewport"];
    [_viewport setAnchorPoint:CGPointMake(0, 0)];
    [_viewport setPosition:CGPointMake(0, 0)];
    [_viewport setBounds:CGRectMake(0, 0, b.size.width, b.size.height)];
    [_viewport setMasksToBounds:getenv("CAHOST_NOMASK") ? NO : YES];
    // AppKit is top-left / y-down, CA is bottom-left / y-up. Flipping the
    // viewport's geometry makes every sublayer below it lay out in page
    // coordinates with y growing downward, which is what WebCore hands us.
    if (getenv("CAHOST_NOFLIP"))
        fprintf(stderr, "NOTE: geometryFlipped disabled\n");
    else if ([_viewport respondsToSelector:@selector(setGeometryFlipped:)])
        [_viewport setGeometryFlipped:YES];
    else
        fprintf(stderr, "NOTE: no setGeometryFlipped:, flipping by hand\n");
    CGColorRef c = makeColor(0.10, 0.11, 0.14, 1);
    [_viewport setBackgroundColor:c];
    CGColorRelease(c);

    // Three page modes, chosen by env var:
    //   default          manual tile grid (see -buildTileGrid)
    //   CAHOST_PLAIN=1   one 620x4000 CALayer, whole page in one backing store
    //   CAHOST_CATILED=1 CATiledLayer - its -drawLayer:inContext: IS called, but
    //                    the tiles never composite under CARenderer. Kept so the
    //                    finding can be re-checked in one run.
    BOOL caTiled = getenv("CAHOST_CATILED") != NULL;
    _page = [(caTiled ? (CALayer *)[TigerTiledLayer layer] : [CALayer layer]) retain];
    [_page setName:@"page"];
    [_page setAnchorPoint:CGPointMake(0, 0)];
    [_page setPosition:CGPointMake(0, 0)];
    [_page setBounds:CGRectMake(0, 0, PAGE_WIDTH, PAGE_HEIGHT)];
    if (caTiled) {
        [(CATiledLayer *)_page setTileSize:CGSizeMake(TILE_SIZE, TILE_SIZE)];
        [(CATiledLayer *)_page setLevelsOfDetail:1];
        [(CATiledLayer *)_page setLevelsOfDetailBias:0];
    }
    [_viewport addSublayer:_page];

    if (caTiled || getenv("CAHOST_PLAIN")) {
        [_page setDelegate:self];
        [_page setNeedsDisplay];
    } else
        [self buildTileGrid];

    // Composited overlays in *page* coordinates, so they scroll with the page
    // but never touch a tile.
    _pulse = [[CALayer layer] retain];
    [_pulse setName:@"pulse"];
    [_pulse setAnchorPoint:CGPointMake(0, 0)];
    [_pulse setBounds:CGRectMake(0, 0, 150, 90)];
    [_pulse setPosition:CGPointMake(430, 180)];
    c = makeColor(0.20, 0.45, 0.95, 0.9);
    [_pulse setBackgroundColor:c];
    CGColorRelease(c);
    c = makeColor(1, 1, 1, 0.9);
    [_pulse setBorderColor:c];
    CGColorRelease(c);
    [_pulse setBorderWidth:3];
    [_pulse setCornerRadius:14];
    [_page addSublayer:_pulse];

    CABasicAnimation *fade = [CABasicAnimation animationWithKeyPath:@"opacity"];
    [fade setFromValue:[NSNumber numberWithFloat:1.0f]];
    [fade setToValue:[NSNumber numberWithFloat:0.15f]];
    [fade setDuration:0.8];
    [fade setAutoreverses:YES];
    [fade setRepeatCount:1e9f];
    [_pulse addAnimation:fade forKey:@"pulse"];

    _spin = [[CALayer layer] retain];
    [_spin setName:@"spin"];
    [_spin setBounds:CGRectMake(0, 0, 120, 120)];
    [_spin setPosition:CGPointMake(500, getenv("CAHOST_SPINTOP") ? 400 : 1900)];
    c = makeColor(0.95, 0.25, 0.35, 1);
    [_spin setBackgroundColor:c];
    CGColorRelease(c);
    [_spin setCornerRadius:12];
    // Under a geometryFlipped ancestor the flip inverts the determinant, so a
    // rotated layer presents its back face and CA 1.6 culls it. Tiger's CALayer
    // defaults doubleSided to NO here; WebCore sets it explicitly anyway.
    [_spin setDoubleSided:YES];
    if (getenv("CAHOST_2D"))
        ;
    if (getenv("CAHOST_SPINROOT")) {
        // Depth probe: same layer, same transform, but a direct child of the
        // layer handed to CARenderer.
        [_spin setPosition:CGPointMake(500, 240)];
        [_viewport addSublayer:_spin];
    } else if (getenv("CAHOST_NOTL")) {
        [_page addSublayer:_spin];
    } else {
        // CA 1.6 drops a non-affine transform on a plain nested CALayer. Giving
        // it a CATransformLayer parent (what GraphicsLayerCA does for
        // preserves-3d) restores it.
        CATransformLayer *tl = [CATransformLayer layer];
        [tl setName:@"spin-3d"];
        [tl setAnchorPoint:CGPointMake(0, 0)];
        [tl setBounds:CGRectMake(0, 0, 200, 200)];
        [tl setPosition:CGPointMake(400, getenv("CAHOST_SPINTOP") ? 300 : 1800)];
        [_spin setPosition:CGPointMake(100, 100)];
        [tl addSublayer:_spin];
        [_page addSublayer:tl];
    }
}

// ---- manual tile grid
//
// CATiledLayer is present in CA 1.6 and its delegate is called, but the tiles it
// produces never reach a CARenderer-driven render tree. So do what the renderer
// can see: a grid of ordinary CALayers, each backed by its own CGBitmapContext
// via the same -drawLayer:inContext: delegate path, painted on demand and
// dropped again when they scroll far out of view.

- (void)buildTileGrid
{
    _tileCols = (int)ceil(PAGE_WIDTH / TILE_SIZE);
    _tileRows = (int)ceil(PAGE_HEIGHT / TILE_SIZE);
    _tileLayers = [[NSMutableArray alloc] init];

    // Implicit animations on contents would cross-fade every tile as it appears.
    NSDictionary *noActions = [NSDictionary dictionaryWithObjectsAndKeys:
        [NSNull null], @"contents", [NSNull null], @"position",
        [NSNull null], @"bounds", [NSNull null], @"onOrderIn",
        [NSNull null], @"onOrderOut", nil];

    for (int row = 0; row < _tileRows; row++) {
        for (int col = 0; col < _tileCols; col++) {
            CGFloat x = col * TILE_SIZE, y = row * TILE_SIZE;
            CGFloat w = MIN(TILE_SIZE, PAGE_WIDTH - x);
            CGFloat h = MIN(TILE_SIZE, PAGE_HEIGHT - y);
            CALayer *tile = [CALayer layer];
            [tile setName:[NSString stringWithFormat:@"tile %d,%d", col, row]];
            [tile setAnchorPoint:CGPointMake(0, 0)];
            [tile setBounds:CGRectMake(0, 0, w, h)];
            [tile setPosition:CGPointMake(x, y)];
            [tile setActions:noActions];
            [tile setDelegate:self];
            [_page addSublayer:tile];
            [_tileLayers addObject:tile];
        }
    }
    fprintf(stderr, "tile grid: %d x %d = %d tiles of %.0f px\n",
            _tileCols, _tileRows, (int)[_tileLayers count], TILE_SIZE);
}

// Paint the tiles that intersect the visible page rect (plus one tile of
// margin), drop the backing store of the rest. This is the whole point of
// tiling: a 4000 px page keeps ~a screenful of bitmaps resident, not 10 MB.
- (void)updateTiles
{
    if (!_tileLayers)
        return;
    NSRect b = [self bounds];
    CGRect visible = CGRectMake(0, _scrollY - TILE_SIZE,
                                PAGE_WIDTH, b.size.height + 2 * TILE_SIZE);
    int live = 0;
    for (int i = 0; i < (int)[_tileLayers count]; i++) {
        CALayer *tile = [_tileLayers objectAtIndex:i];
        CGPoint org = [tile position];
        CGRect r = CGRectMake(org.x, org.y, [tile bounds].size.width,
                              [tile bounds].size.height);
        BOOL want = CGRectIntersectsRect(r, visible);
        BOOL have = [tile contents] != nil;
        if (want && !have)
            [tile setNeedsDisplay];
        else if (!want && have)
            [tile setContents:nil];       // frees the backing store
        if (want)
            live++;
    }
    _liveTiles = live;
}

// ---- CALayer delegate: the software content path
//
// Same shape as 2009's +[WebLayer drawContents:ofLayer:intoContext:]: flip the
// CTM if the layer's contents are not already flipped, take the clip from the
// context (for a tiled layer it is much smaller than the layer bounds), and
// hand that rect to the painting code.

- (void)drawLayer:(CALayer *)layer inContext:(CGContextRef)ctx
{
    CFTimeInterval t0 = CACurrentMediaTime();

    CGContextSaveGState(ctx);
    BOOL flipped = [layer respondsToSelector:@selector(contentsAreFlipped)]
                   && [layer contentsAreFlipped];
    if (!flipped) {
        CGRect lb = [layer bounds];
        CGContextScaleCTM(ctx, 1, -1);
        CGContextTranslateCTM(ctx, 0, -lb.size.height);
    }
    // A tile's own space is 0..256; shift it so paintPage always works in page
    // coordinates. The clip then comes back in page coordinates too.
    if (layer != _page) {
        CGPoint org = [layer position];
        CGContextTranslateCTM(ctx, -org.x, -org.y);
    }
    CGRect clip = CGContextGetClipBoundingBox(ctx);
    paintPage(ctx, clip);
    CGContextRestoreGState(ctx);

    _tileAccum += CACurrentMediaTime() - t0;
    _tiles++;
}

// ---- scrolling
//
// Move the viewport's bounds origin. Nothing below it is invalidated, so no
// tile is repainted; CA just re-composites the existing tile textures.

- (void)setScrollY:(double)y
{
    double maxScroll = PAGE_HEIGHT - [self bounds].size.height;
    if (maxScroll < 0)
        maxScroll = 0;
    if (y < 0) y = 0;
    if (y > maxScroll) y = maxScroll;
    _scrollY = y;

    CGRect b = [_viewport bounds];
    [CATransaction begin];
    [CATransaction setValue:(id)kCFBooleanTrue forKey:kCATransactionDisableActions];
    [_viewport setBounds:CGRectMake(b.origin.x, _scrollY, b.size.width, b.size.height)];
    [CATransaction commit];
    [self updateTiles];

    if (_scroller) {
        [_scroller setFloatValue:(maxScroll > 0 ? (float)(_scrollY / maxScroll) : 0.0f)
                  knobProportion:(float)([self bounds].size.height / PAGE_HEIGHT)];
    }
}

- (void)scrollerAction:(id)sender
{
    double maxScroll = PAGE_HEIGHT - [self bounds].size.height;
    switch ([sender hitPart]) {
    case NSScrollerKnob:
    case NSScrollerKnobSlot:
        [self setScrollY:[sender floatValue] * maxScroll];
        break;
    case NSScrollerDecrementLine: [self setScrollY:_scrollY - 40]; break;
    case NSScrollerIncrementLine: [self setScrollY:_scrollY + 40]; break;
    case NSScrollerDecrementPage: [self setScrollY:_scrollY - 300]; break;
    case NSScrollerIncrementPage: [self setScrollY:_scrollY + 300]; break;
    default: break;
    }
}

- (void)scrollWheel:(NSEvent *)e
{
    [self setScrollY:_scrollY - [e deltaY] * 20.0];
}

// ---- hit testing
//
// AppKit hands us a bottom-left-origin point in the view. The viewport layer
// shares that space (the GL ortho matches), so -hitTest: takes the view point
// directly. Page coordinates are top-left, so convert separately.

- (CGPoint)pagePointForViewPoint:(NSPoint)p
{
    return CGPointMake(p.x, ([self bounds].size.height - p.y) + _scrollY);
}

- (void)reportHitAtViewPoint:(NSPoint)p
{
    CALayer *hit = [_viewport hitTest:CGPointMake(p.x, p.y)];
    CGPoint pagePt = [self pagePointForViewPoint:p];
    fprintf(stderr, "hit: view(%.0f,%.0f) -> page(%.0f,%.0f) -> layer '%s'\n",
            p.x, p.y, pagePt.x, pagePt.y,
            hit ? [[hit name] UTF8String] : "(none)");
}

- (void)mouseDown:(NSEvent *)e
{
    [self reportHitAtViewPoint:[self convertPoint:[e locationInWindow] fromView:nil]];
}

// Headless check: probe known page points, so the printed layer names can be
// checked against the screenshot at the same scroll position.
- (void)reportHitAtPagePoint:(CGPoint)pp label:(const char *)label
{
    NSRect b = [self bounds];
    double viewY = b.size.height - (pp.y - _scrollY);
    if (viewY < 0 || viewY > b.size.height || pp.x < 0 || pp.x > b.size.width) {
        fprintf(stderr, "hit %-9s page(%.0f,%.0f) off-screen at scrollY=%.0f\n",
                label, pp.x, pp.y, _scrollY);
        return;
    }
    NSPoint vp = NSMakePoint(pp.x, viewY);
    CALayer *hit = [_viewport hitTest:CGPointMake(vp.x, vp.y)];
    CGPoint back = [self pagePointForViewPoint:vp];
    fprintf(stderr, "hit %-9s page(%.0f,%.0f) -> view(%.0f,%.0f) -> page(%.0f,%.0f)"
                    " -> layer '%s'\n",
            label, pp.x, pp.y, vp.x, vp.y, back.x, back.y,
            hit ? [[hit name] UTF8String] : "(none)");
}

- (void)runHitTests
{
    NSRect b = [self bounds];
    fprintf(stderr, "-- hit tests at scrollY=%.0f, view %.0fx%.0f\n",
            _scrollY, b.size.width, b.size.height);
    [self reportHitAtPagePoint:CGPointMake(470, 200) label:"pulse"];
    [self reportHitAtPagePoint:CGPointMake(500, 1900) label:"spin"];
    [self reportHitAtPagePoint:CGPointMake(100, _scrollY + 100) label:"text"];
    [self reportHitAtPagePoint:CGPointMake(b.size.width - 4, _scrollY + 100)
                         label:"outside"];
}

// ---- GL / renderer

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
    {
        // Probe what CA 1.6 actually implements, rather than trusting the
        // Leopard headers or the strings in the binary.
        static const char *sels[] = {
            "setGeometryFlipped:", "geometryFlipped", "contentsAreFlipped",
            "setMasksToBounds:", "setContentsGravity:", "setSublayerTransform:",
            "hitTest:", "setNeedsDisplayInRect:", "displayIfNeeded",
            "setTileSize:", "setLevelsOfDetail:", "scrollPoint:", NULL };
        CALayer *probe = [CALayer layer];
        fprintf(stderr, "CATiledLayer class = %s, +shouldDrawOnMainThread = %s\n",
                NSClassFromString(@"CATiledLayer") ? "yes" : "NO",
                [TigerTiledLayer respondsToSelector:@selector(shouldDrawOnMainThread)]
                    ? "yes" : "NO");
        for (int i = 0; sels[i]; i++) {
            SEL sel = sel_registerName(sels[i]);
            fprintf(stderr, "  CALayer %-24s %s   CATiledLayer %s\n", sels[i],
                    [probe respondsToSelector:sel] ? "yes" : "NO ",
                    [[CATiledLayer layer] respondsToSelector:sel] ? "yes" : "NO");
        }
    }

    gChecker = makeCheckerImage();
    [self buildLayerTree];
    [_renderer setLayer:_viewport];
    fprintf(stderr, "page layer class = %s, viewport geometryFlipped = %s\n",
            [[[_page class] description] UTF8String],
            [[_viewport valueForKey:@"geometryFlipped"] description]
                ? [[[_viewport valueForKey:@"geometryFlipped"] description] UTF8String]
                : "?");
    [self reshape];
    [self setScrollY:0];

    _started = CACurrentMediaTime();
    _timer = [NSTimer timerWithTimeInterval:1.0 / 60.0 target:self
                                   selector:@selector(drawFrame)
                                   userInfo:nil repeats:YES];
    [[NSRunLoop currentRunLoop] addTimer:_timer
                                 forMode:(NSString *)kCFRunLoopCommonModes];
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
    [CATransaction begin];
    [CATransaction setValue:(id)kCFBooleanTrue forKey:kCATransactionDisableActions];
    [_viewport setBounds:CGRectMake(0, _scrollY, r.size.width, r.size.height)];
    [CATransaction commit];
    [_renderer setBounds:r];
    [self setScrollY:_scrollY];
}

- (void)drawFrame
{
    NSRect b = [self bounds];
    CGLContextObj cgl = (CGLContextObj)[[self openGLContext] CGLContextObj];
    CGLSetCurrentContext(cgl);

    CFTimeInterval now = CACurrentMediaTime();

    if (!getenv("CAHOST_NO3D")) {
        CATransform3D t = getenv("CAHOST_2D")
            ? CATransform3DMakeRotation(now * 1.5, 0, 0, 1)
            : CATransform3DMakeRotation(now * 1.5, 0.3, 1.0, 0.15);
        t.m34 = getenv("CAHOST_M34") ? atof(getenv("CAHOST_M34")) : -1.0 / 600.0;
        [_spin setTransform:t];
    }

    // Scripted run so the whole measurement fits in one ssh invocation.
    if (_autoScrolling) {
        double step = 900.0 / 60.0;          // ~900 px/s
        double next = _scrollY + step;
        if (next >= _autoTarget) {
            next = _autoTarget;
            _autoScrolling = NO;
            fprintf(stderr, "SCROLL DONE: %.2f ms/frame over %u frames, rss %.1f MB\n",
                    _scrollFrames ? _scrollAccum * 1000.0 / _scrollFrames : 0.0,
                    _scrollFrames, residentBytes() / 1048576.0);
            [self runHitTests];
        }
        [self setScrollY:next];
    }

    // CA normally commits from a run-loop observer; without AppKit's layer
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
    if (_autoScrolling) {
        _scrollAccum += t1 - t0;
        _scrollFrames++;
    }

    if (_frames >= 60) {
        CALayer *pl = [_spin presentationLayer];
        CATransform3D pt = pl ? [pl transform] : CATransform3DIdentity;
        fprintf(stderr, "  spin pres=%p pos=(%.0f,%.0f) m11=%.3f m13=%.3f m34=%.5f\n",
                pl, pl ? [pl position].x : -1, pl ? [pl position].y : -1,
                pt.m11, pt.m13, pt.m34);
        fprintf(stderr,
                "CA render %.2f ms + swap %.2f ms | painted %u tiles, %.2f ms/tile,"
                " %d live | rss %.1f MB\n",
                _accum * 1000.0 / _frames, _swapAccum * 1000.0 / _frames,
                _tiles, _tiles ? _tileAccum * 1000.0 / _tiles : 0.0, _liveTiles,
                residentBytes() / 1048576.0);
        _accum = _swapAccum = 0;
        _frames = 0;
    }

    if (getenv("CAHOST_AUTO") && !_autoScrolling && _scrollY == 0
        && now - _started > 4.0) {
        fprintf(stderr, "-- before scroll: %u tiles painted, %.2f ms/tile, rss %.1f MB\n",
                _tiles, _tiles ? _tileAccum * 1000.0 / _tiles : 0.0,
                residentBytes() / 1048576.0);
        [self runHitTests];
        _autoTarget = 1800;
        _autoScrolling = YES;
    }
}

- (void)drawRect:(NSRect)r { [self drawFrame]; }

@end

// ---------------------------------------------------------------- app

@interface CAHostDelegate : NSObject
@end
@implementation CAHostDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)a { return YES; }
+ (void)resizeWindow:(NSTimer *)t
{
    [(NSWindow *)[t userInfo] setContentSize:NSMakeSize(900, 600)];
    fprintf(stderr, "resized to 900x600\n");
}
@end

int main(int argc, const char **argv)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    NSApplication *app = [NSApplication sharedApplication];

    NSMenu *menubar = [[[NSMenu alloc] init] autorelease];
    NSMenuItem *appItem = [[[NSMenuItem alloc] init] autorelease];
    [menubar addItem:appItem];
    NSMenu *appMenu = [[[NSMenu alloc] init] autorelease];
    [appMenu addItemWithTitle:@"Quit CAHost" action:@selector(terminate:) keyEquivalent:@"q"];
    [appItem setSubmenu:appMenu];
    [app setMainMenu:menubar];

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

    NSRect frame = NSMakeRect(0, 0, 660, 480);
    NSWindow *win = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:(NSTitledWindowMask | NSClosableWindowMask |
                             NSMiniaturizableWindowMask | NSResizableWindowMask)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    [win setTitle:@"CAHost - tiled CA page on Tiger"];

    const CGFloat sw = 15;
    NSView *content = [win contentView];
    NSRect glRect = NSMakeRect(0, 0, frame.size.width - sw, frame.size.height);
    CAHostView *view = [[[CAHostView alloc] initWithFrame:glRect
                                              pixelFormat:pf] autorelease];
    [view setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [content addSubview:view];

    // A real NSScroller sibling: an NSOpenGLView's surface is composited over
    // whatever is under it, so the scroller must not overlap the GL view.
    NSScroller *scroller = [[[NSScroller alloc]
        initWithFrame:NSMakeRect(frame.size.width - sw, 0, sw, frame.size.height)]
        autorelease];
    [scroller setAutoresizingMask:(NSViewMinXMargin | NSViewHeightSizable)];
    [scroller setEnabled:YES];
    [scroller setTarget:view];
    [scroller setAction:@selector(scrollerAction:)];
    [content addSubview:scroller];
    [view setScroller:scroller];

    [win center];
    [win makeKeyAndOrderFront:nil];
    [win makeFirstResponder:view];

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
