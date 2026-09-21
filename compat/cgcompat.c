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
    if (CFEqual(name, kCGColorSpaceGenericRGBLinear) || CFEqual(name, kCGColorSpaceGenericXYZ)) {
        /* Not forwarded to Tiger's GenericRGB on purpose. Tiger caches named
           colorspaces and hands back the *same* object for every name it
           recognises, so forwarding two of our names there would make them
           share one pointer and CGColorSpaceGetName could then answer with the
           wrong one. A fresh ICC-based space keeps every name distinct. */
        return createSRGBFromColorSync();
    }
    if (CFEqual(name, kCGColorSpaceGenericGrayGamma2_2))
        return CGColorSpaceCreateDeviceGray();
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
    /* Never record a second name for a colorspace already in the table: the
       name lookup below is by pointer, so an alias would make it ambiguous and
       break the CopyPropertyList/CreateWithPropertyList round trip. */
    for (i = 0; i < sNamedSpaceCount; ++i) {
        if (sNamedSpaces[i].cs == cs)
            return cs;
    }
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

/* Tiger's CGColorSpace struct, i386, verified on the box across every kind of
   colorspace its API can build (spike/cgtest.c re-checks the layout at runtime):

     +0x0c  kind:  0 DeviceGray  1 DeviceRGB  2 DeviceCMYK  3 CalibratedGray
                   4 CalibratedRGB  5 Lab  6 ICCBased  7 Indexed  9 Pattern
     +0x10  model, already in Apple's CGColorSpaceModel numbering for
            everything except Indexed and Pattern, which store -1
     +0x14  number of components, the value CGColorSpaceGetNumberOfComponents
            returns

   Reading it is what makes Indexed and Pattern reportable at all; the component
   count alone cannot distinguish them from their base space, and WebCore does
   test for Indexed. The +0x14 cross-check against the public accessor is the
   guard: if the layout is ever not what we expect, fall back to the count. */
#define CS_FIELD_KIND 3       /* +0x0c, in ints */
#define CS_FIELD_MODEL 4      /* +0x10 */
#define CS_FIELD_NCOMPONENTS 5 /* +0x14 */

static CGColorSpaceModel modelFromComponentCount(CGColorSpaceRef cs)
{
    switch (CGColorSpaceGetNumberOfComponents(cs)) {
    case 1: return kCGColorSpaceModelMonochrome;
    case 3: return kCGColorSpaceModelRGB;
    case 4: return kCGColorSpaceModelCMYK;
    default: return kCGColorSpaceModelUnknown;
    }
}

CGColorSpaceModel CGColorSpaceGetModel(CGColorSpaceRef cs)
{
    const int* fields;
    int kind, model;

    if (!cs)
        return kCGColorSpaceModelUnknown;

    fields = (const int*)cs;
    if ((size_t)fields[CS_FIELD_NCOMPONENTS] != CGColorSpaceGetNumberOfComponents(cs))
        return modelFromComponentCount(cs);

    kind = fields[CS_FIELD_KIND];
    if (kind == 7)
        return kCGColorSpaceModelIndexed;
    if (kind == 9)
        return kCGColorSpaceModelPattern;

    model = fields[CS_FIELD_MODEL];
    if (model < kCGColorSpaceModelMonochrome || model > kCGColorSpaceModelLab)
        return modelFromComponentCount(cs);
    return (CGColorSpaceModel)model;
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
    int premultiplied;     /* kCGGradientInterpolatesPremultiplied was asked for */
    int alphaOnly;         /* evaluate into a single gray component: the alpha curve */
    /* stopCount * (componentCount + 1) colour+alpha floats, then stopCount
       locations. */
    CGFloat values[1];
} GradientStops;

static CGGradientRef createGradient(CGColorSpaceRef cs, const CGFloat* components,
    const CGFloat* locations, size_t count, int premultiplied)
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
    stops->premultiplied = premultiplied;
    stops->alphaOnly = 0;
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
    return createGradient(cs, components, locations, count, 0);
}

/* kCGGradientInterpolatesPremultiplied set to true is the only option Apple
   defines for the AndOptions creators. */
static int optionsAskForPremultiplied(CFDictionaryRef options)
{
    CFTypeRef value;
    if (!options)
        return 0;
    value = CFDictionaryGetValue(options, kCGGradientInterpolatesPremultiplied);
    return value && CFEqual(value, kCFBooleanTrue);
}

CGGradientRef CGGradientCreateWithColorComponentsAndOptions(CGColorSpaceRef cs,
    const CGFloat* components, const CGFloat* locations, size_t count, CFDictionaryRef options)
{
    return createGradient(cs, components, locations, count,
        optionsAskForPremultiplied(options));
}

/* Colours are read in their own space and copied straight across, widening
   gray to RGB when needed. */
static CGGradientRef createGradientFromColors(CGColorSpaceRef cs, CFArrayRef colors,
    const CGFloat* locations, int premultiplied)
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
    gradient = createGradient(space, components, locations, (size_t)count, premultiplied);
    free(components);
    CGColorSpaceRelease(space);
    return gradient;
}

CGGradientRef CGGradientCreateWithColors(CGColorSpaceRef cs, CFArrayRef colors, const CGFloat* locations)
{
    return createGradientFromColors(cs, colors, locations, 0);
}

CGGradientRef CGGradientCreateWithColorsAndOptions(CGColorSpaceRef cs, CFArrayRef colors,
    const CGFloat* locations, CFDictionaryRef options)
{
    return createGradientFromColors(cs, colors, locations, optionsAskForPremultiplied(options));
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

/* Writes the interpolated alpha as a single gray component, for the mask that
   carries what Tiger's CGShading throws away. */
static void evaluateGradientAlpha(void* info, const float* in, float* out)
{
    const GradientStops* s = (const GradientStops*)info;
    size_t stride = s->componentCount + 1;
    const CGFloat* locations = s->values + s->stopCount * stride;
    float t = in[0];
    size_t i, lo, hi;
    float span, f, alphaLo, alphaHi;

    if (t <= locations[0]) {
        out[0] = s->values[s->componentCount];
        return;
    }
    if (t >= locations[s->stopCount - 1]) {
        out[0] = s->values[(s->stopCount - 1) * stride + s->componentCount];
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
    alphaLo = s->values[lo * stride + s->componentCount];
    alphaHi = s->values[hi * stride + s->componentCount];
    out[0] = alphaLo + (alphaHi - alphaLo) * f;
}

static void evaluateGradient(void* info, const float* in, float* out)
{
    const GradientStops* s = (const GradientStops*)info;
    size_t stride = s->componentCount + 1;

    if (s->alphaOnly) {
        evaluateGradientAlpha(info, in, out);
        return;
    }
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

    if (s->premultiplied) {
        /* Interpolate colour scaled by alpha, then divide back out. Without
           this, red to transparent darkens through the middle: the midpoint
           comes out (0.5, 0, 0) at alpha 0.5 instead of (1, 0, 0) at alpha 0.5.
           WebCore asks for this whenever the gradient's alpha premultiplication
           is Premultiplied, which is the CSS default for legacy sRGB. The
           shading function's output stays unpremultiplied, as CG expects. */
        float alphaLo = s->values[lo * stride + s->componentCount];
        float alphaHi = s->values[hi * stride + s->componentCount];
        float alpha = alphaLo + (alphaHi - alphaLo) * f;
        for (c = 0; c < s->componentCount; ++c) {
            float a = s->values[lo * stride + c] * alphaLo;
            float b = s->values[hi * stride + c] * alphaHi;
            float mixed = a + (b - a) * f;
            out[c] = alpha > 0 ? mixed / alpha : 0;
        }
        out[s->componentCount] = alpha;
        return;
    }

    for (c = 0; c < stride; ++c) {
        float a = s->values[lo * stride + c];
        float b = s->values[hi * stride + c];
        out[c] = a + (b - a) * f;
    }
}

static void releaseGradientInfo(void* info) { free(info); }

/* Returns a CGFunction over t in [0,1]. With alphaOnly it has a single gray
   output carrying the alpha curve; otherwise colour plus alpha. */
static CGFunctionRef createGradientFunction(CGGradientRef gradient, CGColorSpaceRef* outSpace,
    int alphaOnly)
{
    static const CGFunctionCallbacks callbacks = { 0, evaluateGradient, releaseGradientInfo };
    const GradientStops* stops = gradientStops(gradient, outSpace);
    size_t stride, outputs, bytes, c;
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
    copy->alphaOnly = alphaOnly;

    outputs = alphaOnly ? 1 : stride;
    for (c = 0; c < outputs; ++c) {
        range[2 * c] = 0;
        range[2 * c + 1] = 1;
    }
    function = CGFunctionCreate(copy, 1, domain, outputs, range, &callbacks);
    if (!function)
        free(copy);
    return function;
}

static int gradientHasTransparency(CGGradientRef gradient)
{
    const GradientStops* s = gradientStops(gradient, NULL);
    size_t stride, i;
    if (!s)
        return 0;
    stride = s->componentCount + 1;
    for (i = 0; i < s->stopCount; ++i) {
        if (s->values[i * stride + s->componentCount] < 1.0f)
            return 1;
    }
    return 0;
}

static CGShadingRef createShading(int radial, CGColorSpaceRef space, CGFunctionRef function,
    CGPoint start, CGFloat startRadius, CGPoint end, CGFloat endRadius,
    CGGradientDrawingOptions options)
{
    bool extendStart = (options & kCGGradientDrawsBeforeStartLocation) != 0;
    bool extendEnd = (options & kCGGradientDrawsAfterEndLocation) != 0;
    if (radial)
        return CGShadingCreateRadial(space, start, startRadius, end, endRadius, function,
            extendStart, extendEnd);
    return CGShadingCreateAxial(space, start, end, function, extendStart, extendEnd);
}

/* Tiger's CGShading always paints opaque: the alpha component its function
   returns is ignored, whatever the range dimension or colorspace. Verified on
   the box with a constant alpha of 0.5 in both DeviceRGB and ICC sRGB, which
   still came back fully opaque.

   So a gradient with any transparent stop is composed by hand. CG still does
   all the geometry, which is the part worth keeping: the colour ramp is drawn
   into an opaque RGB bitmap and the alpha ramp into a gray bitmap, using the
   same shading in both passes, and the two are combined into a premultiplied
   RGBA image that gets drawn into the clip.

   CGContextClipToMask would also work, and the reason this does not use it is
   historical rather than technical. While building this path a mask-clipped
   draw looked like it was giving the destination an alpha of mask times source
   colour; a later direct probe of ClipToMask, both here and on the audit track,
   showed it is correct with a DeviceGray non-alpha mask and that the earlier
   reading was of the premultiplied colour channel, which IS colour times mask.
   The two-bitmap composite is kept because it is measured and passing, not
   because ClipToMask is broken.

   If this is ever revisited, note the constraint the audit track measured:
   Tiger's ClipToMask accepts only a DeviceGray non-alpha image. A
   CGImageMaskCreate stencil or an RGBA image clips everything away, silently.

   ponytail: the composite is rasterized at the clip's device size, capped
   below. An opaque gradient skips all of this and draws the shading directly. */
#define GRADIENT_MAX_SIDE 2048

static void drawTransparentGradient(CGContextRef context, CGGradientRef gradient, int radial,
    CGPoint start, CGFloat startRadius, CGPoint end, CGFloat endRadius,
    CGGradientDrawingOptions options, CGColorSpaceRef space)
{
    CGRect clip = CGContextGetClipBoundingBox(context);
    CGAffineTransform ctm = CGContextGetCTM(context);
    CGColorSpaceRef gray = NULL, rgbSpace = NULL;
    CGContextRef colorContext = NULL, alphaContext = NULL;
    CGFunctionRef function;
    CGShadingRef shading;
    CGDataProviderRef provider;
    CGImageRef image;
    unsigned char *colorBits = NULL, *alphaBits = NULL, *outBits = NULL;
    size_t wide, high, x, y, colorRow, alphaRow, outRow;
    CGFloat scaleX, scaleY;

    if (CGRectIsEmpty(clip) || CGRectIsInfinite(clip))
        return;

    scaleX = (CGFloat)hypot(ctm.a, ctm.b);
    scaleY = (CGFloat)hypot(ctm.c, ctm.d);
    if (scaleX <= 0)
        scaleX = 1;
    if (scaleY <= 0)
        scaleY = 1;
    wide = (size_t)ceilf(CGRectGetWidth(clip) * scaleX);
    high = (size_t)ceilf(CGRectGetHeight(clip) * scaleY);
    if (!wide || !high)
        return;
    if (wide > GRADIENT_MAX_SIDE) {
        scaleX = GRADIENT_MAX_SIDE / CGRectGetWidth(clip);
        wide = GRADIENT_MAX_SIDE;
    }
    if (high > GRADIENT_MAX_SIDE) {
        scaleY = GRADIENT_MAX_SIDE / CGRectGetHeight(clip);
        high = GRADIENT_MAX_SIDE;
    }

    colorRow = wide * 4;
    alphaRow = wide;
    outRow = wide * 4;
    colorBits = (unsigned char*)calloc(colorRow * high, 1);
    alphaBits = (unsigned char*)calloc(alphaRow * high, 1);
    outBits = (unsigned char*)calloc(outRow * high, 1);
    if (!colorBits || !alphaBits || !outBits)
        goto done;

    /* Keep the gradient's own colorspace for the colour pass when it is RGB, so
       nothing is converted twice. */
    rgbSpace = (space && CGColorSpaceGetNumberOfComponents(space) == 3)
        ? CGColorSpaceRetain(space) : CGColorSpaceCreateDeviceRGB();
    gray = CGColorSpaceCreateDeviceGray();
    colorContext = CGBitmapContextCreate(colorBits, wide, high, 8, colorRow, rgbSpace,
        kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
    alphaContext = CGBitmapContextCreate(alphaBits, wide, high, 8, alphaRow, gray,
        kCGImageAlphaNone);
    if (!colorContext || !alphaContext)
        goto done;

    CGContextScaleCTM(colorContext, scaleX, scaleY);
    CGContextTranslateCTM(colorContext, -CGRectGetMinX(clip), -CGRectGetMinY(clip));
    CGContextScaleCTM(alphaContext, scaleX, scaleY);
    CGContextTranslateCTM(alphaContext, -CGRectGetMinX(clip), -CGRectGetMinY(clip));

    function = createGradientFunction(gradient, NULL, 0);
    if (!function)
        goto done;
    shading = createShading(radial, rgbSpace, function, start, startRadius, end, endRadius,
        options);
    CGFunctionRelease(function);
    if (shading) {
        CGContextDrawShading(colorContext, shading);
        CGShadingRelease(shading);
    }

    function = createGradientFunction(gradient, NULL, 1);
    if (!function)
        goto done;
    shading = createShading(radial, gray, function, start, startRadius, end, endRadius, options);
    CGFunctionRelease(function);
    if (shading) {
        CGContextDrawShading(alphaContext, shading);
        CGShadingRelease(shading);
    }

    /* Both bitmaps are BGRx and gray at the same size; combine into premultiplied BGRA. */
    for (y = 0; y < high; ++y) {
        const unsigned char* src = colorBits + y * colorRow;
        const unsigned char* a = alphaBits + y * alphaRow;
        unsigned char* dst = outBits + y * outRow;
        for (x = 0; x < wide; ++x) {
            unsigned alpha = a[x];
            dst[x * 4 + 0] = (unsigned char)((src[x * 4 + 0] * alpha + 127) / 255);
            dst[x * 4 + 1] = (unsigned char)((src[x * 4 + 1] * alpha + 127) / 255);
            dst[x * 4 + 2] = (unsigned char)((src[x * 4 + 2] * alpha + 127) / 255);
            dst[x * 4 + 3] = (unsigned char)alpha;
        }
    }

    provider = CGDataProviderCreateWithData(NULL, outBits, outRow * high, NULL);
    if (provider) {
        image = CGImageCreate(wide, high, 8, 32, outRow, rgbSpace,
            kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little, provider, NULL, false,
            kCGRenderingIntentDefault);
        CGDataProviderRelease(provider);
        if (image) {
            CGContextDrawImage(context, clip, image);
            CGImageRelease(image);
        }
    }

done:
    if (colorContext)
        CGContextRelease(colorContext);
    if (alphaContext)
        CGContextRelease(alphaContext);
    CGColorSpaceRelease(gray);
    CGColorSpaceRelease(rgbSpace);
    free(colorBits);
    free(alphaBits);
    free(outBits);
}

static void drawGradient(CGContextRef context, CGGradientRef gradient, int radial,
    CGPoint start, CGFloat startRadius, CGPoint end, CGFloat endRadius,
    CGGradientDrawingOptions options)
{
    CGColorSpaceRef space = NULL;
    CGFunctionRef function;
    CGShadingRef shading;

    if (!gradientStops(gradient, &space))
        return;

    if (gradientHasTransparency(gradient)) {
        drawTransparentGradient(context, gradient, radial, start, startRadius, end, endRadius,
            options, space);
        return;
    }

    function = createGradientFunction(gradient, &space, 0);
    if (!function)
        return;
    shading = createShading(radial, space, function, start, startRadius, end, endRadius, options);
    CGFunctionRelease(function);
    if (!shading)
        return;
    CGContextDrawShading(context, shading);
    CGShadingRelease(shading);
}

void CGContextDrawLinearGradient(CGContextRef context, CGGradientRef gradient, CGPoint start,
    CGPoint end, CGGradientDrawingOptions options)
{
    drawGradient(context, gradient, 0, start, 0, end, 0, options);
}

void CGContextDrawRadialGradient(CGContextRef context, CGGradientRef gradient, CGPoint startCenter,
    CGFloat startRadius, CGPoint endCenter, CGFloat endRadius, CGGradientDrawingOptions options)
{
    drawGradient(context, gradient, 1, startCenter, startRadius, endCenter, endRadius, options);
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
/* Shrink two radii that share one side of the rect so they fit on it, keeping
   their ratio. An asymmetric pair that already fits, such as 80 and 20 on a
   100-wide box, is left exactly as asked. */
static void fitRadiiToSide(CGFloat* a, CGFloat* b, CGFloat side)
{
    CGFloat sum = *a + *b;
    if (sum > side && sum > 0) {
        CGFloat scale = side / sum;
        *a *= scale;
        *b *= scale;
    }
}

void CGPathAddUnevenCornersRoundedRect(CGMutablePathRef path, const CGAffineTransform* transform,
    CGRect rect, const CGSize corners[4])
{
    /* Corner order is the one WebCore's addUnevenCornersRoundedRect fills in:
       index 0 and 1 share the maxY side, 2 and 3 share the minY side, 0 and 3
       share minX, 1 and 2 share maxX. WebCore names them bottom-left,
       bottom-right, top-right, top-left because its own space is y-down; in the
       pure coordinates CG sees they are (minX,maxY) (maxX,maxY) (maxX,minY)
       (minX,minY). Do not "correct" this into a top-first order. It would flip
       every asymmetric rounded rect vertically. */
    CGFloat width = CGRectGetWidth(rect), height = CGRectGetHeight(rect);
    CGFloat minX = CGRectGetMinX(rect), maxX = CGRectGetMaxX(rect);
    CGFloat minY = CGRectGetMinY(rect), maxY = CGRectGetMaxY(rect);
    CGSize c[4];
    int i;
    /* Kappa for a quarter ellipse. */
    const CGFloat k = 0.5522847498307933f;

    if (!path || CGRectIsEmpty(rect))
        return;
    for (i = 0; i < 4; ++i) {
        c[i].width = fmaxf(corners[i].width, 0);
        c[i].height = fmaxf(corners[i].height, 0);
    }
    /* Clamp per shared side, not to half the rect. WebCore clamps each radius
       against the rect minus the opposite corner's radius, so a deliberately
       lopsided border-radius is legal and must not be squashed to half. */
    fitRadiiToSide(&c[0].width, &c[1].width, width);
    fitRadiiToSide(&c[2].width, &c[3].width, width);
    fitRadiiToSide(&c[0].height, &c[3].height, height);
    fitRadiiToSide(&c[1].height, &c[2].height, height);
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
    /* Antialiasing has to be off for the loop. Adjacent tiles share an edge, and
       when the tile origin is fractional CG antialiases each tile's edge against
       the backdrop rather than against its neighbour, so roughly 6% of the alpha
       is lost to a seam line at every boundary. Apple's real CGContextDrawTiledImage
       stays fully opaque there. With antialiasing off, a shared edge snaps the
       same way for both tiles, which closes the seam without leaving a gap.

       This matters in practice: GraphicsContextCG.cpp passes a FloatRect straight
       from layout, so any repeated background at a non-integral position or scale
       hits it. Measured on the audit track (spike/cgbehaviour.c): with the origin
       on the integer lattice this loop already byte-matched the host, and only a
       fractional origin diverged, which is what identifies the cause.

       ponytail: a plain draw loop, not a CGPattern. Fine for the one WebCore call
       site; move to CGPattern if a huge clip ever makes this slow. */
    CGContextSaveGState(context);
    CGContextSetShouldAntialias(context, false);
    /* Snap the tile origin to the lattice rect defines, then cover the clip. */
    x = rect.origin.x + floorf((CGRectGetMinX(clip) - rect.origin.x) / CGRectGetWidth(rect))
        * CGRectGetWidth(rect);
    for (; x < CGRectGetMaxX(clip); x += CGRectGetWidth(rect)) {
        y = rect.origin.y + floorf((CGRectGetMinY(clip) - rect.origin.y) / CGRectGetHeight(rect))
            * CGRectGetHeight(rect);
        for (; y < CGRectGetMaxY(clip); y += CGRectGetHeight(rect))
            CGContextDrawImage(context, CGRectMake(x, y, CGRectGetWidth(rect), CGRectGetHeight(rect)), image);
    }
    CGContextRestoreGState(context);
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
    /* A deliberate no-op, and the reason is not that Tiger lacks the knob.

       CGContextSetShouldSmoothFonts, which this used to forward to, is inert:
       on and off give byte-identical pixels and no rendering ever produces a
       colour fringe (spike/fontsmoothtest.c, audit track). So the old mapping
       only looked like it did something.

       There IS a working equivalent with the right semantics.
       CGFontSetShouldAntialias is private but exported, and it is per-font
       rather than context-wide: clearing it takes a glyph run fully aliased
       while shapes in the same context stay smooth. The flag is bit 0 of a byte
       at font+0x3c, with CGFontShouldAntialias reading it back, and
       CGContextGetFont can reach the context's current font.

       It is not wired up because doing so would cost something and buy nothing.
       WebCore's only call site, setCGFontRenderingMode in
       FontCascadeCoreText.cpp:294, passes true unconditionally, and antialiased
       glyphs are already Tiger's default. Against that, the flag mutates the
       CGFont object itself, which is shared and cached, so it would leak into
       every other context using the same font; and WebCore usually sets the
       font after configuring state, so at the moment this is called the context
       may not have the font yet.

       If a caller ever passes false, for instance to support
       -webkit-font-smoothing: none, that is the route to implement: take
       CGContextGetFont, clear the flag, and restore it afterwards. */
    (void)context;
    (void)shouldAntialias;
}

/* Inert because Tiger has nothing to map it to, which was checked rather than
   assumed. Its complete set of smoothing and antialias exports is the context
   Should/Allows pairs and their GState backings, the per-font
   CGFontSetShouldAntialias above, a CGFontAllowsFontSmoothing that takes no
   arguments and reads a process-wide global, and a __CGFontSmoothingMode data
   symbol. There is no CGContextSetFontRenderingStyle and nothing else
   style-shaped, and subpixel smoothing does not work anyway
   (spike/fontsmoothtest.c, audit track; compat/CG-PROBE.md).

   The getter reports Unfiltered because that is what Tiger actually does,
   rather than echoing back whatever was last set. */
void CGContextSetFontAntialiasingStyle(CGContextRef c, CGFontAntialiasingStyle s) { (void)c; (void)s; }
CGFontAntialiasingStyle CGContextGetFontAntialiasingStyle(CGContextRef c)
{
    (void)c;
    return kCGFontAntialiasingStyleUnfiltered;
}
bool CGFontRenderingGetFontSmoothingDisabled(void) { return false; }

/* ====================================================== Tiger ABI mismatch */

/* See CGCompat.h. Tiger returns the matrix by value; WebCore expects a pointer
   into the gstate, so the value is parked in a static and its address returned.
   ponytail: one static, so the result is valid until the next call on this
   thread, which is what the pointer-returning contract implies anyway. Give it
   thread-local storage if the delegate path is ever enabled and threaded. */
const CGAffineTransform* CGGStateGetCTMCompat(CGGStateRef gstate)
{
    static CGAffineTransform sTransform;
    if (!gstate)
        return NULL;
    sTransform = TigerCGGStateGetCTM(gstate);
    return &sTransform;
}

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
