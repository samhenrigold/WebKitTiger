/*
 * cgcompat.c -- CoreGraphics / ImageIO gaps for Mac OS X 10.4.11 (i386).
 *
 * Everything here is built out of API Tiger's CoreGraphics really exports:
 * CGShadingCreateAxial/Radial + CGFunctionCreate for gradients,
 * CGColorSpaceCreateICCBased + ColorSync's shipped sRGB profile for named
 * colorspaces, CGPathApply for path transforms. See compat/CG-SURVEY.md.
 *
 * CGFloat is float on i386, so modern CGFloat prototypes are ABI-identical to
 * Tiger's float-based ones.
 */

#include <ApplicationServices/ApplicationServices.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Pull in our own declarations, but not the CGColorSpaceCreateWithName macro:
   this file has to call Tiger's real one. */
#include <TigerCompat/CGCompat.h>
#undef CGColorSpaceCreateWithName

/* ================================================================ constants */

/* String values are Apple's, so any of these that Tiger's ImageIO happens to
   understand still works, and the rest simply never match a key. */
#define CGSTR(name, value) const CFStringRef name = (const CFStringRef)CFSTR(value)

CGSTR(kCGColorSpaceSRGB, "kCGColorSpaceSRGB");
CGSTR(kCGColorSpaceLinearSRGB, "kCGColorSpaceLinearSRGB");
CGSTR(kCGColorSpaceExtendedSRGB, "kCGColorSpaceExtendedSRGB");
CGSTR(kCGColorSpaceExtendedLinearSRGB, "kCGColorSpaceExtendedLinearSRGB");
CGSTR(kCGColorSpaceGenericRGBLinear, "kCGColorSpaceGenericRGBLinear");
CGSTR(kCGColorSpaceGenericGrayGamma2_2, "kCGColorSpaceGenericGrayGamma2_2");
CGSTR(kCGColorSpaceGenericXYZ, "kCGColorSpaceGenericXYZ");
CGSTR(kCGColorSpaceDisplayP3, "kCGColorSpaceDisplayP3");
CGSTR(kCGColorSpaceLinearDisplayP3, "kCGColorSpaceLinearDisplayP3");
CGSTR(kCGColorSpaceExtendedDisplayP3, "kCGColorSpaceExtendedDisplayP3");
CGSTR(kCGColorSpaceExtendedLinearDisplayP3, "kCGColorSpaceExtendedLinearDisplayP3");
CGSTR(kCGColorSpaceAdobeRGB1998, "kCGColorSpaceAdobeRGB1998");
CGSTR(kCGColorSpaceROMMRGB, "kCGColorSpaceROMMRGB");
CGSTR(kCGColorSpaceITUR_709, "kCGColorSpaceITUR_709");
CGSTR(kCGColorSpaceITUR_2020, "kCGColorSpaceITUR_2020");
CGSTR(kCGColorSpaceExtendedITUR_2020, "kCGColorSpaceExtendedITUR_2020");

CGSTR(kCGColorWhite, "kCGColorWhite");
CGSTR(kCGColorBlack, "kCGColorBlack");
CGSTR(kCGColorClear, "kCGColorClear");

CGSTR(kCGGradientInterpolatesPremultiplied, "kCGGradientInterpolatesPremultiplied");

CGSTR(kCGImageSourceShouldCacheImmediately, "kCGImageSourceShouldCacheImmediately");
CGSTR(kCGImageSourceSkipMetadata, "kCGImageSourceSkipMetadata");
CGSTR(kCGImageSourceUseHardwareAcceleration, "kCGImageSourceUseHardwareAcceleration");
CGSTR(kCGImagePropertyImages, "Images");
CGSTR(kCGImagePropertyImageCount, "ImageCount");
CGSTR(kCGImagePropertyGroups, "Groups");
CGSTR(kCGImagePropertyPrimaryImage, "PrimaryImage");
CGSTR(kCGImagePropertyHEIFDictionary, "{HEIF}");
CGSTR(kCGImagePropertyWebPDictionary, "{WebP}");
CGSTR(kCGImagePropertyPNGDelayTime, "DelayTime");
CGSTR(kCGImagePropertyPNGUnclampedDelayTime, "UnclampedDelayTime");
CGSTR(kCGImagePropertyPNGLoopCount, "LoopCount");
CGSTR(kCGImagePropertyAuxiliaryData, "AuxiliaryData");
CGSTR(kCGImagePropertyAuxiliaryDataType, "AuxiliaryDataType");
CGSTR(kCGImageAuxiliaryDataTypeHDRGainMap, "kCGImageAuxiliaryDataTypeHDRGainMap");
CGSTR(kCGImageAuxiliaryDataTypeISOGainMap, "kCGImageAuxiliaryDataTypeISOGainMap");
CGSTR(kCGImageAuxiliaryDataInfoData, "kCGImageAuxiliaryDataInfoData");
CGSTR(kCGImageAuxiliaryDataInfoColorSpace, "kCGImageAuxiliaryDataInfoColorSpace");
CGSTR(kCGImageAuxiliaryDataInfoMetadata, "kCGImageAuxiliaryDataInfoMetadata");
CGSTR(kCGImageAuxiliaryDataInfoDataDescription, "kCGImageAuxiliaryDataInfoDataDescription");

/* =============================================================== colorspace */

/* One cached colorspace per name, which doubles as the lookup table that gives
   CGColorSpaceGetName something to answer with. Tiger is single-threaded here
   only by convention; WebCore creates these on the main thread.
   ponytail: unsynchronized fixed table, add a lock if a worker thread ever
   creates colorspaces. */
#define CS_CACHE_MAX 24
static struct { CFStringRef name; CGColorSpaceRef cs; } sNamedSpaces[CS_CACHE_MAX];
static int sNamedSpaceCount;

/* Tiger ships the sRGB profile with ColorSync; an ICC-based colorspace off it is
   a real sRGB, not an approximation. */
static CGColorSpaceRef createSRGBFromColorSync(void)
{
    static const char* kPaths[] = {
        "/System/Library/ColorSync/Profiles/sRGB Profile.icc",
        "/Library/ColorSync/Profiles/sRGB Profile.icc",
    };
    size_t i;
    for (i = 0; i < sizeof(kPaths) / sizeof(kPaths[0]); ++i) {
        CGColorSpaceRef alternate, cs;
        CGDataProviderRef provider = CGDataProviderCreateWithFilename(kPaths[i]);
        if (!provider)
            continue;
        alternate = CGColorSpaceCreateDeviceRGB();
        {
            const float range[6] = { 0, 1, 0, 1, 0, 1 };
            cs = CGColorSpaceCreateICCBased(3, range, provider, alternate);
        }
        CGColorSpaceRelease(alternate);
        CGDataProviderRelease(provider);
        if (cs)
            return cs;
    }
    /* No profile on disk: generic RGB is the closest thing Tiger has. */
    return CGColorSpaceCreateWithName(kCGColorSpaceGenericRGB);
}

static CGColorSpaceRef createSpaceForName(CFStringRef name)
{
    if (CFEqual(name, kCGColorSpaceSRGB) || CFEqual(name, kCGColorSpaceExtendedSRGB)
        || CFEqual(name, kCGColorSpaceLinearSRGB) || CFEqual(name, kCGColorSpaceExtendedLinearSRGB)
        || CFEqual(name, kCGColorSpaceDisplayP3) || CFEqual(name, kCGColorSpaceLinearDisplayP3)
        || CFEqual(name, kCGColorSpaceExtendedDisplayP3)
        || CFEqual(name, kCGColorSpaceExtendedLinearDisplayP3)
        || CFEqual(name, kCGColorSpaceAdobeRGB1998) || CFEqual(name, kCGColorSpaceROMMRGB)
        || CFEqual(name, kCGColorSpaceITUR_709) || CFEqual(name, kCGColorSpaceITUR_2020)
        || CFEqual(name, kCGColorSpaceExtendedITUR_2020)) {
        /* ponytail: every wide/linear/extended variant collapses onto sRGB.
           Tiger's CG clamps to [0,1] and has no extended range, so the only
           honest alternatives are sRGB or failing the call. */
        return createSRGBFromColorSync();
    }
    if (CFEqual(name, kCGColorSpaceGenericRGBLinear))
        return CGColorSpaceCreateWithName(kCGColorSpaceGenericRGB);
    if (CFEqual(name, kCGColorSpaceGenericGrayGamma2_2))
        return CGColorSpaceCreateWithName(kCGColorSpaceGenericGray);
    if (CFEqual(name, kCGColorSpaceGenericXYZ))
        return CGColorSpaceCreateWithName(kCGColorSpaceGenericRGB);
    /* Generic{Gray,RGB,CMYK}: Tiger knows these itself. */
    return CGColorSpaceCreateWithName(name);
}

CGColorSpaceRef TigerCGColorSpaceCreateWithName(CFStringRef name)
{
    int i;
    CGColorSpaceRef cs;

    if (!name)
        return NULL;
    for (i = 0; i < sNamedSpaceCount; ++i) {
        if (CFEqual(sNamedSpaces[i].name, name))
            return CGColorSpaceRetain(sNamedSpaces[i].cs);
    }
    cs = createSpaceForName(name);
    if (cs && sNamedSpaceCount < CS_CACHE_MAX) {
        sNamedSpaces[sNamedSpaceCount].name = (CFStringRef)CFRetain(name);
        sNamedSpaces[sNamedSpaceCount].cs = CGColorSpaceRetain(cs);
        ++sNamedSpaceCount;
    }
    return cs;
}

CFStringRef CGColorSpaceGetName(CGColorSpaceRef cs)
{
    int i;
    for (i = 0; i < sNamedSpaceCount; ++i) {
        if (sNamedSpaces[i].cs == cs)
            return sNamedSpaces[i].name;
    }
    return NULL;
}

CGColorSpaceModel CGColorSpaceGetModel(CGColorSpaceRef cs)
{
    /* ponytail: component count is all Tiger exposes, so Indexed and Pattern
       report as their base model. Nothing in WebCore's CG backend branches on
       those two. */
    if (!cs)
        return kCGColorSpaceModelUnknown;
    switch (CGColorSpaceGetNumberOfComponents(cs)) {
    case 1: return kCGColorSpaceModelMonochrome;
    case 3: return kCGColorSpaceModelRGB;
    case 4: return kCGColorSpaceModelCMYK;
    default: return kCGColorSpaceModelUnknown;
    }
}

CGColorSpaceRef CGColorSpaceGetBaseColorSpace(CGColorSpaceRef cs) { (void)cs; return NULL; }
bool CGColorSpaceUsesExtendedRange(CGColorSpaceRef cs) { (void)cs; return false; }
bool CGColorSpaceIsWideGamutRGB(CGColorSpaceRef cs) { (void)cs; return false; }
bool CGColorSpaceSupportsOutput(CGColorSpaceRef cs) { return cs != NULL; }
CGColorSpaceRef CGColorSpaceCreateExtended(CGColorSpaceRef cs) { return CGColorSpaceRetain(cs); }

/* Name-based round trip: enough for WebCore, which only uses the property list
   to ship a colorspace across a process boundary and rebuild it. */
CFPropertyListRef CGColorSpaceCopyPropertyList(CGColorSpaceRef cs)
{
    CFStringRef name = CGColorSpaceGetName(cs);
    if (!name)
        return NULL;
    return CFStringCreateCopy(kCFAllocatorDefault, name);
}

CGColorSpaceRef CGColorSpaceCreateWithPropertyList(CFPropertyListRef plist)
{
    if (!plist || CFGetTypeID(plist) != CFStringGetTypeID())
        return NULL;
    return TigerCGColorSpaceCreateWithName((CFStringRef)plist);
}

/* ==================================================================== color */

CGColorRef CGColorCreateSRGB(CGFloat r, CGFloat g, CGFloat b, CGFloat a)
{
    const CGFloat components[4] = { r, g, b, a };
    CGColorSpaceRef cs = TigerCGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGColorRef color = CGColorCreate(cs, components);
    CGColorSpaceRelease(cs);
    return color;
}

CGColorRef CGColorCreateGenericGray(CGFloat gray, CGFloat alpha)
{
    const CGFloat components[2] = { gray, alpha };
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceGenericGray);
    CGColorRef color = CGColorCreate(cs, components);
    CGColorSpaceRelease(cs);
    return color;
}

CGColorRef CGColorGetConstantColor(CFStringRef colorName)
{
    static CGColorRef sWhite, sBlack, sClear;
    if (!colorName)
        return NULL;
    if (CFEqual(colorName, kCGColorWhite))
        return sWhite ? sWhite : (sWhite = CGColorCreateGenericGray(1, 1));
    if (CFEqual(colorName, kCGColorBlack))
        return sBlack ? sBlack : (sBlack = CGColorCreateGenericGray(0, 1));
    if (CFEqual(colorName, kCGColorClear))
        return sClear ? sClear : (sClear = CGColorCreateGenericGray(0, 0));
    return NULL;
}

/* ================================================================= gradient */

/* A CGGradient is a CFDictionary holding the colorspace (so CF retains and
   releases it for us) and a CFData with the stops. That makes CFRetain,
   CFRelease and RetainPtr<CGGradientRef> work with no CFRuntime class to
   register. */
static const CFStringRef kGradientSpaceKey = (const CFStringRef)CFSTR("cs");
static const CFStringRef kGradientStopsKey = (const CFStringRef)CFSTR("stops");

typedef struct {
    size_t componentCount; /* colour components per stop, alpha excluded */
    size_t stopCount;
    /* stopCount * (componentCount + 1) colour+alpha floats, then stopCount
       locations. */
    CGFloat values[1];
} GradientStops;

static CGGradientRef createGradient(CGColorSpaceRef cs, const CGFloat* components,
    const CGFloat* locations, size_t count)
{
    size_t ncomp, stride, valueCount, bytes, i;
    GradientStops* stops;
    CFDataRef data;
    CFMutableDictionaryRef dict;
    CGColorSpaceRef space;

    if (!components || !count)
        return NULL;

    space = cs ? CGColorSpaceRetain(cs) : CGColorSpaceCreateDeviceRGB();
    ncomp = CGColorSpaceGetNumberOfComponents(space);
    stride = ncomp + 1;
    valueCount = count * stride + count;
    bytes = sizeof(GradientStops) + (valueCount - 1) * sizeof(CGFloat);

    stops = (GradientStops*)calloc(1, bytes);
    if (!stops) {
        CGColorSpaceRelease(space);
        return NULL;
    }
    stops->componentCount = ncomp;
    stops->stopCount = count;
    memcpy(stops->values, components, count * stride * sizeof(CGFloat));
    for (i = 0; i < count; ++i)
        stops->values[count * stride + i] = locations ? locations[i] : (count > 1 ? (CGFloat)i / (count - 1) : 0);

    data = CFDataCreate(kCFAllocatorDefault, (const UInt8*)stops, bytes);
    free(stops);
    if (!data) {
        CGColorSpaceRelease(space);
        return NULL;
    }
    dict = CFDictionaryCreateMutable(kCFAllocatorDefault, 2, &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(dict, kGradientSpaceKey, space);
    CFDictionarySetValue(dict, kGradientStopsKey, data);
    CFRelease(data);
    CGColorSpaceRelease(space);
    return (CGGradientRef)dict;
}

CGGradientRef CGGradientCreateWithColorComponents(CGColorSpaceRef cs, const CGFloat* components,
    const CGFloat* locations, size_t count)
{
    return createGradient(cs, components, locations, count);
}

CGGradientRef CGGradientCreateWithColorComponentsAndOptions(CGColorSpaceRef cs,
    const CGFloat* components, const CGFloat* locations, size_t count, CFDictionaryRef options)
{
    /* The only option Apple defines here is premultiplied interpolation, which
       matters solely where stops differ in alpha. ponytail: ignored; add
       premultiplied blending in evaluateGradient if a gradient with varying
       alpha ever looks wrong. */
    (void)options;
    return createGradient(cs, components, locations, count);
}

/* Colours are read in their own space and copied straight across, widening
   gray to RGB when needed. */
static CGGradientRef createGradientFromColors(CGColorSpaceRef cs, CFArrayRef colors,
    const CGFloat* locations)
{
    CFIndex count, i;
    size_t ncomp, stride, c;
    CGFloat* components;
    CGGradientRef gradient;
    CGColorSpaceRef space;

    if (!colors)
        return NULL;
    count = CFArrayGetCount(colors);
    if (!count)
        return NULL;

    space = cs ? CGColorSpaceRetain(cs) : CGColorSpaceCreateDeviceRGB();
    ncomp = CGColorSpaceGetNumberOfComponents(space);
    stride = ncomp + 1;
    components = (CGFloat*)calloc(count * stride, sizeof(CGFloat));
    if (!components) {
        CGColorSpaceRelease(space);
        return NULL;
    }
    for (i = 0; i < count; ++i) {
        CGColorRef color = (CGColorRef)CFArrayGetValueAtIndex(colors, i);
        const CGFloat* src = CGColorGetComponents(color);
        size_t srcComponents = CGColorSpaceGetNumberOfComponents(CGColorGetColorSpace(color));
        CGFloat* dst = components + i * stride;
        if (srcComponents == 1 && ncomp == 3) {
            dst[0] = dst[1] = dst[2] = src[0];
        } else {
            for (c = 0; c < ncomp; ++c)
                dst[c] = c < srcComponents ? src[c] : 0;
        }
        dst[ncomp] = CGColorGetAlpha(color);
    }
    gradient = createGradient(space, components, locations, (size_t)count);
    free(components);
    CGColorSpaceRelease(space);
    return gradient;
}

CGGradientRef CGGradientCreateWithColors(CGColorSpaceRef cs, CFArrayRef colors, const CGFloat* locations)
{
    return createGradientFromColors(cs, colors, locations);
}

CGGradientRef CGGradientCreateWithColorsAndOptions(CGColorSpaceRef cs, CFArrayRef colors,
    const CGFloat* locations, CFDictionaryRef options)
{
    (void)options;
    return createGradientFromColors(cs, colors, locations);
}

CGGradientRef CGGradientRetain(CGGradientRef g) { return g ? (CGGradientRef)CFRetain(g) : NULL; }
void CGGradientRelease(CGGradientRef g) { if (g) CFRelease(g); }
CFTypeID CGGradientGetTypeID(void) { return CFDictionaryGetTypeID(); }

/* The CGFunction owns its own copy of the stops so evaluation never touches CF. */
static const GradientStops* gradientStops(CGGradientRef gradient, CGColorSpaceRef* outSpace)
{
    CFDataRef data;
    if (!gradient || CFGetTypeID(gradient) != CFDictionaryGetTypeID())
        return NULL;
    if (outSpace)
        *outSpace = (CGColorSpaceRef)CFDictionaryGetValue((CFDictionaryRef)gradient, kGradientSpaceKey);
    data = (CFDataRef)CFDictionaryGetValue((CFDictionaryRef)gradient, kGradientStopsKey);
    return data ? (const GradientStops*)CFDataGetBytePtr(data) : NULL;
}

static void evaluateGradient(void* info, const float* in, float* out)
{
    const GradientStops* s = (const GradientStops*)info;
    size_t stride = s->componentCount + 1;
    const CGFloat* locations = s->values + s->stopCount * stride;
    float t = in[0];
    size_t i, c, lo, hi;
    float span, f;

    if (t <= locations[0]) {
        for (c = 0; c < stride; ++c)
            out[c] = s->values[c];
        return;
    }
    if (t >= locations[s->stopCount - 1]) {
        const CGFloat* last = s->values + (s->stopCount - 1) * stride;
        for (c = 0; c < stride; ++c)
            out[c] = last[c];
        return;
    }
    lo = 0;
    for (i = 1; i < s->stopCount; ++i) {
        if (locations[i] >= t) {
            lo = i - 1;
            break;
        }
    }
    hi = lo + 1;
    span = locations[hi] - locations[lo];
    f = span > 0 ? (t - locations[lo]) / span : 0;
    for (c = 0; c < stride; ++c) {
        float a = s->values[lo * stride + c];
        float b = s->values[hi * stride + c];
        out[c] = a + (b - a) * f;
    }
}

static void releaseGradientInfo(void* info) { free(info); }

/* Returns a CGFunction over t in [0,1] outputting colour+alpha, or NULL. */
static CGFunctionRef createGradientFunction(CGGradientRef gradient, CGColorSpaceRef* outSpace)
{
    static const CGFunctionCallbacks callbacks = { 0, evaluateGradient, releaseGradientInfo };
    const GradientStops* stops = gradientStops(gradient, outSpace);
    size_t stride, bytes, c;
    float domain[2] = { 0, 1 };
    float range[16];
    GradientStops* copy;
    CGFunctionRef function;

    if (!stops)
        return NULL;
    stride = stops->componentCount + 1;
    if (stride > 8)
        return NULL;
    bytes = sizeof(GradientStops)
        + (stops->stopCount * stride + stops->stopCount - 1) * sizeof(CGFloat);
    copy = (GradientStops*)malloc(bytes);
    if (!copy)
        return NULL;
    memcpy(copy, stops, bytes);

    for (c = 0; c < stride; ++c) {
        range[2 * c] = 0;
        range[2 * c + 1] = 1;
    }
    function = CGFunctionCreate(copy, 1, domain, stride, range, &callbacks);
    if (!function)
        free(copy);
    return function;
}

void CGContextDrawLinearGradient(CGContextRef context, CGGradientRef gradient, CGPoint start,
    CGPoint end, CGGradientDrawingOptions options)
{
    CGColorSpaceRef space = NULL;
    CGFunctionRef function = createGradientFunction(gradient, &space);
    CGShadingRef shading;
    if (!function)
        return;
    shading = CGShadingCreateAxial(space, start, end, function,
        (options & kCGGradientDrawsBeforeStartLocation) != 0,
        (options & kCGGradientDrawsAfterEndLocation) != 0);
    CGFunctionRelease(function);
    if (!shading)
        return;
    CGContextDrawShading(context, shading);
    CGShadingRelease(shading);
}

void CGContextDrawRadialGradient(CGContextRef context, CGGradientRef gradient, CGPoint startCenter,
    CGFloat startRadius, CGPoint endCenter, CGFloat endRadius, CGGradientDrawingOptions options)
{
    CGColorSpaceRef space = NULL;
    CGFunctionRef function = createGradientFunction(gradient, &space);
    CGShadingRef shading;
    if (!function)
        return;
    shading = CGShadingCreateRadial(space, startCenter, startRadius, endCenter, endRadius, function,
        (options & kCGGradientDrawsBeforeStartLocation) != 0,
        (options & kCGGradientDrawsAfterEndLocation) != 0);
    CGFunctionRelease(function);
    if (!shading)
        return;
    CGContextDrawShading(context, shading);
    CGShadingRelease(shading);
}

void CGContextDrawConicGradient(CGContextRef context, CGGradientRef gradient, CGPoint center,
    CGFloat angle)
{
    /* ponytail: CGShading has no conic form on Tiger, so this fills 360 one-degree
       wedges with the colour at each wedge's mid-angle. Banding is invisible at
       one degree; swap in a CGLayer + per-pixel fill if it ever shows. */
    enum { kWedges = 360 };
    CGColorSpaceRef space = NULL;
    const GradientStops* stops = gradientStops(gradient, &space);
    CGRect clip;
    CGFloat radius;
    int i;
    size_t stride;

    if (!stops)
        return;
    stride = stops->componentCount + 1;
    clip = CGContextGetClipBoundingBox(context);
    if (CGRectIsEmpty(clip) || CGRectIsInfinite(clip))
        return;
    /* Reach every corner of the clip from the centre. */
    radius = (CGFloat)hypot(
        fmaxf(fabsf(CGRectGetMinX(clip) - center.x), fabsf(CGRectGetMaxX(clip) - center.x)),
        fmaxf(fabsf(CGRectGetMinY(clip) - center.y), fabsf(CGRectGetMaxY(clip) - center.y)));

    CGContextSaveGState(context);
    /* Antialiasing has to be off: two antialiased fills each covering half a
       pixel composite to 0.75 alpha, not 1, so an antialiased fan leaves the
       whole disc translucent. With aliased, overlapping wedges every pixel is
       fully covered by at least one of them. */
    CGContextSetShouldAntialias(context, false);
    for (i = 0; i < kWedges; ++i) {
        float t = (i + 0.5f) / kWedges;
        float components[8];
        /* Wedges overlap by half a step on each side. At one degree a wedge is
           narrower than a pixel, so without the overlap no pixel is ever fully
           covered and the antialiased edges composite to partial alpha. */
        float from = angle + (float)(2 * M_PI) * (i - 0.5f) / kWedges;
        float to = angle + (float)(2 * M_PI) * (i + 1.5f) / kWedges;
        evaluateGradient((void*)stops, &t, components);
        CGContextSetFillColorSpace(context, space);
        /* CGContextSetFillColor takes colour components followed by alpha. */
        CGContextSetFillColor(context, components);
        CGContextBeginPath(context);
        CGContextMoveToPoint(context, center.x, center.y);
        CGContextAddArc(context, center.x, center.y, radius, from, to, 0);
        CGContextClosePath(context);
        CGContextFillPath(context);
    }
    CGContextRestoreGState(context);
}

/* ===================================================================== path */

CGPathRef CGPathCreateWithRect(CGRect rect, const CGAffineTransform* transform)
{
    CGMutablePathRef path = CGPathCreateMutable();
    CGPathAddRect(path, transform, rect);
    return path;
}

/* Corner radii are clamped to half the rect, the way CG does it. */
void CGPathAddUnevenCornersRoundedRect(CGMutablePathRef path, const CGAffineTransform* transform,
    CGRect rect, const CGSize corners[4])
{
    /* corners are ordered top-left, top-right, bottom-right, bottom-left. */
    CGFloat maxW = CGRectGetWidth(rect) / 2, maxH = CGRectGetHeight(rect) / 2;
    CGFloat minX = CGRectGetMinX(rect), maxX = CGRectGetMaxX(rect);
    CGFloat minY = CGRectGetMinY(rect), maxY = CGRectGetMaxY(rect);
    CGSize c[4];
    int i;
    /* Kappa for a quarter ellipse. */
    const CGFloat k = 0.5522847498307933f;

    if (!path || CGRectIsEmpty(rect))
        return;
    for (i = 0; i < 4; ++i) {
        c[i].width = fminf(fmaxf(corners[i].width, 0), maxW);
        c[i].height = fminf(fmaxf(corners[i].height, 0), maxH);
    }
    /* Y grows upward in CG, so "top" is maxY. */
    CGPathMoveToPoint(path, transform, minX + c[3].width, minY);
    CGPathAddLineToPoint(path, transform, maxX - c[2].width, minY);
    CGPathAddCurveToPoint(path, transform, maxX - c[2].width * (1 - k), minY,
        maxX, minY + c[2].height * (1 - k), maxX, minY + c[2].height);
    CGPathAddLineToPoint(path, transform, maxX, maxY - c[1].height);
    CGPathAddCurveToPoint(path, transform, maxX, maxY - c[1].height * (1 - k),
        maxX - c[1].width * (1 - k), maxY, maxX - c[1].width, maxY);
    CGPathAddLineToPoint(path, transform, minX + c[0].width, maxY);
    CGPathAddCurveToPoint(path, transform, minX + c[0].width * (1 - k), maxY,
        minX, maxY - c[0].height * (1 - k), minX, maxY - c[0].height);
    CGPathAddLineToPoint(path, transform, minX, minY + c[3].height);
    CGPathAddCurveToPoint(path, transform, minX, minY + c[3].height * (1 - k),
        minX + c[3].width * (1 - k), minY, minX + c[3].width, minY);
    CGPathCloseSubpath(path);
}

void CGPathAddRoundedRect(CGMutablePathRef path, const CGAffineTransform* transform, CGRect rect,
    CGFloat cornerWidth, CGFloat cornerHeight)
{
    CGSize corners[4];
    int i;
    for (i = 0; i < 4; ++i) {
        corners[i].width = cornerWidth;
        corners[i].height = cornerHeight;
    }
    CGPathAddUnevenCornersRoundedRect(path, transform, rect, corners);
}

CGPathRef CGPathCreateWithRoundedRect(CGRect rect, CGFloat cornerWidth, CGFloat cornerHeight,
    const CGAffineTransform* transform)
{
    CGMutablePathRef path = CGPathCreateMutable();
    CGPathAddRoundedRect(path, transform, rect, cornerWidth, cornerHeight);
    return path;
}

typedef struct {
    CGMutablePathRef path;
    const CGAffineTransform* transform;
} PathTransformContext;

static void transformPathElement(void* info, const CGPathElement* element)
{
    PathTransformContext* ctx = (PathTransformContext*)info;
    const CGAffineTransform* t = ctx->transform;
    switch (element->type) {
    case kCGPathElementMoveToPoint:
        CGPathMoveToPoint(ctx->path, t, element->points[0].x, element->points[0].y);
        break;
    case kCGPathElementAddLineToPoint:
        CGPathAddLineToPoint(ctx->path, t, element->points[0].x, element->points[0].y);
        break;
    case kCGPathElementAddQuadCurveToPoint:
        CGPathAddQuadCurveToPoint(ctx->path, t, element->points[0].x, element->points[0].y,
            element->points[1].x, element->points[1].y);
        break;
    case kCGPathElementAddCurveToPoint:
        CGPathAddCurveToPoint(ctx->path, t, element->points[0].x, element->points[0].y,
            element->points[1].x, element->points[1].y,
            element->points[2].x, element->points[2].y);
        break;
    case kCGPathElementCloseSubpath:
        CGPathCloseSubpath(ctx->path);
        break;
    }
}

CGMutablePathRef CGPathCreateMutableCopyByTransformingPath(CGPathRef path,
    const CGAffineTransform* transform)
{
    PathTransformContext ctx;
    if (!path)
        return NULL;
    ctx.path = CGPathCreateMutable();
    ctx.transform = transform;
    CGPathApply(path, &ctx, transformPathElement);
    return ctx.path;
}

CGPathRef CGPathCreateCopyByTransformingPath(CGPathRef path, const CGAffineTransform* transform)
{
    return CGPathCreateMutableCopyByTransformingPath(path, transform);
}

CGRect CGPathGetPathBoundingBox(CGPathRef path)
{
    /* ponytail: Tiger only has the control-point bounding box, so curves report
       slightly large. Flatten in CGPathApply if a caller needs it tight. */
    return CGPathGetBoundingBox(path);
}

/* ================================================================== context */

CGContextRef CGBitmapContextCreateWithData(void* data, size_t width, size_t height,
    size_t bitsPerComponent, size_t bytesPerRow, CGColorSpaceRef cs, uint32_t bitmapInfo,
    CGBitmapContextReleaseDataCallback releaseCallback, void* releaseInfo)
{
    /* ponytail: Tiger's CGBitmapContextCreate has no release hook, so the
       callback never fires and the caller keeps ownership of the buffer. Every
       WebCore call site here passes a buffer it frees itself. */
    (void)releaseCallback;
    (void)releaseInfo;
    return CGBitmapContextCreate(data, width, height, bitsPerComponent, bytesPerRow, cs, bitmapInfo);
}

CGColorSpaceRef CGContextGetColorSpace(CGContextRef context)
{
    if (!context || CGContextGetType(context) != kCGContextTypeBitmap)
        return NULL;
    return CGBitmapContextGetColorSpace(context);
}

void CGContextBeginTransparencyLayerWithRect(CGContextRef context, CGRect rect, CFDictionaryRef info)
{
    /* CG clips to the rect for the life of the layer; EndTransparencyLayer pops
       the state the layer pushed, so clipping here is scoped correctly. */
    CGContextBeginTransparencyLayer(context, info);
    CGContextClipToRect(context, rect);
}

void CGContextStrokeArc(CGContextRef context, CGPoint center, CGFloat radius, CGFloat startAngle,
    CGFloat endAngle, int clockwise)
{
    CGContextBeginPath(context);
    CGContextAddArc(context, center.x, center.y, radius, startAngle, endAngle, clockwise);
    CGContextStrokePath(context);
}

void CGContextDrawPathDirect(CGContextRef context, CGPathDrawingMode mode, CGPathRef path,
    const CGRect* boundingBox)
{
    (void)boundingBox; /* an optimization hint only */
    CGContextBeginPath(context);
    CGContextAddPath(context, path);
    CGContextDrawPath(context, mode);
}

void CGContextDrawTiledImage(CGContextRef context, CGRect rect, CGImageRef image)
{
    CGRect clip;
    CGFloat x, y;
    if (!image || CGRectGetWidth(rect) <= 0 || CGRectGetHeight(rect) <= 0)
        return;
    clip = CGContextGetClipBoundingBox(context);
    if (CGRectIsEmpty(clip) || CGRectIsInfinite(clip))
        return;
    /* Snap the tile origin to the lattice rect defines, then cover the clip.
       ponytail: a plain draw loop, not a CGPattern. Fine for the one WebCore
       call site; move to CGPattern if a huge clip ever makes this slow. */
    x = rect.origin.x + floorf((CGRectGetMinX(clip) - rect.origin.x) / CGRectGetWidth(rect))
        * CGRectGetWidth(rect);
    for (; x < CGRectGetMaxX(clip); x += CGRectGetWidth(rect)) {
        y = rect.origin.y + floorf((CGRectGetMinY(clip) - rect.origin.y) / CGRectGetHeight(rect))
            * CGRectGetHeight(rect);
        for (; y < CGRectGetMaxY(clip); y += CGRectGetHeight(rect))
            CGContextDrawImage(context, CGRectMake(x, y, CGRectGetWidth(rect), CGRectGetHeight(rect)), image);
    }
}

/* Font rendering state Tiger does not carry. The setters drop the request; the
   getters answer with what Tiger's rasterizer actually does, which is integral
   glyph positions and no subpixel quantization. */
void CGContextSetAllowsFontSubpixelPositioning(CGContextRef c, bool v) { (void)c; (void)v; }
bool CGContextGetAllowsFontSubpixelPositioning(CGContextRef c) { (void)c; return false; }
void CGContextSetAllowsFontSubpixelQuantization(CGContextRef c, bool v) { (void)c; (void)v; }
bool CGContextGetAllowsFontSubpixelQuantization(CGContextRef c) { (void)c; return false; }
void CGContextSetShouldSubpixelPositionFonts(CGContextRef c, bool v) { (void)c; (void)v; }
void CGContextSetShouldSubpixelQuantizeFonts(CGContextRef c, bool v) { (void)c; (void)v; }

void CGContextSetShouldAntialiasFonts(CGContextRef context, bool shouldAntialias)
{
    /* Closest Tiger equivalent: font smoothing. Turning it off still leaves
       grayscale antialiasing on, so text never goes fully aliased. */
    CGContextSetShouldSmoothFonts(context, shouldAntialias);
}

void CGContextSetFontAntialiasingStyle(CGContextRef c, CGFontAntialiasingStyle s) { (void)c; (void)s; }
CGFontAntialiasingStyle CGContextGetFontAntialiasingStyle(CGContextRef c)
{
    (void)c;
    return kCGFontAntialiasingStyleUnfiltered;
}
bool CGFontRenderingGetFontSmoothingDisabled(void) { return false; }

/* ================================================================== ImageIO */

size_t CGImageSourceGetPrimaryImageIndex(CGImageSourceRef source) { (void)source; return 0; }

CFDictionaryRef CGImageSourceCopyAuxiliaryDataInfoAtIndexWithOptions(CGImageSourceRef source,
    size_t index, CFStringRef auxiliaryImageDataType, CFDictionaryRef options)
{
    /* Gain maps and depth data postdate Tiger's ImageIO by a decade. */
    (void)source; (void)index; (void)auxiliaryImageDataType; (void)options;
    return NULL;
}

void CGImageSourceSetAllowableTypes(CFArrayRef allowableTypes) { (void)allowableTypes; }
void CGImageSourceDisableHardwareDecoding(CGImageSourceRef source) { (void)source; }
void CGImageSourceEnableRestrictedDecoding(void) { }

/* ============================================================ image caching */

void CGImageSetCachingFlags(CGImageRef image, CGImageCachingFlags flags) { (void)image; (void)flags; }
CGImageCachingFlags CGImageGetCachingFlags(CGImageRef image) { (void)image; return kCGImageCachingDefault; }
void CGImageSetProperty(CGImageRef image, CFStringRef key, CFTypeRef value)
{
    (void)image; (void)key; (void)value;
}
