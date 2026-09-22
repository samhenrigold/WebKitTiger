/* TIGER: draw WebCore's native controls with Tiger's own Aqua.
 * Declared in <TigerCompat/AquaControls.h>. MRR, fragile runtime.
 *
 * A port of WebCore/platform/graphics/mac/controls/*.mm to what 10.4 has. The
 * cell types, bezel styles, the control-size-from-font rule and the
 * cellSize/cellOutsets tables are taken from those files rather than invented,
 * because the whole point is that the artwork lands where WebCore already
 * expects it. Where 10.4 cannot do what the modern file does, the difference is
 * commented at the site rather than silently approximated.
 */

#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#import <TigerCompat/AquaControls.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* NSControlSize is 0 regular, 1 small, 2 mini on 10.4; 3 (large) is 10.16 and
 * has no artwork here, so the tables keep four entries to match WebCore's and
 * the large slot falls back to regular. */
#define kSizeCount 3

/* NSRectFromCGRect is 10.5. On 10.4 NSRect and CGRect are distinct structs that
 * happen to be four floats each on i386, but the conversion is written out
 * rather than cast, so it stays correct if that ever stops being true. */
static NSRect toNSRect(CGRect r)
{
    return NSMakeRect(r.origin.x, r.origin.y, r.size.width, r.size.height);
}

/* ------------------------------------------------------------ shared state */

static NSWindow *gWindow;
static NSView *gView;

/* ControlFactoryMac keeps one cell per kind and reconfigures it per draw; the
 * same cells, created the same way, are kept here. */
static NSButtonCell *gButtonCell;
static NSButtonCell *gDefaultButtonCell;
static NSButtonCell *gCheckboxCell;
static NSButtonCell *gRadioCell;
static NSPopUpButtonCell *gPopUpCell;
static NSTextFieldCell *gTextFieldCell;
static NSTextFieldCell *gSearchFieldCell;
static NSSliderCell *gSliderCell;
static NSLevelIndicatorCell *gLevelIndicatorCell;
static NSProgressIndicator *gProgressIndicator;

static void ensureDrawingView(void)
{
    if (gView)
        return;
    /* Tiger has no viewless cell drawing (-drawWithFrame: needs a view for the
     * focus ring and for the window's key state), so ControlFactoryMac's
     * drawingView path is the one that applies. */
    NSRect frame = NSMakeRect(0, 0, 1024, 1024);
    gWindow = [[NSWindow alloc] initWithContentRect:frame
                                          styleMask:NSTitledWindowMask
                                            backing:NSBackingStoreBuffered
                                              defer:YES];
    gView = [[NSView alloc] initWithFrame:frame];
    [gWindow setContentView:gView];
}

static NSButtonCell *buttonCell(BOOL isDefault)
{
    NSButtonCell **slot = isDefault ? &gDefaultButtonCell : &gButtonCell;
    if (!*slot) {
        NSButtonCell *cell = [[NSButtonCell alloc] init];
        [cell setTitle:nil];
        [cell setButtonType:NSMomentaryPushInButton];
        if (isDefault)
            [cell setKeyEquivalent:@"\r"];
        *slot = cell;
    }
    return *slot;
}

static NSButtonCell *toggleCell(BOOL isRadio)
{
    NSButtonCell **slot = isRadio ? &gRadioCell : &gCheckboxCell;
    if (!*slot) {
        NSButtonCell *cell = [[NSButtonCell alloc] init];
        [cell setTitle:nil];
        [cell setFocusRingType:NSFocusRingTypeExterior];
        if (isRadio)
            [cell setButtonType:NSRadioButton];
        else {
            [cell setButtonType:NSSwitchButton];
            [cell setAllowsMixedState:YES];
        }
        *slot = cell;
    }
    return *slot;
}

static NSPopUpButtonCell *popUpCell(void)
{
    if (!gPopUpCell) {
        gPopUpCell = [[NSPopUpButtonCell alloc] initTextCell:@"" pullsDown:NO];
        [gPopUpCell setUsesItemFromMenu:NO];
        [gPopUpCell setFocusRingType:NSFocusRingTypeExterior];
        /* ControlFactoryMac also pins the layout direction left-to-right; that
         * accessor is 10.11 and Tiger's AppKit has no other direction. */
    }
    return gPopUpCell;
}

static NSTextFieldCell *textFieldCell(void)
{
    if (!gTextFieldCell) {
        gTextFieldCell = [[NSTextFieldCell alloc] initTextCell:@""];
        [gTextFieldCell setBezeled:YES];
        [gTextFieldCell setBezelStyle:NSTextFieldSquareBezel];
        [gTextFieldCell setEditable:YES];
        [gTextFieldCell setDrawsBackground:YES];
        [gTextFieldCell setFocusRingType:NSFocusRingTypeExterior];
    }
    return gTextFieldCell;
}

static NSTextFieldCell *searchFieldCell(void)
{
    if (!gSearchFieldCell) {
        /* ControlFactoryMac uses NSSearchFieldCell, which Tiger has, but its
         * 10.4 bezel drawing goes through the search field's own subviews. The
         * rounded text field bezel is the same artwork for the frame itself,
         * and the magnifier and cancel glyphs are separate parts WebCore draws
         * as SearchFieldResults and SearchFieldCancelButton anyway. */
        gSearchFieldCell = [[NSTextFieldCell alloc] initTextCell:@""];
        [gSearchFieldCell setBezeled:YES];
        [gSearchFieldCell setBezelStyle:NSTextFieldRoundedBezel];
        [gSearchFieldCell setEditable:YES];
        [gSearchFieldCell setDrawsBackground:YES];
        [gSearchFieldCell setFocusRingType:NSFocusRingTypeExterior];
    }
    return gSearchFieldCell;
}

static NSSliderCell *sliderCell(void)
{
    if (!gSliderCell) {
        gSliderCell = [[NSSliderCell alloc] init];
        [gSliderCell setSliderType:NSLinearSlider];
        [gSliderCell setControlSize:NSSmallControlSize];
        [gSliderCell setFocusRingType:NSFocusRingTypeExterior];
        [gSliderCell setMinValue:0];
        [gSliderCell setMaxValue:1];
    }
    return gSliderCell;
}

static NSLevelIndicatorCell *levelIndicatorCell(void)
{
    if (!gLevelIndicatorCell) {
        gLevelIndicatorCell = [[NSLevelIndicatorCell alloc]
            initWithLevelIndicatorStyle:NSContinuousCapacityLevelIndicatorStyle];
        [gLevelIndicatorCell setMinValue:0];
        [gLevelIndicatorCell setMaxValue:1];
    }
    return gLevelIndicatorCell;
}

/* ----------------------------------------------- the button family, HITheme

   WebCore's ButtonMac, ToggleButtonMac and MenuListMac draw with NSButtonCell
   and NSPopUpButtonCell and let the cell pick its active or inactive artwork
   from its view's window. On Tiger that does not work: -[NSCell
   drawWithFrame:inView:] produces byte-identical pixels whether or not the
   window is key, so the window-inactive appearance is unreachable through the
   cell. HIThemeDrawButton takes the state as an argument, does have distinct
   active, inactive, pressed and disabled artwork, and is what AppKit draws
   through underneath, so the pixels are the same ones the cell would produce if
   it could be asked.

   The cell objects above are still configured, because the geometry comes from
   them; only the final blit is HITheme. */

static ThemeDrawState themeStateFor(unsigned states)
{
    if (states & TigerControlStatePressed)
        return kThemeStatePressed;
    if (!(states & TigerControlStateEnabled)) {
        return (states & TigerControlStateWindowActive)
            ? kThemeStateUnavailable : kThemeStateUnavailableInactive;
    }
    if (!(states & TigerControlStateWindowActive))
        return kThemeStateInactive;
    return kThemeStateActive;
}

static void drawThemeButtonKind(CGContextRef context, ThemeButtonKind kind,
                                CGRect rect, const TigerControlStyle *style)
{
    HIThemeButtonDrawInfo info;
    memset(&info, 0, sizeof(info));
    info.version = 0;
    info.state = themeStateFor(style->states);
    info.kind = kind;
    if (style->states & TigerControlStateIndeterminate)
        info.value = kThemeButtonMixed;
    else
        info.value = (style->states & TigerControlStateChecked) ? kThemeButtonOn : kThemeButtonOff;
    info.adornment = kThemeAdornmentNone;
    if (style->states & TigerControlStateFocused)
        info.adornment |= kThemeAdornmentFocus;
    if (style->states & TigerControlStateDefault)
        info.adornment |= kThemeAdornmentDefault;
    HIThemeDrawButton(&rect, &info, context, kHIThemeOrientationNormal, NULL);
}

/* ------------------------------------------------- size class and geometry */

/* ControlMac::controlSizeForFont. The thresholds are AppKit's system font sizes
 * per control size, asked for rather than hardcoded. */
int TigerControlSizeClassForStyle(const TigerControlStyle *style)
{
    float fontSize = style ? style->fontSize : 12;
    if (fontSize >= [NSFont systemFontSizeForControlSize:NSRegularControlSize])
        return 0;
    if (fontSize >= [NSFont systemFontSizeForControlSize:NSSmallControlSize])
        return 1;
    return 2;
}

typedef struct { int top, right, bottom, left; } Outsets;

/* The tables from the *Mac.mm files, in NSControlSize order: regular, small,
 * mini. WebCore's arrays carry a fourth large entry, which 10.4 has no artwork
 * for. */
static CGSize cellSizeFor(TigerControlKind kind, int sizeClass)
{
    static const CGSize buttonSizes[kSizeCount]   = { { 0, 20 }, { 0, 16 }, { 0, 13 } };
    static const CGSize checkboxSizes[kSizeCount] = { { 14, 14 }, { 12, 12 }, { 10, 10 } };
    static const CGSize radioSizes[kSizeCount]    = { { 16, 16 }, { 12, 12 }, { 10, 10 } };
    static const CGSize menuListSizes[kSizeCount] = { { 0, 21 }, { 0, 18 }, { 0, 15 } };

    switch (kind) {
    case TigerControlCheckbox:  return checkboxSizes[sizeClass];
    case TigerControlRadio:     return radioSizes[sizeClass];
    case TigerControlMenuList:
    case TigerControlMenuListButton: return menuListSizes[sizeClass];
    case TigerControlButton:
    case TigerControlDefaultButton:
    case TigerControlSquareButton: return buttonSizes[sizeClass];
    default: break;
    }
    return CGSizeMake(0, 0);
}

static Outsets cellOutsetsFor(TigerControlKind kind, int sizeClass)
{
    static const Outsets buttonOutsets[kSizeCount]   = { { 5, 7, 7, 7 }, { 4, 6, 7, 6 }, { 1, 2, 2, 2 } };
    static const Outsets checkboxOutsets[kSizeCount] = { { 2, 2, 2, 2 }, { 2, 1, 2, 1 }, { 0, 0, 1, 0 } };
    static const Outsets radioOutsets[kSizeCount]    = { { 1, 0, 1, 2 }, { 1, 1, 2, 1 }, { 0, 0, 1, 1 } };
    static const Outsets menuListOutsets[kSizeCount] = { { 0, 3, 1, 3 }, { 0, 3, 2, 3 }, { 0, 1, 0, 1 } };
    static const Outsets none = { 0, 0, 0, 0 };

    switch (kind) {
    case TigerControlCheckbox:  return checkboxOutsets[sizeClass];
    case TigerControlRadio:     return radioOutsets[sizeClass];
    case TigerControlMenuList:
    case TigerControlMenuListButton: return menuListOutsets[sizeClass];
    case TigerControlButton:
    case TigerControlDefaultButton:
    case TigerControlSquareButton: return buttonOutsets[sizeClass];
    default: break;
    }
    return none;
}

/* ControlMac::inflatedRect: centre the cell's natural size in the border box
 * along any axis the cell constrains, then grow by the outsets, which is where
 * the bezel's shadow lives. */
static CGRect inflatedRect(CGRect bounds, TigerControlKind kind, int sizeClass, float zoom)
{
    CGSize size = cellSizeFor(kind, sizeClass);
    Outsets outsets = cellOutsetsFor(kind, sizeClass);
    CGRect rect = bounds;

    if (size.width > 0) {
        float wanted = size.width * zoom;
        rect.origin.x += (rect.size.width - wanted) / 2;
        rect.size.width = wanted;
    }
    if (size.height > 0) {
        float wanted = size.height * zoom;
        rect.origin.y += (rect.size.height - wanted) / 2;
        rect.size.height = wanted;
    }

    rect.origin.x -= outsets.left * zoom;
    rect.origin.y -= outsets.top * zoom;
    rect.size.width += (outsets.left + outsets.right) * zoom;
    rect.size.height += (outsets.top + outsets.bottom) * zoom;
    return rect;
}

CGRect TigerControlDrawingBounds(TigerControlKind kind, const TigerControlStyle *style)
{
    if (!style)
        return CGRectZero;
    int sizeClass = TigerControlSizeClassForStyle(style);
    float zoom = (style->zoomFactor > 0) ? style->zoomFactor : 1;

    switch (kind) {
    case TigerControlButton:
    case TigerControlDefaultButton:
    case TigerControlSquareButton: {
        CGSize natural = cellSizeFor(TigerControlButton, 0);
        if (kind == TigerControlSquareButton || style->rect.size.height > natural.height * zoom)
            return style->rect;
        return inflatedRect(style->rect, TigerControlButton, sizeClass, zoom);
    }
    case TigerControlCheckbox:
    case TigerControlRadio:
    case TigerControlMenuList:
    case TigerControlMenuListButton:
        return inflatedRect(style->rect, kind, sizeClass, zoom);
    default:
        break;
    }
    return style->rect;
}

CGSize TigerControlPreferredSize(TigerControlKind kind, const TigerControlStyle *style)
{
    int sizeClass = TigerControlSizeClassForStyle(style);
    CGSize size = cellSizeFor(kind, sizeClass);
    float zoom = (style && style->zoomFactor > 0) ? style->zoomFactor : 1;
    return CGSizeMake(size.width * zoom, size.height * zoom);
}

/* ------------------------------------------------------------ cell states */

static NSControlSize appKitControlSize(int sizeClass)
{
    if (sizeClass == 1)
        return NSSmallControlSize;
    if (sizeClass == 2)
        return NSMiniControlSize;
    return NSRegularControlSize;
}

/* ControlMac::updateCheckedState / updateEnabledState / updateFocusedState /
 * updatePressedState, minus the private animated setters, which are 10.7. */
static void applyStates(NSCell *cell, const TigerControlStyle *style, int sizeClass)
{
    unsigned states = style->states;

    [cell setControlSize:appKitControlSize(sizeClass)];

    if (states & TigerControlStateIndeterminate)
        [cell setState:NSMixedState];
    else
        [cell setState:(states & TigerControlStateChecked) ? NSOnState : NSOffState];

    [cell setEnabled:(states & TigerControlStateEnabled) ? YES : NO];
    [cell setHighlighted:(states & TigerControlStatePressed) ? YES : NO];
    [cell setShowsFirstResponder:(states & TigerControlStateFocused) ? YES : NO];
}

/* ------------------------------------------------------- context plumbing */

static NSGraphicsContext *gSavedContext;

static void beginDrawing(CGContextRef context)
{
    ensureDrawingView();
    gSavedContext = [[NSGraphicsContext currentContext] retain];
    /* Tiger spells it graphicsContextWithGraphicsPort:; the port is the
     * CGContextRef. flipped:YES matches WebCore's coordinate system, which is
     * why the *Mac.mm files un-flip rather than flip. */
    NSGraphicsContext *gc = [NSGraphicsContext graphicsContextWithGraphicsPort:(void *)context
                                                                       flipped:YES];
    [NSGraphicsContext setCurrentContext:gc];
    CGContextSaveGState(context);
}

static void endDrawing(CGContextRef context)
{
    CGContextRestoreGState(context);
    [NSGraphicsContext setCurrentContext:gSavedContext];
    [gSavedContext release];
    gSavedContext = nil;
}

/* -------------------------------------------------------------- scrollbar */

static void drawScrollbar(CGContextRef context, TigerControlKind kind,
                          const TigerControlStyle *style)
{
    /* ScrollbarThemeMac drove HIThemeDrawTrack this way before the 10.7
     * NSScrollerImp path replaced it, so this is the pre-overlay drawing
     * restored rather than something new. */
    HIThemeTrackDrawInfo info;
    memset(&info, 0, sizeof(info));
    info.version = 0;
    info.kind = style->smallScrollbar ? kThemeScrollBarSmall : kThemeScrollBarMedium;
    info.bounds = style->rect;
    info.min = 0;
    info.max = 1000;
    info.value = (SInt32)(style->value * 1000);
    info.attributes = kThemeTrackShowThumb;
    if (kind == TigerControlScrollbarHorizontal)
        info.attributes |= kThemeTrackHorizontal;

    if (!(style->states & TigerControlStateWindowActive))
        info.enableState = kThemeTrackInactive;
    else if (!(style->states & TigerControlStateEnabled))
        info.enableState = kThemeTrackDisabled;
    else
        info.enableState = kThemeTrackActive;

    info.trackInfo.scrollbar.viewsize = 200;
    info.trackInfo.scrollbar.pressState =
        (style->states & TigerControlStatePressed) ? kThemeThumbPressed : 0;

    HIThemeDrawTrack(&info, NULL, context, kHIThemeOrientationNormal);
}

/* ------------------------------------------------------------ focus rings */

void TigerDrawFocusRing(CGContextRef context, CGRect rect, float cornerRadius)
{
    beginDrawing(context);
    NSRect r = NSMakeRect(rect.origin.x, rect.origin.y, rect.size.width, rect.size.height);
    NSSetFocusRingStyle(NSFocusRingOnly);
    if (cornerRadius > 0) {
        /* Tiger's NSBezierPath has no rounded-rect convenience, so the four
         * corners are appended by hand. */
        NSBezierPath *path = [NSBezierPath bezierPath];
        float radius = cornerRadius;
        if (radius > r.size.width / 2) radius = r.size.width / 2;
        if (radius > r.size.height / 2) radius = r.size.height / 2;
        [path appendBezierPathWithArcWithCenter:NSMakePoint(NSMinX(r) + radius, NSMinY(r) + radius)
                                         radius:radius startAngle:180 endAngle:270];
        [path appendBezierPathWithArcWithCenter:NSMakePoint(NSMaxX(r) - radius, NSMinY(r) + radius)
                                         radius:radius startAngle:270 endAngle:360];
        [path appendBezierPathWithArcWithCenter:NSMakePoint(NSMaxX(r) - radius, NSMaxY(r) - radius)
                                         radius:radius startAngle:0 endAngle:90];
        [path appendBezierPathWithArcWithCenter:NSMakePoint(NSMinX(r) + radius, NSMaxY(r) - radius)
                                         radius:radius startAngle:90 endAngle:180];
        [path closePath];
        [path fill];
    } else
        [[NSBezierPath bezierPathWithRect:r] fill];
    endDrawing(context);
}

/* ------------------------------------------------------------------ entry */

/* Render a control into a fresh premultiplied-BGRA buffer, for a caller that has no
 * AppKit of its own. The x86_64 web process paints its own pixels in fast mode and
 * its own tiles in faithful mode, so a bitmap -- not a replayed draw -- is the one
 * artefact that serves both; it asks the 32-bit UI process for this and blits it.
 *
 * style->rect should be at the origin: the cell paints outside the border box, so the
 * buffer covers TigerControlDrawingBounds and *outOriginX/Y report where its top-left
 * sits relative to style->rect.origin (usually negative). Row 0 is the top row and the
 * byte order is CAIRO_FORMAT_ARGB32's on a little-endian machine. free() the result.
 */
void *TigerRenderControlBitmap(TigerControlKind kind, const TigerControlStyle *style,
                               int *outWidth, int *outHeight,
                               int *outOriginX, int *outOriginY)
{
    CGRect bounds;
    int x, y, w, h;
    size_t stride, bytes;
    void *data;
    CGColorSpaceRef space;
    CGContextRef ctx;
    TigerControlStyle shifted;

    if (!style || kind >= TigerControlKindCount)
        return NULL;

    bounds = TigerControlDrawingBounds(kind, style);
    x = (int)floorf(bounds.origin.x);
    y = (int)floorf(bounds.origin.y);
    w = (int)ceilf(bounds.origin.x + bounds.size.width) - x;
    h = (int)ceilf(bounds.origin.y + bounds.size.height) - y;
    /* A control larger than this is not a control; refuse rather than allocate it. */
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096)
        return NULL;

    stride = (size_t)w * 4;
    bytes = stride * (size_t)h;
    /* CGBitmapContextGetData is NULL on 10.4 for a context CoreGraphics allocated
     * itself (spike/aquaatlas), so the buffer is ours. calloc: transparent backdrop,
     * so whatever the cell does not cover keeps showing the page. */
    data = calloc(1, bytes);
    if (!data)
        return NULL;

    space = CGColorSpaceCreateDeviceRGB();
    ctx = CGBitmapContextCreate(data, w, h, 8, stride, space,
                                kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
    CGColorSpaceRelease(space);
    if (!ctx) {
        free(data);
        return NULL;
    }

    /* No CTM flip. beginDrawing already wraps the port in an NSGraphicsContext with
     * flipped:YES, so AppKit draws y-down into an untransformed bitmap context and memory
     * row 0 ends up being the top row -- the setup spike/aquaatlas draws its atlas with,
     * and the one whose output was byte-identical to live 10.4 controls. Flipping here as
     * well would flip it twice. The control is moved instead of the context, so its drawing
     * bounds land exactly on the buffer. */
    shifted = *style;
    shifted.rect.origin.x -= x;
    shifted.rect.origin.y -= y;
    TigerDrawControl(ctx, kind, &shifted);
    CGContextRelease(ctx);

    if (outWidth) *outWidth = w;
    if (outHeight) *outHeight = h;
    if (outOriginX) *outOriginX = x;
    if (outOriginY) *outOriginY = y;
    return data;
}


void TigerControlStyleInit(TigerControlStyle *style, CGRect rect)
{
    if (!style)
        return;
    memset(style, 0, sizeof(*style));
    style->states = TigerControlStateEnabled | TigerControlStateWindowActive;
    style->fontSize = 12;
    style->zoomFactor = 1;
    style->rect = rect;
    style->value = 0;
    style->meterLow = 0.25;
    style->meterHigh = 0.75;
    style->meterOptimum = 1.0;
}

void TigerDrawControl(CGContextRef context, TigerControlKind kind, const TigerControlStyle *style)
{
    if (!context || !style || kind >= TigerControlKindCount)
        return;

    int sizeClass = TigerControlSizeClassForStyle(style);
    float zoom = (style->zoomFactor > 0) ? style->zoomFactor : 1;
    CGRect rect = style->rect;

    if (kind == TigerControlScrollbarVertical || kind == TigerControlScrollbarHorizontal) {
        drawScrollbar(context, kind, style);
        return;
    }

    if (kind == TigerControlFocusRing) {
        TigerDrawFocusRing(context, rect, 0);
        return;
    }

    /* Measured against live NSControls in a real window on 10.4: an NSButton
     * and an NSButtonCell check box draw identical pixels whether or not their
     * window is key, but an NSPopUpButtonCell does not. So the window's key
     * state is the only way to reach the popup's inactive artwork, and it costs
     * nothing for the controls that ignore it. An earlier version drew the
     * whole button family through HIThemeDrawButton to get an inactive
     * appearance instead; that produced artwork which does not match a live
     * control, which is a worse failure than not varying. */
    ensureDrawingView();
    if (style->states & TigerControlStateWindowActive)
        [gWindow makeKeyAndOrderFront:nil];
    else
        [gWindow orderOut:nil];

    beginDrawing(context);

    NSRect frame;
    NSCell *cell = nil;

    switch (kind) {
    case TigerControlButton:
    case TigerControlDefaultButton:
    case TigerControlSquareButton: {
        NSButtonCell *button = buttonCell(kind == TigerControlDefaultButton);
        /* ButtonMac::bezelStyle: a button taller than the rounded bezel's
         * natural height for its size class gets the square bezel, because the
         * rounded one cannot stretch vertically. */
        CGSize natural = cellSizeFor(TigerControlButton, 0);
        if (kind == TigerControlSquareButton || rect.size.height > natural.height * zoom)
            [button setBezelStyle:NSShadowlessSquareBezelStyle];
        else
            [button setBezelStyle:NSRoundedBezelStyle];
        applyStates(button, style, sizeClass);
        cell = button;
        frame = ([button bezelStyle] == NSRoundedBezelStyle)
            ? toNSRect(inflatedRect(rect, TigerControlButton, sizeClass, zoom))
            : toNSRect(rect);
        break;
    }

    case TigerControlCheckbox:
    case TigerControlRadio: {
        NSButtonCell *toggle = toggleCell(kind == TigerControlRadio);
        applyStates(toggle, style, sizeClass);
        cell = toggle;
        frame = toNSRect(inflatedRect(rect, kind, sizeClass, zoom));
        break;
    }

    case TigerControlMenuList:
    case TigerControlMenuListButton: {
        NSPopUpButtonCell *popUp = popUpCell();
        applyStates(popUp, style, sizeClass);
        cell = popUp;
        frame = toNSRect(inflatedRect(rect, kind, sizeClass, zoom));
        break;
    }

    case TigerControlTextField:
    case TigerControlTextArea: {
        NSTextFieldCell *field = textFieldCell();
        applyStates(field, style, sizeClass);
        /* TextFieldMac disables the cell for a read-only field too, so the
         * bezel matches what the user can do with it. */
        if (style->states & TigerControlStateReadOnly)
            [field setEnabled:NO];
        cell = field;
        frame = toNSRect(rect);
        break;
    }

    case TigerControlSearchField: {
        NSTextFieldCell *field = searchFieldCell();
        applyStates(field, style, sizeClass);
        cell = field;
        frame = toNSRect(rect);
        break;
    }

    case TigerControlSliderThumbHorizontal:
    case TigerControlSliderThumbVertical: {
        NSSliderCell *slider = sliderCell();
        applyStates(slider, style, sizeClass);
        /* SliderThumbMac never draws a focus ring for the thumb. */
        [slider setShowsFirstResponder:NO];
        [slider setDoubleValue:style->value];
        endDrawing(context);
        beginDrawing(context);
        [slider drawKnob:toNSRect(rect)];
        endDrawing(context);
        return;
    }

    case TigerControlSliderTrackHorizontal:
    case TigerControlSliderTrackVertical: {
        NSSliderCell *slider = sliderCell();
        applyStates(slider, style, sizeClass);
        [slider setDoubleValue:style->value];
        [slider drawBarInside:toNSRect(rect) flipped:YES];
        endDrawing(context);
        return;
    }

    case TigerControlProgressBar: {
        if (!gProgressIndicator) {
            gProgressIndicator = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(0, 0, 10, 10)];
            [gProgressIndicator setStyle:NSProgressIndicatorBarStyle];
            [gProgressIndicator setMinValue:0];
            [gProgressIndicator setMaxValue:1];
            [gView addSubview:gProgressIndicator];
        }
        [gProgressIndicator setFrame:toNSRect(rect)];
        [gProgressIndicator setControlSize:appKitControlSize(sizeClass)];
        [gProgressIndicator setIndeterminate:(style->states & TigerControlStateIndeterminate) ? YES : NO];
        if (!(style->states & TigerControlStateIndeterminate))
            [gProgressIndicator setDoubleValue:style->value];
        /* ProgressBarMac advances the barber pole by animationProgress; Tiger's
         * NSProgressIndicator owns its own animation timer and exposes no phase,
         * so an indeterminate bar draws one fixed frame here. */
        [gProgressIndicator drawRect:[gProgressIndicator bounds]];
        endDrawing(context);
        return;
    }

    case TigerControlMeter: {
        NSLevelIndicatorCell *level = levelIndicatorCell();
        applyStates(level, style, sizeClass);
        [level setDoubleValue:style->value];
        [level setWarningValue:style->meterLow];
        [level setCriticalValue:style->meterHigh];
        cell = level;
        frame = toNSRect(rect);
        break;
    }

    case TigerControlInnerSpinButton: {
        /* NSStepperCell exists on Tiger but WebCore's InnerSpinButtonMac draws
         * through HIThemeDrawButton with the little arrows, which is the same
         * artwork and takes the pressed sub-state directly. */
        HIThemeButtonDrawInfo info;
        memset(&info, 0, sizeof(info));
        info.version = 0;
        info.kind = kThemeIncDecButton;
        info.value = kThemeButtonOff;
        info.adornment = kThemeAdornmentNone;
        if (!(style->states & TigerControlStateEnabled))
            info.state = kThemeStateUnavailable;
        else if (style->states & TigerControlStatePressed)
            info.state = (style->states & TigerControlStateSpinUp)
                ? kThemeStatePressedUp : kThemeStatePressedDown;
        else if (!(style->states & TigerControlStateWindowActive))
            info.state = kThemeStateInactive;
        else
            info.state = kThemeStateActive;
        endDrawing(context);
        HIThemeDrawButton(&rect, &info, context, kHIThemeOrientationNormal, NULL);
        return;
    }

    default:
        endDrawing(context);
        return;
    }

    if (cell)
        [cell drawWithFrame:frame inView:gView];
    endDrawing(context);
}

/* ------------------------------------------------- colours and font sizes */

static void appendColor(NSMutableString *json, const char *name, NSColor *color, BOOL last)
{
    NSColor *rgb = [color colorUsingColorSpaceName:NSDeviceRGBColorSpace];
    float r = 0, g = 0, b = 0, a = 1;
    if (rgb)
        [rgb getRed:&r green:&g blue:&b alpha:&a];
    else {
        /* Pattern colours such as +controlColor have no components; sample one
         * pixel instead of reporting nothing. */
        unsigned char px[4] = { 255, 255, 255, 255 };
        CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
        CGContextRef ctx = CGBitmapContextCreate(px, 1, 1, 8, 4, space,
                                                 kCGImageAlphaPremultipliedFirst);
        CGColorSpaceRelease(space);
        if (ctx) {
            NSGraphicsContext *saved = [NSGraphicsContext currentContext];
            [NSGraphicsContext setCurrentContext:
                [NSGraphicsContext graphicsContextWithGraphicsPort:(void *)ctx flipped:NO]];
            [color set];
            NSRectFill(NSMakeRect(0, 0, 1, 1));
            [NSGraphicsContext setCurrentContext:saved];
            CGContextRelease(ctx);
            a = px[0] / 255.0f; r = px[1] / 255.0f; g = px[2] / 255.0f; b = px[3] / 255.0f;
        }
    }
    [json appendFormat:@"    \"%s\": [%.4f, %.4f, %.4f, %.4f]%s\n",
                       name, r, g, b, a, last ? "" : ","];
}

int TigerWriteControlMetricsJSON(const char *path)
{
    if (!path)
        return 0;
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    NSMutableString *json = [NSMutableString string];

    [json appendString:@"{\n  \"colors\": {\n"];
    appendColor(json, "selectedTextBackground", [NSColor selectedTextBackgroundColor], NO);
    appendColor(json, "selectedText", [NSColor selectedTextColor], NO);
    appendColor(json, "secondarySelectedControl", [NSColor secondarySelectedControlColor], NO);
    appendColor(json, "alternateSelectedControl", [NSColor alternateSelectedControlColor], NO);
    appendColor(json, "alternateSelectedControlText", [NSColor alternateSelectedControlTextColor], NO);
    appendColor(json, "keyboardFocusIndicator", [NSColor keyboardFocusIndicatorColor], NO);
    appendColor(json, "control", [NSColor controlColor], NO);
    appendColor(json, "controlText", [NSColor controlTextColor], NO);
    appendColor(json, "controlBackground", [NSColor controlBackgroundColor], NO);
    appendColor(json, "disabledControlText", [NSColor disabledControlTextColor], NO);
    appendColor(json, "text", [NSColor textColor], NO);
    appendColor(json, "textBackground", [NSColor textBackgroundColor], NO);
    appendColor(json, "windowBackground", [NSColor windowBackgroundColor], NO);
    appendColor(json, "grid", [NSColor gridColor], NO);
    appendColor(json, "headerText", [NSColor headerTextColor], YES);
    [json appendString:@"  },\n"];

    NSArray *rows = [NSColor controlAlternatingRowBackgroundColors];
    [json appendString:@"  \"alternatingRowColors\": ["];
    for (unsigned i = 0; i < [rows count]; ++i) {
        NSColor *rgb = [[rows objectAtIndex:i] colorUsingColorSpaceName:NSDeviceRGBColorSpace];
        float r = 1, g = 1, b = 1, a = 1;
        if (rgb)
            [rgb getRed:&r green:&g blue:&b alpha:&a];
        [json appendFormat:@"[%.4f, %.4f, %.4f, %.4f]%s", r, g, b, a,
                           (i + 1 == [rows count]) ? "" : ", "];
    }
    [json appendString:@"],\n"];

    /* Font metrics. RenderThemeMac asks for the system font per control size
     * and lays the control out around it. */
    [json appendString:@"  \"systemFont\": {\n"];
    const NSControlSize sizes[kSizeCount] = { NSRegularControlSize, NSSmallControlSize, NSMiniControlSize };
    const char *names[kSizeCount] = { "regular", "small", "mini" };
    for (unsigned i = 0; i < kSizeCount; ++i) {
        float pointSize = [NSFont systemFontSizeForControlSize:sizes[i]];
        NSFont *font = [NSFont systemFontOfSize:pointSize];
        [json appendFormat:@"    \"%s\": {\"family\": \"%@\", \"size\": %.2f, "
                           @"\"ascender\": %.2f, \"descender\": %.2f, \"lineHeight\": %.2f}%s\n",
                           names[i], [font familyName], pointSize,
                           [font ascender], [font descender],
                           [font ascender] - [font descender] + [font leading],
                           (i + 1 == kSizeCount) ? "" : ","];
    }
    [json appendString:@"  },\n"];

    [json appendFormat:@"  \"smallSystemFontSize\": %.2f,\n", [NSFont smallSystemFontSize]];
    [json appendFormat:@"  \"systemFontSize\": %.2f,\n", [NSFont systemFontSize]];
    [json appendFormat:@"  \"labelFontSize\": %.2f\n", [NSFont labelFontSize]];
    [json appendString:@"}\n"];

    BOOL ok = [json writeToFile:[NSString stringWithUTF8String:path] atomically:YES];
    [pool release];
    return ok ? 1 : 0;
}
