/*
 * CGCompat.h -- CoreGraphics / ImageIO API that Mac OS X 10.4.11 lacks but
 * WebCore's CG backend calls. Implemented in compat/cgcompat.c on top of the
 * primitives Tiger's CoreGraphics does export (CGShading, CGFunction,
 * CGColorSpaceCreateICCBased, CGPathApply, ...).
 *
 * Prototypes match Apple's exactly so WebCore compiles unchanged. Classification
 * of every gap lives in compat/CG-SURVEY.md.
 */
#ifndef TIGERCOMPAT_CGCOMPAT_H
#define TIGERCOMPAT_CGCOMPAT_H

/* These includes are deliberately narrow, and this list is a constraint, not a
   convenience. The SDK overlay appends this header to <CoreGraphics/CGColor.h>
   and the other sub-headers WebCore names, so anything included here is
   inflicted on every translation unit that includes any CoreGraphics header.

   Not <CoreGraphics/CoreGraphics.h>: the umbrella drags in CGRemoteOperation,
   CGSession, CGPSConverter and CGEvent, and through them the whole
   <CoreServices/CoreServices.h> tree. That is how CarbonCore's AssertMacros
   `check` and Finder's `Marker` ended up colliding with JavaScriptCore.

   Not <ApplicationServices/ApplicationServices.h> either, which adds QuickDraw
   on top of that.

   Not <ImageIO/CGImageSource.h>, which includes the CoreGraphics umbrella
   itself; CGImageSourceRef is forward-declared below instead. */
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CGBase.h>
#include <CoreGraphics/CGGeometry.h>
#include <CoreGraphics/CGAffineTransform.h>
#include <CoreGraphics/CGColorSpace.h>
#include <CoreGraphics/CGColor.h>
#include <CoreGraphics/CGContext.h>
#include <CoreGraphics/CGPath.h>
#include <CoreGraphics/CGImage.h>
#include <CoreGraphics/CGFont.h>
#include <CoreGraphics/CGDataProvider.h>

/* CGFloat's owner is the SDK overlay's CGBase.h, which every CoreGraphics header
   includes. This guarded copy is the fallback for a consumer reached without the
   overlay on -F, such as a hand-run spike. Apple's guard macro means whichever
   comes first wins and the two can never collide. */
#ifndef CGFLOAT_DEFINED
#define CGFLOAT_DEFINED 1
typedef float CGFloat;
#define CGFLOAT_MIN FLT_MIN
#define CGFLOAT_MAX FLT_MAX
#define CGFLOAT_IS_DOUBLE 0
#define CGFLOAT_EPSILON FLT_EPSILON
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- colorspace */

/* 10.5+ colorspace names. Tiger exports only Generic{Gray,RGB,CMYK}. */
extern const CFStringRef kCGColorSpaceSRGB;
extern const CFStringRef kCGColorSpaceLinearSRGB;
extern const CFStringRef kCGColorSpaceExtendedSRGB;
extern const CFStringRef kCGColorSpaceExtendedLinearSRGB;
extern const CFStringRef kCGColorSpaceGenericRGBLinear;
extern const CFStringRef kCGColorSpaceGenericGrayGamma2_2;
extern const CFStringRef kCGColorSpaceGenericXYZ;
extern const CFStringRef kCGColorSpaceDisplayP3;
extern const CFStringRef kCGColorSpaceLinearDisplayP3;
extern const CFStringRef kCGColorSpaceExtendedDisplayP3;
extern const CFStringRef kCGColorSpaceExtendedLinearDisplayP3;
extern const CFStringRef kCGColorSpaceAdobeRGB1998;
extern const CFStringRef kCGColorSpaceROMMRGB;
extern const CFStringRef kCGColorSpaceITUR_709;
extern const CFStringRef kCGColorSpaceITUR_2020;
extern const CFStringRef kCGColorSpaceExtendedITUR_2020;

typedef enum {
    kCGColorSpaceModelUnknown = -1,
    kCGColorSpaceModelMonochrome,
    kCGColorSpaceModelRGB,
    kCGColorSpaceModelCMYK,
    kCGColorSpaceModelLab,
    kCGColorSpaceModelDeviceN,
    kCGColorSpaceModelIndexed,
    kCGColorSpaceModelPattern,
    kCGColorSpaceModelXYZ
} CGColorSpaceModel;

/* Tiger exports CGColorSpaceCreateWithName but only understands the three
   Generic names, so this shadows it by macro rather than by duplicate symbol. */
CGColorSpaceRef TigerCGColorSpaceCreateWithName(CFStringRef name);
#define CGColorSpaceCreateWithName TigerCGColorSpaceCreateWithName

CGColorSpaceModel CGColorSpaceGetModel(CGColorSpaceRef);
CFStringRef CGColorSpaceGetName(CGColorSpaceRef);
CGColorSpaceRef CGColorSpaceGetBaseColorSpace(CGColorSpaceRef);
bool CGColorSpaceUsesExtendedRange(CGColorSpaceRef);
bool CGColorSpaceIsWideGamutRGB(CGColorSpaceRef);
bool CGColorSpaceSupportsOutput(CGColorSpaceRef);
CGColorSpaceRef CGColorSpaceCreateExtended(CGColorSpaceRef);
CFPropertyListRef CGColorSpaceCopyPropertyList(CGColorSpaceRef);
CGColorSpaceRef CGColorSpaceCreateWithPropertyList(CFPropertyListRef);

/* -------------------------------------------------------------------- color */

CGColorRef CGColorCreateSRGB(CGFloat r, CGFloat g, CGFloat b, CGFloat a);
CGColorRef CGColorCreateGenericGray(CGFloat gray, CGFloat alpha);
extern const CFStringRef kCGColorWhite;
extern const CFStringRef kCGColorBlack;
extern const CFStringRef kCGColorClear;
CGColorRef CGColorGetConstantColor(CFStringRef colorName);

/* ----------------------------------------------------------------- gradient */

typedef struct CGGradient *CGGradientRef;

enum {
    kCGGradientDrawsBeforeStartLocation = (1 << 0),
    kCGGradientDrawsAfterEndLocation = (1 << 1)
};
typedef uint32_t CGGradientDrawingOptions;

extern const CFStringRef kCGGradientInterpolatesPremultiplied;

CGGradientRef CGGradientCreateWithColorComponents(CGColorSpaceRef, const CGFloat* components,
    const CGFloat* locations, size_t count);
CGGradientRef CGGradientCreateWithColors(CGColorSpaceRef, CFArrayRef colors, const CGFloat* locations);
CGGradientRef CGGradientCreateWithColorComponentsAndOptions(CGColorSpaceRef, const CGFloat* components,
    const CGFloat* locations, size_t count, CFDictionaryRef options);
CGGradientRef CGGradientCreateWithColorsAndOptions(CGColorSpaceRef, CFArrayRef colors,
    const CGFloat* locations, CFDictionaryRef options);
CGGradientRef CGGradientRetain(CGGradientRef);
void CGGradientRelease(CGGradientRef);
CFTypeID CGGradientGetTypeID(void);

void CGContextDrawLinearGradient(CGContextRef, CGGradientRef, CGPoint start, CGPoint end,
    CGGradientDrawingOptions);
void CGContextDrawRadialGradient(CGContextRef, CGGradientRef, CGPoint startCenter, CGFloat startRadius,
    CGPoint endCenter, CGFloat endRadius, CGGradientDrawingOptions);
void CGContextDrawConicGradient(CGContextRef, CGGradientRef, CGPoint center, CGFloat angle);

/* --------------------------------------------------------------------- path */

CGPathRef CGPathCreateWithRect(CGRect, const CGAffineTransform*);
CGPathRef CGPathCreateWithRoundedRect(CGRect, CGFloat cornerWidth, CGFloat cornerHeight,
    const CGAffineTransform*);
void CGPathAddRoundedRect(CGMutablePathRef, const CGAffineTransform*, CGRect,
    CGFloat cornerWidth, CGFloat cornerHeight);
void CGPathAddUnevenCornersRoundedRect(CGMutablePathRef, const CGAffineTransform*, CGRect,
    const CGSize corners[4]);
CGPathRef CGPathCreateCopyByTransformingPath(CGPathRef, const CGAffineTransform*);
CGMutablePathRef CGPathCreateMutableCopyByTransformingPath(CGPathRef, const CGAffineTransform*);
CGRect CGPathGetPathBoundingBox(CGPathRef);

/* ------------------------------------------------------------------ context */

/* The Porter-Duff blend modes arrived in 10.5; Tiger's CGBlendMode enum stops at
   Luminosity. The numeric values are Apple's, so this compiles, but measured on
   the box Tiger honours NONE of them: all twelve composite exactly as
   kCGBlendModeNormal, silently. There is no shim for this, because the mode is
   context state consumed by every later drawing call; the port has to avoid the
   paths that depend on it. See compat/CG-SURVEY.md. */
enum {
    kCGBlendModeClear = 16,
    kCGBlendModeCopy = 17,
    kCGBlendModeSourceIn = 18,
    kCGBlendModeSourceOut = 19,
    kCGBlendModeSourceAtop = 20,
    kCGBlendModeDestinationOver = 21,
    kCGBlendModeDestinationIn = 22,
    kCGBlendModeDestinationOut = 23,
    kCGBlendModeDestinationAtop = 24,
    kCGBlendModeXOR = 25,
    kCGBlendModePlusDarker = 26,
    kCGBlendModePlusLighter = 27
};

typedef void (*CGBitmapContextReleaseDataCallback)(void* releaseInfo, void* data);
CGContextRef CGBitmapContextCreateWithData(void* data, size_t width, size_t height,
    size_t bitsPerComponent, size_t bytesPerRow, CGColorSpaceRef, uint32_t bitmapInfo,
    CGBitmapContextReleaseDataCallback, void* releaseInfo);
/* Tiger exports CGContextGetType but declares it nowhere. */
typedef enum {
    kCGContextTypeUnknown,
    kCGContextTypePDF,
    kCGContextTypePostScript,
    kCGContextTypeWindow,
    kCGContextTypeBitmap,
    kCGContextTypeGL,
    kCGContextTypeDisplayList,
    kCGContextTypeKSeparation,
    kCGContextTypeIOSurface,
    kCGContextTypeCount
} CGContextType;
CGContextType CGContextGetType(CGContextRef);

CGColorSpaceRef CGContextGetColorSpace(CGContextRef);
void CGContextBeginTransparencyLayerWithRect(CGContextRef, CGRect, CFDictionaryRef);
void CGContextStrokeArc(CGContextRef, CGPoint center, CGFloat radius, CGFloat startAngle,
    CGFloat endAngle, int clockwise);
void CGContextDrawPathDirect(CGContextRef, CGPathDrawingMode, CGPathRef, const CGRect* boundingBox);
void CGContextDrawTiledImage(CGContextRef, CGRect, CGImageRef);

/* Font rendering knobs that arrived after 10.4. Tiger's rasterizer has no
   equivalent state, so the setters are no-ops and the getters report the
   behaviour Tiger actually has.

   CGContextSetShouldAntialiasFonts is a no-op too, and measured rather than
   assumed: Tiger's CGContextSetShouldSmoothFonts does nothing in a bitmap
   context, and the knob that works, CGContextSetShouldAntialias, is
   context-wide and would alias shapes as well as glyphs. See cgcompat.c. */
void CGContextSetAllowsFontSubpixelPositioning(CGContextRef, bool);
bool CGContextGetAllowsFontSubpixelPositioning(CGContextRef);
void CGContextSetAllowsFontSubpixelQuantization(CGContextRef, bool);
bool CGContextGetAllowsFontSubpixelQuantization(CGContextRef);
void CGContextSetShouldSubpixelPositionFonts(CGContextRef, bool);
void CGContextSetShouldSubpixelQuantizeFonts(CGContextRef, bool);
void CGContextSetShouldAntialiasFonts(CGContextRef, bool);

/* Values are Apple's, matching PAL's CoreGraphicsSPI.h. */
enum {
    kCGFontAntialiasingStyleUnfiltered = 0 << 7,
    kCGFontAntialiasingStyleFilterLight = 1 << 7,
    kCGFontAntialiasingStyleUnfilteredCustomDilation = (8 << 7)
};
typedef uint32_t CGFontAntialiasingStyle;
void CGContextSetFontAntialiasingStyle(CGContextRef, CGFontAntialiasingStyle);
CGFontAntialiasingStyle CGContextGetFontAntialiasingStyle(CGContextRef);
bool CGFontRenderingGetFontSmoothingDisabled(void);

/* Private, exported by Tiger, and the faithful upgrade path for
   CGContextSetShouldAntialiasFonts if a call site ever passes false, for
   instance to support -webkit-font-smoothing: none. Declared here so the names
   live in one place; nothing in compat calls them today.

   Unlike CGContextSetShouldAntialias, this flag is per-FONT, so clearing it
   aliases glyphs while shapes in the same context stay smooth. That is exactly
   the semantics CGContextSetShouldAntialiasFonts wants. Two cautions before
   using it: the flag mutates the CGFont object, which is shared and cached, so
   it leaks into every other context using that font until restored; and WebCore
   usually sets the font after configuring state, so CGContextGetFont may not
   have the right font yet when the setter runs.

   Signatures read off Tiger's prologues rather than assumed: the setter takes
   the font at 0x8 and the flag at 0xc and stores bit 0 of the byte at
   font+0x3c; the getter takes the font at 0x8 and returns that bit. */
void CGFontSetShouldAntialias(CGFontRef, bool);
bool CGFontShouldAntialias(CGFontRef);

/* ------------------------------------------------------- Tiger ABI mismatch */

/* Tiger's CGGStateGetCTM returns the matrix BY VALUE. WebCore's
   CoreGraphicsSPI.h declares it as returning `const CGAffineTransform *`.
   Calling it through the modern declaration would put the CGGStateRef in the
   hidden struct-return slot, so CG would take its second stack word as the
   gstate and write 24 bytes through the gstate pointer instead of reading it.

   Found by comparing Tiger's prologue against the modern prototype: the
   function reads 0x8(%ebp) as a destination and 0xc(%ebp) as the source, then
   copies six dwords from source+4. It is the only mismatch in 285 screened
   CoreGraphics and ImageIO entry points.

   It is also unreachable on Tiger today, because a CGGStateRef can only come
   from a delegate-backed context and Tiger exports neither
   CGContextCreateWithDelegate nor CGContextGetGState. The adapter exists so
   that enabling that path later cannot silently corrupt memory. */
#ifndef CGGSTATE_TYPEDEF_DEFINED
typedef struct CGGState *CGGStateRef;
#endif
CGAffineTransform TigerCGGStateGetCTM(CGGStateRef) __asm__("_CGGStateGetCTM");
const CGAffineTransform* CGGStateGetCTMCompat(CGGStateRef);
#define CGGStateGetCTM CGGStateGetCTMCompat

/* --------------------------------------------------------------------- font */

/* Tiger's CoreGraphics exports these three, but the 10.4u SDK's CGFont.h
   declares none of them. Found by disassembling the box on the CoreText track
   and confirmed working there; no shim needed, only a declaration. */
/* Exported and linkable, but NON-FUNCTIONAL on Tiger: it returns NULL for .ttf
   and .dfont alike, as does CGFontCreateWithName, so there is no route from
   font bytes to a CGFontRef through CoreGraphics on this OS. Use
   ATSFontActivateFromMemory plus CGFontCreateWithPlatformFont, which is what
   the CoreText track's web font path does. Declared only so existing callers
   link; nothing here depends on it. */
CGFontRef CGFontCreateWithDataProvider(CGDataProviderRef);
CGPathRef CGFontGetGlyphPath(CGFontRef, const CGAffineTransform*, int unused, CGGlyph);
int CGFontGetUnitsPerEm(CGFontRef);

/* Tiger has no CGFontCopyTableTags and no CGContextShowGlyphsAtPositions.
   Anything reaching for those needs a different route, not a declaration. */

/* ------------------------------------------------------------------ ImageIO */

/* Tiger's ImageIO has the whole 10.4 CGImageSource/CGImageDestination surface.
   These are the later additions WebCore reaches for.

   CGImageSourceRef is forward-declared rather than pulled from
   <ImageIO/CGImageSource.h>, which would include the CoreGraphics umbrella and
   with it all of CoreServices. The spelling is the SDK's exactly, and the guard
   is the SDK header's own, so including that header before or after this one is
   equally fine. */
#ifndef CGIMAGESOURCE_H_
typedef struct CGImageSource *CGImageSourceRef;
#endif

size_t CGImageSourceGetPrimaryImageIndex(CGImageSourceRef);
CFDictionaryRef CGImageSourceCopyAuxiliaryDataInfoAtIndexWithOptions(CGImageSourceRef, size_t index,
    CFStringRef auxiliaryImageDataType, CFDictionaryRef options);
void CGImageSourceSetAllowableTypes(CFArrayRef allowableTypes);
void CGImageSourceDisableHardwareDecoding(CGImageSourceRef);
void CGImageSourceEnableRestrictedDecoding(void);

extern const CFStringRef kCGImageSourceShouldCacheImmediately;
extern const CFStringRef kCGImageSourceSkipMetadata;
extern const CFStringRef kCGImageSourceUseHardwareAcceleration;
extern const CFStringRef kCGImagePropertyImages;
extern const CFStringRef kCGImagePropertyImageCount;
extern const CFStringRef kCGImagePropertyGroups;
extern const CFStringRef kCGImagePropertyPrimaryImage;
extern const CFStringRef kCGImagePropertyHEIFDictionary;
extern const CFStringRef kCGImagePropertyWebPDictionary;
extern const CFStringRef kCGImagePropertyPNGDelayTime;
extern const CFStringRef kCGImagePropertyPNGUnclampedDelayTime;
extern const CFStringRef kCGImagePropertyPNGLoopCount;
extern const CFStringRef kCGImagePropertyAuxiliaryData;
extern const CFStringRef kCGImagePropertyAuxiliaryDataType;
extern const CFStringRef kCGImageAuxiliaryDataTypeHDRGainMap;
extern const CFStringRef kCGImageAuxiliaryDataTypeISOGainMap;
extern const CFStringRef kCGImageAuxiliaryDataInfoData;
extern const CFStringRef kCGImageAuxiliaryDataInfoColorSpace;
extern const CFStringRef kCGImageAuxiliaryDataInfoMetadata;
extern const CFStringRef kCGImageAuxiliaryDataInfoDataDescription;

/* ------------------------------------------------------------ image caching */

enum {
    kCGImageCachingDefault = 0,
    kCGImageCachingTransient = 1,
    kCGImageCachingTemporary = 3
};
typedef uint32_t CGImageCachingFlags;
void CGImageSetCachingFlags(CGImageRef, CGImageCachingFlags);
CGImageCachingFlags CGImageGetCachingFlags(CGImageRef);
void CGImageSetProperty(CGImageRef, CFStringRef, CFTypeRef);

#ifdef __cplusplus
}
#endif

#endif /* TIGERCOMPAT_CGCOMPAT_H */
