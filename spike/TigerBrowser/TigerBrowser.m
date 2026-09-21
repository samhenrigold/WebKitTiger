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

// Fixed page-coordinate rects standing in for a real <select> element and a
// real right-click target, until real layout geometry exists.
#define TI_SELECT_PAGE_RECT CGRectMake(100, 100, 140, 22)
#define TI_CONTEXTMENU_PAGE_RECT CGRectMake(100, 140, 140, 22)

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

// Logs PASS/FAIL rather than aborting -- this is a self-test run to produce
// a complete diagnostic transcript, not a unit test suite; a crash on the
// first failure would hide every assertion after it.
static void tiAssert(BOOL condition, NSString *description)
{
    tiLog(@"  ASSERT %@: %@", condition ? @"PASS" : @"FAIL", description);
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
#define kTIDownArrow ([NSString stringWithFormat:@"%C", (unichar)NSDownArrowFunctionKey])

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

    // Visual anchors for the <select>/context-menu spike -- just outlined
    // boxes with a label, so a screenshot shows where those page rects are.
    CGRect selectAnchor = TI_SELECT_PAGE_RECT;
    if (CGRectIntersectsRect(selectAnchor, dirty)) {
        CGContextSetRGBFillColor(ctx, 0.95, 0.95, 0.97, 1.0);
        CGContextFillRect(ctx, selectAnchor);
        CGContextSetRGBStrokeColor(ctx, 0.4, 0.4, 0.4, 1.0);
        CGContextStrokeRect(ctx, selectAnchor);
        CGContextSelectFont(ctx, "Helvetica", 12, kCGEncodingMacRoman);
        CGContextSetTextMatrix(ctx, CGAffineTransformMakeScale(1.0, -1.0));
        CGContextSetRGBFillColor(ctx, 0.1, 0.1, 0.1, 1.0);
        CGContextShowTextAtPoint(ctx, selectAnchor.origin.x + 6, selectAnchor.origin.y + 15,
                                  "<select> stub", 13);
    }
    CGRect contextAnchor = TI_CONTEXTMENU_PAGE_RECT;
    if (CGRectIntersectsRect(contextAnchor, dirty)) {
        CGContextSetRGBFillColor(ctx, 0.95, 0.95, 0.97, 1.0);
        CGContextFillRect(ctx, contextAnchor);
        CGContextSetRGBStrokeColor(ctx, 0.4, 0.4, 0.4, 1.0);
        CGContextStrokeRect(ctx, contextAnchor);
        CGContextSelectFont(ctx, "Helvetica", 12, kCGEncodingMacRoman);
        CGContextSetTextMatrix(ctx, CGAffineTransformMakeScale(1.0, -1.0));
        CGContextSetRGBFillColor(ctx, 0.1, 0.1, 0.1, 1.0);
        CGContextShowTextAtPoint(ctx, contextAnchor.origin.x + 6, contextAnchor.origin.y + 15,
                                  "right-click stub", 17);
    }
}

// ------------------------------------------------------- native menus spike
//
// What WebPopupMenuProxyMac (<select>) and WebContextMenuProxyMac (right-
// click) do on macOS, driven from a serialized item list the way one would
// arrive from a web process: plain NSDictionary items (this IS the
// serialized form -- no custom class needed) with keys "title" (NSString),
// "enabled" (NSNumber BOOL, default YES), "checked" (NSNumber BOOL, default
// NO -- context menu only, WebPopupItem has no checked concept at all,
// only WebCore::ContextMenuItem::Type::CheckableAction does), "separator"
// (NSNumber BOOL), "submenu" (NSArray of item dicts, context menu only --
// NSPopUpButtonCell menus are flat, matching <select>/<optgroup> having no
// nested popups). Order in the array is the item's index, exactly like the
// real Vector<WebPopupItem>/Vector<ContextMenuItem>.

static NSDictionary *tiItem(NSString *title, BOOL enabled, BOOL checked)
{
    return [NSDictionary dictionaryWithObjectsAndKeys:
            title, @"title",
            [NSNumber numberWithBool:enabled], @"enabled",
            [NSNumber numberWithBool:checked], @"checked", nil];
}

static NSDictionary *tiSeparatorItem(void)
{
    return [NSDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithBool:YES], @"separator", nil];
}

static NSDictionary *tiSubmenuItem(NSString *title, NSArray *submenuItems)
{
    return [NSDictionary dictionaryWithObjectsAndKeys:
            title, @"title",
            [NSNumber numberWithBool:YES], @"enabled",
            submenuItems, @"submenu", nil];
}

// Mirrors WebPopupMenuProxyMac::populate: a real NSPopUpButtonCell, not a
// plain NSMenu -- initTextCell:pullsDown:NO, usesItemFromMenu:NO (so each
// item shows its own title rather than always showing the button's title),
// autoenablesItems:NO (we set enabled per item ourselves, matching how a
// real <option disabled> arrives).
static NSPopUpButtonCell *tiBuildPopupCell(NSArray *items, int selectedIndex)
{
    NSPopUpButtonCell *cell = [[[NSPopUpButtonCell alloc] initTextCell:@"" pullsDown:NO] autorelease];
    [cell setUsesItemFromMenu:NO];
    [cell setAutoenablesItems:NO];
    unsigned i;
    for (i = 0; i < [items count]; i++) {
        NSDictionary *item = [items objectAtIndex:i];
        if ([[item objectForKey:@"separator"] boolValue]) {
            [[cell menu] addItem:[NSMenuItem separatorItem]];
            continue;
        }
        [cell addItemWithTitle:@""];
        id menuItem = [cell lastItem];
        [menuItem setTitle:[item objectForKey:@"title"]];
        [menuItem setEnabled:[[item objectForKey:@"enabled"] boolValue]];
    }
    [cell selectItemAtIndex:selectedIndex];
    return cell;
}

static NSMenu *tiBuildMenuRecursive(NSArray *items, id target, SEL action, int *tagCounter);

// Mirrors WebContextMenuProxyMac's item model: a real NSMenu, submenus
// recursive, separators via +[NSMenuItem separatorItem], checked state via
// -setState:. Every leaf item gets a unique tag (this stub's stand-in for
// the real ContextMenuAction id) and a target/action so popUpContextMenu:
// (which returns void) still tells us which item was chosen, the same way
// a real NSMenuItem's action does.
static NSMenu *tiBuildMenuRecursive(NSArray *items, id target, SEL action, int *tagCounter)
{
    NSMenu *menu = [[[NSMenu alloc] initWithTitle:@""] autorelease];
    [menu setAutoenablesItems:NO];
    unsigned mi;
    for (mi = 0; mi < [items count]; mi++) {
        NSDictionary *item = [items objectAtIndex:mi];
        if ([[item objectForKey:@"separator"] boolValue]) {
            [menu addItem:[NSMenuItem separatorItem]];
            continue;
        }
        NSArray *submenuItems = [item objectForKey:@"submenu"];
        NSMenuItem *menuItem = [[[NSMenuItem alloc] initWithTitle:[item objectForKey:@"title"]
                                                             action:(submenuItems ? NULL : action)
                                                      keyEquivalent:@""] autorelease];
        [menuItem setEnabled:[[item objectForKey:@"enabled"] boolValue]];
        [menuItem setState:[[item objectForKey:@"checked"] boolValue] ? NSOnState : NSOffState];
        if (submenuItems) {
            [menuItem setSubmenu:tiBuildMenuRecursive(submenuItems, target, action, tagCounter)];
        } else {
            [menuItem setTarget:target];
            [menuItem setTag:(*tagCounter)++];
        }
        [menu addItem:menuItem];
    }
    return menu;
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
    // Two-tier answer per logs/textinput-plan.md §3a: while this is set (the
    // duration of the current -interpretKeyEvents: call, and only that), the
    // four sync query methods answer from "pending" (zero staleness, same
    // call stack, nothing has been dispatched yet); otherwise from "applied"
    // (the last state confirmed by the simulated content-process round
    // trip -- possibly stale, but that staleness is unavoidable and bounded).
    BOOL _inInterpretKeyEvents;
    NSTimer *_selfTestTimer;
    NSArray *_selfTestScript;
    unsigned _selfTestIndex;
    NSTimer *_focusJumpTimer;

    // ---- native menus (WebPopupMenuProxyMac / WebContextMenuProxyMac spike)
    NSPopUpButtonCell *_selectPopupCell;
    int _lastContextMenuChosenTag;
    NSString *_lastContextMenuChosenTitle;
    NSTimer *_menuKeyScriptTimer;
    NSArray *_menuKeyScript;
    unsigned _menuKeyScriptIndex;
}
- (void)setScroller:(NSScroller *)s;
- (void)setURLText:(NSString *)text;
- (void)setScrollY:(double)y;
- (void)startTextInputSelfTest;
- (int)showSelectPopupWithItems:(NSArray *)items selectedIndex:(int)selectedIndex pageRect:(CGRect)pageRect;
- (void)showContextMenuWithItems:(NSArray *)items atPageRect:(CGRect)pageRect;
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
    [_focusJumpTimer invalidate];
    [_menuKeyScriptTimer invalidate];
    [_renderer release];
    [_viewport release];
    [_page release];
    [_tileLayers release];
    [_urlText release];
    [_appliedDocumentText release];
    [_pendingDocumentText release];
    [_selfTestScript release];
    [_selectPopupCell release];
    [_lastContextMenuChosenTitle release];
    [_menuKeyScript release];
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

- (NSRect)viewRectForPageRect:(CGRect)pageRect
{
    // Page space is top-left/y-down (geometryFlipped viewport); the view is
    // bottom-left/y-up AppKit space. Same conversion as -pagePointForViewPoint:,
    // inverted, plus the rect's height.
    CGFloat viewY = [self bounds].size.height - (pageRect.origin.y - _scrollY) - pageRect.size.height;
    return NSMakeRect(pageRect.origin.x, viewY, pageRect.size.width, pageRect.size.height);
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

// Two-tier answer (logs/textinput-plan.md §3a): while _inInterpretKeyEvents
// is set, every query reads "pending" -- same call stack, nothing has been
// dispatched to a (real, eventual) content process yet, so this is exactly
// accurate, not just less-stale. Once -interpretKeyEvents: has returned,
// queries read "applied" -- the last state actually confirmed by the
// simulated round trip, which can be stale by up to one round trip, exactly
// like the real async-IPC case this models.
- (NSRange)tiCurrentSelectedRange { return _inInterpretKeyEvents ? _pendingSelectedRange : _appliedSelectedRange; }
- (NSRange)tiCurrentMarkedRange { return _inInterpretKeyEvents ? _pendingMarkedRange : _appliedMarkedRange; }
- (NSMutableString *)tiCurrentDocumentText { return _inInterpretKeyEvents ? _pendingDocumentText : _appliedDocumentText; }
- (NSString *)tiCurrentTierName { return _inInterpretKeyEvents ? @"pending" : @"applied"; }

- (void)keyDown:(NSEvent *)event
{
    tiLog(@"--");
    tiLog(@"keyDown: keyCode=%u characters='%@' charactersIgnoringModifiers='%@' modifierFlags=0x%lx",
          (unsigned)[event keyCode], [event characters], [event charactersIgnoringModifiers],
          (unsigned long)[event modifierFlags]);

    NSRange appliedBeforeThisKeyDown = _appliedSelectedRange;

    _keyDownCommandLog = [[NSMutableArray alloc] init];
    _keyDownHadPendingMutation = NO;
    // Fresh pending state starts as a copy of the last *applied* (i.e.
    // possibly still-stale) state, per keydown -- composes correctly if a
    // single interpretKeyEvents: call produces several callbacks (e.g. a
    // dead key's setMarkedText: followed immediately by unmarkText).
    [_pendingDocumentText setString:_appliedDocumentText];
    _pendingSelectedRange = _appliedSelectedRange;
    _pendingMarkedRange = _appliedMarkedRange;

    _inInterpretKeyEvents = YES;
    [self interpretKeyEvents:[NSArray arrayWithObject:event]];
    _inInterpretKeyEvents = NO;

    tiLog(@"keyDown: interpretKeyEvents: produced %lu command(s): [%@]",
          (unsigned long)[_keyDownCommandLog count],
          [_keyDownCommandLog componentsJoinedByString:@", "]);
    [_keyDownCommandLog release];
    _keyDownCommandLog = nil;

    // §3a point 3: a query made right after -interpretKeyEvents: returns, but
    // before the simulated round trip lands, must (a) answer instantly --
    // it's a local read, never blocking IPC -- and (b) see the OLD applied
    // state, not the pending mutation this keydown just made.
    NSDate *queryStart = [NSDate date];
    NSRange queryResult = [self selectedRange];
    double queryElapsedMs = -[queryStart timeIntervalSinceNow] * 1000.0;
    tiAssert(queryElapsedMs < 1.0,
              [NSString stringWithFormat:@"post-keyDown query never blocks (%.3fms < 1ms)", queryElapsedMs]);
    if (_keyDownHadPendingMutation) {
        tiAssert(NSEqualRanges(queryResult, appliedBeforeThisKeyDown),
                  [NSString stringWithFormat:@"post-keyDown query sees OLD applied state %@, not pending %@",
                          NSStringFromRange(queryResult), NSStringFromRange(_pendingSelectedRange)]);
    }

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
    NSRange pendingSnapshot = _pendingSelectedRange;
    [_appliedDocumentText setString:_pendingDocumentText];
    _appliedSelectedRange = _pendingSelectedRange;
    _appliedMarkedRange = _pendingMarkedRange;
    tiLog(@"  simulated content-process reply landed: %@", [self tiDescribeApplied]);

    // §3a point 2: a query issued once the round trip has landed must now see
    // the applied (just-updated) state.
    tiAssert(NSEqualRanges([self selectedRange], pendingSnapshot),
              @"post-round-trip query sees the newly applied state");
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

    // §3a point 1: a query issued from inside this very call (still within
    // -interpretKeyEvents:, e.g. an IME verifying its own insertText: before
    // returning) must see the just-inserted character, not the old applied
    // state -- zero staleness, because nothing has been dispatched yet.
    if ([plain length] > 0) {
        NSRange q = [self selectedRange];
        tiAssert(NSEqualRanges(q, _pendingSelectedRange),
                  [NSString stringWithFormat:@"in-callback query sees just-inserted %@ (selectedRange -> %@)",
                          desc, NSStringFromRange(q)]);
    }
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
    BOOL has = [self tiCurrentMarkedRange].location != NSNotFound;
    tiLog(@"  hasMarkedText -> %@ (from %@ tier)", has ? @"YES" : @"NO", [self tiCurrentTierName]);
    return has;
}

- (long)conversationIdentifier
{
    return (long)self;
}

- (NSAttributedString *)attributedSubstringFromRange:(NSRange)theRange
{
    NSMutableString *doc = [self tiCurrentDocumentText];
    NSRange marked = [self tiCurrentMarkedRange];
    tiLog(@"  attributedSubstringFromRange:%@ (from %@ tier, doc len %lu)",
          NSStringFromRange(theRange), [self tiCurrentTierName], (unsigned long)[doc length]);
    if (theRange.location == NSNotFound || NSMaxRange(theRange) > [doc length])
        return nil;
    NSString *sub = [doc substringWithRange:theRange];
    NSMutableAttributedString *attr = [[[NSMutableAttributedString alloc] initWithString:sub] autorelease];
    NSRange markedIntersection = NSIntersectionRange(theRange, marked);
    if (markedIntersection.length > 0) {
        NSRange local = NSMakeRange(markedIntersection.location - theRange.location, markedIntersection.length);
        [attr addAttribute:NSUnderlineStyleAttributeName value:[NSNumber numberWithInt:1] range:local];
    }
    return attr;
}

- (NSRange)markedRange
{
    NSRange marked = [self tiCurrentMarkedRange];
    tiLog(@"  markedRange -> %@ (from %@ tier)",
          marked.location == NSNotFound ? @"{NSNotFound,0}" : NSStringFromRange(marked), [self tiCurrentTierName]);
    return marked;
}

- (NSRange)selectedRange
{
    NSRange sel = [self tiCurrentSelectedRange];
    tiLog(@"  selectedRange -> %@ (from %@ tier)", NSStringFromRange(sel), [self tiCurrentTierName]);
    return sel;
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
          @" from %@ tier -- a real cache miss here is visibly wrong, not just logically stale,"
          @" per the plan's §3 note)",
          NSStringFromRange(theRange), rect.origin.x, rect.origin.y, rect.size.width, rect.size.height,
          [self tiCurrentTierName]);
    return rect;
}

- (unsigned int)characterIndexForPoint:(NSPoint)thePoint
{
    unsigned int idx = (unsigned int)[[self tiCurrentDocumentText] length];
    tiLog(@"  characterIndexForPoint:{%.0f,%.0f} -> %u (stub: always end-of-document, from %@ tier)",
          thePoint.x, thePoint.y, idx, [self tiCurrentTierName]);
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

// ---- native menus: <select> popup (WebPopupMenuProxyMac) and context menu
// (WebContextMenuProxyMac), driven from the serialized item list above.

// Mirrors WebPopupMenuProxyMac::showPopupMenu: build the cell, attach it at
// the control's rect, let AppKit run its own native tracking loop
// (performClickWithFrame:inView: does attach+track+dismiss in one call --
// the public-API equivalent of the private PAL::popUpMenu() SPI upstream
// uses), then read back which item the user picked. Real keyboard
// navigation (arrows, type-select, Return, Escape) all come from AppKit's
// own NSMenu tracking, not anything this file implements.
- (int)showSelectPopupWithItems:(NSArray *)items selectedIndex:(int)selectedIndex pageRect:(CGRect)pageRect
{
    tiLog(@"showSelectPopupWithItems: %lu item(s), selectedIndex=%d, pageRect={%.0f,%.0f,%.0f,%.0f}",
          (unsigned long)[items count], selectedIndex, pageRect.origin.x, pageRect.origin.y,
          pageRect.size.width, pageRect.size.height);
    unsigned i;
    for (i = 0; i < [items count]; i++) {
        NSDictionary *item = [items objectAtIndex:i];
        if ([[item objectForKey:@"separator"] boolValue])
            tiLog(@"  [%u] (separator)", i);
        else
            tiLog(@"  [%u] '%@' enabled=%@", i, [item objectForKey:@"title"],
                  [[item objectForKey:@"enabled"] boolValue] ? @"YES" : @"NO");
    }

    [_selectPopupCell release];
    _selectPopupCell = [tiBuildPopupCell(items, selectedIndex) retain];

    NSRect frame = [self viewRectForPageRect:pageRect];
    tiLog(@"  attaching at view rect {%.0f,%.0f,%.0f,%.0f}, native tracking begins (blocks until dismissed)...",
          frame.origin.x, frame.origin.y, frame.size.width, frame.size.height);

    [_selectPopupCell performClickWithFrame:frame inView:self];

    int chosen = [_selectPopupCell indexOfSelectedItem];
    tiLog(@"  native tracking returned: chosen index = %d ('%@')", chosen,
          (chosen >= 0 && chosen < (int)[items count]) ? [[items objectAtIndex:chosen] objectForKey:@"title"] : @"?");
    return chosen; // "returned through a callback": the caller (here, the self-test) treats this as one.
}

- (void)contextMenuItemChosen:(id)sender
{
    _lastContextMenuChosenTag = (int)[sender tag];
    [_lastContextMenuChosenTitle release];
    _lastContextMenuChosenTitle = [[sender title] copy];
    tiLog(@"  context menu item chosen: tag=%d title='%@'", _lastContextMenuChosenTag, _lastContextMenuChosenTitle);
}

// Mirrors WebContextMenuProxyMac's use of +[NSMenu popUpContextMenu:withEvent:forView:]
// with a recursively-built native NSMenu (submenus, separators, checked
// state all real AppKit, not drawn by hand).
- (void)showContextMenuWithItems:(NSArray *)items atPageRect:(CGRect)pageRect
{
    tiLog(@"showContextMenuWithItems: %lu item(s), pageRect={%.0f,%.0f,%.0f,%.0f}",
          (unsigned long)[items count], pageRect.origin.x, pageRect.origin.y,
          pageRect.size.width, pageRect.size.height);

    _lastContextMenuChosenTag = -1;
    [_lastContextMenuChosenTitle release];
    _lastContextMenuChosenTitle = nil;

    int tagCounter = 0;
    NSMenu *menu = tiBuildMenuRecursive(items, self, @selector(contextMenuItemChosen:), &tagCounter);

    NSRect viewRect = [self viewRectForPageRect:pageRect];
    NSPoint locationInWindow = [self convertPoint:viewRect.origin toView:nil];
    NSEvent *fakeRightMouseDown = [NSEvent mouseEventWithType:NSRightMouseDown
                                                       location:locationInWindow
                                                  modifierFlags:0
                                                      timestamp:CACurrentMediaTime()
                                                   windowNumber:[[self window] windowNumber]
                                                        context:nil
                                                    eventNumber:0
                                                     clickCount:1
                                                       pressure:1.0];

    tiLog(@"  popUpContextMenu:withEvent:forView:, native tracking begins (blocks until dismissed)...");
    [NSMenu popUpContextMenu:menu withEvent:fakeRightMouseDown forView:self];

    if (_lastContextMenuChosenTag >= 0)
        tiLog(@"  native tracking returned: chosen tag=%d title='%@'", _lastContextMenuChosenTag, _lastContextMenuChosenTitle);
    else
        tiLog(@"  native tracking returned: no item chosen (dismissed, e.g. Escape)");
}

// Both -performClickWithFrame:inView: and +popUpContextMenu:withEvent:forView:
// block synchronously in their own native event-tracking loop, so a keyboard
// sequence has to be posted asynchronously from a timer *scheduled before*
// the call, in a run loop mode NSMenu's tracking loop still services
// (NSRunLoopCommonModes covers NSEventTrackingRunLoopMode). This is the
// only way to drive the shell's own scripted self-test through real,
// unmodified AppKit menu tracking rather than reimplementing arrow/Return/
// Escape handling by hand.
- (void)postMenuKeyScript:(NSArray *)events
{
    [_menuKeyScript release];
    _menuKeyScript = [events retain];
    _menuKeyScriptIndex = 0;
    [_menuKeyScriptTimer invalidate];
    [_menuKeyScriptTimer release];
    _menuKeyScriptTimer = [[NSTimer timerWithTimeInterval:0.8 target:self
                                                  selector:@selector(fireMenuKeyScriptEvent:)
                                                  userInfo:nil repeats:YES] retain];
    [[NSRunLoop currentRunLoop] addTimer:_menuKeyScriptTimer forMode:(NSString *)kCFRunLoopCommonModes];
}

- (void)fireMenuKeyScriptEvent:(NSTimer *)timer
{
    if (_menuKeyScriptIndex >= [_menuKeyScript count]) {
        [_menuKeyScriptTimer invalidate];
        [_menuKeyScriptTimer release];
        _menuKeyScriptTimer = nil;
        return;
    }
    NSEvent *event = [_menuKeyScript objectAtIndex:_menuKeyScriptIndex];
    _menuKeyScriptIndex++;
    tiLog(@"  posting scripted key into native menu tracking: keyCode=%u characters='%@'",
          (unsigned)[event keyCode], [event characters]);
    [NSApp postEvent:event atStart:NO];
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

    // §3a point 3 (the "applied" refresh path): a selection change with no
    // keyboard involvement at all -- an in-page focus jump is the real-world
    // example -- lands in "applied" directly, independent of any keydown
    // round trip. Runs throughout the script so its log lines interleave
    // with the keydown-driven ones, same as they would in practice.
    _focusJumpTimer = [[NSTimer scheduledTimerWithTimeInterval:0.6 target:self
                                                       selector:@selector(simulateNonKeyboardSelectionChange:)
                                                       userInfo:nil repeats:YES] retain];
}

- (void)simulateNonKeyboardSelectionChange:(NSTimer *)timer
{
    unsigned long len = [_appliedDocumentText length];
    unsigned long newLoc = (_appliedSelectedRange.location == 0 && len > 0) ? len : 0;
    _appliedSelectedRange = NSMakeRange(newLoc, 0);
    tiLog(@"  simulated non-keyboard selection change (e.g. in-page focus jump, no keydown"
          @" involved): applied selectedRange -> %@", NSStringFromRange(_appliedSelectedRange));
}

- (void)fireNextSelfTestEvent:(NSTimer *)timer
{
    if (_selfTestIndex >= [_selfTestScript count]) {
        [_selfTestTimer invalidate];
        [_selfTestTimer release];
        _selfTestTimer = nil;
        [_focusJumpTimer invalidate];
        [_focusJumpTimer release];
        _focusJumpTimer = nil;
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

        // ---- native menus (WebPopupMenuProxyMac / WebContextMenuProxyMac) ----
        NSArray *selectItems = [NSArray arrayWithObjects:
            tiItem(@"Alpha", YES, NO), tiItem(@"Bravo", YES, NO),
            tiItem(@"Charlie (disabled)", NO, NO), tiSeparatorItem(),
            tiItem(@"Delta", YES, NO), nil];

        tiLog(@"-- <select> popup: arrows + type-select ('D') + Return --");
        [self postMenuKeyScript:[NSArray arrayWithObjects:
            [self tiEventChars:kTIDownArrow ignMods:kTIDownArrow mods:0 keyCode:TKC_DownArrow],
            [self tiEventChars:@"D" ignMods:@"D" mods:0 keyCode:TKC_D],
            [self tiEventChars:@"\r" ignMods:@"\r" mods:0 keyCode:TKC_Return], nil]];
        int chosen1 = [self showSelectPopupWithItems:selectItems selectedIndex:0 pageRect:TI_SELECT_PAGE_RECT];
        tiAssert(chosen1 == 4, [NSString stringWithFormat:@"type-select 'D' + Return chose Delta (index 4), got %d", chosen1]);

        tiLog(@"-- <select> popup: arrows + Escape (cancel leaves selection unchanged) --");
        [self postMenuKeyScript:[NSArray arrayWithObjects:
            [self tiEventChars:kTIDownArrow ignMods:kTIDownArrow mods:0 keyCode:TKC_DownArrow],
            [self tiEventChars:@"\x1b" ignMods:@"\x1b" mods:0 keyCode:TKC_Escape], nil]];
        int chosen2 = [self showSelectPopupWithItems:selectItems selectedIndex:0 pageRect:TI_SELECT_PAGE_RECT];
        tiAssert(chosen2 == 0, [NSString stringWithFormat:@"Escape cancels, selection stays at index 0, got %d", chosen2]);

        NSArray *contextItems = [NSArray arrayWithObjects:
            tiItem(@"Open Link", YES, NO), tiSeparatorItem(),
            tiSubmenuItem(@"Share", [NSArray arrayWithObjects:
                tiItem(@"Mail", YES, NO), tiItem(@"Messages", YES, NO), nil]),
            tiItem(@"Inspect Element", YES, NO),
            tiItem(@"Reload", YES, YES) /* checked, demonstrates NSOnState */, nil];

        tiLog(@"-- context menu: Down + Return (top item) --");
        [self postMenuKeyScript:[NSArray arrayWithObjects:
            [self tiEventChars:kTIDownArrow ignMods:kTIDownArrow mods:0 keyCode:TKC_DownArrow],
            [self tiEventChars:@"\r" ignMods:@"\r" mods:0 keyCode:TKC_Return], nil]];
        [self showContextMenuWithItems:contextItems atPageRect:TI_CONTEXTMENU_PAGE_RECT];
        tiAssert(_lastContextMenuChosenTag == 0 && [_lastContextMenuChosenTitle isEqualToString:@"Open Link"],
                  [NSString stringWithFormat:@"Down + Return chose 'Open Link' (tag 0), got tag=%d title='%@'",
                          _lastContextMenuChosenTag, _lastContextMenuChosenTitle]);

        tiLog(@"-- context menu: Down, Down, Right (open Share submenu), Down, Escape (cancel) --");
        [self postMenuKeyScript:[NSArray arrayWithObjects:
            [self tiEventChars:kTIDownArrow ignMods:kTIDownArrow mods:0 keyCode:TKC_DownArrow],
            [self tiEventChars:kTIDownArrow ignMods:kTIDownArrow mods:0 keyCode:TKC_DownArrow],
            [self tiEventChars:kTIRightArrow ignMods:kTIRightArrow mods:0 keyCode:TKC_RightArrow],
            [self tiEventChars:kTIDownArrow ignMods:kTIDownArrow mods:0 keyCode:TKC_DownArrow],
            [self tiEventChars:@"\x1b" ignMods:@"\x1b" mods:0 keyCode:TKC_Escape], nil]];
        [self showContextMenuWithItems:contextItems atPageRect:TI_CONTEXTMENU_PAGE_RECT];
        tiAssert(_lastContextMenuChosenTag == -1, @"Escape inside the Share submenu cancels the whole menu, nothing chosen");

        [[NSApp delegate] performSelector:@selector(showReferencePopupForScreenshot) withObject:nil];

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
    NSPopUpButton *_referencePopup;  // real native control, for visual comparison
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
- (void)showReferencePopupForScreenshot;
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

    // Reference NSPopUpButton, docked top-right of the toolbar: a genuinely
    // separate, real native control (not built through tiBuildPopupCell) to
    // visually compare against the shell's serialized-item-list popup.
    const float refPopupWidth = 110.0;
    NSRect refPopupRect = NSMakeRect(bounds.size.width - refPopupWidth - pad, 4, refPopupWidth, 24);
    _referencePopup = [[[NSPopUpButton alloc] initWithFrame:refPopupRect pullsDown:NO] autorelease];
    [_referencePopup addItemsWithTitles:[NSArray arrayWithObjects:@"Ref A", @"Ref B", @"Ref C", nil]];
    [_referencePopup setAutoresizingMask:NSViewMinXMargin];
    [bar addSubview:_referencePopup];

    float addrX = pad + 76 + 34 + pad;
    NSRect addrRect = NSMakeRect(addrX, 4, bounds.size.width - addrX - pad - refPopupWidth - pad, 24);
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

// Real, separately-built NSPopUpButton (not through tiBuildPopupCell at
// all) opened and auto-dismissed via a scripted Return, purely so a
// screenshot can be compared directly against the shell's serialized-item
// <select> popup -- see spike/TigerBrowser/README.md.
- (void)dismissReferencePopupWithReturn:(NSTimer *)timer
{
    NSEvent *ret = [NSEvent keyEventWithType:NSKeyDown location:NSZeroPoint modifierFlags:0
                                    timestamp:CACurrentMediaTime() windowNumber:[_window windowNumber]
                                      context:nil characters:@"\r" charactersIgnoringModifiers:@"\r"
                                    isARepeat:NO keyCode:36];
    [NSApp postEvent:ret atStart:NO];
}

- (void)showReferencePopupForScreenshot
{
    tiLog(@"-- reference NSPopUpButton: opening for visual comparison (auto-dismiss via Return after 2s) --");
    NSTimer *t = [NSTimer timerWithTimeInterval:2.0 target:self
                                        selector:@selector(dismissReferencePopupWithReturn:)
                                        userInfo:nil repeats:NO];
    [[NSRunLoop currentRunLoop] addTimer:t forMode:(NSString *)kCFRunLoopCommonModes];
    [_referencePopup performClick:nil];
    tiLog(@"-- reference NSPopUpButton dismissed, selected '%@' --", [_referencePopup titleOfSelectedItem]);
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
- (void)showReferencePopupForScreenshot { [_controller showReferencePopupForScreenshot]; }

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
