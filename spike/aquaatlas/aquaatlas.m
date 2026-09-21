/* aquaatlas -- render Tiger's real Aqua controls into an atlas.
 *
 * Built and run on the Tiger box as a 32-bit MRR Cocoa tool. The 64-bit content
 * process has no AppKit, so every control WebCore's RenderThemeMac, ThemeMac and
 * ScrollbarThemeMac would draw is rendered here by the real NSCell and HITheme
 * code and shipped across as pixels plus a manifest.
 *
 * Output: one PNG per control/state/size into an output directory, and
 * atlas.json beside them. The manifest is the interface the content-process
 * theme codes against, so it carries more than the file names: 9-slice insets
 * derived by measurement, content insets, minimum sizes, and the system colours
 * RenderTheme asks NSColor for.
 *
 * Everything is measured rather than tabulated. Sizes come from -cellSize, and
 * the 9-slice insets come from rendering each resizable control at three widths
 * and three heights and diffing the pixels to find which rows and columns are
 * invariant. A hardcoded inset would be a guess about artwork we can just look
 * at.
 */

#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---------------------------------------------------------------- states */

enum {
    StateNormal        = 0,
    StatePressed       = 1 << 0,
    StateDisabled      = 1 << 1,
    StateChecked       = 1 << 2,
    StateMixed         = 1 << 3,
    StateFocused       = 1 << 4,
    StateWindowInactive = 1 << 5
};

static NSString *stateName(unsigned state)
{
    if (!state)
        return @"normal";
    NSMutableArray *parts = [NSMutableArray array];
    if (state & StatePressed)        [parts addObject:@"pressed"];
    if (state & StateDisabled)       [parts addObject:@"disabled"];
    if (state & StateChecked)        [parts addObject:@"checked"];
    if (state & StateMixed)          [parts addObject:@"mixed"];
    if (state & StateFocused)        [parts addObject:@"focused"];
    if (state & StateWindowInactive) [parts addObject:@"inactive"];
    return [parts componentsJoinedByString:@"-"];
}

static NSString *sizeName(NSControlSize size)
{
    if (size == NSSmallControlSize)
        return @"small";
    if (size == NSMiniControlSize)
        return @"mini";
    return @"regular";
}

/* ------------------------------------------------------- drawing surface */

/* One offscreen window, reused. NSCell drawing needs a view, and several cells
 * ask that view's window whether it is key before choosing their active or
 * inactive artwork, so the window is real and simply lives off the edge of the
 * screen rather than being a dummy. */
static NSWindow *gWindow;
static NSView *gView;

static void setUpOffscreenWindow(void)
{
    /* Two things are needed before a cell will draw its active artwork, and
     * both were missing at first, which made every NSCell variant come out in
     * the window-inactive state without anything saying so.
     *
     * A tool launched over ssh is a background process, so NSApp never becomes
     * active. TransformProcessType is the 10.3+ way to promote one.
     *
     * And a borderless window answers NO to -canBecomeKeyWindow, so it can
     * never be key however hard it is asked. Titled it is. */
    ProcessSerialNumber psn = { 0, kCurrentProcess };
    TransformProcessType(&psn, kProcessTransformToForegroundApplication);
    SetFrontProcess(&psn);

    NSRect frame = NSMakeRect(0, 0, 512, 512);
    gWindow = [[NSWindow alloc] initWithContentRect:frame
                                          styleMask:NSTitledWindowMask
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    gView = [[NSView alloc] initWithFrame:frame];
    [gWindow setContentView:gView];
}

static void setWindowActive(BOOL active)
{
    if (active) {
        [NSApp activateIgnoringOtherApps:YES];
        [gWindow makeKeyAndOrderFront:nil];
        [gWindow makeMainWindow];
    } else {
        [gWindow orderOut:nil];
        [NSApp deactivate];
    }
    /* Let the window server catch up; key state is not synchronous. */
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.3]];
}

/* The buffer is allocated here rather than letting CoreGraphics do it, because
 * Tiger's CGBitmapContextGetData returns NULL for a context created with a NULL
 * data pointer. Reading the pixels back is the whole basis of the 9-slice
 * measurement, so the buffer has to be one we hold. */
typedef struct {
    CGContextRef context;
    unsigned char *data;
    unsigned width, height;
    size_t rowBytes;
} Bitmap;

static Bitmap createBitmap(unsigned width, unsigned height)
{
    Bitmap bitmap;
    memset(&bitmap, 0, sizeof(bitmap));
    if (!width || !height)
        return bitmap;

    size_t rowBytes = (size_t)width * 4;
    unsigned char *data = calloc(rowBytes, height);
    if (!data)
        return bitmap;

    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    /* ARGB, premultiplied, 1x, which is what the content process will blit. */
    CGContextRef context = CGBitmapContextCreate(data, width, height, 8, rowBytes,
                                                 space, kCGImageAlphaPremultipliedFirst);
    CGColorSpaceRelease(space);
    if (!context) {
        free(data);
        return bitmap;
    }
    CGContextClearRect(context, CGRectMake(0, 0, width, height));
    CGContextSetShouldAntialias(context, true);

    bitmap.context = context;
    bitmap.data = data;
    bitmap.width = width;
    bitmap.height = height;
    bitmap.rowBytes = rowBytes;
    return bitmap;
}

static void releaseBitmap(Bitmap *bitmap)
{
    if (bitmap->context)
        CGContextRelease(bitmap->context);
    free(bitmap->data);
    memset(bitmap, 0, sizeof(*bitmap));
}

/* A begin/end pair rather than a block-taking helper: blocks need a runtime
 * Tiger does not have, and pulling libtigercompat in for one closure would give
 * this tool a dependency it otherwise does not need. */
static NSGraphicsContext *gSavedContext;

static void beginAppKitContext(CGContextRef context, unsigned height)
{
    gSavedContext = [[NSGraphicsContext currentContext] retain];
    /* Tiger spells it graphicsContextWithGraphicsPort:; the port already is a
     * CGContextRef. The origin is flipped by hand so cells draw top-down. */
    NSGraphicsContext *gc = [NSGraphicsContext graphicsContextWithGraphicsPort:(void *)context
                                                                       flipped:YES];
    [NSGraphicsContext setCurrentContext:gc];
    CGContextSaveGState(context);
    CGContextTranslateCTM(context, 0, height);
    CGContextScaleCTM(context, 1, -1);
}

static void endAppKitContext(CGContextRef context)
{
    CGContextRestoreGState(context);
    [NSGraphicsContext setCurrentContext:gSavedContext];
    [gSavedContext release];
    gSavedContext = nil;
}

/* ------------------------------------------------------------ PNG output */

static BOOL writePNG(const Bitmap *bitmap, NSString *path)
{
    CGImageRef image = CGBitmapContextCreateImage(bitmap->context);
    if (!image)
        return NO;
    CFURLRef url = CFURLCreateWithFileSystemPath(NULL, (CFStringRef)path,
                                                 kCFURLPOSIXPathStyle, false);
    CGImageDestinationRef dest = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, NULL);
    BOOL ok = NO;
    if (dest) {
        CGImageDestinationAddImage(dest, image, NULL);
        ok = CGImageDestinationFinalize(dest);
        CFRelease(dest);
    }
    CFRelease(url);
    CGImageRelease(image);
    return ok;
}

/* --------------------------------------------------------- 9-slice insets
 *
 * Render the same control at three sizes along an axis and find how many
 * leading and trailing lines of pixels are identical in all three. Those are
 * the parts that do not stretch; everything between them is the repeatable
 * middle. Measuring beats tabulating here because the artwork is the authority
 * and it differs per control, per size and per state.
 */

static BOOL columnsEqual(const Bitmap *a, unsigned ax, const Bitmap *b, unsigned bx)
{
    if (!a->data || !b->data || a->height != b->height
        || ax >= a->width || bx >= b->width)
        return NO;
    for (unsigned y = 0; y < a->height; ++y) {
        if (memcmp(a->data + y * a->rowBytes + ax * 4,
                   b->data + y * b->rowBytes + bx * 4, 4) != 0)
            return NO;
    }
    return YES;
}

static BOOL rowsEqual(const Bitmap *a, unsigned ay, const Bitmap *b, unsigned by)
{
    if (!a->data || !b->data || a->width != b->width
        || ay >= a->height || by >= b->height)
        return NO;
    return memcmp(a->data + ay * a->rowBytes, b->data + by * b->rowBytes,
                  (size_t)a->width * 4) == 0;
}

/* left/right insets from three renders of increasing width */
static void horizontalInsets(const Bitmap *a, const Bitmap *b, const Bitmap *c,
                             unsigned *left, unsigned *right)
{
    unsigned wa = a->width, wb = b->width, wc = c->width;
    unsigned limit = wa / 2;
    unsigned l = 0;
    while (l < limit && columnsEqual(a, l, b, l) && columnsEqual(a, l, c, l))
        ++l;
    unsigned r = 0;
    while (r < limit && columnsEqual(a, wa - 1 - r, b, wb - 1 - r) && columnsEqual(a, wa - 1 - r, c, wc - 1 - r))
        ++r;
    /* A symmetric control such as a capsule button is all end-cap: the columns
     * stay stable right up to the centre seam, so l and r meet and leave no
     * middle for a 9-slice consumer to tile. Give back one column, sampled at
     * the centre, which for a stretchable Aqua control is exactly the repeat. */
    if (l + r >= wa)
        r = (l < wa) ? (wa - 1 - l) : 0;
    *left = l;
    *right = r;
}

static void verticalInsets(const Bitmap *a, const Bitmap *b, const Bitmap *c,
                           unsigned *top, unsigned *bottom)
{
    unsigned ha = a->height, hb = b->height, hc = c->height;
    unsigned limit = ha / 2;
    unsigned t = 0;
    while (t < limit && rowsEqual(a, t, b, t) && rowsEqual(a, t, c, t))
        ++t;
    unsigned bt = 0;
    while (bt < limit && rowsEqual(a, ha - 1 - bt, b, hb - 1 - bt) && rowsEqual(a, ha - 1 - bt, c, hc - 1 - bt))
        ++bt;
    if (t + bt >= ha)
        bt = (t < ha) ? (ha - 1 - t) : 0;
    *top = t;
    *bottom = bt;
}

/* ------------------------------------------------------------- painters */

typedef enum {
    PainterButton, PainterCheckbox, PainterRadio, PainterDisclosure,
    PainterPopUpMenu, PainterPopUpList, PainterTextField, PainterSearchField,
    PainterSliderThumbH, PainterSliderThumbV, PainterSliderTrackH, PainterSliderTrackV,
    PainterProgressBar, PainterProgressIndeterminate, PainterLevelIndicator,
    PainterScrollTrackV, PainterScrollTrackH, PainterScrollThumbV, PainterScrollThumbH,
    PainterScrollArrowsV, PainterScrollArrowsH, PainterFocusRing
} PainterKind;

typedef struct {
    const char *identifier;
    PainterKind kind;
    BOOL stretchX;
    BOOL stretchY;
    unsigned supportedStates;   /* which state bits are meaningful */
} ControlSpec;

static const ControlSpec kControls[] = {
    { "button",                PainterButton,       YES, NO,  StatePressed|StateDisabled|StateFocused|StateWindowInactive },
    { "checkbox",              PainterCheckbox,     NO,  NO,  StatePressed|StateDisabled|StateChecked|StateMixed|StateFocused|StateWindowInactive },
    { "radio",                 PainterRadio,        NO,  NO,  StatePressed|StateDisabled|StateChecked|StateFocused|StateWindowInactive },
    { "disclosure",            PainterDisclosure,   NO,  NO,  StatePressed|StateDisabled|StateChecked|StateWindowInactive },
    { "popup-menu",            PainterPopUpMenu,    YES, NO,  StatePressed|StateDisabled|StateFocused|StateWindowInactive },
    { "popup-list",            PainterPopUpList,    YES, NO,  StatePressed|StateDisabled|StateFocused|StateWindowInactive },
    { "textfield",             PainterTextField,    YES, YES, StateDisabled|StateFocused|StateWindowInactive },
    { "searchfield",           PainterSearchField,  YES, NO,  StateDisabled|StateFocused|StateWindowInactive },
    { "slider-thumb-h",        PainterSliderThumbH, NO,  NO,  StatePressed|StateDisabled|StateWindowInactive },
    { "slider-thumb-v",        PainterSliderThumbV, NO,  NO,  StatePressed|StateDisabled|StateWindowInactive },
    { "slider-track-h",        PainterSliderTrackH, YES, NO,  StateDisabled|StateWindowInactive },
    { "slider-track-v",        PainterSliderTrackV, NO,  YES, StateDisabled|StateWindowInactive },
    { "progress-bar",          PainterProgressBar,  YES, NO,  StateDisabled|StateWindowInactive },
    { "progress-indeterminate", PainterProgressIndeterminate, YES, NO, StateWindowInactive },
    { "level-indicator",       PainterLevelIndicator, YES, NO, StateDisabled|StateWindowInactive },
    /* One image per orientation, not three. HIThemeDrawTrack draws the whole
     * scrollbar -- track, thumb and both arrows -- so asking it for a "thumb"
     * returned a complete scrollbar three times over. The parts are reported as
     * rects inside the one image instead, measured with
     * HIThemeGetTrackPartBounds, which is what ScrollbarThemeMac needs anyway. */
    { "scrollbar-v",           PainterScrollTrackV, NO,  YES, StatePressed|StateDisabled|StateWindowInactive },
    { "scrollbar-h",           PainterScrollTrackH, YES, NO,  StatePressed|StateDisabled|StateWindowInactive },
    { "focus-ring",            PainterFocusRing,    YES, YES, 0 },
};
static const unsigned kControlCount = sizeof(kControls) / sizeof(kControls[0]);

static NSCell *cellForKind(PainterKind kind, NSControlSize size, unsigned state)
{
    NSCell *cell = nil;
    switch (kind) {
    case PainterButton: {
        NSButtonCell *b = [[[NSButtonCell alloc] init] autorelease];
        [b setBezelStyle:NSRoundedBezelStyle];
        [b setButtonType:NSMomentaryPushInButton];
        [b setTitle:@""];
        cell = b;
        break;
    }
    case PainterCheckbox: {
        NSButtonCell *b = [[[NSButtonCell alloc] init] autorelease];
        [b setButtonType:NSSwitchButton];
        [b setAllowsMixedState:YES];
        [b setTitle:@""];
        cell = b;
        break;
    }
    case PainterRadio: {
        NSButtonCell *b = [[[NSButtonCell alloc] init] autorelease];
        [b setButtonType:NSRadioButton];
        [b setTitle:@""];
        cell = b;
        break;
    }
    case PainterDisclosure: {
        NSButtonCell *b = [[[NSButtonCell alloc] init] autorelease];
        [b setBezelStyle:NSDisclosureBezelStyle];
        [b setButtonType:NSOnOffButton];
        [b setTitle:@""];
        cell = b;
        break;
    }
    case PainterPopUpMenu:
    case PainterPopUpList: {
        NSPopUpButtonCell *p = [[[NSPopUpButtonCell alloc] initTextCell:@"" pullsDown:NO] autorelease];
        [p setBezelStyle:NSRoundedBezelStyle];
        /* The <select> popup and a pull-down menu button are different artwork;
         * the arrow position is the visible difference. */
        [p setArrowPosition:(kind == PainterPopUpList) ? NSPopUpArrowAtBottom : NSPopUpArrowAtCenter];
        cell = p;
        break;
    }
    case PainterTextField: {
        NSTextFieldCell *t = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
        [t setBezeled:YES];
        [t setBezelStyle:NSTextFieldSquareBezel];
        [t setDrawsBackground:YES];
        cell = t;
        break;
    }
    case PainterSearchField: {
        NSTextFieldCell *t = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
        [t setBezeled:YES];
        [t setBezelStyle:NSTextFieldRoundedBezel];
        [t setDrawsBackground:YES];
        cell = t;
        break;
    }
    case PainterSliderThumbH:
    case PainterSliderTrackH: {
        NSSliderCell *s = [[[NSSliderCell alloc] init] autorelease];
        [s setSliderType:NSLinearSlider];
        cell = s;
        break;
    }
    case PainterSliderThumbV:
    case PainterSliderTrackV: {
        NSSliderCell *s = [[[NSSliderCell alloc] init] autorelease];
        [s setSliderType:NSLinearSlider];
        cell = s;
        break;
    }
    case PainterLevelIndicator: {
        NSLevelIndicatorCell *l =
            [[[NSLevelIndicatorCell alloc] initWithLevelIndicatorStyle:NSContinuousCapacityLevelIndicatorStyle] autorelease];
        [l setMinValue:0];
        [l setMaxValue:100];
        [l setDoubleValue:60];
        cell = l;
        break;
    }
    default:
        return nil;
    }

    [cell setControlSize:size];
    [cell setEnabled:!(state & StateDisabled)];
    [cell setHighlighted:(state & StatePressed) ? YES : NO];
    [cell setShowsFirstResponder:(state & StateFocused) ? YES : NO];
    if ([cell allowsMixedState] && (state & StateMixed))
        [cell setState:NSMixedState];
    else
        [cell setState:(state & StateChecked) ? NSOnState : NSOffState];
    if (kind == PainterSliderThumbH || kind == PainterSliderThumbV
        || kind == PainterSliderTrackH || kind == PainterSliderTrackV) {
        NSSliderCell *s = (NSSliderCell *)cell;
        [s setMinValue:0];
        [s setMaxValue:1];
        [s setDoubleValue:0.5];
    }
    return cell;
}

/* The button family goes through HITheme rather than NSCell.
 *
 * Measured, not assumed: -[NSCell drawWithFrame:inView:] produces byte-identical
 * artwork whether or not the cell's window is key, so the window-inactive axis
 * is simply not reachable that way. HIThemeDrawButton takes the state as an
 * argument and does have distinct active, inactive, pressed and disabled
 * artwork, and it is what AppKit draws through underneath, so the pixels are
 * the same ones a real Aqua control would show. */
static ThemeDrawState themeState(unsigned state)
{
    if (state & StatePressed)
        return kThemeStatePressed;
    if (state & StateDisabled)
        return (state & StateWindowInactive) ? kThemeStateUnavailableInactive : kThemeStateUnavailable;
    if (state & StateWindowInactive)
        return kThemeStateInactive;
    return kThemeStateActive;
}

/* Tiger's ThemeButtonKind has no mini variants at all, and small ones only for
 * the check box, radio button, bevel button and combo box. For everything else
 * the size comes from the bounds, which is how Tiger's own HITheme works: there
 * is no kThemeSmallPushButton to ask for. Mini falls back to small, which is
 * the closest artwork this system has. */
static ThemeButtonKind themeButtonKind(PainterKind kind, NSControlSize size)
{
    BOOL smallish = (size != NSRegularControlSize);
    switch (kind) {
    case PainterButton:
        return kThemePushButton;
    case PainterCheckbox:
        return smallish ? kThemeCheckBoxSmall : kThemeCheckBox;
    case PainterRadio:
        return smallish ? kThemeRadioButtonSmall : kThemeRadioButton;
    case PainterDisclosure:
        return kThemeDisclosureTriangle;
    case PainterPopUpMenu:
        return smallish ? kThemeBevelButtonSmall : kThemeBevelButton;
    case PainterPopUpList:
        return kThemePopupButton;
    default:
        return kThemePushButton;
    }
}

static void drawThemeButton(CGContextRef context, PainterKind kind, NSRect rect,
                            NSControlSize size, unsigned state)
{
    HIThemeButtonDrawInfo info;
    memset(&info, 0, sizeof(info));
    info.version = 0;
    info.state = themeState(state);
    info.kind = themeButtonKind(kind, size);
    if (state & StateMixed)
        info.value = kThemeButtonMixed;
    else
        info.value = (state & StateChecked) ? kThemeButtonOn : kThemeButtonOff;
    info.adornment = (state & StateFocused) ? kThemeAdornmentFocus : kThemeAdornmentNone;

    /* The focus ring is drawn outside the control, so leave room for it. */
    /* float, not CGFloat: the 10.4 SDK has no CGFloat, and this tool stays off
     * the compat overlay on purpose. On i386 they are the same type anyway. */
    float inset = (state & StateFocused) ? 3 : 1;
    HIRect bounds = CGRectMake(rect.origin.x + inset, rect.origin.y + inset,
                               rect.size.width - inset * 2, rect.size.height - inset * 2);
    HIThemeDrawButton(&bounds, &info, context, kHIThemeOrientationNormal, NULL);
}

/* HITheme scrollbar metrics. NSScroller would draw a whole scroller; WebCore
 * wants the parts separately, and HIThemeDrawTrack is the API AppKit itself
 * sits on. */
static void fillScrollbarInfo(HIThemeTrackDrawInfo *out, PainterKind kind, NSRect rect,
                              NSControlSize size, unsigned state);

static void drawScrollbar(CGContextRef context, PainterKind kind, NSRect rect,
                          NSControlSize size, unsigned state)
{
    HIThemeTrackDrawInfo info;
    fillScrollbarInfo(&info, kind, rect, size, state);
    HIThemeDrawTrack(&info, NULL, context, kHIThemeOrientationNormal);
}

static void fillScrollbarInfo(HIThemeTrackDrawInfo *outInfo, PainterKind kind, NSRect rect,
                              NSControlSize size, unsigned state)
{
    HIThemeTrackDrawInfo info;
    memset(&info, 0, sizeof(info));
    info.version = 0;
    info.kind = (size == NSRegularControlSize) ? kThemeScrollBarMedium : kThemeScrollBarSmall;
    info.bounds = CGRectMake(rect.origin.x, rect.origin.y, rect.size.width, rect.size.height);
    info.min = 0;
    info.max = 100;
    info.value = 30;
    info.attributes = kThemeTrackShowThumb;
    if (kind == PainterScrollTrackH || kind == PainterScrollThumbH || kind == PainterScrollArrowsH)
        info.attributes |= kThemeTrackHorizontal;
    if (state & StateWindowInactive)
        info.enableState = kThemeTrackInactive;
    else if (state & StateDisabled)
        info.enableState = kThemeTrackDisabled;
    else
        info.enableState = kThemeTrackActive;
    info.trackInfo.scrollbar.viewsize = 40;
    info.trackInfo.scrollbar.pressState =
        (state & StatePressed) ? kThemeThumbPressed : 0;

    *outInfo = info;
}

/* Part rects inside the rendered scrollbar, measured rather than derived from
 * the metrics, so they stay right if the artwork differs from the arithmetic. */
static NSString *scrollbarPartsJSON(PainterKind kind, NSRect rect,
                                    NSControlSize size, unsigned state)
{
    HIThemeTrackDrawInfo info;
    fillScrollbarInfo(&info, kind, rect, size, state);

    struct { const char *name; ControlPartCode part; } parts[] = {
        { "thumb",         kControlIndicatorPart },
        { "trackBefore",   kControlPageUpPart },
        { "trackAfter",    kControlPageDownPart },
        { "arrowDecrement", kControlUpButtonPart },
        { "arrowIncrement", kControlDownButtonPart },
    };
    NSMutableString *json = [NSMutableString stringWithString:@", \"parts\": {"];
    for (unsigned i = 0; i < 5; ++i) {
        HIRect bounds;
        if (HIThemeGetTrackPartBounds(&info, parts[i].part, &bounds) != noErr)
            continue;
        [json appendFormat:@"%s\"%s\": [%d, %d, %d, %d]", (i && [json length] > 11) ? ", " : "",
                           parts[i].name, (int)bounds.origin.x, (int)bounds.origin.y,
                           (int)bounds.size.width, (int)bounds.size.height];
    }
    [json appendString:@"}"];
    return json;
}

/* Returns the natural size of a variant, or NSZeroSize if it cannot be drawn. */
static NSSize naturalSize(const ControlSpec *spec, NSControlSize size)
{
    switch (spec->kind) {
    case PainterProgressBar:
    case PainterProgressIndeterminate:
        return NSMakeSize(120, size == NSRegularControlSize ? 16 : (size == NSSmallControlSize ? 12 : 10));
    case PainterScrollTrackV:
    case PainterScrollThumbV:
        return NSMakeSize(size == NSRegularControlSize ? 15 : 11, 80);
    case PainterScrollTrackH:
    case PainterScrollThumbH:
        return NSMakeSize(80, size == NSRegularControlSize ? 15 : 11);
    case PainterScrollArrowsV:
        return NSMakeSize(size == NSRegularControlSize ? 15 : 11, 40);
    case PainterScrollArrowsH:
        return NSMakeSize(40, size == NSRegularControlSize ? 15 : 11);
    case PainterFocusRing:
        return NSMakeSize(40, 22);
    case PainterSliderTrackH:
        return NSMakeSize(80, size == NSRegularControlSize ? 21 : 15);
    case PainterSliderTrackV:
        return NSMakeSize(size == NSRegularControlSize ? 21 : 15, 80);
    case PainterTextField:
        return NSMakeSize(80, size == NSRegularControlSize ? 22 : (size == NSSmallControlSize ? 19 : 16));
    case PainterSearchField:
        return NSMakeSize(80, size == NSRegularControlSize ? 22 : (size == NSSmallControlSize ? 19 : 16));
    case PainterLevelIndicator:
        return NSMakeSize(80, size == NSRegularControlSize ? 16 : 12);
    default:
        break;
    }
    NSCell *cell = cellForKind(spec->kind, size, StateNormal);
    if (!cell)
        return NSZeroSize;
    NSSize natural = [cell cellSize];
    /* Untitled buttons and popups come back narrow; widen so the middle of the
     * bezel exists and the 9-slice diff has something to find. */
    if (spec->stretchX && natural.width < 48)
        natural.width = 48;
    if (natural.height < 1 || natural.height > 200)
        natural.height = 22;
    return natural;
}

/* The one place a variant is actually drawn. */
static Bitmap renderVariant(const ControlSpec *spec, NSControlSize size,
                            unsigned state, NSSize wanted)
{
    unsigned width = (unsigned)ceil(wanted.width);
    unsigned height = (unsigned)ceil(wanted.height);

    Bitmap bitmap = createBitmap(width, height);
    CGContextRef context = bitmap.context;
    if (!context)
        return bitmap;

    NSRect rect = NSMakeRect(0, 0, width, height);

    switch (spec->kind) {
    case PainterButton: case PainterCheckbox: case PainterRadio:
    case PainterDisclosure: case PainterPopUpMenu: case PainterPopUpList:
        drawThemeButton(context, spec->kind, rect, size, state);
        break;

    case PainterScrollTrackV: case PainterScrollTrackH:
    case PainterScrollThumbV: case PainterScrollThumbH:
    case PainterScrollArrowsV: case PainterScrollArrowsH:
        drawScrollbar(context, spec->kind, rect, size, state);
        break;

    case PainterFocusRing:
        beginAppKitContext(context, height);
        NSSetFocusRingStyle(NSFocusRingOnly);
        [[NSBezierPath bezierPathWithRect:NSInsetRect(rect, 3, 3)] fill];
        endAppKitContext(context);
        break;

    case PainterProgressBar:
    case PainterProgressIndeterminate: {
        NSProgressIndicator *bar = [[NSProgressIndicator alloc] initWithFrame:rect];
        [bar setStyle:NSProgressIndicatorBarStyle];
        [bar setIndeterminate:(spec->kind == PainterProgressIndeterminate)];
        [bar setControlSize:size];
        if (spec->kind == PainterProgressBar) {
            [bar setMinValue:0];
            [bar setMaxValue:100];
            [bar setDoubleValue:60];
        }
        [gView addSubview:bar];
        beginAppKitContext(context, height);
        [bar drawRect:rect];
        endAppKitContext(context);
        [bar removeFromSuperview];
        [bar release];
        break;
    }

    default: {
        NSCell *cell = cellForKind(spec->kind, size, state);
        if (!cell) {
            releaseBitmap(&bitmap);
            return bitmap;
        }
        beginAppKitContext(context, height);
        if (spec->kind == PainterSliderThumbH || spec->kind == PainterSliderThumbV)
            [(NSSliderCell *)cell drawKnob:rect];
        else if (spec->kind == PainterSliderTrackH || spec->kind == PainterSliderTrackV)
            [(NSSliderCell *)cell drawBarInside:rect flipped:YES];
        else
            [cell drawWithFrame:rect inView:gView];
        endAppKitContext(context);
        break;
    }
    }
    return bitmap;
}

/* --------------------------------------------------------------- colours */

static NSString *colorJSON(NSColor *color)
{
    NSColor *rgb = [color colorUsingColorSpaceName:NSDeviceRGBColorSpace];
    if (rgb) {
        float r = 0, g = 0, b = 0, a = 0;
        [rgb getRed:&r green:&g blue:&b alpha:&a];
        return [NSString stringWithFormat:@"[%.4f, %.4f, %.4f, %.4f]", r, g, b, a];
    }

    /* +controlColor and +windowBackgroundColor are pattern colours on Tiger, so
     * they have no RGB components to ask for. Filling a pixel with the pattern
     * and reading it back gives the content process something it can actually
     * use, which is better than a null the theme would have to guess around. */
    Bitmap pixel = createBitmap(1, 1);
    if (!pixel.context)
        return @"null";
    beginAppKitContext(pixel.context, 1);
    [color set];
    NSRectFill(NSMakeRect(0, 0, 1, 1));
    endAppKitContext(pixel.context);
    unsigned char *p = pixel.data;
    /* ARGB premultiplied; alpha is 255 for both of these. */
    float a = p[0] / 255.0f, r = p[1] / 255.0f, g = p[2] / 255.0f, b = p[3] / 255.0f;
    NSString *result = [NSString stringWithFormat:@"[%.4f, %.4f, %.4f, %.4f]", r, g, b, a];
    releaseBitmap(&pixel);
    return result;
}

static void appendColors(NSMutableString *json)
{
    struct { const char *name; NSColor *color; } colors[] = {
        { "selectedTextBackground",    [NSColor selectedTextBackgroundColor] },
        { "selectedText",              [NSColor selectedTextColor] },
        { "secondarySelectedControl",  [NSColor secondarySelectedControlColor] },
        { "alternateSelectedControl",  [NSColor alternateSelectedControlColor] },
        { "alternateSelectedControlText", [NSColor alternateSelectedControlTextColor] },
        { "keyboardFocusIndicator",    [NSColor keyboardFocusIndicatorColor] },
        { "control",                   [NSColor controlColor] },
        { "controlText",               [NSColor controlTextColor] },
        { "controlBackground",         [NSColor controlBackgroundColor] },
        { "disabledControlText",       [NSColor disabledControlTextColor] },
        { "text",                      [NSColor textColor] },
        { "textBackground",            [NSColor textBackgroundColor] },
        { "windowBackground",          [NSColor windowBackgroundColor] },
        { "grid",                      [NSColor gridColor] },
        { "headerText",                [NSColor headerTextColor] },
        { "highlight",                 [NSColor highlightColor] },
        { "shadow",                    [NSColor shadowColor] },
    };
    [json appendString:@"  \"colors\": {\n"];
    unsigned count = sizeof(colors) / sizeof(colors[0]);
    for (unsigned i = 0; i < count; ++i) {
        [json appendFormat:@"    \"%s\": %@%s\n", colors[i].name,
                           colorJSON(colors[i].color), (i + 1 < count) ? "," : ""];
    }
    [json appendString:@"  },\n"];

    NSArray *rowColors = [NSColor controlAlternatingRowBackgroundColors];
    [json appendString:@"  \"alternatingRowColors\": ["];
    for (unsigned i = 0; i < [rowColors count]; ++i)
        [json appendFormat:@"%@%s", colorJSON([rowColors objectAtIndex:i]),
                           (i + 1 < [rowColors count]) ? ", " : ""];
    [json appendString:@"],\n"];
}

/* ------------------------------------------------------------------ main */

int main(int argc, const char **argv)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    setbuf(stdout, NULL);

    NSString *outDir = (argc > 1) ? [NSString stringWithUTF8String:argv[1]] : @"atlas";
    [[NSFileManager defaultManager] createDirectoryAtPath:outDir attributes:nil];

    [NSApplication sharedApplication];
    setUpOffscreenWindow();

    NSMutableString *json = [NSMutableString string];
    [json appendString:@"{\n"];
    [json appendFormat:@"  \"generator\": \"spike/aquaatlas\",\n"];
    [json appendFormat:@"  \"system\": \"%@\",\n",
        [[NSDictionary dictionaryWithContentsOfFile:
            @"/System/Library/CoreServices/SystemVersion.plist"] objectForKey:@"ProductVersion"]];
    [json appendString:@"  \"scale\": 1,\n"];
    [json appendString:@"  \"pixelFormat\": \"ARGB8888-premultiplied\",\n"];
    appendColors(json);
    [json appendString:@"  \"images\": [\n"];

    const NSControlSize sizes[] = { NSRegularControlSize, NSSmallControlSize, NSMiniControlSize };
    unsigned written = 0, skipped = 0;
    BOOL firstEntry = YES;

    for (unsigned windowActive = 0; windowActive < 2; ++windowActive) {
        setWindowActive(!windowActive);
        fprintf(stderr, "pass %s: NSApp active=%d, window key=%d, main=%d\n",
                windowActive ? "inactive" : "active",
                (int)[NSApp isActive], (int)[gWindow isKeyWindow], (int)[gWindow isMainWindow]);

        /* Refuse to write an atlas whose "active" artwork was drawn by an
         * application that never became active. This is not theoretical: the
         * first run after the .app bundle is created cannot be foregrounded,
         * because LaunchServices has not registered it yet, and the two passes
         * then produce identical pixels with nothing to say so. Everything
         * downstream would look plausible and be the wrong artwork. */
        if (!windowActive && !([NSApp isActive] && [gWindow isKeyWindow])) {
            fprintf(stderr,
                "aquaatlas: the active pass is not active (NSApp active=%d, key=%d).\n"
                "  Run from inside a .app bundle, and run it twice: the first launch\n"
                "  only registers the bundle with LaunchServices.\n"
                "  Set AQUAATLAS_ALLOW_INACTIVE=1 to write the atlas anyway.\n",
                (int)[NSApp isActive], (int)[gWindow isKeyWindow]);
            if (!getenv("AQUAATLAS_ALLOW_INACTIVE"))
                return 2;
        }

        for (unsigned c = 0; c < kControlCount; ++c) {
            const ControlSpec *spec = &kControls[c];

            for (unsigned s = 0; s < 3; ++s) {
                NSControlSize size = sizes[s];
                NSSize natural = naturalSize(spec, size);
                if (natural.width <= 0 || natural.height <= 0) {
                    ++skipped;
                    continue;
                }

                /* The stored image is padded along each stretchable axis so it
                 * contains a real middle. At its natural size a capsule button
                 * is all end-cap and nothing else: the measurement correctly
                 * reports left+right == width, and a 9-slice consumer would
                 * then have no middle column to tile. Sixteen extra pixels is
                 * enough for any Aqua control's repeat. */
                NSSize canonical = natural;
                if (spec->stretchX)
                    canonical.width += 16;
                if (spec->stretchY)
                    canonical.height += 16;

                /* Every combination of the states this control supports, plus
                 * the plain one. Enumerating the power set of the supported
                 * bits would explode; WebCore asks for one modifier at a time
                 * plus checked-and-pressed, so that is what is rendered. */
                unsigned variants[16];
                unsigned variantCount = 0;
                variants[variantCount++] = StateNormal;
                unsigned bits[] = { StatePressed, StateDisabled, StateChecked,
                                    StateMixed, StateFocused };
                for (unsigned b = 0; b < 5; ++b) {
                    if (spec->supportedStates & bits[b])
                        variants[variantCount++] = bits[b];
                }
                if ((spec->supportedStates & StateChecked) && (spec->supportedStates & StatePressed))
                    variants[variantCount++] = StateChecked | StatePressed;
                if ((spec->supportedStates & StateChecked) && (spec->supportedStates & StateDisabled))
                    variants[variantCount++] = StateChecked | StateDisabled;

                for (unsigned v = 0; v < variantCount; ++v) {
                    unsigned state = variants[v];
                    if (windowActive)
                        state |= StateWindowInactive;

                    if (getenv("AQUAATLAS_TRACE"))
                        fprintf(stderr, "  %s %s %s %ux%u\n", spec->identifier,
                                [sizeName(size) UTF8String], [stateName(state) UTF8String],
                                (unsigned)canonical.width, (unsigned)canonical.height);
                    Bitmap bitmap = renderVariant(spec, size, state, canonical);
                    if (!bitmap.context) {
                        ++skipped;
                        continue;
                    }

                    unsigned left = 0, right = 0, top = 0, bottom = 0;
                    if (spec->stretchX) {
                        Bitmap b = renderVariant(spec, size, state,
                                                 NSMakeSize(canonical.width + 8, canonical.height));
                        Bitmap d = renderVariant(spec, size, state,
                                                 NSMakeSize(canonical.width + 16, canonical.height));
                        if (b.context && d.context)
                            horizontalInsets(&bitmap, &b, &d, &left, &right);
                        releaseBitmap(&b);
                        releaseBitmap(&d);
                    }
                    if (spec->stretchY) {
                        Bitmap b = renderVariant(spec, size, state,
                                                 NSMakeSize(canonical.width, canonical.height + 8));
                        Bitmap d = renderVariant(spec, size, state,
                                                 NSMakeSize(canonical.width, canonical.height + 16));
                        if (b.context && d.context)
                            verticalInsets(&bitmap, &b, &d, &top, &bottom);
                        releaseBitmap(&b);
                        releaseBitmap(&d);
                    }

                    NSString *name = [NSString stringWithFormat:@"%s-%@-%@.png",
                                      spec->identifier, sizeName(size), stateName(state)];
                    NSString *path = [outDir stringByAppendingPathComponent:name];
                    if (!writePNG(&bitmap, path)) {
                        releaseBitmap(&bitmap);
                        ++skipped;
                        continue;
                    }

                    unsigned w = bitmap.width, h = bitmap.height;
                    [json appendFormat:@"%s    {\"file\": \"%@\", \"control\": \"%s\", \"state\": \"%@\", "
                                       @"\"size\": \"%@\", \"rect\": [0, 0, %u, %u], "
                                       @"\"slice\": {\"left\": %u, \"right\": %u, \"top\": %u, \"bottom\": %u}, "
                                       @"\"stretch\": {\"x\": %s, \"y\": %s}, "
                                       @"\"minSize\": [%u, %u], \"naturalSize\": [%u, %u]%@}",
                                       firstEntry ? "" : ",\n", name, spec->identifier,
                                       stateName(state), sizeName(size), w, h,
                                       left, right, top, bottom,
                                       spec->stretchX ? "true" : "false",
                                       spec->stretchY ? "true" : "false",
                                       left + right + 1, top + bottom + 1,
                                       (unsigned)natural.width, (unsigned)natural.height,
                                       (spec->kind == PainterScrollTrackV || spec->kind == PainterScrollTrackH)
                                           ? scrollbarPartsJSON(spec->kind,
                                                 NSMakeRect(0, 0, w, h), size, state)
                                           : @""];
                    firstEntry = NO;
                    ++written;
                    releaseBitmap(&bitmap);
                }
            }
        }
    }

    [json appendString:@"\n  ]\n}\n"];
    NSString *manifest = [outDir stringByAppendingPathComponent:@"atlas.json"];
    [json writeToFile:manifest atomically:YES];

    printf("wrote %u images, skipped %u, manifest %s\n", written, skipped, [manifest UTF8String]);
    [pool release];
    return written ? 0 : 1;
}
