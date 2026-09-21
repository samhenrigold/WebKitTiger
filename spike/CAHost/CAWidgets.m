// CAWidgets - real Aqua controls over the CARenderer-hosted scrolling page.
//
// The feel test for the "controls absolutely need to look like Aqua" decision.
// The page is the same shape as CAHost: a 620x4000 layer tree of software
// painted tiles under a CARenderer in an NSOpenGLView. On top of it sit real
// AppKit controls, positioned from a table in page coordinates and moved with
// the tiles every frame.
//
// The gotcha this had to solve first: on 10.4 an NSOpenGLView is a window server
// surface composited ABOVE the window's backing store, so sibling views are
// invisible. Three things together fix it, and all three are needed:
//
//   1. NSOpenGLCPSurfaceOrder = -1 on the GL context, which puts the surface
//      below the window's content instead of above it.
//   2. The GL view must answer NO to -isOpaque, or AppKit never draws anything
//      underneath it.
//   3. The GL view's -drawRect: must punch a transparent hole with
//      NSRectFillUsingOperation(bounds, NSCompositeCopy). Plain NSRectFill with
//      clearColor composites clear over the backing store and leaves it opaque,
//      so the surface stays hidden and you get a white rectangle.
//
// Plus [window setOpaque:NO] and a clear window background.
//
// Build: make -f Makefile CAWidgets.
//
//   CAW_AUTO=1        scripted scroll with timings, then a focus-ring pass
//   CAW_NOORDER=1     leave the surface order alone, to see quirk (1)
//   CAW_NOSYNC=1      do not force a window flush in the frame, to see the lag

#include <AvailabilityMacros.h>
#undef AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER
#define AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER
#undef AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER_BUT_DEPRECATED
#define AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER_BUT_DEPRECATED

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CoreAnimation.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>
#include <mach/mach.h>

// In this QuartzCore build but not in the Leopard headers.
@interface CALayer (TigerCAPrivate)
- (void)setGeometryFlipped:(BOOL)b;
- (BOOL)contentsAreFlipped;
@end

#define PAGE_WIDTH   620.0
#define PAGE_HEIGHT  4000.0
#define TILE_SIZE    256.0
#define SCROLLER_W   15.0

// The fake overflow:scroll region, in page coordinates.
#define OVF_X        40.0
#define OVF_Y        1500.0
#define OVF_W        520.0
#define OVF_H        230.0
#define OVF_CONTENT  520.0      // scrollable height inside it

static BOOL gCheckValue = NO;   // what the checkbox toggles; the tiles repaint it
static double gCheckPageY = 0;

// ---------------------------------------------------------------- page paint

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

static void paintPage(CGContextRef ctx, CGRect dirty)
{
    CGContextSetRGBFillColor(ctx, 1.0, 0.99, 0.96, 1.0);
    CGContextFillRect(ctx, dirty);

    CGContextSelectFont(ctx, "Helvetica", 14, kCGEncodingMacRoman);
    CGContextSetTextMatrix(ctx, CGAffineTransformMakeScale(1.0, -1.0));

    for (int i = 0; i < 53; i++) {
        CGFloat y = 40 + i * 75.0;
        if (!CGRectIntersectsRect(CGRectMake(0, y - 20, PAGE_WIDTH, 75), dirty))
            continue;
        CGContextSetRGBFillColor(ctx, 0.35, 0.40, 0.55, 1.0);
        char text[80];
        snprintf(text, sizeof(text), "page y = %.0f", y);
        CGContextShowTextAtPoint(ctx, 300, y + 14, text, strlen(text));
        CGContextSetRGBFillColor(ctx, 0.86, 0.88, 0.94, 1.0);
        CGContextFillRect(ctx, CGRectMake(280, y + 22, 300, 1));
    }

    // The fake overflow region's own background, so the clipping is visible.
    CGRect ovf = CGRectMake(OVF_X, OVF_Y, OVF_W, OVF_H);
    if (CGRectIntersectsRect(ovf, dirty)) {
        CGContextSetRGBFillColor(ctx, 0.91, 0.93, 0.97, 1.0);
        CGContextFillRect(ctx, ovf);
        CGContextSetRGBStrokeColor(ctx, 0.55, 0.6, 0.7, 1.0);
        CGContextStrokeRect(ctx, ovf);
        CGContextSetRGBFillColor(ctx, 0.25, 0.3, 0.45, 1.0);
        CGContextShowTextAtPoint(ctx, OVF_X + 10, OVF_Y + 18,
                                 "overflow:scroll region - controls clip here", 42);
    }

    // The content-process round trip: whatever the checkbox last set is painted
    // into the tile, not drawn by AppKit.
    CGRect label = CGRectMake(40, gCheckPageY + 26, 400, 20);
    if (gCheckPageY > 0 && CGRectIntersectsRect(label, dirty)) {
        char text[80];
        snprintf(text, sizeof(text), "painted by the page: checkbox = %s",
                 gCheckValue ? "ON" : "OFF");
        CGContextSetRGBFillColor(ctx, gCheckValue ? 0.05 : 0.6,
                                 gCheckValue ? 0.45 : 0.25, 0.15, 1.0);
        CGContextShowTextAtPoint(ctx, 44, gCheckPageY + 40, text, strlen(text));
    }
}

// ------------------------------------------------- controls that pass the wheel
//
// A control eats -scrollWheel: by default, so the page would stop scrolling
// whenever the pointer was over one. Forwarding to the next responder puts the
// event back on the view that owns the page.

// Tab only reaches buttons, popups and sliders when "full keyboard access" is
// on, which is a global preference and off by default on Tiger (the key is not
// even present in NSGlobalDomain on this box). Setting AppleKeyboardUIMode in
// our own defaults domain does NOT work; AppKit does not read it from there.
// Forcing -acceptsFirstResponder is what actually makes the key view loop
// traverse them, and it is what a browser wants anyway, since Tab-to-control is
// the page's behaviour, not the system's.
#define WHEEL_THROUGH \
    - (void)scrollWheel:(NSEvent *)e { [[self nextResponder] scrollWheel:e]; } \
    - (BOOL)acceptsFirstResponder { return YES; }

@interface CAWButton : NSButton @end
@implementation CAWButton WHEEL_THROUGH @end
@interface CAWPopUp : NSPopUpButton @end
@implementation CAWPopUp WHEEL_THROUGH @end
@interface CAWSlider : NSSlider @end
@implementation CAWSlider WHEEL_THROUGH @end
@interface CAWProgress : NSProgressIndicator @end
@implementation CAWProgress WHEEL_THROUGH @end

// A plain NSView clips its subviews to its own bounds, which is all the fake
// overflow region needs.
@interface CAWClipView : NSView @end
@implementation CAWClipView
- (BOOL)isFlipped { return YES; }
- (void)scrollWheel:(NSEvent *)e { [[self nextResponder] scrollWheel:e]; }
@end

// ---------------------------------------------------------------- layout table

enum { W_PUSH, W_DEFAULT, W_CHECK, W_RADIO, W_POPUP, W_SLIDER, W_PROGDET, W_PROGINDET };

typedef struct {
    int kind;
    double pageX, pageY, w, h;
    BOOL inOverflow;            // page coords become region-local coords
    const char *title;
} WidgetSpec;

static WidgetSpec gSpecs[64];
static int gSpecCount;

static void addSpec(int kind, double x, double y, double w, double h,
                    BOOL ovf, const char *title)
{
    WidgetSpec s = { kind, x, y, w, h, ovf, title };
    gSpecs[gSpecCount++] = s;
}

// 34 controls down the page plus 6 inside the overflow region = 40 live.
static void buildSpecTable(void)
{
    static const int cycle[] = { W_PUSH, W_CHECK, W_POPUP, W_SLIDER, W_RADIO,
                                 W_PROGDET, W_DEFAULT, W_PROGINDET };
    double y = 200;
    for (int i = 0; i < 34; i++) {
        int kind = cycle[i % 8];
        if (y > OVF_Y - 90 && y < OVF_Y + OVF_H + 20) {   // leave the region alone
            y = OVF_Y + OVF_H + 40;
        }
        double w = 150, h = 26;
        switch (kind) {
        case W_CHECK:      w = 170; h = 18; break;
        case W_RADIO:      w = 170; h = 54; break;   // a 3-button group
        case W_SLIDER:     w = 200; h = 20; break;
        case W_POPUP:      w = 190; h = 26; break;
        case W_PROGDET:    w = 200; h = 16; break;
        case W_PROGINDET:  w = 32;  h = 32; break;
        case W_DEFAULT:    w = 130; h = 32; break;
        default:           w = 130; h = 32; break;
        }
        addSpec(kind, 44, y, w, h, NO, NULL);
        y += 75;
    }
    // Inside the fake overflow region, region-local coordinates.
    addSpec(W_PUSH,   16,  36, 130, 32, YES, "in overflow 1");
    addSpec(W_CHECK,  16,  88, 170, 18, YES, "clipped checkbox");
    addSpec(W_POPUP,  16, 126, 190, 26, YES, NULL);
    addSpec(W_SLIDER, 16, 176, 200, 20, YES, NULL);
    addSpec(W_PUSH,   16, 216, 130, 32, YES, "in overflow 2");
    addSpec(W_CHECK,  16, 268, 170, 18, YES, "below the fold");
}

// ------------------------------------------------- scripted keyboard / popup

static void postKey(unsigned short code, NSString *chars, unsigned flags)
{
    NSWindow *w = [NSApp keyWindow];
    if (!w)
        return;
    NSEvent *down = [NSEvent keyEventWithType:NSKeyDown location:NSZeroPoint
        modifierFlags:flags timestamp:0 windowNumber:[w windowNumber] context:nil
        characters:chars charactersIgnoringModifiers:chars isARepeat:NO keyCode:code];
    NSEvent *up = [NSEvent keyEventWithType:NSKeyUp location:NSZeroPoint
        modifierFlags:flags timestamp:0 windowNumber:[w windowNumber] context:nil
        characters:chars charactersIgnoringModifiers:chars isARepeat:NO keyCode:code];
    [NSApp postEvent:down atStart:NO];
    [NSApp postEvent:up atStart:NO];
}

// ---------------------------------------------------------------- the GL view

@interface CAWGLView : NSOpenGLView
{
    CARenderer *_renderer;
    CALayer *_viewport;
    CALayer *_page;
    NSMutableArray *_tiles;
    int _tileCols, _tileRows;
}
- (void)setupRenderer;
- (void)updateTilesForScroll:(double)scrollY;
- (void)invalidatePageRect:(CGRect)r;
- (double)renderFrame;
@end

@implementation CAWGLView

- (BOOL)isOpaque { return NO; }     // quirk (2): or nothing draws underneath
// The controls are subviews of this view, not siblings. That matters: when a
// control moves, AppKit invalidates the rect in its SUPERVIEW, and only an
// ancestor gets asked to repaint it. As a sibling the GL view is never asked,
// and the vacated area keeps whatever was there.
- (BOOL)isFlipped { return YES; }

- (void)drawRect:(NSRect)r
{
    // quirk (3): NSCompositeCopy, not plain NSRectFill. Only the dirty rect:
    // filling the whole bounds erases controls drawn earlier in the same pass.
    [[NSColor clearColor] set];
    NSRectFillUsingOperation(r, NSCompositeCopy);
}

- (void)prepareOpenGL
{
    [super prepareOpenGL];
    CGLContextObj cgl = (CGLContextObj)[[self openGLContext] CGLContextObj];
    GLint swap = 1;
    CGLSetParameter(cgl, kCGLCPSwapInterval, &swap);
    if (!getenv("CAW_NOORDER")) {
        GLint order = -1;           // quirk (1)
        [[self openGLContext] setValues:&order forParameter:NSOpenGLCPSurfaceOrder];
    }
    CGLSetCurrentContext(cgl);
    fprintf(stderr, "GL_RENDERER = %s, surface order = %s\n", glGetString(GL_RENDERER),
            getenv("CAW_NOORDER") ? "default (above the window)" : "-1 (below)");
    [self setupRenderer];
}

- (void)setupRenderer
{
    CGLContextObj cgl = (CGLContextObj)[[self openGLContext] CGLContextObj];
    _renderer = [[CARenderer rendererWithCGLContext:cgl options:nil] retain];
    if (!_renderer) {
        fprintf(stderr, "FATAL: no CARenderer\n");
        exit(1);
    }

    NSRect b = [self bounds];
    _viewport = [[CALayer layer] retain];
    [_viewport setAnchorPoint:CGPointMake(0, 0)];
    [_viewport setPosition:CGPointMake(0, 0)];
    [_viewport setBounds:CGRectMake(0, 0, b.size.width, b.size.height)];
    [_viewport setMasksToBounds:YES];
    [_viewport setGeometryFlipped:YES];
    CGColorRef c = makeColor(0.12, 0.13, 0.16, 1);
    [_viewport setBackgroundColor:c];
    CGColorRelease(c);

    _page = [[CALayer layer] retain];
    [_page setAnchorPoint:CGPointMake(0, 0)];
    [_page setPosition:CGPointMake(0, 0)];
    [_page setBounds:CGRectMake(0, 0, PAGE_WIDTH, PAGE_HEIGHT)];
    [_viewport addSublayer:_page];

    _tileCols = (int)ceil(PAGE_WIDTH / TILE_SIZE);
    _tileRows = (int)ceil(PAGE_HEIGHT / TILE_SIZE);
    _tiles = [[NSMutableArray alloc] init];
    NSDictionary *noActions = [NSDictionary dictionaryWithObjectsAndKeys:
        [NSNull null], @"contents", [NSNull null], @"position",
        [NSNull null], @"bounds", [NSNull null], @"onOrderIn",
        [NSNull null], @"onOrderOut", nil];
    for (int row = 0; row < _tileRows; row++)
        for (int col = 0; col < _tileCols; col++) {
            CGFloat x = col * TILE_SIZE, y = row * TILE_SIZE;
            CALayer *tile = [CALayer layer];
            [tile setAnchorPoint:CGPointMake(0, 0)];
            [tile setBounds:CGRectMake(0, 0, MIN(TILE_SIZE, PAGE_WIDTH - x),
                                       MIN(TILE_SIZE, PAGE_HEIGHT - y))];
            [tile setPosition:CGPointMake(x, y)];
            [tile setActions:noActions];
            [tile setDelegate:self];
            [_page addSublayer:tile];
            [_tiles addObject:tile];
        }
    [_renderer setLayer:_viewport];
    [self reshapeRenderer];
}

- (void)drawLayer:(CALayer *)layer inContext:(CGContextRef)ctx
{
    CGContextSaveGState(ctx);
    if (![layer contentsAreFlipped]) {
        CGContextScaleCTM(ctx, 1, -1);
        CGContextTranslateCTM(ctx, 0, -[layer bounds].size.height);
    }
    CGPoint org = [layer position];
    CGContextTranslateCTM(ctx, -org.x, -org.y);
    paintPage(ctx, CGContextGetClipBoundingBox(ctx));
    CGContextRestoreGState(ctx);
}

- (void)invalidatePageRect:(CGRect)r
{
    for (int i = 0; i < (int)[_tiles count]; i++) {
        CALayer *tile = [_tiles objectAtIndex:i];
        CGPoint o = [tile position];
        CGRect t = CGRectMake(o.x, o.y, [tile bounds].size.width,
                              [tile bounds].size.height);
        if (CGRectIntersectsRect(t, r) && [tile contents])
            [tile setNeedsDisplay];
    }
}

- (void)updateTilesForScroll:(double)scrollY
{
    NSRect b = [self bounds];
    CGRect visible = CGRectMake(0, scrollY - TILE_SIZE,
                                PAGE_WIDTH, b.size.height + 2 * TILE_SIZE);
    for (int i = 0; i < (int)[_tiles count]; i++) {
        CALayer *tile = [_tiles objectAtIndex:i];
        CGPoint o = [tile position];
        CGRect t = CGRectMake(o.x, o.y, [tile bounds].size.width,
                              [tile bounds].size.height);
        BOOL want = CGRectIntersectsRect(t, visible);
        BOOL have = [tile contents] != nil;
        if (want && !have)
            [tile setNeedsDisplay];
        else if (!want && have)
            [tile setContents:nil];
    }
    [CATransaction begin];
    [CATransaction setValue:(id)kCFBooleanTrue forKey:kCATransactionDisableActions];
    [_viewport setBounds:CGRectMake(0, scrollY, b.size.width, b.size.height)];
    [CATransaction commit];
}

- (void)reshapeRenderer
{
    NSRect b = [self bounds];
    CGLSetCurrentContext((CGLContextObj)[[self openGLContext] CGLContextObj]);
    glViewport(0, 0, (GLsizei)b.size.width, (GLsizei)b.size.height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, b.size.width, 0, b.size.height, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    [_renderer setBounds:CGRectMake(0, 0, b.size.width, b.size.height)];
}

- (void)reshape { [self reshapeRenderer]; }

- (double)renderFrame
{
    NSRect b = [self bounds];
    CGLContextObj cgl = (CGLContextObj)[[self openGLContext] CGLContextObj];
    CGLSetCurrentContext(cgl);
    CFTimeInterval t0 = CACurrentMediaTime();
    [CATransaction flush];
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    [_renderer beginFrameAtTime:CACurrentMediaTime() timeStamp:NULL];
    [_renderer addUpdateRect:CGRectMake(0, 0, b.size.width, b.size.height)];
    [_renderer render];
    [_renderer endFrame];
    CFTimeInterval t1 = CACurrentMediaTime();
    [[self openGLContext] flushBuffer];
    return t1 - t0;
}

@end

// ---------------------------------------------------------------- the host

@interface CAWHostView : NSView
{
    CAWGLView *_gl;
    CAWClipView *_clip;         // the fake overflow region
    NSScroller *_pageScroller;
    NSScroller *_ovfScroller;
    NSMutableArray *_controls;  // NSView*, parallel to gSpecs
    NSTimer *_timer;

    double _scrollY;
    double _ovfScrollY;
    double _autoTarget;
    BOOL _autoScrolling;
    CFTimeInterval _started;

    double _caAccum, _layoutAccum, _flushAccum;
    unsigned _frames;
    double _scrollCA, _scrollLayout, _scrollFlush;
    unsigned _scrollFrames;
    int _visibleCount;
    BOOL _keyLoopBuilt;
    NSMutableArray *_keyChain;
}
- (void)buildControls;
- (void)setScrollY:(double)y;
- (void)tick;
@end

@implementation CAWHostView

- (BOOL)isFlipped { return YES; }

- (void)dealloc
{
    [_timer invalidate];
    [_controls release];
    [_keyChain release];
    [super dealloc];
}

// ---- construction

- (id)initWithFrame:(NSRect)f pixelFormat:(NSOpenGLPixelFormat *)pf
{
    if (!(self = [super initWithFrame:f]))
        return nil;
    NSRect inner = NSMakeRect(0, 0, f.size.width - SCROLLER_W, f.size.height);

    _gl = [[[CAWGLView alloc] initWithFrame:inner pixelFormat:pf] autorelease];
    [_gl setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [self addSubview:_gl];

    _clip = [[[CAWClipView alloc] initWithFrame:NSMakeRect(0, 0, OVF_W, OVF_H)]
             autorelease];
    [_gl addSubview:_clip];

    _pageScroller = [[[NSScroller alloc]
        initWithFrame:NSMakeRect(f.size.width - SCROLLER_W, 0, SCROLLER_W,
                                 f.size.height)] autorelease];
    [_pageScroller setAutoresizingMask:(NSViewMinXMargin | NSViewHeightSizable)];
    [_pageScroller setEnabled:YES];
    [_pageScroller setTarget:self];
    [_pageScroller setAction:@selector(pageScrollerAction:)];
    [self addSubview:_pageScroller];

    _ovfScroller = [[[NSScroller alloc]
        initWithFrame:NSMakeRect(0, 0, SCROLLER_W, OVF_H)] autorelease];
    [_ovfScroller setEnabled:YES];
    [_ovfScroller setTarget:self];
    [_ovfScroller setAction:@selector(ovfScrollerAction:)];
    [_gl addSubview:_ovfScroller];

    _controls = [[NSMutableArray alloc] init];
    return self;
}

- (void)buildControls
{
    NSFont *f = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
    for (int i = 0; i < gSpecCount; i++) {
        WidgetSpec *s = &gSpecs[i];
        NSView *parent = s->inOverflow ? (NSView *)_clip : (NSView *)_gl;
        NSRect r = NSMakeRect(0, 0, s->w, s->h);
        NSView *v = nil;

        switch (s->kind) {
        case W_PUSH:
        case W_DEFAULT: {
            CAWButton *b = [[[CAWButton alloc] initWithFrame:r] autorelease];
            [b setBezelStyle:NSRoundedBezelStyle];
            [b setFont:f];
            [b setTitle:s->title ? [NSString stringWithUTF8String:s->title]
                                 : (s->kind == W_DEFAULT ? @"Default" : @"Push")];
            [b setTarget:self];
            [b setAction:@selector(buttonHit:)];
            [b setTag:i];
            if (s->kind == W_DEFAULT)
                [b setKeyEquivalent:@"\r"];
            v = b;
            break;
        }
        case W_CHECK: {
            CAWButton *b = [[[CAWButton alloc] initWithFrame:r] autorelease];
            [b setButtonType:NSSwitchButton];
            [b setFont:f];
            [b setTitle:s->title ? [NSString stringWithUTF8String:s->title]
                                 : @"Repaint the page"];
            [b setTarget:self];
            [b setAction:@selector(checkHit:)];
            [b setTag:i];
            if (!s->inOverflow && gCheckPageY == 0)
                gCheckPageY = s->pageY;     // the one the tiles echo
            v = b;
            break;
        }
        case W_RADIO: {
            NSView *group = [[[NSView alloc] initWithFrame:r] autorelease];
            for (int k = 0; k < 3; k++) {
                CAWButton *b = [[[CAWButton alloc]
                    initWithFrame:NSMakeRect(0, k * 18, s->w, 18)] autorelease];
                [b setButtonType:NSRadioButton];
                [b setFont:f];
                [b setTitle:[NSString stringWithFormat:@"Choice %d", k + 1]];
                [b setState:k == 0 ? NSOnState : NSOffState];
                [b setTarget:self];
                [b setAction:@selector(radioHit:)];
                [b setTag:i * 10 + k];
                [group addSubview:b];
            }
            v = group;
            break;
        }
        case W_POPUP: {
            CAWPopUp *p = [[[CAWPopUp alloc] initWithFrame:r pullsDown:NO] autorelease];
            [p setFont:f];
            for (int k = 0; k < 20; k++)
                [p addItemWithTitle:[NSString stringWithFormat:@"Item %d", k + 1]];
            [p setTarget:self];
            [p setAction:@selector(popupHit:)];
            [p setTag:i];
            v = p;
            break;
        }
        case W_SLIDER: {
            CAWSlider *sl = [[[CAWSlider alloc] initWithFrame:r] autorelease];
            [sl setMinValue:0];
            [sl setMaxValue:100];
            [sl setDoubleValue:35];
            [sl setTarget:self];
            [sl setAction:@selector(sliderHit:)];
            [sl setTag:i];
            v = sl;
            break;
        }
        case W_PROGDET: {
            CAWProgress *p = [[[CAWProgress alloc] initWithFrame:r] autorelease];
            [p setIndeterminate:NO];
            [p setMinValue:0];
            [p setMaxValue:100];
            [p setDoubleValue:45];
            v = p;
            break;
        }
        case W_PROGINDET: {
            CAWProgress *p = [[[CAWProgress alloc] initWithFrame:r] autorelease];
            [p setStyle:NSProgressIndicatorSpinningStyle];
            [p setIndeterminate:YES];
            [p startAnimation:nil];
            v = p;
            break;
        }
        }
        [parent addSubview:v];
        [_controls addObject:v];
    }
    fprintf(stderr, "built %d controls (%d in the overflow region)\n",
            gSpecCount, 6);
}

// ---- actions

- (void)buttonHit:(id)sender
{
    fprintf(stderr, "button hit: tag %d, page y %.0f\n",
            (int)[sender tag], gSpecs[[sender tag]].pageY);
}

- (void)checkHit:(id)sender
{
    if (gSpecs[[sender tag]].inOverflow) {
        fprintf(stderr, "overflow checkbox -> %d\n", (int)[sender state]);
        return;
    }
    gCheckValue = [sender state] == NSOnState;
    fprintf(stderr, "checkbox -> %s, repainting the tile under it\n",
            gCheckValue ? "ON" : "OFF");
    // The content-process round trip, simulated: invalidate, the tile repaints,
    // the new value appears in pixels the page painted.
    [_gl invalidatePageRect:CGRectMake(40, gCheckPageY + 20, 400, 30)];
}

- (void)radioHit:(id)sender
{
    NSView *group = [sender superview];
    NSEnumerator *e = [[group subviews] objectEnumerator];
    id b;
    while ((b = [e nextObject]))
        [b setState:(b == sender) ? NSOnState : NSOffState];
    fprintf(stderr, "radio -> tag %d\n", (int)[sender tag]);
}

- (void)popupHit:(id)sender
{
    fprintf(stderr, "popup tag %d -> item %d (%s)\n", (int)[sender tag],
            (int)[sender indexOfSelectedItem],
            [[sender titleOfSelectedItem] UTF8String]);
}

- (void)sliderHit:(id)sender
{
    fprintf(stderr, "slider tag %d -> %.0f\n", (int)[sender tag],
            [sender doubleValue]);
}

// ---- scrolling

- (void)setScrollY:(double)y
{
    double maxScroll = PAGE_HEIGHT - [_gl bounds].size.height;
    if (maxScroll < 0) maxScroll = 0;
    _scrollY = y < 0 ? 0 : (y > maxScroll ? maxScroll : y);
    [_pageScroller setFloatValue:(maxScroll > 0 ? (float)(_scrollY / maxScroll) : 0)
                  knobProportion:(float)([_gl bounds].size.height / PAGE_HEIGHT)];
}

- (void)pageScrollerAction:(id)sender
{
    double maxScroll = PAGE_HEIGHT - [_gl bounds].size.height;
    switch ([sender hitPart]) {
    case NSScrollerKnob:
    case NSScrollerKnobSlot: [self setScrollY:[sender floatValue] * maxScroll]; break;
    case NSScrollerDecrementLine: [self setScrollY:_scrollY - 40]; break;
    case NSScrollerIncrementLine: [self setScrollY:_scrollY + 40]; break;
    case NSScrollerDecrementPage: [self setScrollY:_scrollY - 300]; break;
    case NSScrollerIncrementPage: [self setScrollY:_scrollY + 300]; break;
    default: break;
    }
}

- (void)ovfScrollerAction:(id)sender
{
    double maxScroll = OVF_CONTENT - OVF_H;
    switch ([sender hitPart]) {
    case NSScrollerKnob:
    case NSScrollerKnobSlot: _ovfScrollY = [sender floatValue] * maxScroll; break;
    case NSScrollerDecrementLine: _ovfScrollY -= 20; break;
    case NSScrollerIncrementLine: _ovfScrollY += 20; break;
    case NSScrollerDecrementPage: _ovfScrollY -= 100; break;
    case NSScrollerIncrementPage: _ovfScrollY += 100; break;
    default: break;
    }
    if (_ovfScrollY < 0) _ovfScrollY = 0;
    if (_ovfScrollY > maxScroll) _ovfScrollY = maxScroll;
}

// The wheel arrives here from every control, because they forward it.
- (void)scrollWheel:(NSEvent *)e
{
    [self setScrollY:_scrollY - [e deltaY] * 20.0];
}

- (BOOL)acceptsFirstResponder { return YES; }

// AppKit's own Tab traversal is gated on the "full keyboard access" mode, which
// is a global preference, off by default on Tiger, and not overridable from our
// own defaults domain. A browser owns Tab anyway, so do what WebCore's
// FocusController does and walk our own chain.
- (void)moveFocusBy:(int)delta
{
    int n = (int)[_keyChain count];
    if (!n)
        return;
    id fr = [[self window] firstResponder];
    int idx = -1;
    for (int i = 0; i < n; i++)
        if ([_keyChain objectAtIndex:i] == fr) { idx = i; break; }
    idx = (idx < 0) ? 0 : (idx + delta + n) % n;
    [[self window] makeFirstResponder:[_keyChain objectAtIndex:idx]];
}

- (void)keyDown:(NSEvent *)e
{
    NSString *ch = [e charactersIgnoringModifiers];
    if ([ch length] == 1 && [ch characterAtIndex:0] == '\t') {
        [self moveFocusBy:([e modifierFlags] & NSShiftKeyMask) ? -1 : 1];
        return;
    }
    [super keyDown:e];
}

// ---- per-frame layout
//
// Every control's frame is recomputed from its page rect and set BEFORE the
// CARenderer frame, so the controls and the tiles move in the same screen
// update instead of a frame apart.

- (int)layoutControls
{
    NSRect vb = [_gl bounds];
    int visible = 0;

    // The overflow region's clip view, positioned like any other page element.
    double clipTop = OVF_Y - _scrollY;
    NSRect clipFrame = NSMakeRect(OVF_X, clipTop, OVF_W - SCROLLER_W, OVF_H);
    BOOL clipVisible = clipTop + OVF_H > 0 && clipTop < vb.size.height;
    if (![_clip isHidden] && (!clipVisible || !NSEqualRects([_clip frame], clipFrame)))
        [_gl setNeedsDisplayInRect:[_clip frame]];
    [_clip setHidden:!clipVisible];
    if (clipVisible && !NSEqualRects([_clip frame], clipFrame)) {
        [_clip setFrame:clipFrame];
        // A clip view that just moved or reappeared has to redraw its whole
        // subtree; AppKit will not do it for a non-opaque view on its own.
        [_clip setNeedsDisplay:YES];
        NSEnumerator *e = [[_clip subviews] objectEnumerator];
        id sv;
        while ((sv = [e nextObject]))
            [sv setNeedsDisplay:YES];
    }
    if (![_ovfScroller isHidden] && !clipVisible)
        [_gl setNeedsDisplayInRect:[_ovfScroller frame]];
    [_ovfScroller setHidden:!clipVisible];
    if (clipVisible) {
        [_gl setNeedsDisplayInRect:[_ovfScroller frame]];
        [_ovfScroller setFrame:NSMakeRect(OVF_X + OVF_W - SCROLLER_W, clipTop,
                                          SCROLLER_W, OVF_H)];
        [_ovfScroller setFloatValue:(float)(_ovfScrollY / (OVF_CONTENT - OVF_H))
                     knobProportion:(float)(OVF_H / OVF_CONTENT)];
    }

    for (int i = 0; i < gSpecCount; i++) {
        WidgetSpec *s = &gSpecs[i];
        NSView *v = [_controls objectAtIndex:i];
        if (s->inOverflow) {
            if (!clipVisible) {
                [v setHidden:YES];
                continue;
            }
            NSRect r = NSMakeRect(s->pageX, s->pageY - _ovfScrollY, s->w, s->h);
            if (!NSEqualRects([v frame], r)) {
                [[v superview] setNeedsDisplayInRect:[v frame]];
                [v setFrame:r];
                [v setNeedsDisplay:YES];
            }
            BOOL vis = NSIntersectsRect(r, [_clip bounds]);
            [v setHidden:!vis];
            if (vis) visible++;
            continue;
        }
        NSRect r = NSMakeRect(s->pageX, s->pageY - _scrollY, s->w, s->h);
        BOOL vis = r.origin.y + r.size.height > 0 && r.origin.y < vb.size.height;
        if (![v isHidden] && !vis)
            [[v superview] setNeedsDisplayInRect:[v frame]];
        [v setHidden:!vis];
        if (vis) {
            // Moving a subview of a non-opaque view leaves the vacated rect
            // untouched in the backing store: there is no opaque ancestor to
            // repaint it, so the control smears a trail down the page. Invalidate
            // the old rect so the GL view punches it transparent again, and ask
            // for the new one explicitly, because -setFrame: alone does not
            // redraw reliably here. Only when it actually moved, or the window
            // flush costs 25 ms a frame for nothing.
            if (!NSEqualRects([v frame], r)) {
                [[v superview] setNeedsDisplayInRect:[v frame]];
                [v setFrame:r];
                [v setNeedsDisplay:YES];
            }
            visible++;
        }
    }
    // AppKit only walks the key view loop that nextKeyView links describe, and
    // the visible set changes as the page scrolls, so rebuild the chain whenever
    // it does. Without this Tab does nothing at all.
    if (visible != _visibleCount || !_keyLoopBuilt) {
        NSMutableArray *chain = [NSMutableArray array];
        for (int i = 0; i < gSpecCount; i++) {
            NSView *v = [_controls objectAtIndex:i];
            if ([v isHidden] || gSpecs[i].kind == W_PROGDET
                || gSpecs[i].kind == W_PROGINDET)
                continue;
            if (gSpecs[i].kind == W_RADIO) {
                NSEnumerator *e = [[v subviews] objectEnumerator];
                id b;
                while ((b = [e nextObject]))
                    [chain addObject:b];
            } else
                [chain addObject:v];
        }
        for (int i = 0; i < (int)[chain count]; i++)
            [[chain objectAtIndex:i]
                setNextKeyView:[chain objectAtIndex:(i + 1) % [chain count]]];
        [_keyChain release];
        _keyChain = [chain retain];
        if ([chain count] && ![[[self window] firstResponder] isKindOfClass:[NSControl class]]) {
            [[self window] setInitialFirstResponder:[chain objectAtIndex:0]];
            [[self window] makeFirstResponder:[chain objectAtIndex:0]];
        }
        _keyLoopBuilt = YES;
    }

    static int once = 0;
    if (!once++ && getenv("CAW_DEBUG")) {
        for (int i = 0; i < 6; i++) {
            NSView *v = [_controls objectAtIndex:i];
            fprintf(stderr, "  [%d] %s frame %s hidden=%d super=%s win=%p\n", i,
                    [[[v class] description] UTF8String],
                    [NSStringFromRect([v frame]) UTF8String], (int)[v isHidden],
                    [[[[v superview] class] description] UTF8String], [v window]);
        }
    }
    return visible;
}

// ---- the frame

- (void)tick
{
    CFTimeInterval now = CACurrentMediaTime();

    if (_autoScrolling) {
        double next = _scrollY + 900.0 / 60.0;
        if (next >= _autoTarget) {
            next = _autoTarget;
            _autoScrolling = NO;
            fprintf(stderr,
                    "SCROLL DONE over %u frames: CA %.2f ms + layout %.2f ms"
                    " + window flush %.2f ms = %.2f ms/frame, %d controls visible\n",
                    _scrollFrames,
                    _scrollCA * 1000.0 / _scrollFrames,
                    _scrollLayout * 1000.0 / _scrollFrames,
                    _scrollFlush * 1000.0 / _scrollFrames,
                    (_scrollCA + _scrollLayout + _scrollFlush) * 1000.0 / _scrollFrames,
                    _visibleCount);
        }
        [self setScrollY:next];
    }

    CFTimeInterval t0 = CACurrentMediaTime();
    _visibleCount = [self layoutControls];
    [_gl updateTilesForScroll:_scrollY];
    CFTimeInterval t1 = CACurrentMediaTime();

    double ca = [_gl renderFrame];
    CFTimeInterval t2 = CACurrentMediaTime();

    // Without this the AppKit half lands on the next run-loop flush, a frame
    // behind the GL surface, and the controls visibly lag the page.
    if (!getenv("CAW_NOSYNC")) {
        [[self window] displayIfNeeded];
        [[self window] flushWindowIfNeeded];
    }
    CFTimeInterval t3 = CACurrentMediaTime();

    _layoutAccum += t1 - t0;
    _caAccum += ca;
    _flushAccum += t3 - t2;
    _frames++;
    if (_autoScrolling) {
        _scrollLayout += t1 - t0;
        _scrollCA += ca;
        _scrollFlush += t3 - t2;
        _scrollFrames++;
    }
    (void)t2;

    if (_frames >= 60) {
        struct task_basic_info info;
        mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
        task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&info, &count);
        fprintf(stderr, "CA %.2f ms | layout %.2f ms | flush %.2f ms | %d visible"
                        " | rss %.1f MB\n",
                _caAccum * 1000.0 / _frames, _layoutAccum * 1000.0 / _frames,
                _flushAccum * 1000.0 / _frames, _visibleCount,
                info.resident_size / 1048576.0);
        _caAccum = _layoutAccum = _flushAccum = 0;
        _frames = 0;
    }

    if (getenv("CAW_KEYS")) {
        static int step = 0;
        static CFTimeInterval last = 0;
        if (now - _started > 3.0 && now - last > 0.7 && step < 8) {
            last = now;
            switch (step) {
            case 0: case 1: case 2:
                postKey(48, @"\t", 0);           // Tab: walk the key view loop
                break;
            case 3:
                // Land on the page checkbox deterministically, then press it:
                // the point is that space toggles a focused control and that the
                // page repaints the new value into a tile.
                for (int i = 0; i < gSpecCount; i++)
                    if (gSpecs[i].kind == W_CHECK && !gSpecs[i].inOverflow) {
                        [[self window] makeFirstResponder:
                            [_controls objectAtIndex:i]];
                        break;
                    }
                postKey(49, @" ", 0);            // space: press / toggle
                break;
            case 4: case 5:
                postKey(48, @"\t", 0);
                break;
            case 6:
                postKey(124, [NSString stringWithFormat:@"%C", (unsigned short)NSRightArrowFunctionKey], NSNumericPadKeyMask);
                break;
            case 7:
                postKey(36, @"\r", 0);           // return: the default button
                break;
            }
            step++;
            id fr = [[self window] firstResponder];
            fprintf(stderr, "key step %d -> first responder %s tag %d at %s\n", step,
                    [[[fr class] description] UTF8String],
                    [fr respondsToSelector:@selector(tag)] ? (int)[fr tag] : -1,
                    [fr isKindOfClass:[NSView class]]
                        ? [NSStringFromRect([fr frame]) UTF8String] : "-");
        }
    }

    if (getenv("CAW_POPUP")) {
        static BOOL done = NO;
        if (!done && now - _started > 3.0) {
            done = YES;
            for (int i = 0; i < gSpecCount; i++)
                if (gSpecs[i].kind == W_POPUP && ![[_controls objectAtIndex:i] isHidden]) {
                    fprintf(stderr, "opening the popup menu (this blocks in tracking)\n");
                    [(NSPopUpButton *)[_controls objectAtIndex:i] performClick:nil];
                    break;
                }
        }
    }

    if (getenv("CAW_AUTO") && !_autoScrolling && _scrollY == 0
        && now - _started > 4.0) {
        fprintf(stderr, "-- starting scripted scroll, %d controls live\n", gSpecCount);
        _autoTarget = 1450;
        _autoScrolling = YES;
    }
}

- (void)start
{
    [self buildControls];
    [self setScrollY:0];
    _started = CACurrentMediaTime();
    _timer = [NSTimer timerWithTimeInterval:1.0 / 60.0 target:self
                                   selector:@selector(tick)
                                   userInfo:nil repeats:YES];
    [[NSRunLoop currentRunLoop] addTimer:_timer
                                 forMode:(NSString *)kCFRunLoopCommonModes];
}

@end

// ---------------------------------------------------------------- app

@interface CAWDelegate : NSObject
@end
@implementation CAWDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)a { return YES; }
@end

int main(int argc, const char **argv)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    // Aqua focus rings and Tab-to-button only exist when full keyboard access is
    // on. It is a global preference and off by default on Tiger; setting it in
    // our own domain turns it on for this process without touching the machine.
    [[NSUserDefaults standardUserDefaults]
        setObject:[NSNumber numberWithInt:2] forKey:@"AppleKeyboardUIMode"];

    NSApplication *app = [NSApplication sharedApplication];
    NSMenu *menubar = [[[NSMenu alloc] init] autorelease];
    NSMenuItem *appItem = [[[NSMenuItem alloc] init] autorelease];
    [menubar addItem:appItem];
    NSMenu *appMenu = [[[NSMenu alloc] init] autorelease];
    [appMenu addItemWithTitle:@"Quit" action:@selector(terminate:) keyEquivalent:@"q"];
    [appItem setSubmenu:appMenu];
    [app setMainMenu:menubar];

    buildSpecTable();

    NSOpenGLPixelFormatAttribute attrs[] = {
        NSOpenGLPFADoubleBuffer, NSOpenGLPFAAccelerated,
        NSOpenGLPFAColorSize, 32, NSOpenGLPFADepthSize, 24, 0
    };
    NSOpenGLPixelFormat *pf =
        [[[NSOpenGLPixelFormat alloc] initWithAttributes:attrs] autorelease];

    NSRect frame = NSMakeRect(0, 0, 660, 520);
    NSWindow *win = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:(NSTitledWindowMask | NSClosableWindowMask |
                             NSMiniaturizableWindowMask | NSResizableWindowMask)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    [win setTitle:@"CAWidgets - Aqua controls over a CA-hosted page"];
    [win setOpaque:NO];
    [win setBackgroundColor:[NSColor clearColor]];

    CAWHostView *host = [[[CAWHostView alloc] initWithFrame:frame
                                                pixelFormat:pf] autorelease];
    [host setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [win setContentView:host];
    [win setFrameOrigin:NSMakePoint(40, 300)];
    [win makeKeyAndOrderFront:nil];
    [win orderFrontRegardless];
    [win makeFirstResponder:host];

    [host start];
    [app setDelegate:[[[CAWDelegate alloc] init] autorelease]];
    [app activateIgnoringOtherApps:YES];
    [app run];
    [pool release];
    return 0;
}
