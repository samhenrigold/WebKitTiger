# CoreGraphics / ImageIO survey for Mac OS X 10.4.11 (i386)

Input: `logs/api/missing-CG.txt` (144 names WebCore's CG backend references that are not in
Tiger's CoreGraphics exports), `logs/api/tiger-CG.txt` (3568 CG exports),
`logs/api/tiger-ImageIO.txt` (699 ImageIO exports, generated with `tiger-nm` during this pass),
`logs/api/used-CG.txt` / `used-kCG.txt`.

Implementation: `compat/cgcompat.c`, declarations in `compat/include/TigerCompat/CGCompat.h`.
Test: `spike/cgtest.c`, 40 checks, all passing on the box.

## Counts

| Bucket | Count |
|---|---|
| Not a real gap: macro, typedef, inline, or a WebCore class | 15 |
| Not a real gap: exported by another Tiger framework | 23 |
| Implemented on Tiger's CG | 43 |
| Stubbed (ImageIO additions and image caching) | 8 |
| Gated off: SPI or post-10.4 API the port turns off | 58 |
| Total | 147 |

The total exceeds 144 because `CGContextGetType`, `CGColorSpaceCreateWithName` and the
`CGBlendMode` Porter-Duff values are handled here too. They are not in `missing-CG.txt`:
Tiger exports the first two and the third is an enum, but the 10.4u SDK declares none of them.

## A caveat on the usage counts

`logs/api/used-CG.txt` counts every occurrence of a name in `WebKit/Source`, and for SPI that
WebCore declares itself the declaration in `PAL/pal/spi/cg/CoreGraphicsSPI.h` is one of those
occurrences. A count of 1 or 2 therefore often means "declared, never called". Checking each
implemented name for a file outside `CoreGraphicsSPI.h` that mentions it:

| | Count |
|---|---|
| Implemented names with a real call site | 26 |
| Implemented names that are declaration-only today | 17 |

The declaration-only group is almost entirely constants nothing reads yet, plus
`CGGradientRetain` `CGGradientRelease` `CGGradientGetTypeID` `CGGradientCreateWithColors`
`CGGradientCreateWithColorsAndOptions` and `CGFontRenderingGetFontSmoothingDisabled`. WebCore
retains and releases gradients through `RetainPtr`, which is why the explicit retain and
release are unused; they are one line each and keep the port linking if that changes.

The gradient drawing calls are real: `GradientRendererCG.cpp` calls all three.

Worth applying this check before shimming anything else off the missing list.

## Not a real gap (15)

`CGRectMake` `CGPointMake` `CGSizeMake` are inline functions in the SDK's `CGGeometry.h`.
`CGRect` `CGPoint` `CGSize` `CGAffineTransform` `CGColorSpaceRef` are types.
`CGRound` `CGFloor` `CGCeiling` `CGFAbs` `CGFloatMin` are macros and an inline that WebCore
defines itself in `PAL/pal/spi/cg/CoreGraphicsSPI.h`. `CGContextStateSaver` is a WebCore class.

`CGFloat` is the one entry in this group that needs action: the 10.4u SDK predates it and is
float-based throughout. `CGCompat.h` defines `typedef float CGFloat` plus the `CGFLOAT_*`
macros, which makes every modern CGFloat prototype ABI-identical to Tiger's float one.

## Exported by another Tiger framework (23)

`CGLDescribeRenderer` `CGLDestroyRendererInfo` `CGLQueryRendererInfo` are in
`OpenGL.framework`, not CoreGraphics.

Tiger's ImageIO already has 20 of the names the CG diff flagged, so they need nothing:
`CGImageSourceCreateWithURL` `CreateWithData` `CreateIncremental` `UpdateData`
`CreateImageAtIndex` `CreateThumbnailAtIndex` `GetCount` `GetType` `GetTypeWithData`
`GetStatus` `GetStatusAtIndex` `CopyProperties` `CopyPropertiesAtIndex` `CopyTypeIdentifiers`,
and `CGImageDestinationCreateWithData` `CreateWithDataConsumer` `AddImage`
`AddImageFromSource` `Finalize` `CopyTypeIdentifiers`. Incremental decoding is there, which
matters for WebCore's image loader.

## Implemented (43)

Built on primitives Tiger does export. Verified by `spike/cgtest.c` on the box.

**Gradients.** Tiger has `CGShading` and `CGFunction` but no `CGGradient`. A `CGGradient` here
is a `CFDictionary` holding the colorspace and a `CFData` of stops, so `CFRetain`, `CFRelease`
and `RetainPtr<CGGradientRef>` work with no CFRuntime class to register. Drawing builds a
`CGFunction` that interpolates the stops piecewise-linearly and hands it to
`CGShadingCreateAxial` or `CGShadingCreateRadial`; the drawing options map onto the shading's
extend flags.

`CGGradientCreateWithColorComponents` `CGGradientCreateWithColors`
`CGGradientCreateWithColorComponentsAndOptions` `CGGradientCreateWithColorsAndOptions`
`CGGradientRetain` `CGGradientRelease` `CGGradientGetTypeID` `CGContextDrawLinearGradient`
`CGContextDrawRadialGradient` `CGContextDrawConicGradient`.

`CGContextDrawConicGradient` is the one approximation: CGShading has no conic form, so it
fills 360 one-degree wedges. Two details the test caught. The wedges must overlap, because a
wedge narrower than a pixel never fully covers one. Antialiasing must be off, because two
antialiased fills each covering half a pixel composite to 0.75 alpha, not 1, which left the
whole disc translucent.

**Colorspaces.** Tiger's `CGColorSpaceCreateWithName` knows only `GenericGray`, `GenericRGB`
and `GenericCMYK`. The shim adds the 10.5+ names, building sRGB from the ICC profile ColorSync
ships at `/System/Library/ColorSync/Profiles/sRGB Profile.icc` via `CGColorSpaceCreateICCBased`,
so it is a real sRGB rather than an approximation. Every wide-gamut, linear and extended
variant collapses onto that sRGB: Tiger's CG clamps to [0,1] and has no extended range, so the
only alternatives were sRGB or failing the call.

Because Tiger exports `CGColorSpaceCreateWithName` itself, the shim is named
`TigerCGColorSpaceCreateWithName` and `CGCompat.h` redirects the call with a macro. That keeps
WebCore's source unchanged and avoids a duplicate symbol against the system dylib.

`CGColorSpaceCreateWithName` `CGColorSpaceGetModel` `CGColorSpaceGetName`
`CGColorSpaceGetBaseColorSpace` `CGColorSpaceUsesExtendedRange` `CGColorSpaceIsWideGamutRGB`
`CGColorSpaceSupportsOutput` `CGColorSpaceCreateExtended` `CGColorSpaceCopyPropertyList`
`CGColorSpaceCreateWithPropertyList`, plus `CGColorCreateSRGB` `CGColorCreateGenericGray`
`CGColorGetConstantColor`.

`CGColorSpaceGetModel` derives the model from the component count, which is all Tiger exposes,
so Indexed and Pattern report as their base model. The property list round trip is name-based.

**Paths.** `CGPathCreateWithRect` `CGPathCreateWithRoundedRect` `CGPathAddRoundedRect`
`CGPathAddUnevenCornersRoundedRect` `CGPathCreateCopyByTransformingPath`
`CGPathCreateMutableCopyByTransformingPath` `CGPathGetPathBoundingBox`. Rounded rects are built
from four kappa curves with the radii clamped to half the rect, the way CG does it. Transformed
copies go through `CGPathApply`. `CGPathGetPathBoundingBox` forwards to Tiger's
`CGPathGetBoundingBox`, which is the control-point box, so curves report slightly large.

**Context.** `CGBitmapContextCreateWithData` `CGContextGetColorSpace`
`CGContextBeginTransparencyLayerWithRect` `CGContextStrokeArc` `CGContextDrawPathDirect`
`CGContextDrawTiledImage`, the six font subpixel positioning and quantization setters and
getters, `CGContextSetShouldAntialiasFonts`, `CGContextSetFontAntialiasingStyle` and its
getter, and `CGFontRenderingGetFontSmoothingDisabled`.

Two of these carry a known ceiling. `CGBitmapContextCreateWithData` drops the release callback,
because Tiger's `CGBitmapContextCreate` has no such hook; the caller keeps ownership of the
buffer, which is what every WebCore call site does anyway. `CGContextDrawTiledImage` is a plain
draw loop over the clip rather than a `CGPattern`.

The font knobs are no-ops whose getters report what Tiger's rasterizer actually does, which is
integral glyph positions and no subpixel quantization. `CGContextSetShouldAntialiasFonts` maps
onto `CGContextSetShouldSmoothFonts`, the closest thing Tiger has.

**Constants.** About 40 `CFStringRef` data symbols Tiger does not export, defined with Apple's
string values: the colorspace names, `kCGColorWhite`/`Black`/`Clear`,
`kCGGradientInterpolatesPremultiplied`, and the later ImageIO property and option keys
(`kCGImagePropertyGroups`, `kCGImagePropertyHEIFDictionary`, the PNG animation keys, the
auxiliary data keys). Keys Tiger's ImageIO does not understand simply never match, which is the
right behaviour. Also the `CGColorSpaceModel`, `CGContextType`, `CGGradientDrawingOptions`,
`CGFontAntialiasingStyle` and `CGImageCachingFlags` enums, and the twelve Porter-Duff
`CGBlendMode` values that arrived in 10.5, at Apple's numeric values.

## Stubbed (8)

Tiger's ImageIO predates all of these by a decade, so they return the empty answer rather than
failing: `CGImageSourceGetPrimaryImageIndex` returns 0,
`CGImageSourceCopyAuxiliaryDataInfoAtIndexWithOptions` returns NULL, and
`CGImageSourceSetAllowableTypes` `CGImageSourceDisableHardwareDecoding`
`CGImageSourceEnableRestrictedDecoding` are no-ops. Likewise `CGImageSetCachingFlags`
`CGImageGetCachingFlags` `CGImageSetProperty`, which are CG SPI for a per-image cache Tiger
does not have.

## Gated off (58)

None of these can be built on Tiger. Each needs a `HAVE()`/`USE()`/`ENABLE()` gate turned off
in the port rather than a shim.

**IOSurface, 17.** `HAVE(IOSURFACE)` must be off: Tiger has no IOSurface framework at all.
`CGIOSurfaceContextCreate` `CreateImage` `CreateImageReference` `GetColorSpace` `GetBitmapInfo`
`GetSurface` `SetDisplayMask` `InvalidateSurface` `FlushQueue`, `CGImageCreateFromIOSurface`,
`CGImageProviderCreate` `CGImageProviderGetSize` `CGImageCreateWithImageProvider`,
`CGImageBlockCreate` `CGImageBlockRelease` `CGImageBlockSetCreate` `CGImageBlockSetRelease`.

**HDR and EDR, 10.** No headroom, gain map or tone mapping concept exists in 2005 CG.
`CGContextGetEDRTargetHeadroom` `SetEDRTargetHeadroom` `GetContentToneMappingInfo`
`SetContentToneMappingInfo`, `CGImageApplyHDRGainMap` `CGImageCreateWithContentHeadroom`
`CGImageGetContentHeadroom` `CGImageGetContentAverageLightLevelNits`
`CGImageGetHDRGainMapHeadroom` `CGImageCreatePixelBufferAttributesForHDRTarget`.

**PDF, 8.** The port does not need PDF output or `ENABLE(UNIFIED_PDF)`.
`CGContextDrawPDFPageWithAnnotations` `CGPDFContextClose` `CGPDFDictionaryGetNameString`
`CGPDFDocumentIsTaggedPDF` `CGPDFPageLayoutGetAreaOfInterestAtPoint` `CGIsInLockdownModeForPDF`
`CGEnterLockdownModeForPDF` `CGEnterLockdownModeForFonts`.

**Window server SPI, 4.** `CGSCopyConnectionProperty` `CGSHWCaptureWindowList`
`CGSPackagesEnableConnectionOcclusionNotifications`
`CGSPackagesEnableConnectionWindowModificationNotifications`. Tiger's CGS has neither the
occlusion nor the hardware capture protocol.

**CGStyle, 2.** `CGStyleCreateGaussianBlur` `CGStyleCreateColorMatrix`, behind
`HAVE(CGSTYLE_COLORMATRIX_BLUR)`. Focus rings and filters have to take WebCore's software path.

**Display and event, 3.** `CGDisplayModeGetPixelsWide` `CGDisplayModeGetPixelsHigh`
`CGEventCopyIOHIDEvent`. Tiger's display mode API is `CFDictionary`-based, with no
`CGDisplayMode` object.

**Font SPI, 3.** `CGFontCopyFamilyName` `CGFontGetGlyphsForUnichars`
`CGFontGetGlyphAdvancesForStyle`. These belong to the CoreText track, which already has
Tiger's private CoreText mapped, rather than to this file.

Three neighbours of theirs do exist on Tiger and only lacked a declaration, so `CGCompat.h`
declares them: `CGFontCreateWithDataProvider` `CGFontGetGlyphPath` `CGFontGetUnitsPerEm`.
The CoreText track found them by disassembling the box and confirmed all three work there.
Tiger has no `CGFontCopyTableTags` and no `CGContextShowGlyphsAtPositions` at all.

**Other SPI, 11.** `CGContextCreateWithDelegate` and the whole `CGContextDelegate` /
`CGGState` / display-list surface, `CGContextSetOwnerIdentity`,
`CGDataProviderCreateMultiRangeDirectAccess` `CGDataProviderSetProperty`, `CGImageDumpToFile`,
`CGImageMetadataCreateFromXMPData` `CGImageMetadataCreateXMPData` (Tiger's ImageIO has no
`CGImageMetadata` type), `CGPathAddContinuousRoundedRect` (behind
`HAVE(CG_PATH_CONTINUOUS_ROUNDED_RECT)`), `CGPathCreateCopyByIntersectingPath` (path booleans,
which would need a full clipper), `CGPatternCreateWithImageTransformStep` (behind
`HAVE(CGPATTERN_CREATE_WITH_IMAGE_TRANSFORM_STEP)`), `CGSubimageCacheWithTimer`.

## Reaching WebCore

WebCore never includes `<TigerCompat/CGCompat.h>`. It gets these declarations from the SDK
overlay: `compat/sdk-overlay/CoreGraphics.framework/Headers` holds twelve generated hook
headers, `CoreGraphics.h` plus the eleven sub-headers WebCore names directly, each of which
includes the SDK's real header and then `CGCompat.h`. `make-cg-hooks.sh` in that directory
regenerates them, and `compat/sdk-overlay/README.md` records the mechanics.

`<ImageIO/ImageIO.h>` needs no overlay of its own, because ImageIO's `CGImageSource.h`
includes `<CoreGraphics/CoreGraphics.h>` and so picks up the hook.

### Constant audit

Enumerators never reach the linker, so a missing one is invisible to the export diffs and
surfaces as a compile error deep in the build. The CoreText track hit this and found 14 real
gaps that way, so the same audit ran here: compile a translation unit that references all 246
`kCG*` names WebCore uses, through the overlay, and see which fail to declare.

| | Count |
|---|---|
| Referenced `kCG*` names | 246 |
| Undeclared through the overlay | 19 |
| Of those, declared by PAL's own SPI headers | 12 |
| Extraction artifacts, not real names | 2 |
| Genuinely undeclared | 5 |

The two artifacts are `kCGColorSpaceITUR` and `kCGColorSpaceExtendedITUR`: the usage extraction
truncated `kCGColorSpaceITUR_2020` and `kCGColorSpaceExtendedITUR_2020`, both of which this
header defines. `kCGDisplayStreamYCbCrMatrix` is the same truncation of
`kCGDisplayStreamYCbCrMatrix_SMPTE_240M_1995`.

Of the five genuinely undeclared, three sit in code already gated off here:
`kCGContentAverageLightLevel` in the tone mapping path, the display stream matrix in
ScreenCaptureKit capture, and `kCGFlexRangeAlternateColorSpace`, which PAL's `ImageIOSPI.h`
declares anyway. Two are live and need a gate:
`kCGEventUnacceleratedPointerMovementX` and `...Y`, used by pointer lock in
`PlatformEventFactoryMac.mm`. Tiger's CGEvent has no such field.

`ImageIOSPI.h` overlaps this header on thirteen names, all of them `extern const CFStringRef`
declarations or function prototypes identical to these. No enums, so no gate needed there.

### Conflicts with PAL's CoreGraphicsSPI.h

WebCore declares much of this SPI itself, so a unit including both sees both. Overlapping
function prototypes are character-identical, which C and C++ both allow. Three groups are not,
because a redefined enumerator is a hard error, and they need `#if !PLATFORM(TIGER)` on the PAL
side (sent to the build track):

| PAL lines | What |
|---|---|
| 97-108 | `CGContextType` and its ten enumerators |
| 125-132 | `kCGFontAntialiasingStyle*` and the typedef |
| 134-138 | `kCGImageCaching*` and the typedef |

Comparing against that header caught two wrong values on this side, now corrected: the
antialiasing styles shift by 7, not 3, and `kCGImageCachingTemporary` is 3, not 2.

One build flag falls out of this too. WebCore writes
`kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little`, and on Tiger those are two
distinct anonymous enums, so C++20 onward deprecates the bitwise operation between them. At
C++23 with `-Werror` every such site fails, and the port needs
`-Wno-deprecated-anon-enum-enum-conversion`.

## Test

`spike/cgtest.c`, built and run on the 10.4.11 box:

```
toolchain/bin/tiger-clang -O1 -g -o build/cgtest spike/cgtest.c \
  -ltigercompat -framework ApplicationServices
scp -O build/cgtest build/cgtest.png tiger:/tmp/ && ssh tiger /tmp/cgtest
```

It creates a bitmap context in the shimmed sRGB, draws a linear, radial and conic gradient and
reads back pixels at the endpoints and midpoint, fills a rounded rect through
`CGContextDrawPathDirect` and checks that the corner stays unpainted, transforms a path, checks
the transparency layer clips to its rect, and decodes a PNG through `CGImageSource`. 40 checks,
all passing.

One thing the test surfaced that is worth knowing for the rest of the port: filling with
`CGContextSetRGBFillColor` in an ICC sRGB context goes through a generic-RGB to sRGB
conversion, so pure green comes back as (119, 246, 60). That is correct colorimetry, not a bug,
but it means pixel assertions elsewhere should set colors with an explicit sRGB `CGColorRef`.
