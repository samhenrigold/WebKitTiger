/* TIGER: AppKit shims. Declared in <TigerCompat/AppKitCompat.h>.
 * MRR, fragile runtime. See logs/webcore-plan.md section 4.
 *
 * Nothing here pretends a capability Tiger does not have. Each accessor
 * answers the value a 2005 machine genuinely produces: one backing pixel per
 * point, line-based scrolling with no phase, no force touch, never occluded.
 */

#import <AppKit/AppKit.h>
#import <TigerCompat/AppKitCompat.h>

/* =========================================================================
 * NSEvent
 * ========================================================================= */

/* Modern AppKit raises NSInternalInconsistencyException from several of these
 * when the event type does not carry the value; -phase on a mouse event throws
 * there and answers NSEventPhaseNone here. Returning the quiet value is the
 * safe direction, and WebCore only asks on scroll events. */
@implementation NSEvent (TigerCompat)

- (NSEventPhase)phase                   { return NSEventPhaseNone; }
- (NSEventPhase)momentumPhase           { return NSEventPhaseNone; }

- (BOOL)hasPreciseScrollingDeltas       { return NO; }

/* With hasPreciseScrollingDeltas NO, WebCore reads these as line counts and
 * scales them by the line height itself, which is exactly what -deltaX and
 * -deltaY mean on Tiger. */
- (CGFloat)scrollingDeltaX              { return [self deltaX]; }
- (CGFloat)scrollingDeltaY              { return [self deltaY]; }

- (BOOL)isDirectionInvertedFromDevice   { return NO; }

- (NSInteger)stage                      { return 0; }

- (CGEventRef)CGEvent                   { return NULL; }

/* Bit 0 is the left button, bit 1 the right, bit 2 the middle, matching what
 * currentMouseButton() in PlatformEventFactoryMac.mm tests for. */
+ (NSUInteger)pressedMouseButtons
{
    NSUInteger buttons = 0;
    if (CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, kCGMouseButtonLeft))
        buttons |= 1 << 0;
    if (CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, kCGMouseButtonRight))
        buttons |= 1 << 1;
    if (CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, kCGMouseButtonCenter))
        buttons |= 1 << 2;
    return buttons;
}

@end

/* =========================================================================
 * Backing store
 * ========================================================================= */

@implementation NSView (TigerCompat)

- (CGFloat)backingScaleFactor { return 1.0f; }

- (NSRect)convertRectToBacking:(NSRect)rect { return rect; }
- (NSRect)convertRectFromBacking:(NSRect)rect { return rect; }
- (NSPoint)convertPointToBacking:(NSPoint)point { return point; }
- (NSPoint)convertPointFromBacking:(NSPoint)point { return point; }

@end

@implementation NSWindow (TigerCompat)

/* Tiger has -userSpaceScaleFactor, the Quartz resolution-independence knob that
 * shipped in 10.4 and is 1.0 on every real machine. It is the closest thing to
 * a backing scale, and unlike a hardcoded 1.0 it tracks the setting if somebody
 * has changed it. */
- (CGFloat)backingScaleFactor { return [self userSpaceScaleFactor]; }

- (NSWindowOcclusionState)occlusionState { return NSWindowOcclusionStateVisible; }

/* Both corners are converted rather than translating the origin and carrying
 * the size across. Window-to-screen is a pure translation while
 * -userSpaceScaleFactor is 1.0, which it is on every real Tiger machine, but if
 * it is not then the size scales too, and converting corners is right either
 * way for the same three lines. */
- (NSRect)convertRectToScreen:(NSRect)rect
{
    NSPoint lower = [self convertBaseToScreen:NSMakePoint(NSMinX(rect), NSMinY(rect))];
    NSPoint upper = [self convertBaseToScreen:NSMakePoint(NSMaxX(rect), NSMaxY(rect))];
    return NSMakeRect(lower.x, lower.y, upper.x - lower.x, upper.y - lower.y);
}

- (NSRect)convertRectFromScreen:(NSRect)rect
{
    NSPoint lower = [self convertScreenToBase:NSMakePoint(NSMinX(rect), NSMinY(rect))];
    NSPoint upper = [self convertScreenToBase:NSMakePoint(NSMaxX(rect), NSMaxY(rect))];
    return NSMakeRect(lower.x, lower.y, upper.x - lower.x, upper.y - lower.y);
}

@end

@implementation NSScreen (TigerCompat)

/* Tiger predates Retina, and -userSpaceScaleFactor is the Quartz
 * resolution-independence knob, 1.0 on every real machine. Same answer as the
 * NSView and NSWindow shims above, by the same route. */
- (CGFloat)backingScaleFactor { return [self userSpaceScaleFactor]; }

- (NSEdgeInsets)safeAreaInsets
{
    NSEdgeInsets zero = { 0.0f, 0.0f, 0.0f, 0.0f };
    return zero;
}

@end

/* =========================================================================
 * Layout direction
 * ========================================================================= */

@implementation NSMenu (TigerCompat)
- (NSUserInterfaceLayoutDirection)userInterfaceLayoutDirection
{
    return NSUserInterfaceLayoutDirectionLeftToRight;
}

+ (NSMenuType)menuTypeForEvent:(NSEvent *)event
{
    if (!event)
        return NSMenuTypeNone;
    switch ([event type]) {
    case NSRightMouseDown:
    case NSRightMouseUp:
        return NSMenuTypeContextMenu;
    case NSLeftMouseDown:
    case NSLeftMouseUp:
        /* Control-click is the one-button-mouse context menu, which is how
         * every Tiger application raises one. */
        return ([event modifierFlags] & NSControlKeyMask) ? NSMenuTypeContextMenu : NSMenuTypeNone;
    default:
        return NSMenuTypeNone;
    }
}
@end

/* =========================================================================
 * NSCursor and NSSpellChecker
 * ========================================================================= */

@implementation NSCursor (TigerCompat)

/* Tiger draws no distinct contextual-menu or drag-copy pointer. The arrow is
 * what the window server shows for both situations on this system, so this is
 * the honest answer rather than a placeholder. */
+ (NSCursor *)contextualMenuCursor { return [NSCursor arrowCursor]; }
+ (NSCursor *)dragCopyCursor { return [NSCursor arrowCursor]; }

@end

@implementation NSSpellChecker (TigerCompat)

/* 10.6, and it refreshes whichever of the spelling, grammar and substitutions
 * panels are open. Tiger has only the spelling panel and refreshes it from
 * -updateSpellingPanelWithMisspelledWord:, which WebKit calls separately.
 *
 * ponytail: a no-op, so an open spelling panel will not refresh from this call
 * alone. Upgrade path if that is ever visible: forward to
 * -updateSpellingPanelWithMisspelledWord: with the current word. */
- (void)updatePanels { }

@end

/* =========================================================================
 * NSGraphicsContext
 * ========================================================================= */

@implementation NSGraphicsContext (TigerCompat)

+ (NSGraphicsContext *)graphicsContextWithCGContext:(CGContextRef)context flipped:(BOOL)flipped
{
    return [self graphicsContextWithGraphicsPort:(void *)context flipped:flipped];
}

@end

/* =========================================================================
 * NSWorkspace accessibility display settings
 * ========================================================================= */

@implementation NSWorkspace (TigerCompat)
- (BOOL)accessibilityDisplayShouldIncreaseContrast { return NO; }
- (BOOL)accessibilityDisplayShouldDifferentiateWithoutColor { return NO; }
- (BOOL)accessibilityDisplayShouldInvertColors { return NO; }
- (BOOL)accessibilityDisplayShouldReduceMotion { return NO; }
@end

@implementation NSView (TigerCompatLayoutDirection)
- (NSUserInterfaceLayoutDirection)userInterfaceLayoutDirection
{
    return NSUserInterfaceLayoutDirectionLeftToRight;
}
@end

/* =========================================================================
 * NSColor
 * ========================================================================= */

@implementation NSColor (TigerCompat)

+ (NSColor *)unemphasizedSelectedContentBackgroundColor
{
    return [NSColor secondarySelectedControlColor];
}

+ (NSColor *)selectedContentBackgroundColor
{
    return [NSColor alternateSelectedControlColor];
}

+ (NSColor *)unemphasizedSelectedTextColor
{
    return [NSColor selectedTextColor];
}

+ (NSColor *)unemphasizedSelectedTextBackgroundColor
{
    return [NSColor selectedTextBackgroundColor];
}

+ (NSArray *)alternatingContentBackgroundColors
{
    return [NSColor controlAlternatingRowBackgroundColors];
}

/* The 10.10 label hierarchy is defined as black at decreasing alpha. Tiger's
 * +labelColor is the primary one; these are the documented secondary, tertiary
 * and quaternary alphas. */
+ (NSColor *)secondaryLabelColor
{
    return [NSColor colorWithCalibratedWhite:0.0f alpha:0.5f];
}

+ (NSColor *)tertiaryLabelColor
{
    return [NSColor colorWithCalibratedWhite:0.0f alpha:0.26f];
}

+ (NSColor *)quaternaryLabelColor
{
    return [NSColor colorWithCalibratedWhite:0.0f alpha:0.1f];
}

+ (NSColor *)findHighlightColor
{
    return [NSColor colorWithCalibratedRed:1.0f green:1.0f blue:0.0f alpha:1.0f];
}

+ (NSColor *)colorWithSRGBRed:(CGFloat)red green:(CGFloat)green blue:(CGFloat)blue alpha:(CGFloat)alpha
{
    /* Deliberately not a colour-space conversion: the components go through
     * unchanged, so 0.25 stays 0.25 where a modern system converts sRGB to
     * calibrated RGB and yields 0.198. Tiger has no sRGB space to convert
     * through. Every WebKit caller passes black, white or clear, where the two
     * agree at any gamma, so nothing in the tree can tell the difference. */
    return [NSColor colorWithCalibratedRed:red green:green blue:blue alpha:alpha];
}

+ (NSColor *)colorWithCGColor:(CGColorRef)cgColor
{
    if (!cgColor)
        return nil;

    size_t count = CGColorGetNumberOfComponents(cgColor);
    const CGFloat *components = CGColorGetComponents(cgColor);
    if (!components)
        return nil;

    /* Tiger's CGColor is float-based, so components are CGFloat already. Only
     * the two layouts WebCore produces are handled: grayscale+alpha and
     * RGB+alpha. */
    if (count == 2)
        return [NSColor colorWithCalibratedWhite:components[0] alpha:components[1]];
    if (count >= 4) {
        return [NSColor colorWithCalibratedRed:components[0]
                                         green:components[1]
                                          blue:components[2]
                                         alpha:components[3]];
    }
    return nil;
}

- (CGColorRef)CGColor
{
    NSColor *rgb = [self colorUsingColorSpaceName:NSDeviceRGBColorSpace];
    if (!rgb)
        return NULL;

    CGFloat components[4];
    [rgb getRed:&components[0] green:&components[1] blue:&components[2] alpha:&components[3]];

    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGColorRef result = CGColorCreate(space, components);
    CGColorSpaceRelease(space);

    /* AppKit hands back a colour tied to the NSColor's lifetime; an autoreleased
     * one is the closest equivalent that cannot leak. */
    return (CGColorRef)[(id)result autorelease];
}

@end
