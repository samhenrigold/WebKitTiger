/*
 * cgtest.c -- exercises compat/cgcompat.c on the real 10.4.11 box.
 *
 * Build (from the repo root, in bash, after `make -C compat install`):
 *   toolchain/bin/tiger-clang -O1 -o build/cgtest spike/cgtest.c \
 *     -F compat/sdk-overlay \
 *     -F sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks \
 *     -ltigercompat -framework ApplicationServices
 */

/* Deliberately includes only the framework headers, the way WebCore does: the
   shims must arrive through the SDK overlay, not through a direct include of
   TigerCompat/CGCompat.h. */
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int gFailures;

static void expect(int ok, const char* what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++gFailures;
}

#define W 64
#define H 64

static unsigned char* gPixels;

/* Bitmap is BGRA premultiplied little-endian, so byte 0 is blue. Coordinates
   are CG's, origin bottom-left, so the row index is flipped. */
static void pixelAt(int x, int y, int* r, int* g, int* b, int* a)
{
    const unsigned char* p = gPixels + (size_t)(H - 1 - y) * W * 4 + (size_t)x * 4;
    *b = p[0]; *g = p[1]; *r = p[2]; *a = p[3];
}

static int near(int value, int expected, int tolerance)
{
    return value >= expected - tolerance && value <= expected + tolerance;
}

static void clear(CGContextRef context)
{
    CGContextClearRect(context, CGRectMake(0, 0, W, H));
}

int main(void)
{
    CGColorSpaceRef srgb, again, gray;
    CGContextRef context;
    CGGradientRef gradient;
    CGFloat components[8], locations[2];
    CGPathRef path;
    CGRect box;
    int r, g, b, a;

    /* ---- colorspace ---- */
    srgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    expect(srgb != NULL, "CGColorSpaceCreateWithName(kCGColorSpaceSRGB) returns a colorspace");
    expect(CGColorSpaceGetNumberOfComponents(srgb) == 3, "sRGB has 3 components");
    expect(CGColorSpaceGetModel(srgb) == kCGColorSpaceModelRGB, "CGColorSpaceGetModel says RGB");
    expect(CGColorSpaceGetName(srgb) == kCGColorSpaceSRGB || CFEqual(CGColorSpaceGetName(srgb), kCGColorSpaceSRGB),
        "CGColorSpaceGetName round-trips");
    expect(!CGColorSpaceUsesExtendedRange(srgb), "CGColorSpaceUsesExtendedRange is false");
    expect(CGColorSpaceSupportsOutput(srgb), "CGColorSpaceSupportsOutput is true");
    again = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    expect(again == srgb, "named colorspaces are cached");
    CGColorSpaceRelease(again);
    gray = CGColorSpaceCreateWithName(kCGColorSpaceGenericGray);
    expect(gray && CGColorSpaceGetModel(gray) == kCGColorSpaceModelMonochrome,
        "GenericGray still resolves through Tiger and reports Monochrome");
    CGColorSpaceRelease(gray);

    /* ---- bitmap context in sRGB ---- */
    gPixels = (unsigned char*)calloc(W * H, 4);
    context = CGBitmapContextCreateWithData(gPixels, W, H, 8, W * 4, srgb,
        kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little, NULL, NULL);
    expect(context != NULL, "CGBitmapContextCreateWithData makes an sRGB context");
    if (!context)
        return 1;
    expect(CGContextGetColorSpace(context) != NULL, "CGContextGetColorSpace returns the bitmap's space");

    /* ---- linear gradient: red at x=0 to blue at x=W ---- */
    components[0] = 1; components[1] = 0; components[2] = 0; components[3] = 1;
    components[4] = 0; components[5] = 0; components[6] = 1; components[7] = 1;
    locations[0] = 0; locations[1] = 1;
    gradient = CGGradientCreateWithColorComponentsAndOptions(srgb, components, locations, 2, NULL);
    expect(gradient != NULL, "CGGradientCreateWithColorComponentsAndOptions");
    expect(CGGradientRetain(gradient) == gradient, "CGGradientRetain returns the same object");
    CGGradientRelease(gradient);

    clear(context);
    CGContextDrawLinearGradient(context, gradient, CGPointMake(0, 0), CGPointMake(W, 0),
        kCGGradientDrawsBeforeStartLocation | kCGGradientDrawsAfterEndLocation);
    pixelAt(1, H / 2, &r, &g, &b, &a);
    expect(near(r, 255, 12) && near(b, 0, 12) && near(a, 255, 2), "linear gradient is red at the start");
    pixelAt(W - 2, H / 2, &r, &g, &b, &a);
    expect(near(r, 0, 12) && near(b, 255, 12), "linear gradient is blue at the end");
    pixelAt(W / 2, H / 2, &r, &g, &b, &a);
    expect(near(r, 128, 30) && near(b, 128, 30), "linear gradient is halfway at the midpoint");

    /* ---- radial gradient centred in the bitmap ---- */
    clear(context);
    CGContextDrawRadialGradient(context, gradient, CGPointMake(W / 2, H / 2), 0,
        CGPointMake(W / 2, H / 2), W / 2, kCGGradientDrawsAfterEndLocation);
    pixelAt(W / 2, H / 2, &r, &g, &b, &a);
    expect(near(r, 255, 12) && near(b, 0, 12), "radial gradient is red at the centre");
    pixelAt(1, H / 2, &r, &g, &b, &a);
    expect(near(b, 255, 20) && near(r, 0, 20), "radial gradient is blue at the rim");

    /* ---- conic gradient just has to paint something everywhere ---- */
    clear(context);
    CGContextDrawConicGradient(context, gradient, CGPointMake(W / 2, H / 2), 0);
    pixelAt(W / 2, 4, &r, &g, &b, &a);
    expect(a > 250 && (r > 40 || b > 40), "conic gradient covers the clip opaquely");
    /* A quarter turn counterclockwise from the zero angle is a quarter of the
       way from red to blue. The zero angle itself is the wrap seam where t=0
       and t=1 meet, so it is not a meaningful sample. */
    pixelAt(W / 2, H - 5, &r, &g, &b, &a);
    expect(a > 250 && near(r, 191, 24) && near(b, 64, 24),
        "conic gradient is a quarter of the way along at a quarter turn");

    CGGradientRelease(gradient);

    /* ---- premultiplied interpolation: red to transparent must not darken ---- */
    {
        /* Red to CSS `transparent`, which is transparent BLACK. That is what
           makes the two interpolation modes differ: with transparent red the
           colour is constant and both modes agree. */
        CGFloat fade[8] = { 1, 0, 0, 1, 0, 0, 0, 0 };
        CFTypeRef keys[] = { kCGGradientInterpolatesPremultiplied };
        CFTypeRef values[] = { kCFBooleanTrue };
        CFDictionaryRef options = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CGGradientRef premul = CGGradientCreateWithColorComponentsAndOptions(srgb, fade,
            locations, 2, options);
        CGGradientRef straight = CGGradientCreateWithColorComponentsAndOptions(srgb, fade,
            locations, 2, NULL);

        clear(context);
        CGContextDrawLinearGradient(context, premul, CGPointMake(0, 0), CGPointMake(W, 0), 0);
        pixelAt(W / 2, H / 2, &r, &g, &b, &a);
        /* Premultiplied against a cleared backdrop: the stored pixel is colour
           times alpha, so red stays at full strength for its alpha. */
        expect(near(a, 128, 20) && near(r, a, 14),
            "premultiplied red-to-transparent keeps full red at the midpoint");

        clear(context);
        CGContextDrawLinearGradient(context, straight, CGPointMake(0, 0), CGPointMake(W, 0), 0);
        pixelAt(W / 2, H / 2, &r, &g, &b, &a);
        expect(near(a, 128, 20) && r < a - 20,
            "unpremultiplied interpolation still darkens, as asked");

        CGGradientRelease(premul);
        CGGradientRelease(straight);
        CFRelease(options);
    }

    /* ---- colorspace names must not alias onto one object ---- */
    {
        CGColorSpaceRef linear = CGColorSpaceCreateWithName(kCGColorSpaceGenericRGBLinear);
        CGColorSpaceRef generic = CGColorSpaceCreateWithName(kCGColorSpaceGenericRGB);
        CGColorSpaceRef xyz = CGColorSpaceCreateWithName(kCGColorSpaceGenericXYZ);
        expect(linear != generic && xyz != generic && linear != xyz,
            "GenericRGBLinear, GenericRGB and GenericXYZ are distinct objects");
        expect(CFEqual(CGColorSpaceGetName(generic), kCGColorSpaceGenericRGB)
            && CFEqual(CGColorSpaceGetName(linear), kCGColorSpaceGenericRGBLinear),
            "each name reports itself back");
        {
            CFPropertyListRef plist = CGColorSpaceCopyPropertyList(linear);
            CGColorSpaceRef back = CGColorSpaceCreateWithPropertyList(plist);
            expect(back == linear, "property list round trip returns the same colorspace");
            CGColorSpaceRelease(back);
            if (plist)
                CFRelease(plist);
        }
        CGColorSpaceRelease(linear);
        CGColorSpaceRelease(generic);
        CGColorSpaceRelease(xyz);
    }

    /* ---- colorspace model, including the two the component count cannot see ---- */
    {
        unsigned char table[12] = { 0 };
        CGColorSpaceRef device = CGColorSpaceCreateDeviceRGB();
        CGColorSpaceRef indexed = CGColorSpaceCreateIndexed(device, 3, table);
        CGColorSpaceRef pattern = CGColorSpaceCreatePattern(device);
        CGColorSpaceRef cmyk = CGColorSpaceCreateDeviceCMYK();
        expect(CGColorSpaceGetModel(indexed) == kCGColorSpaceModelIndexed,
            "CGColorSpaceGetModel reports Indexed");
        expect(CGColorSpaceGetModel(pattern) == kCGColorSpaceModelPattern,
            "CGColorSpaceGetModel reports Pattern");
        expect(CGColorSpaceGetModel(device) == kCGColorSpaceModelRGB
            && CGColorSpaceGetModel(cmyk) == kCGColorSpaceModelCMYK
            && CGColorSpaceGetModel(NULL) == kCGColorSpaceModelUnknown,
            "CGColorSpaceGetModel still reports RGB, CMYK and Unknown");
        CGColorSpaceRelease(indexed);
        CGColorSpaceRelease(pattern);
        CGColorSpaceRelease(cmyk);
        CGColorSpaceRelease(device);
    }

    /* ---- uneven corners: a lopsided pair that fits must survive ---- */
    {
        CGSize corners[4];
        CGMutablePathRef uneven = CGPathCreateMutable();
        corners[0].width = 80; corners[0].height = 10;
        corners[1].width = 20; corners[1].height = 10;
        corners[2].width = 10; corners[2].height = 10;
        corners[3].width = 10; corners[3].height = 10;
        CGPathAddUnevenCornersRoundedRect(uneven, NULL, CGRectMake(0, 0, 100, 60), corners);
        /* 80 and 20 sum to exactly the 100-wide side, so neither is reduced.
           The old half-clamp squashed the 80 to 50, which showed up as the
           corner at (60, 59) being inside the path. */
        expect(!CGPathContainsPoint(uneven, NULL, CGPointMake(20, 57), false),
            "an 80px corner radius is not squashed to half the rect");
        /* The 20px corner's ellipse is centred at (80, 50) with radii 20 by 10,
           so these two points bracket it. */
        expect(CGPathContainsPoint(uneven, NULL, CGPointMake(85, 55), false)
            && !CGPathContainsPoint(uneven, NULL, CGPointMake(99, 59), false),
            "the 20px corner on the same side is still a 20px corner");
        CGPathRelease(uneven);
    }

    /* ---- rounded rect path ---- */
    path = CGPathCreateWithRoundedRect(CGRectMake(8, 8, 48, 48), 12, 12, NULL);
    expect(path != NULL, "CGPathCreateWithRoundedRect");
    box = CGPathGetPathBoundingBox(path);
    expect(near((int)box.origin.x, 8, 1) && near((int)box.size.width, 48, 1),
        "rounded rect bounding box matches the rect");
    expect(!CGPathContainsPoint(path, NULL, CGPointMake(9, 9), false),
        "the rounded corner is outside the path");
    expect(CGPathContainsPoint(path, NULL, CGPointMake(32, 32), false),
        "the rounded rect centre is inside the path");

    clear(context);
    {
        CGColorRef green = CGColorCreateSRGB(0, 1, 0, 1);
        CGContextSetFillColorWithColor(context, green);
        CGColorRelease(green);
    }
    CGContextDrawPathDirect(context, kCGPathFill, path, NULL);
    pixelAt(32, 32, &r, &g, &b, &a);
    expect(near(g, 255, 4) && near(r, 0, 4), "CGContextDrawPathDirect fills the rounded rect");
    pixelAt(9, 9, &r, &g, &b, &a);
    expect(a < 40, "the rounded corner stays unpainted");

    /* ---- transformed copy ---- */
    {
        CGAffineTransform translate = CGAffineTransformMakeTranslation(10, 20);
        CGPathRef moved = CGPathCreateCopyByTransformingPath(path, &translate);
        CGRect movedBox = CGPathGetPathBoundingBox(moved);
        expect(near((int)movedBox.origin.x, 18, 1) && near((int)movedBox.origin.y, 28, 1),
            "CGPathCreateCopyByTransformingPath applies the transform");
        CGPathRelease(moved);
    }
    CGPathRelease(path);

    /* ---- rect path ---- */
    {
        CGPathRef rectPath = CGPathCreateWithRect(CGRectMake(4, 4, 10, 10), NULL);
        CGRect rectBox = CGPathGetPathBoundingBox(rectPath);
        expect(CGRectEqualToRect(rectBox, CGRectMake(4, 4, 10, 10)), "CGPathCreateWithRect");
        CGPathRelease(rectPath);
    }

    /* ---- constant colors ---- */
    expect(CGColorGetConstantColor(kCGColorBlack) != NULL, "CGColorGetConstantColor(black)");
    expect(CGColorGetAlpha(CGColorGetConstantColor(kCGColorClear)) == 0, "clear is transparent");
    {
        CGColorRef c = CGColorCreateSRGB(1, 0.5f, 0.25f, 1);
        expect(c && near((int)(CGColorGetComponents(c)[1] * 255), 127, 2), "CGColorCreateSRGB");
        CGColorRelease(c);
    }

    /* ---- font state no-ops must not crash ---- */
    CGContextSetAllowsFontSubpixelPositioning(context, true);
    CGContextSetAllowsFontSubpixelQuantization(context, true);
    CGContextSetShouldAntialiasFonts(context, true);
    CGContextSetFontAntialiasingStyle(context, kCGFontAntialiasingStyleFilterLight);
    expect(!CGContextGetAllowsFontSubpixelPositioning(context)
        && CGContextGetFontAntialiasingStyle(context) == kCGFontAntialiasingStyleUnfiltered,
        "font subpixel/antialiasing shims report Tiger's real behaviour");

    /* ---- transparency layer with rect ---- */
    clear(context);
    CGContextBeginTransparencyLayerWithRect(context, CGRectMake(0, 0, 16, 16), NULL);
    {
        CGColorRef yellow = CGColorCreateSRGB(1, 1, 0, 1);
        CGContextSetFillColorWithColor(context, yellow);
        CGColorRelease(yellow);
    }
    CGContextFillRect(context, CGRectMake(0, 0, W, H));
    CGContextEndTransparencyLayer(context);
    pixelAt(4, 4, &r, &g, &b, &a);
    expect(near(r, 255, 4) && near(g, 255, 4) && near(b, 0, 4), "transparency layer paints inside its rect");
    pixelAt(40, 40, &r, &g, &b, &a);
    expect(a < 40, "transparency layer clips to its rect");

    /* ---- ImageIO: decode a PNG ---- */
    {
        CFURLRef url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,
            (const UInt8*)"/tmp/cgtest.png", strlen("/tmp/cgtest.png"), false);
        CGImageSourceRef source = CGImageSourceCreateWithURL(url, NULL);
        expect(source != NULL, "CGImageSourceCreateWithURL on Tiger's ImageIO");
        if (source) {
            CGImageRef image;
            expect(CGImageSourceGetCount(source) >= 1, "CGImageSourceGetCount");
            expect(CGImageSourceGetPrimaryImageIndex(source) == 0, "CGImageSourceGetPrimaryImageIndex shim");
            expect(CGImageSourceCopyAuxiliaryDataInfoAtIndexWithOptions(source, 0,
                kCGImageAuxiliaryDataTypeHDRGainMap, NULL) == NULL,
                "CGImageSourceCopyAuxiliaryDataInfoAtIndexWithOptions returns NULL");
            image = CGImageSourceCreateImageAtIndex(source, 0, NULL);
            expect(image != NULL, "CGImageSourceCreateImageAtIndex decodes the PNG");
            if (image) {
                printf("      decoded %zux%zu\n", CGImageGetWidth(image), CGImageGetHeight(image));
                expect(CGImageGetWidth(image) > 0 && CGImageGetHeight(image) > 0,
                    "decoded PNG has a plausible size");
                CGImageSetCachingFlags(image, kCGImageCachingTransient);
                expect(CGImageGetCachingFlags(image) == kCGImageCachingDefault,
                    "CGImageSetCachingFlags/GetCachingFlags shims");
                CGImageRelease(image);
            }
            CFRelease(source);
        }
        CGImageSourceSetAllowableTypes(NULL);
        if (url)
            CFRelease(url);
    }

    CGContextRelease(context);
    CGColorSpaceRelease(srgb);
    free(gPixels);

    printf("%s: %d failure(s)\n", gFailures ? "FAILED" : "ALL PASS", gFailures);
    return gFailures ? 1 : 0;
}
