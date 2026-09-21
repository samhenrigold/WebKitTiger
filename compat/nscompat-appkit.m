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
    /* Not a colour-space conversion. Every WebKit caller passes black, white or
     * clear, where sRGB and Tiger's calibrated RGB agree exactly. */
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
