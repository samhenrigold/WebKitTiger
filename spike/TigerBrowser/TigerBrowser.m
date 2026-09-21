// TigerBrowser - the 32-bit UI-process app shell for the WebKit2-shaped split
// (see NOTES.md "DIRECTION SET BY THE USER" and "WEBKIT2 SPLIT SURVEY").
//
// This merges spike/CAHost's Core Animation compositor host (phase 2: tiled
// CALayer page, software-painted via -drawLayer:inContext:, hosted in a
// CARenderer over an NSOpenGLView) into the browser chrome shell that used to
// host a classic WebKit1 WebView. No real web content and no 64-bit content
// process are wired up yet -- TigerPageView paints a stub placeholder page
// (checkerboard tiles + the current URL banner) so the window's scrolling,
// resizing and hit-testing can be exercised and built on. See README.md for
// the file/class structure this leaves for the next piece of UI-process work.
//
// Build: see Makefile. MRR, fragile ObjC runtime.

#import <Cocoa/Cocoa.h>
#import <mach-o/dyld.h>
#include <errno.h>
#include <stdarg.h>

// The Leopard CA headers mark the whole API 10.5+, and we deploy at 10.4. The
// binary (the rebundled Apple TV QuartzCore) is a Darwin 8 build, so the
// availability annotation is simply wrong for us; neuter it, same trick as
// spike/CAHost/CAHost.m.
#include <AvailabilityMacros.h>
#undef AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER
#define AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER
#undef AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER_BUT_DEPRECATED
#define AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER_BUT_DEPRECATED

#import <QuartzCore/CoreAnimation.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>

// Present in this QuartzCore binary but absent from the Leopard headers
// (geometryFlipped is 10.6 public; contentsAreFlipped was always private).
@interface CALayer (TigerCAPrivate)
- (void)setGeometryFlipped:(BOOL)b;
- (BOOL)geometryFlipped;
- (BOOL)contentsAreFlipped;
@end

#define PAGE_WIDTH   800.0
#define PAGE_HEIGHT  3000.0
#define TILE_SIZE    256.0
#define BANNER_HEIGHT 60.0

// ------------------------------------------------------------- text input log
//
// logs/textinput-plan.md spike: everything the NSTextInput/NSInputManager
// path produces gets logged here, to a real file opened line-buffered
// (setvbuf _IOLBF), not just to stderr -- stderr redirected to a file over
// ssh on this box is fully block-buffered, which lost log lines in an
// earlier spike (spike/TigerBrowser's own README "Known rough edges").

static FILE *gTextInputLog;

static void tiLogOpen(const char *path)
{
    gTextInputLog = fopen(path, "w");
    if (gTextInputLog)
        setvbuf(gTextInputLog, NULL, _IOLBF, 0);
    else
        fprintf(stderr, "WARNING: could not open text input log at %s: %s\n", path, strerror(errno));
}

static void tiLog(NSString *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    NSString *msg = [[NSString alloc] initWithFormat:fmt arguments:args];
    va_end(args);
    fprintf(stderr, "%s\n", [msg UTF8String]);
    if (gTextInputLog) {
        fprintf(gTextInputLog, "%s\n", [msg UTF8String]);
        fflush(gTextInputLog);
    }
    [msg release];
}

// Standard US ANSI virtual keycodes (HIToolbox Events.h's kVK_* constants,
// not available in the AppKit-only headers this app imports, so spelled out
// locally). Used only by the synthetic self-test driver below.
enum {
    TKC_A = 0, TKC_S = 1, TKC_D = 2, TKC_H = 4, TKC_E = 14,
    TKC_Return = 36, TKC_Tab = 48, TKC_Space = 49, TKC_Delete = 51, TKC_Escape = 53,
    TKC_ForwardDelete = 117,
    TKC_LeftArrow = 123, TKC_RightArrow = 124, TKC_DownArrow = 125, TKC_UpArrow = 126,
};

// NSEvent's `characters`/`charactersIgnoringModifiers` for non-printing keys
// are not empty strings -- they're the OpenStep-reserved private-use glyphs
// (NSEvent.h's NS*FunctionKey constants, 0xF700-0xF8FF). A real key press
// already carries these by the time NSInputManager sees it; a synthetic
// NSEvent has to supply them explicitly or interpretKeyEvents: has nothing
// to dispatch from (see the self-test's report note on this).
#define kTILeftArrow ([NSString stringWithFormat:@"%C", (unichar)NSLeftArrowFunctionKey])
#define kTIRightArrow ([NSString stringWithFormat:@"%C", (unichar)NSRightArrowFunctionKey])
#define kTIForwardDelete ([NSString stringWithFormat:@"%C", (unichar)NSDeleteFunctionKey])

// ---------------------------------------------------------- frameworks check

// "Frameworks loaded" check the task asked for: fail loudly at launch if the
// private, decollided QuartzCore (Contents/Frameworks, rebundle.sh's output)
// isn't the one actually bound. Running against the system QuartzCore instead
// would silently lose CARenderer/CATiledLayer/geometryFlipped behavior this
// whole view depends on (or, if this process ever also loads QTKit later,
// collide on 207 duplicate CI* class names -- see spike/CAHost/decollide.py).
static void checkFrameworksLoaded(void)
{
    uint32_t count = _dyld_image_count();
    BOOL foundPrivate = NO, foundSystem = NO;
    uint32_t i;
    for (i = 0; i < count; i++) {
        const char *name = _dyld_get_image_name(i);
        if (!name || !strstr(name, "QuartzCore.framework"))
            continue;
        // dyld records the install name as recorded in the load command, not
        // a resolved absolute path -- our private copy's install name is
        // "@executable_path/../Frameworks/QuartzCore.framework/...", which
        // never contains "/System/Library". That's the discriminator.
        if (strstr(name, "/System/Library/Frameworks/QuartzCore.framework"))
            foundSystem = YES;
        else
            foundPrivate = YES;
        fprintf(stderr, "  QuartzCore image: %s\n", name);
    }
    if (!foundPrivate) {
        fprintf(stderr, "FATAL: bundled Contents/Frameworks/QuartzCore.framework is NOT loaded "
                         "(private, decollided Apple TV QuartzCore expected). Refusing to run.\n");
        NSRunAlertPanel(@"TigerBrowser", @"The bundled QuartzCore.framework failed to load. "
                        @"TigerBrowser cannot host Core Animation without it.", @"Quit", nil, nil);
        exit(1);
    }
    fprintf(stderr, "Frameworks loaded check: OK (private QuartzCore bound%s)\n",
            foundSystem ? "; system QuartzCore ALSO loaded, unexpected" : "");
}

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
    size_t x, y;
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            int on = ((x + y) & 1);
            CGContextSetRGBFillColor(bmp, on ? 0.85 : 0.94, on ? 0.87 : 0.94,
                                      on ? 0.90 : 0.94, 1.0);
            CGContextFillRect(bmp, CGRectMake(x * 8, y * 8, 8, 8));
        }
    }
    CGImageRef img = CGBitmapContextCreateImage(bmp);
    CGContextRelease(bmp);
    return img;
}

// -------------------------------------------------- the stub page "painter"
//
// Stands in for WebCore's -paintGraphicsLayerContents until the 64-bit content
// process exists. A checkerboard fill (so tiling/scrolling is obvious) plus a
// banner near the top of the page showing the current URL.

static CGImageRef gChecker;

static void paintStubPage(CGContextRef ctx, CGRect dirty, NSString *urlText)
{
    CGContextSetRGBFillColor(ctx, 1, 1, 1, 1);
    CGContextFillRect(ctx, dirty);

    if (gChecker) {
        CGFloat startX = floor(dirty.origin.x / 64) * 64;
        CGFloat startY = floor(dirty.origin.y / 64) * 64;
        CGFloat x, y;
        for (y = startY; y < dirty.origin.y + dirty.size.height; y += 64) {
            for (x = startX; x < dirty.origin.x + dirty.size.width; x += 64)
                CGContextDrawImage(ctx, CGRectMake(x, y, 64, 64), gChecker);
        }
    }

    CGRect banner = CGRectMake(0, 0, PAGE_WIDTH, BANNER_HEIGHT);
    if (CGRectIntersectsRect(banner, dirty)) {
        CGContextSetRGBFillColor(ctx, 0.15, 0.25, 0.55, 1.0);
        CGContextFillRect(ctx, banner);
        CGContextSelectFont(ctx, "Helvetica", 16, kCGEncodingMacRoman);
        CGContextSetTextMatrix(ctx, CGAffineTransformMakeScale(1.0, -1.0));
        CGContextSetRGBFillColor(ctx, 1, 1, 1, 1);
        const char *text = urlText ? [urlText UTF8String] : "(no URL)";
        CGContextShowTextAtPoint(ctx, 16, 38, text, strlen(text));
    }
}

// ---------------------------------------------------------------- TigerPageView
//
// The compositor host: viewport (masksToBounds, geometryFlipped) > page, with
// a manual grid of tile CALayers painted on demand -- CATiledLayer's delegate
// runs on this QuartzCore build but its tiles never reach CARenderer's output
// (spike/CAHost's finding), so tiling is done by hand, same as CAHost phase 2.

@interface TigerPageView : NSOpenGLView <NSTextInput>
{
    CARenderer *_renderer;
    CALayer *_viewport;
    CALayer *_page;
    NSMutableArray *_tileLayers;
    int _tileCols, _tileRows;
    NSScroller *_scroller;
    NSTimer *_timer;
    double _scrollY;
    NSString *_urlText;

    // ---- text input (logs/textinput-plan.md spike) ----
    // "Applied" = what the synchronous NSTextInput query methods answer from
    // (stands in for the UI process's locally cached EditorState, per the
    // plan's §3 flagged design problem). "Pending" = what this keyDown's
    // insertText:/setMarkedText:/unmarkText calls have produced so far;
    // copied into "applied" only after a simulated IPC round trip delay, so
    // a query made before that lands sees stale state on purpose.
    NSMutableString *_appliedDocumentText;
    NSRange _appliedSelectedRange;
    NSRange _appliedMarkedRange;
    NSMutableString *_pendingDocumentText;
    NSRange _pendingSelectedRange;
    NSRange _pendingMarkedRange;
    NSMutableArray *_keyDownCommandLog;   // this keydown's command names, for the summary line
    BOOL _keyDownHadPendingMutation;
    NSTimer *_selfTestTimer;
    NSArray *_selfTestScript;
    unsigned _selfTestIndex;
}
- (void)setScroller:(NSScroller *)s;
- (void)setURLText:(NSString *)text;
- (void)setScrollY:(double)y;
- (void)startTextInputSelfTest;
@end

@implementation TigerPageView

- (id)initWithFrame:(NSRect)frameRect pixelFormat:(NSOpenGLPixelFormat *)format
{
    self = [super initWithFrame:frameRect pixelFormat:format];
    if (!self)
        return nil;
    _appliedDocumentText = [[NSMutableString alloc] initWithString:@"The quick brown fox jumps over the lazy dog."];
    _appliedSelectedRange = NSMakeRange([_appliedDocumentText length], 0);
    _appliedMarkedRange = NSMakeRange(NSNotFound, 0);
    _pendingDocumentText = [_appliedDocumentText mutableCopy];
    _pendingSelectedRange = _appliedSelectedRange;
    _pendingMarkedRange = _appliedMarkedRange;
    return self;
}

- (void)dealloc
{
    [_timer invalidate];
    [_selfTestTimer invalidate];
    [_renderer release];
    [_viewport release];
    [_page release];
    [_tileLayers release];
    [_urlText release];
    [_appliedDocumentText release];
    [_pendingDocumentText release];
    [_selfTestScript release];
    [super dealloc];
}

- (BOOL)isOpaque { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)setScroller:(NSScroller *)s { _scroller = s; }

// ---- layer tree / tile grid

- (void)buildLayerTree
{
    NSRect b = [self bounds];

    _viewport = [[CALayer layer] retain];
    [_viewport setName:@"viewport"];
    [_viewport setAnchorPoint:CGPointMake(0, 0)];
    [_viewport setPosition:CGPointMake(0, 0)];
    [_viewport setBounds:CGRectMake(0, 0, b.size.width, b.size.height)];
    [_viewport setMasksToBounds:YES];
    if ([_viewport respondsToSelector:@selector(setGeometryFlipped:)])
        [_viewport setGeometryFlipped:YES];
    CGColorRef c = makeColor(0.85, 0.85, 0.85, 1);
    [_viewport setBackgroundColor:c];
    CGColorRelease(c);

    _page = [[CALayer layer] retain];
    [_page setName:@"page"];
    [_page setAnchorPoint:CGPointMake(0, 0)];
    [_page setPosition:CGPointMake(0, 0)];
    [_page setBounds:CGRectMake(0, 0, PAGE_WIDTH, PAGE_HEIGHT)];
    [_viewport addSublayer:_page];

    [self buildTileGrid];
}

- (void)buildTileGrid
{
    _tileCols = (int)ceil(PAGE_WIDTH / TILE_SIZE);
    _tileRows = (int)ceil(PAGE_HEIGHT / TILE_SIZE);
    _tileLayers = [[NSMutableArray alloc] init];

    NSDictionary *noActions = [NSDictionary dictionaryWithObjectsAndKeys:
        [NSNull null], @"contents", [NSNull null], @"position",
        [NSNull null], @"bounds", nil];

    int row, col;
    for (row = 0; row < _tileRows; row++) {
        for (col = 0; col < _tileCols; col++) {
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
    fprintf(stderr, "TigerPageView: tile grid %d x %d = %d tiles of %.0fpx\n",
            _tileCols, _tileRows, (int)[_tileLayers count], TILE_SIZE);
}

- (void)updateTiles
{
    NSRect b = [self bounds];
    CGRect visible = CGRectMake(0, _scrollY - TILE_SIZE, PAGE_WIDTH, b.size.height + 2 * TILE_SIZE);
    int i;
    for (i = 0; i < (int)[_tileLayers count]; i++) {
        CALayer *tile = [_tileLayers objectAtIndex:i];
        CGPoint org = [tile position];
        CGRect r = CGRectMake(org.x, org.y, [tile bounds].size.width, [tile bounds].size.height);
        BOOL want = CGRectIntersectsRect(r, visible);
        BOOL have = [tile contents] != nil;
        if (want && !have)
            [tile setNeedsDisplay];
        else if (!want && have)
            [tile setContents:nil];
    }
}

- (void)setURLText:(NSString *)text
{
    [_urlText release];
    _urlText = [text copy];
    // Only the banner (page y 0..BANNER_HEIGHT) needs repainting.
    CGRect banner = CGRectMake(0, 0, PAGE_WIDTH, BANNER_HEIGHT);
    int i;
    for (i = 0; i < (int)[_tileLayers count]; i++) {
        CALayer *tile = [_tileLayers objectAtIndex:i];
        CGPoint org = [tile position];
        CGRect r = CGRectMake(org.x, org.y, [tile bounds].size.width, [tile bounds].size.height);
        if (CGRectIntersectsRect(r, banner) && [tile contents])
            [tile setNeedsDisplay];
    }
}

// ---- CALayer delegate: the software content path (per-tile paint)

- (void)drawLayer:(CALayer *)layer inContext:(CGContextRef)ctx
{
    CGContextSaveGState(ctx);
    BOOL flipped = [layer respondsToSelector:@selector(contentsAreFlipped)]
                   && [layer contentsAreFlipped];
    if (!flipped) {
        CGRect lb = [layer bounds];
        CGContextScaleCTM(ctx, 1, -1);
        CGContextTranslateCTM(ctx, 0, -lb.size.height);
    }
    CGPoint org = [layer position];
    CGContextTranslateCTM(ctx, -org.x, -org.y);
    CGRect clip = CGContextGetClipBoundingBox(ctx);
    paintStubPage(ctx, clip, _urlText);
    CGContextRestoreGState(ctx);
}

// ---- scrolling

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

// ---- hit testing (view point -> page point -> layer), logged like CAHost

- (CGPoint)pagePointForViewPoint:(NSPoint)p
{
    return CGPointMake(p.x, ([self bounds].size.height - p.y) + _scrollY);
}

- (void)mouseDown:(NSEvent *)e
{
    NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
    CALayer *hit = [_viewport hitTest:CGPointMake(p.x, p.y)];
    CGPoint pagePt = [self pagePointForViewPoint:p];
    fprintf(stderr, "hit: view(%.0f,%.0f) -> page(%.0f,%.0f) -> layer '%s'\n",
            p.x, p.y, pagePt.x, pagePt.y, hit ? [[hit name] UTF8String] : "(none)");
    [[self window] makeFirstResponder:self];
}

// ---- Edit menu: responder-chain actions that just log, per the task (no
// real text editing model exists yet -- this is the shell).

- (void)logEditCommand:(NSString *)name
{
    tiLog(@"Edit menu command (page view responder chain): %@", name);
}
- (void)cut:(id)sender { [self logEditCommand:@"cut:"]; }
- (void)copy:(id)sender { [self logEditCommand:@"copy:"]; }
- (void)paste:(id)sender { [self logEditCommand:@"paste:"]; }
- (void)selectAll:(id)sender { [self logEditCommand:@"selectAll:"]; }
- (void)undo:(id)sender { [self logEditCommand:@"undo:"]; }
- (void)redo:(id)sender { [self logEditCommand:@"redo:"]; }

// ---- text input (logs/textinput-plan.md spike) ----
//
// keyDown: drives Tiger's synchronous NSInputManager path
// (-interpretKeyEvents:), which synchronously calls back into whichever of
// the methods below the active input method/keyboard layout needs, in
// order, before returning -- no async holding tank, unlike the modern
// NSTextInputContext path (see the plan's §1a). This spike logs every call
// instead of building a Vector<KeypressCommand>/sending IPC.

- (NSString *)tiDescribeString:(id)aString
{
    if ([aString isKindOfClass:[NSAttributedString class]])
        return [NSString stringWithFormat:@"NSAttributedString '%@'", [(NSAttributedString *)aString string]];
    return [NSString stringWithFormat:@"NSString '%@'", aString];
}

- (NSString *)tiDescribeApplied
{
    return [NSString stringWithFormat:@"selectedRange=%@ markedRange=%@ doc(len=%lu)=\"%@\"",
            NSStringFromRange(_appliedSelectedRange),
            _appliedMarkedRange.location == NSNotFound ? @"{NSNotFound,0}" : NSStringFromRange(_appliedMarkedRange),
            (unsigned long)[_appliedDocumentText length], _appliedDocumentText];
}

- (void)keyDown:(NSEvent *)event
{
    tiLog(@"--");
    tiLog(@"keyDown: keyCode=%u characters='%@' charactersIgnoringModifiers='%@' modifierFlags=0x%lx",
          (unsigned)[event keyCode], [event characters], [event charactersIgnoringModifiers],
          (unsigned long)[event modifierFlags]);

    _keyDownCommandLog = [[NSMutableArray alloc] init];
    _keyDownHadPendingMutation = NO;
    // Fresh pending state starts as a copy of the last *applied* (i.e.
    // possibly still-stale) state, per keydown -- composes correctly if a
    // single interpretKeyEvents: call produces several callbacks (e.g. a
    // dead key's setMarkedText: followed immediately by unmarkText).
    [_pendingDocumentText setString:_appliedDocumentText];
    _pendingSelectedRange = _appliedSelectedRange;
    _pendingMarkedRange = _appliedMarkedRange;

    [self interpretKeyEvents:[NSArray arrayWithObject:event]];

    tiLog(@"keyDown: interpretKeyEvents: produced %lu command(s): [%@]",
          (unsigned long)[_keyDownCommandLog count],
          [_keyDownCommandLog componentsJoinedByString:@", "]);
    [_keyDownCommandLog release];
    _keyDownCommandLog = nil;

    if (_keyDownHadPendingMutation) {
        tiLog(@"  scheduling simulated content-process round trip (+50ms); synchronous queries"
              @" until then will see the OLD state: %@", [self tiDescribeApplied]);
        [self performSelector:@selector(applyPendingEditorState) withObject:nil afterDelay:0.05];
    }
}

// Stands in for the async EditorState reply the real content process would
// eventually send back over IPC after executing the KeypressCommands this
// keydown collected.
- (void)applyPendingEditorState
{
    [_appliedDocumentText setString:_pendingDocumentText];
    _appliedSelectedRange = _pendingSelectedRange;
    _appliedMarkedRange = _pendingMarkedRange;
    tiLog(@"  simulated content-process reply landed: %@", [self tiDescribeApplied]);
}

- (void)insertText:(id)aString
{
    NSString *desc = [self tiDescribeString:aString];
    BOOL hadMarkedText = _pendingMarkedRange.location != NSNotFound;
    tiLog(@"  insertText: %@ (markedTextActive=%@)", desc, hadMarkedText ? @"YES" : @"NO");
    [_keyDownCommandLog addObject:[NSString stringWithFormat:@"insertText:%@", desc]];

    NSString *plain = [aString isKindOfClass:[NSAttributedString class]] ? [(NSAttributedString *)aString string] : aString;
    NSRange replace = hadMarkedText ? _pendingMarkedRange : _pendingSelectedRange;
    if (replace.location == NSNotFound || NSMaxRange(replace) > [_pendingDocumentText length])
        replace = NSMakeRange([_pendingDocumentText length], 0);
    [_pendingDocumentText replaceCharactersInRange:replace withString:plain];
    _pendingSelectedRange = NSMakeRange(replace.location + [plain length], 0);
    _pendingMarkedRange = NSMakeRange(NSNotFound, 0);
    _keyDownHadPendingMutation = YES;
}

- (void)doCommandBySelector:(SEL)aSelector
{
    NSString *name = NSStringFromSelector(aSelector);
    tiLog(@"  doCommandBySelector: %@  (recorded only, NOT executed -- see plan for why the real"
          @" content process, not this view, owns Editor::Command dispatch)", name);
    [_keyDownCommandLog addObject:[NSString stringWithFormat:@"doCommandBySelector:%@", name]];
}

- (void)setMarkedText:(id)aString selectedRange:(NSRange)selRange
{
    NSString *desc = [self tiDescribeString:aString];
    tiLog(@"  setMarkedText:%@ selectedRange:%@", desc, NSStringFromRange(selRange));
    [_keyDownCommandLog addObject:[NSString stringWithFormat:@"setMarkedText:%@ sel:%@", desc, NSStringFromRange(selRange)]];

    NSString *plain = [aString isKindOfClass:[NSAttributedString class]] ? [(NSAttributedString *)aString string] : aString;
    NSRange replace = _pendingMarkedRange.location != NSNotFound ? _pendingMarkedRange : _pendingSelectedRange;
    if (replace.location == NSNotFound || NSMaxRange(replace) > [_pendingDocumentText length])
        replace = NSMakeRange([_pendingDocumentText length], 0);
    [_pendingDocumentText replaceCharactersInRange:replace withString:plain];
    _pendingMarkedRange = NSMakeRange(replace.location, [plain length]);
    _pendingSelectedRange = NSMakeRange(replace.location + selRange.location, selRange.length);
    _keyDownHadPendingMutation = YES;
}

- (void)unmarkText
{
    tiLog(@"  unmarkText (confirming composition \"%@\")",
          _pendingMarkedRange.location != NSNotFound
              ? [_pendingDocumentText substringWithRange:_pendingMarkedRange] : @"");
    [_keyDownCommandLog addObject:@"unmarkText"];
    _pendingMarkedRange = NSMakeRange(NSNotFound, 0);
    _keyDownHadPendingMutation = YES;
}

- (BOOL)hasMarkedText
{
    BOOL has = _appliedMarkedRange.location != NSNotFound;
    tiLog(@"  hasMarkedText -> %@ (from cached/applied EditorState)", has ? @"YES" : @"NO");
    return has;
}

- (long)conversationIdentifier
{
    return (long)self;
}

- (NSAttributedString *)attributedSubstringFromRange:(NSRange)theRange
{
    tiLog(@"  attributedSubstringFromRange:%@ (from cached/applied EditorState, doc len %lu)",
          NSStringFromRange(theRange), (unsigned long)[_appliedDocumentText length]);
    if (theRange.location == NSNotFound || NSMaxRange(theRange) > [_appliedDocumentText length])
        return nil;
    NSString *sub = [_appliedDocumentText substringWithRange:theRange];
    NSMutableAttributedString *attr = [[[NSMutableAttributedString alloc] initWithString:sub] autorelease];
    NSRange markedIntersection = NSIntersectionRange(theRange, _appliedMarkedRange);
    if (markedIntersection.length > 0) {
        NSRange local = NSMakeRange(markedIntersection.location - theRange.location, markedIntersection.length);
        [attr addAttribute:NSUnderlineStyleAttributeName value:[NSNumber numberWithInt:1] range:local];
    }
    return attr;
}

- (NSRange)markedRange
{
    tiLog(@"  markedRange -> %@ (from cached/applied EditorState)",
          _appliedMarkedRange.location == NSNotFound ? @"{NSNotFound,0}" : NSStringFromRange(_appliedMarkedRange));
    return _appliedMarkedRange;
}

- (NSRange)selectedRange
{
    tiLog(@"  selectedRange -> %@ (from cached/applied EditorState)", NSStringFromRange(_appliedSelectedRange));
    return _appliedSelectedRange;
}

- (NSRect)firstRectForCharacterRange:(NSRange)theRange
{
    // Fixed page position standing in for a real caret rect (would come from
    // the content process's layout). Page (24, 24), a 1x18 sliver, converted
    // view -> window -> screen the Tiger way (no -convertRectToScreen:, 10.7+).
    CGPoint viewPt = CGPointMake(24, [self bounds].size.height - (24 - _scrollY));
    NSPoint winPt = [self convertPoint:NSMakePoint(viewPt.x, viewPt.y) toView:nil];
    NSPoint screenPt = [[self window] convertBaseToScreen:winPt];
    NSRect rect = NSMakeRect(screenPt.x, screenPt.y, 1, 18);
    tiLog(@"  firstRectForCharacterRange:%@ -> {%.0f,%.0f,%.0f,%.0f} (fixed page-position stub,"
          @" from cached/applied EditorState -- a real cache miss here is visibly wrong, not just"
          @" logically stale, per the plan's §3 note)",
          NSStringFromRange(theRange), rect.origin.x, rect.origin.y, rect.size.width, rect.size.height);
    return rect;
}

- (unsigned int)characterIndexForPoint:(NSPoint)thePoint
{
    unsigned int idx = (unsigned int)[_appliedDocumentText length];
    tiLog(@"  characterIndexForPoint:{%.0f,%.0f} -> %u (stub: always end-of-document)",
          thePoint.x, thePoint.y, idx);
    return idx;
}

- (NSArray *)validAttributesForMarkedText
{
    // NSMarkedClauseSegmentAttributeName/NSTextAlternativesAttributeName/
    // NSTextInsertionUndoableAttributeName (webkitlegacy-plan.md's three
    // flagged 10.5+ constants) are absent from the 10.4u SDK's
    // NSAttributedString.h entirely -- confirmed by grep, not just omitted
    // here. NSUnderlineStyleAttributeName is Tiger-safe (10.0+).
    tiLog(@"  validAttributesForMarkedText -> [NSUnderlineStyleAttributeName] (Tiger-safe subset only)");
    return [NSArray arrayWithObject:NSUnderlineStyleAttributeName];
}

// ---- synthetic self-test driver (no ssh-typed input is possible on the box)

- (NSEvent *)tiEventChars:(NSString *)chars ignMods:(NSString *)ignMods mods:(unsigned int)mods keyCode:(unsigned short)code
{
    return [NSEvent keyEventWithType:NSKeyDown
                             location:NSZeroPoint
                        modifierFlags:mods
                            timestamp:CACurrentMediaTime()
                         windowNumber:[[self window] windowNumber]
                              context:nil
                           characters:chars
         charactersIgnoringModifiers:ignMods
                            isARepeat:NO
                              keyCode:code];
}

- (void)startTextInputSelfTest
{
    tiLog(@"==== TigerBrowser text input self-test starting ====");
    tiLog(@"initial EditorState: %@", [self tiDescribeApplied]);

    NSMutableArray *script = [NSMutableArray array];
    // Plain typing: "Hi".
    [script addObject:[self tiEventChars:@"H" ignMods:@"H" mods:0 keyCode:TKC_H]];
    [script addObject:[self tiEventChars:@"i" ignMods:@"i" mods:0 keyCode:34 /* I */]];
    // Return, Tab, Escape.
    [script addObject:[self tiEventChars:@"\r" ignMods:@"\r" mods:0 keyCode:TKC_Return]];
    [script addObject:[self tiEventChars:@"\t" ignMods:@"\t" mods:0 keyCode:TKC_Tab]];
    [script addObject:[self tiEventChars:@"\x1b" ignMods:@"\x1b" mods:0 keyCode:TKC_Escape]];
    // Shift-arrows: extend selection left/right.
    [script addObject:[self tiEventChars:kTILeftArrow ignMods:kTILeftArrow mods:NSShiftKeyMask keyCode:TKC_LeftArrow]];
    [script addObject:[self tiEventChars:kTIRightArrow ignMods:kTIRightArrow mods:NSShiftKeyMask keyCode:TKC_RightArrow]];
    // Option-arrows: word movement.
    [script addObject:[self tiEventChars:kTILeftArrow ignMods:kTILeftArrow mods:NSAlternateKeyMask keyCode:TKC_LeftArrow]];
    [script addObject:[self tiEventChars:kTIRightArrow ignMods:kTIRightArrow mods:NSAlternateKeyMask keyCode:TKC_RightArrow]];
    // Cmd-arrows: line movement.
    [script addObject:[self tiEventChars:kTILeftArrow ignMods:kTILeftArrow mods:NSCommandKeyMask keyCode:TKC_LeftArrow]];
    [script addObject:[self tiEventChars:kTIRightArrow ignMods:kTIRightArrow mods:NSCommandKeyMask keyCode:TKC_RightArrow]];
    // Delete / Forward Delete.
    [script addObject:[self tiEventChars:@"\x7f" ignMods:@"\x7f" mods:0 keyCode:TKC_Delete]];
    [script addObject:[self tiEventChars:kTIForwardDelete ignMods:kTIForwardDelete mods:0 keyCode:TKC_ForwardDelete]];
    // Emacs bindings: Ctrl-A / Ctrl-E / Ctrl-K / Ctrl-D.
    [script addObject:[self tiEventChars:@"\x01" ignMods:@"a" mods:NSControlKeyMask keyCode:TKC_A]];
    [script addObject:[self tiEventChars:@"\x05" ignMods:@"e" mods:NSControlKeyMask keyCode:TKC_E]];
    [script addObject:[self tiEventChars:@"\x0b" ignMods:@"k" mods:NSControlKeyMask keyCode:40 /* K */]];
    [script addObject:[self tiEventChars:@"\x04" ignMods:@"d" mods:NSControlKeyMask keyCode:TKC_D]];
    // Dead key: Option-E (acute accent dead key on US layout), then E -> e-acute.
    [script addObject:[self tiEventChars:@"" ignMods:@"" mods:NSAlternateKeyMask keyCode:TKC_E]];
    [script addObject:[self tiEventChars:@"e" ignMods:@"e" mods:0 keyCode:TKC_E]];

    _selfTestScript = [script retain];
    _selfTestIndex = 0;
    _selfTestTimer = [[NSTimer scheduledTimerWithTimeInterval:0.2 target:self
                                                      selector:@selector(fireNextSelfTestEvent:)
                                                      userInfo:nil repeats:YES] retain];
}

- (void)fireNextSelfTestEvent:(NSTimer *)timer
{
    if (_selfTestIndex >= [_selfTestScript count]) {
        [_selfTestTimer invalidate];
        [_selfTestTimer release];
        _selfTestTimer = nil;
        // Edit-menu commands route to the same log via the responder chain
        // (menu items are target-nil; calling the actions directly here is
        // equivalent to what AppKit does when a keyEquivalent/menu click
        // finds this view as first responder).
        tiLog(@"-- exercising Edit menu commands (responder chain) --");
        [self cut:nil];
        [self copy:nil];
        [self paste:nil];
        [self selectAll:nil];
        [self undo:nil];
        [self redo:nil];
        tiLog(@"-- exercising Cmd-F find bar (still works after the merge) --");
        [[NSApp delegate] performSelector:@selector(performFindPanelAction:) withObject:nil];
        [[NSApp delegate] performSelector:@selector(performFindPanelAction:) withObject:nil];
        tiLog(@"==== TigerBrowser text input self-test complete ====");
        return;
    }
    NSEvent *event = [_selfTestScript objectAtIndex:_selfTestIndex];
    _selfTestIndex++;
    [self keyDown:event];
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

    gChecker = makeCheckerImage();
    [self buildLayerTree];
    [_renderer setLayer:_viewport];
    [self reshape];
    [self setScrollY:0];

    _timer = [NSTimer timerWithTimeInterval:1.0 / 60.0 target:self
                                    selector:@selector(drawFrame)
                                    userInfo:nil repeats:YES];
    [[NSRunLoop currentRunLoop] addTimer:_timer forMode:(NSString *)kCFRunLoopCommonModes];

    // No way to type over ssh; drive the NSTextInput spike with a scripted
    // sequence of synthetic key events once the window has settled, instead
    // of requiring interactive input.
    if (getenv("TIGERBROWSER_TEXTINPUT_TEST")) {
        [[self window] makeFirstResponder:self];
        [self performSelector:@selector(startTextInputSelfTest) withObject:nil afterDelay:1.5];
    }
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

    if (!_viewport)
        return;
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
    if (!_renderer)
        return;
    NSRect b = [self bounds];
    CGLContextObj cgl = (CGLContextObj)[[self openGLContext] CGLContextObj];
    CGLSetCurrentContext(cgl);

    CFTimeInterval now = CACurrentMediaTime();
    // CA normally commits from a run-loop observer; without AppKit's layer
    // support we must flush by hand or the render tree never updates.
    [CATransaction flush];

    glClearColor(1, 1, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    [_renderer beginFrameAtTime:now timeStamp:NULL];
    [_renderer addUpdateRect:CGRectMake(0, 0, b.size.width, b.size.height)];
    [_renderer render];
    [_renderer endFrame];
    [[self openGLContext] flushBuffer];
}

- (void)drawRect:(NSRect)r { [self drawFrame]; }

@end

// ------------------------------------------------------------ find bar

@interface TigerFindBar : NSView
{
    NSTextField *_field;
}
- (id)initWithFrame:(NSRect)frame;
- (NSTextField *)field;
@end

@implementation TigerFindBar

- (id)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (!self)
        return nil;
    NSTextField *label = [[[NSTextField alloc] initWithFrame:NSMakeRect(8, 6, 40, 20)] autorelease];
    [label setStringValue:@"Find:"];
    [label setEditable:NO];
    [label setSelectable:NO];
    [label setBezeled:NO];
    [label setDrawsBackground:NO];
    [self addSubview:label];

    _field = [[NSTextField alloc] initWithFrame:NSMakeRect(52, 4, frame.size.width - 140, 22)];
    [_field setAutoresizingMask:NSViewWidthSizable];
    [self addSubview:_field];
    [_field release];

    NSButton *done = [[[NSButton alloc] initWithFrame:NSMakeRect(frame.size.width - 80, 3, 70, 24)] autorelease];
    [done setBezelStyle:NSRoundedBezelStyle];
    [done setTitle:@"Done"];
    [done setTarget:self];
    [done setAction:@selector(donePressed:)];
    [done setAutoresizingMask:NSViewMinXMargin];
    [self addSubview:done];

    return self;
}

- (NSTextField *)field { return _field; }

- (void)donePressed:(id)sender
{
    [[self window] makeFirstResponder:[[self window] contentView]];
    [self setHidden:YES];
    fprintf(stderr, "Find bar: closed\n");
}

@end

// ---------------------------------------------------------- window controller

@interface TigerBrowserController : NSObject
{
    NSWindow *_window;
    TigerPageView *_pageView;
    NSScroller *_scroller;
    TigerFindBar *_findBar;
    NSTextField *_addressField;
    NSTextField *_statusField;
    NSButton *_backButton;
    NSButton *_forwardButton;
    NSMutableArray *_history;
    unsigned long _historyIndex;
    BOOL _findBarVisible;
}
- (id)initWithURLString:(NSString *)urlString;
- (void)showWindow;
- (void)goBack:(id)sender;
- (void)goForward:(id)sender;
- (void)reload:(id)sender;
- (void)loadFromAddressField:(id)sender;
- (void)focusAddressField:(id)sender;
- (void)toggleFindBar:(id)sender;
- (void)zoomActualSize:(id)sender;
- (void)zoomIn:(id)sender;
- (void)zoomOut:(id)sender;
@end

@implementation TigerBrowserController

- (id)initWithURLString:(NSString *)urlString
{
    self = [super init];
    if (!self)
        return nil;

    _history = [[NSMutableArray alloc] init];
    _historyIndex = 0;

    NSRect frame = NSMakeRect(80, 100, 940, 680);
    _window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:(NSTitledWindowMask | NSClosableWindowMask |
                             NSMiniaturizableWindowMask | NSResizableWindowMask)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    [_window setTitle:@"TigerBrowser"];
    [_window setReleasedWhenClosed:NO];

    NSView *content = [_window contentView];
    NSRect bounds = [content bounds];
    const float barHeight = 32.0;
    const float statusHeight = 18.0;
    const float findBarHeight = 30.0;
    const float scrollerWidth = 15.0;
    const float pad = 6.0;

    // Toolbar row.
    NSRect barRect = NSMakeRect(0, bounds.size.height - barHeight, bounds.size.width, barHeight);
    NSView *bar = [[[NSView alloc] initWithFrame:barRect] autorelease];
    [bar setAutoresizingMask:(NSViewWidthSizable | NSViewMinYMargin)];

    _backButton = [[[NSButton alloc] initWithFrame:NSMakeRect(pad, 4, 34, 24)] autorelease];
    [_backButton setBezelStyle:NSRoundedBezelStyle];
    [_backButton setTitle:@"<"];
    [_backButton setTarget:self];
    [_backButton setAction:@selector(goBack:)];
    [_backButton setAutoresizingMask:NSViewMaxXMargin];
    [bar addSubview:_backButton];

    _forwardButton = [[[NSButton alloc] initWithFrame:NSMakeRect(pad + 38, 4, 34, 24)] autorelease];
    [_forwardButton setBezelStyle:NSRoundedBezelStyle];
    [_forwardButton setTitle:@">"];
    [_forwardButton setTarget:self];
    [_forwardButton setAction:@selector(goForward:)];
    [_forwardButton setAutoresizingMask:NSViewMaxXMargin];
    [bar addSubview:_forwardButton];

    NSButton *reloadButton = [[[NSButton alloc] initWithFrame:NSMakeRect(pad + 76, 4, 34, 24)] autorelease];
    [reloadButton setBezelStyle:NSRoundedBezelStyle];
    [reloadButton setTitle:@"R"];
    [reloadButton setTarget:self];
    [reloadButton setAction:@selector(reload:)];
    [reloadButton setAutoresizingMask:NSViewMaxXMargin];
    [bar addSubview:reloadButton];

    float addrX = pad + 76 + 34 + pad;
    NSRect addrRect = NSMakeRect(addrX, 4, bounds.size.width - addrX - pad, 24);
    _addressField = [[[NSTextField alloc] initWithFrame:addrRect] autorelease];
    [_addressField setAutoresizingMask:NSViewWidthSizable];
    [_addressField setTarget:self];
    [_addressField setAction:@selector(loadFromAddressField:)];
    [bar addSubview:_addressField];

    [content addSubview:bar];

    // Find bar, hidden by default, sits just below the toolbar.
    NSRect findRect = NSMakeRect(0, bounds.size.height - barHeight - findBarHeight,
                                  bounds.size.width, findBarHeight);
    _findBar = [[TigerFindBar alloc] initWithFrame:findRect];
    [_findBar setAutoresizingMask:(NSViewWidthSizable | NSViewMinYMargin)];
    [_findBar setHidden:YES];
    [[_findBar field] setTarget:self];
    [[_findBar field] setAction:@selector(performFind:)];
    [content addSubview:_findBar];

    // Status bar.
    NSRect statusRect = NSMakeRect(0, 0, bounds.size.width, statusHeight);
    _statusField = [[[NSTextField alloc] initWithFrame:statusRect] autorelease];
    [_statusField setAutoresizingMask:(NSViewWidthSizable | NSViewMaxYMargin)];
    [_statusField setEditable:NO];
    [_statusField setSelectable:NO];
    [_statusField setBezeled:NO];
    [_statusField setDrawsBackground:NO];
    [_statusField setFont:[NSFont systemFontOfSize:10]];
    [_statusField setStringValue:@"TigerBrowser shell -- no content process yet"];
    [content addSubview:_statusField];

    // Page view + its scroller sibling (an NSOpenGLView's surface composites
    // over whatever is under it, so per CAHost the scroller can't overlap it).
    NSRect pageRect = NSMakeRect(0, statusHeight, bounds.size.width - scrollerWidth,
                                  bounds.size.height - barHeight - statusHeight);
    NSOpenGLPixelFormatAttribute attrs[] = {
        NSOpenGLPFADoubleBuffer, NSOpenGLPFAAccelerated,
        NSOpenGLPFAColorSize, 32, NSOpenGLPFADepthSize, 24, 0
    };
    NSOpenGLPixelFormat *pf = [[[NSOpenGLPixelFormat alloc] initWithAttributes:attrs] autorelease];
    if (!pf) {
        fprintf(stderr, "FATAL: no OpenGL pixel format\n");
        exit(1);
    }
    _pageView = [[TigerPageView alloc] initWithFrame:pageRect pixelFormat:pf];
    [_pageView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [content addSubview:_pageView];

    NSRect scrollerRect = NSMakeRect(bounds.size.width - scrollerWidth, statusHeight,
                                      scrollerWidth, bounds.size.height - barHeight - statusHeight);
    _scroller = [[[NSScroller alloc] initWithFrame:scrollerRect] autorelease];
    [_scroller setAutoresizingMask:(NSViewMinXMargin | NSViewHeightSizable)];
    [_scroller setEnabled:YES];
    [_scroller setTarget:_pageView];
    [_scroller setAction:@selector(scrollerAction:)];
    [content addSubview:_scroller];
    [_pageView setScroller:_scroller];

    // Full keyboard access: an explicit tab chain across the toolbar and into
    // the page view (Tiger's window doesn't infer one from view geometry).
    [_backButton setNextKeyView:_forwardButton];
    [_forwardButton setNextKeyView:reloadButton];
    [reloadButton setNextKeyView:_addressField];
    [_addressField setNextKeyView:_pageView];
    [_pageView setNextKeyView:_scroller];
    [_scroller setNextKeyView:_backButton];
    [_window setInitialFirstResponder:_addressField];

    if (urlString)
        [self loadURLString:urlString];

    return self;
}

- (void)dealloc
{
    [_history release];
    [_pageView release];
    [_findBar release];
    [_window release];
    [super dealloc];
}

- (void)showWindow
{
    [_window makeKeyAndOrderFront:nil];
}

- (void)loadURLString:(NSString *)urlString
{
    if (!urlString || [urlString length] == 0)
        return;
    // Truncate any forward history, push the new entry (same shape a real
    // back/forward list needs, even though nothing is actually fetched yet).
    if ([_history count] > 0 && _historyIndex + 1 < [_history count])
        [_history removeObjectsInRange:NSMakeRange(_historyIndex + 1, [_history count] - _historyIndex - 1)];
    [_history addObject:urlString];
    _historyIndex = [_history count] - 1;

    [_addressField setStringValue:urlString];
    [_window setTitle:urlString];
    [_pageView setURLText:urlString];
    [_statusField setStringValue:[NSString stringWithFormat:@"Loaded (stub): %@", urlString]];
    fprintf(stderr, "TigerBrowser: loadURLString '%s' (history index %lu/%lu)\n",
            [urlString UTF8String], (unsigned long)_historyIndex, (unsigned long)[_history count]);
}

- (void)loadFromAddressField:(id)sender
{
    NSString *text = [_addressField stringValue];
    if ([text length] == 0)
        return;
    NSRange schemeRange = [text rangeOfString:@"://"];
    if (schemeRange.location == NSNotFound)
        text = [NSString stringWithFormat:@"http://%@", text];
    [self loadURLString:text];
}

- (void)goBack:(id)sender
{
    if (_historyIndex == 0 || [_history count] == 0)
        return;
    _historyIndex--;
    NSString *url = [_history objectAtIndex:_historyIndex];
    [_addressField setStringValue:url];
    [_window setTitle:url];
    [_pageView setURLText:url];
    fprintf(stderr, "TigerBrowser: goBack -> '%s'\n", [url UTF8String]);
}

- (void)goForward:(id)sender
{
    if ([_history count] == 0 || _historyIndex + 1 >= [_history count])
        return;
    _historyIndex++;
    NSString *url = [_history objectAtIndex:_historyIndex];
    [_addressField setStringValue:url];
    [_window setTitle:url];
    [_pageView setURLText:url];
    fprintf(stderr, "TigerBrowser: goForward -> '%s'\n", [url UTF8String]);
}

- (void)reload:(id)sender
{
    fprintf(stderr, "TigerBrowser: reload (stub, no content process)\n");
    [_statusField setStringValue:@"Reloaded (stub)"];
}

- (void)focusAddressField:(id)sender
{
    [_window makeFirstResponder:_addressField];
    [_addressField selectText:nil];
}

// ---- Find (Cmd-F)

- (void)toggleFindBar:(id)sender
{
    _findBarVisible = !_findBarVisible;
    [_findBar setHidden:!_findBarVisible];
    if (_findBarVisible) {
        [_window makeFirstResponder:[_findBar field]];
        tiLog(@"Find bar: opened (Cmd-F)");
    } else {
        [_window makeFirstResponder:_pageView];
        tiLog(@"Find bar: closed");
    }
}

- (void)performFind:(id)sender
{
    tiLog(@"Find: '%@' (stub, no content to search yet)", [[_findBar field] stringValue]);
}

// ---- View menu (stub actions, just log per the task)

- (void)zoomActualSize:(id)sender { fprintf(stderr, "View: Actual Size\n"); }
- (void)zoomIn:(id)sender { fprintf(stderr, "View: Zoom In\n"); }
- (void)zoomOut:(id)sender { fprintf(stderr, "View: Zoom Out\n"); }

@end

// ---------------------------------------------------------------- app delegate

@interface TigerBrowserAppDelegate : NSObject
{
    TigerBrowserController *_controller;
}
- (id)initWithURLString:(NSString *)urlString;
@end

@implementation TigerBrowserAppDelegate

- (id)initWithURLString:(NSString *)urlString
{
    self = [super init];
    if (!self)
        return nil;
    _controller = [[TigerBrowserController alloc] initWithURLString:urlString];
    return self;
}

- (void)dealloc
{
    [_controller release];
    [super dealloc];
}

- (void)applicationDidFinishLaunching:(NSNotification *)note
{
    [_controller showWindow];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app
{
    return YES;
}

- (void)openLocation:(id)sender { [_controller focusAddressField:sender]; }
- (void)performFindPanelAction:(id)sender { [_controller toggleFindBar:sender]; }
- (void)zoomActualSize:(id)sender { [_controller zoomActualSize:sender]; }
- (void)zoomIn:(id)sender { [_controller zoomIn:sender]; }
- (void)zoomOut:(id)sender { [_controller zoomOut:sender]; }

@end

// ---------------------------------------------------------------------- main

int main(int argc, const char **argv)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    NSApplication *app = [NSApplication sharedApplication];

    tiLogOpen(getenv("TIGERBROWSER_TEXTINPUT_LOG") ? getenv("TIGERBROWSER_TEXTINPUT_LOG")
                                                    : "/tmp/tigerbrowser-textinput.log");
    checkFrameworksLoaded();

    NSString *urlString = @"about:blank";
    if (argc > 1)
        urlString = [NSString stringWithUTF8String:argv[1]];

    TigerBrowserAppDelegate *delegate = [[TigerBrowserAppDelegate alloc] initWithURLString:urlString];
    [app setDelegate:delegate];

    NSMenu *menubar = [[NSMenu alloc] init];
    NSMenuItem *appMenuItem = [[NSMenuItem alloc] init];
    [menubar addItem:appMenuItem];
    [app setMainMenu:menubar];

    NSMenu *appMenu = [[NSMenu alloc] init];
    NSMenuItem *quitItem = [[NSMenuItem alloc] initWithTitle:@"Quit TigerBrowser"
                                                        action:@selector(terminate:)
                                                 keyEquivalent:@"q"];
    [appMenu addItem:quitItem];
    [quitItem release];
    [appMenuItem setSubmenu:appMenu];
    [appMenu release];
    [appMenuItem release];

    // File menu: Open Location (Cmd-L).
    NSMenu *fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
    NSMenuItem *fileMenuItem = [[NSMenuItem alloc] initWithTitle:@"File" action:NULL keyEquivalent:@""];
    [fileMenuItem setSubmenu:fileMenu];
    [menubar addItem:fileMenuItem];
    [fileMenuItem release];
    NSMenuItem *openLocationItem = [[NSMenuItem alloc] initWithTitle:@"Open Location..."
                                                                action:@selector(openLocation:)
                                                         keyEquivalent:@"l"];
    [openLocationItem setTarget:delegate];
    [fileMenu addItem:openLocationItem];
    [openLocationItem release];
    [fileMenu release];

    // Edit menu: standard items, target nil so they dispatch through the
    // responder chain to TigerPageView's cut:/copy:/paste:/selectAll:/undo:/redo:.
    NSMenu *editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
    NSMenuItem *editMenuItem = [[NSMenuItem alloc] initWithTitle:@"Edit" action:NULL keyEquivalent:@""];
    [editMenuItem setSubmenu:editMenu];
    [menubar addItem:editMenuItem];
    [editMenuItem release];
    struct { NSString *title; SEL sel; NSString *key; } editItems[] = {
        { @"Undo", @selector(undo:), @"z" },
        { @"Redo", @selector(redo:), @"Z" },
        { @"Cut", @selector(cut:), @"x" },
        { @"Copy", @selector(copy:), @"c" },
        { @"Paste", @selector(paste:), @"v" },
        { @"Select All", @selector(selectAll:), @"a" },
    };
    unsigned e;
    for (e = 0; e < sizeof(editItems) / sizeof(editItems[0]); e++) {
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:editItems[e].title
                                                        action:editItems[e].sel
                                                 keyEquivalent:editItems[e].key];
        [editMenu addItem:item];
        [item release];
    }
    NSMenuItem *findItem = [[NSMenuItem alloc] initWithTitle:@"Find..."
                                                        action:@selector(performFindPanelAction:)
                                                 keyEquivalent:@"f"];
    [findItem setTarget:delegate];
    [editMenu addItem:findItem];
    [findItem release];
    [editMenu release];

    // View menu: Actual Size / Zoom In / Zoom Out, logged stubs.
    NSMenu *viewMenu = [[NSMenu alloc] initWithTitle:@"View"];
    NSMenuItem *viewMenuItem = [[NSMenuItem alloc] initWithTitle:@"View" action:NULL keyEquivalent:@""];
    [viewMenuItem setSubmenu:viewMenu];
    [menubar addItem:viewMenuItem];
    [viewMenuItem release];
    struct { NSString *title; SEL sel; NSString *key; } viewItems[] = {
        { @"Actual Size", @selector(zoomActualSize:), @"0" },
        { @"Zoom In", @selector(zoomIn:), @"=" },
        { @"Zoom Out", @selector(zoomOut:), @"-" },
    };
    unsigned v;
    for (v = 0; v < sizeof(viewItems) / sizeof(viewItems[0]); v++) {
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:viewItems[v].title
                                                        action:viewItems[v].sel
                                                 keyEquivalent:viewItems[v].key];
        [item setTarget:delegate];
        [viewMenu addItem:item];
        [item release];
    }
    [viewMenu release];

    [menubar release];

    [app activateIgnoringOtherApps:YES];
    [app run];

    [pool release];
    return 0;
}
