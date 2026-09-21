/* TigerCompat/AppKitCompat.h -- AppKit API that Mac OS X 10.4 lacks, where the
 * gap needs an implementation rather than a header rename.
 *
 * The pure renames (NSEventType*, NSEventMask*, NSEventModifierFlag*,
 * NSWindowStyleMask*) are macros in compat/sdk-overlay and are not here.
 *
 * Implemented in compat/nscompat-appkit.m. Include after <AppKit/AppKit.h>.
 * Safe from ObjC++ (C++23) and from both MRR and ARC translation units.
 *
 * Scope comes from logs/webcore-plan.md section 4. Everything here answers with
 * the value a 2005 machine would have produced anyway: one backing pixel per
 * point, no phased or precise scrolling, no force touch, no occlusion, and the
 * Aqua colours the old theme used.
 */

#ifndef TIGERCOMPAT_APPKITCOMPAT_H
#define TIGERCOMPAT_APPKITCOMPAT_H

#import <AppKit/AppKit.h>
#import <TigerCompat/NSCompat.h>

/* -------------------------------------------------------------------------
 * NSEvent, the 10.7 to 10.10 additions.
 *
 * Declared as properties, not plain methods: WebCore reaches most of these
 * with dot syntax, and dot syntax on an `id` only resolves through a declared
 * property.
 * ------------------------------------------------------------------------- */
@interface NSEvent (TigerCompat)

/* 10.7. Tiger's scroll events carry no phase, so both answer NSEventPhaseNone
 * and WebCore's phased-scrolling paths stay inert. */
@property (readonly) NSEventPhase phase;
@property (readonly) NSEventPhase momentumPhase;

/* 10.7. NO sends WebCore down the line-based path, where it multiplies
 * -deltaX/-deltaY by the line height itself. That is what a Tiger scroll wheel
 * actually reports, so the scrolling is correct rather than merely compiling. */
@property (readonly) BOOL hasPreciseScrollingDeltas;
@property (readonly) CGFloat scrollingDeltaX;
@property (readonly) CGFloat scrollingDeltaY;

/* 10.7, "natural" scrolling. It arrived in 10.7 as well, so NO is right. */
@property (readonly, getter=isDirectionInvertedFromDevice) BOOL directionInvertedFromDevice;

/* 10.10, force touch. No Tiger trackpad reports pressure stages. */
@property (readonly) NSInteger stage;

/* 10.5. Tiger's AppKit does not keep a CGEventRef on an NSEvent, and the only
 * caller wants the unaccelerated pointer movement that Quartz did not expose
 * until 10.15. NULL, so that pointer-lock movement reads as zero rather than
 * reading garbage. */
@property (readonly) CGEventRef CGEvent;

@end

/* -------------------------------------------------------------------------
 * Backing store. Tiger has no Retina, so a point is a pixel.
 * ------------------------------------------------------------------------- */
@interface NSView (TigerCompat)
/* 10.7 */
@property (readonly) CGFloat backingScaleFactor;
- (NSRect)convertRectToBacking:(NSRect)rect;
- (NSRect)convertRectFromBacking:(NSRect)rect;
- (NSPoint)convertPointToBacking:(NSPoint)point;
- (NSPoint)convertPointFromBacking:(NSPoint)point;
@end

/* 10.9. Tiger's window server never tells an application it is covered. */
typedef NSUInteger NSWindowOcclusionState;
enum { NSWindowOcclusionStateVisible = 1UL << 1 };

@interface NSWindow (TigerCompat)
/* 10.7 */
@property (readonly) CGFloat backingScaleFactor;
/* 10.9 */
@property (readonly) NSWindowOcclusionState occlusionState;
@end

/* 10.7. NSEdgeInsets and the NSScreen inset accessor that uses it. */
#if !defined(TIGER_NSEDGEINSETS_DEFINED)
#define TIGER_NSEDGEINSETS_DEFINED 1
typedef struct NSEdgeInsets {
    CGFloat top;
    CGFloat left;
    CGFloat bottom;
    CGFloat right;
} NSEdgeInsets;
#endif

@interface NSScreen (TigerCompat)
/* 10.7. WebView -_backingScaleFactor falls back to the main screen, and
 * PlatformScreenMac reads it for every screen. */
@property (readonly) CGFloat backingScaleFactor;
/* 12.0. Nothing intrudes on a Tiger screen: no notch, no rounded corners, no
 * home indicator. All four insets are zero, which makes safeScreenFrame()
 * return the plain frame. */
@property (readonly) NSEdgeInsets safeAreaInsets;
@end

/* -------------------------------------------------------------------------
 * NSColor. Twelve semantic colours that were renamed or added after 10.4,
 * mapped onto the Aqua colours the old theme used. See plan section 4.5:
 * Tiger's NSColor.h already has everything else, and its component getters
 * take float, which is CGFloat on i386.
 * ------------------------------------------------------------------------- */
@interface NSColor (TigerCompat)

/* 10.14 renames of colours Tiger has under their old names. */
+ (NSColor *)unemphasizedSelectedContentBackgroundColor;
+ (NSColor *)selectedContentBackgroundColor;
+ (NSColor *)unemphasizedSelectedTextColor;
+ (NSColor *)unemphasizedSelectedTextBackgroundColor;
+ (NSArray *)alternatingContentBackgroundColors;

/* 10.10 label hierarchy. Tiger has +labelColor; the three weaker ones are
 * calibrated whites, which is what they are on a light Aqua background. */
+ (NSColor *)secondaryLabelColor;
+ (NSColor *)tertiaryLabelColor;
+ (NSColor *)quaternaryLabelColor;

/* 10.13. The find-on-page highlight, a calibrated yellow. */
+ (NSColor *)findHighlightColor;

/* 10.7. This does NOT convert between colour spaces: the components are passed
 * through into Tiger's calibrated RGB unchanged, so 0.25 stays 0.25 where a
 * modern system would yield 0.198. Tiger has no sRGB colour space to convert
 * through, and every WebKit caller passes black, white or clear, where the two
 * spaces agree at any gamma. Do not read this as the spaces being equivalent.
 * WebCore's real colour fidelity goes through CoreGraphics, not NSColor. */
+ (NSColor *)colorWithSRGBRed:(CGFloat)red green:(CGFloat)green blue:(CGFloat)blue alpha:(CGFloat)alpha;

/* 10.8, and 10.8 the other way. These two carry the Color-to-NSColor cache in
 * ColorMac.mm, so they convert for real rather than approximating. */
+ (NSColor *)colorWithCGColor:(CGColorRef)cgColor;
@property (readonly) CGColorRef CGColor;

@end

#endif /* TIGERCOMPAT_APPKITCOMPAT_H */
