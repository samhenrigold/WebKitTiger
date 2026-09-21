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

/* Deliberately NOT <ApplicationServices/ApplicationServices.h>: the SDK overlay
   appends this header to <CoreGraphics/CoreGraphics.h>, and pulling all of
   ApplicationServices in from there would drag QuickDraw's Rect and Point
   macros into every WebCore translation unit.

   <ImageIO/CGImageSource.h> typedefs CGImageSourceRef on line 10, before its
   own include of CoreGraphics.h, so this resolves correctly in both include
   orders: reached through CoreGraphics.h, or through ImageIO.h first. */
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/CGImageSource.h>

/* CGFloat's owner is the SDK overlay's CGBase.h, which every CoreGraphics header
   includes. This guarded copy is the fallback for builds that do not put the
   overlay on -F, which is how compat itself builds. Apple's guard macro means
   whichever comes first wins and the two can never collide. */
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
   Luminosity. The numeric values are Apple's, so a Tiger CG that happens to
   understand one gets the right mode and ignores the rest. */
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
   behaviour Tiger actually has. */
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

/* --------------------------------------------------------------------- font */

/* Tiger's CoreGraphics exports these three, but the 10.4u SDK's CGFont.h
   declares none of them. Found by disassembling the box on the CoreText track
   and confirmed working there; no shim needed, only a declaration. */
CGFontRef CGFontCreateWithDataProvider(CGDataProviderRef);
CGPathRef CGFontGetGlyphPath(CGFontRef, const CGAffineTransform*, int unused, CGGlyph);
int CGFontGetUnitsPerEm(CGFontRef);

/* Tiger has no CGFontCopyTableTags and no CGContextShowGlyphsAtPositions.
   Anything reaching for those needs a different route, not a declaration. */

/* ------------------------------------------------------------------ ImageIO */

/* Tiger's ImageIO has the whole 10.4 CGImageSource/CGImageDestination surface.
   These are the later additions WebCore reaches for. */
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
