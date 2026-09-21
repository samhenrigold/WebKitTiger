# CoreGraphics behavioural probe (Tiger vs modern)

`spike/cgprobe.c`, 33 checks. This probes Tiger's **own** CoreGraphics, not the shims in
`compat/cgcompat.c`.

## Why it exists

Three failure modes matter on this port, and only the third needs a running machine:

1. the symbol is missing, caught by the export diffs
2. the symbol exists with a different signature, caught by the ABI screen in `CG-SURVEY.md`
3. the symbol exists, takes its arguments, returns plausibly, and does the wrong thing

The audit track's static screen came back clean for CoreGraphics, so everything left is
mode 3. Two were already known: `CGShading` discards the alpha its function returns, and the
10.5 blend modes composite as Normal. Both were found by measuring pixels, neither is visible
to any static check.

## Method

Each check draws into a 32x32 bitmap with plain CoreGraphics calls and samples a few pixels.
The expected values come from running the same source against modern CoreGraphics on the host
Mac, embedded as `spike/cgprobe-expected.h`. Tolerance is 12 per channel, which absorbs
rasteriser and colour-management drift without hiding a real divergence.

```
cc -O1 -DEMIT_ONLY -o build/cgprobe-host spike/cgprobe.c -framework ApplicationServices
./build/cgprobe-host --emit > spike/cgprobe-expected.h

toolchain/bin/tiger-clang -O1 -I spike -o build/cgprobe spike/cgprobe.c \
  -F compat/sdk-overlay \
  -F sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks \
  -ltigercompat -framework ApplicationServices
scp -O build/cgprobe tiger:/tmp/ && ssh tiger /tmp/cgprobe
```

Checks were chosen by ranking real call sites under
`WebKit/Source/WebCore/platform/graphics/{cg,cocoa}`.

**A check is only as good as its discrimination.** The first version of the blend check used an
opaque source, which makes Copy and Normal produce the same pixel, so it passed on a CG that
ignores Copy completely. The source is half alpha now. Worth remembering before trusting any
"matches" line here.

## Result

26 of 33 match. Seven diverge, in three groups.

### 1. Five blend modes are silently ignored (5 checks)

| Mode | Tiger | Modern |
|---|---|---|
| Copy | 128 63 0 191 | 128 0 0 128 |
| XOR | 128 63 0 191 | 64 64 0 127 |
| DestinationOver | 128 63 0 191 | 64 128 0 192 |
| PlusLighter | 128 63 0 191 | 128 128 0 255 |
| Clear | 128 63 0 191 | 0 0 0 0 |

Tiger returns Normal's pixel (128 64 0 192) for every one. Multiply and Screen, which 10.4
does have, match.

**WebCore hits it.** `GraphicsContextCG.cpp` maps every `CompositeOperator` onto these, so any
canvas `globalCompositeOperation` or CSS `mix-blend-mode` other than source-over degrades
silently. `NativeImageCG.cpp` and `GraphicsContextGLCG.cpp` also set Copy directly.

**No shim is possible.** The blend mode is context state consumed by every later drawing call,
not a parameter that can be intercepted. This has to be gated or accepted in the port. The one
concrete bug it causes, `NativeImageCG.cpp` compositing over an uninitialized buffer, is
recorded in `CG-SURVEY.md` and was sent to the build track.

### 2. Shadows are lighter (1 check)

Alpha along the shadow, `CGContextSetShadowWithColor` with offset (4, -4) and blur 2:

| x | 14 | 16 | 18 | 20 | 22 |
|---|---|---|---|---|---|
| Tiger | 179 | 179 | 126 | 10 | 0 |
| Modern | 241 | 241 | 228 | 72 | 0 |

The geometry is right: same offset, same extent, the falloff ends in the same place. Tiger's
shadow is simply lighter, around 74% of modern in the flat region, with a slightly sharper
edge.

**WebCore hits it**, wherever `box-shadow` or `text-shadow` is drawn. Nothing breaks; shadows
render weaker than they should.

**A shim is possible but not shipped.** `CGContextSetShadowWithColor` takes the colour, so a
wrapper could scale its alpha up. One ratio measured at one blur radius is not enough to fit a
correction, though, and a wrong fudge factor is worse than a consistently light shadow. If this
matters visually, measure across blur radii first.

### 3. Colour conversion on draw differs (1 check)

Filling with a DeviceRGB green into a generic RGB context:

| | R | G | B |
|---|---|---|---|
| Tiger | 107 | 250 | 44 |
| Modern | 34 | 255 | 6 |

Expected, and not a bug. Modern macOS treats DeviceRGB as sRGB; Tiger's generic RGB is the
1.8-gamma space of its era with different primaries. Both are "green".

**Do not chase this one.** It is why `CGCompat.h` builds sRGB from the ICC profile ColorSync
ships rather than mapping it onto generic RGB, and why pixel assertions elsewhere should set
colours with an explicit sRGB `CGColorRef`.

## What matched

Worth recording, because these were the plausible suspects:

`CGContextSetAlpha`, transparency layers (including that a layer composites once rather than
per-operation), `CGContextClipToRect`, `CGContextClipToRects`, non-zero and even-odd fill
rules, `CGContextEOClip`, arc and curve fills, `CGContextSetLineDash`, line caps and joins,
`CGContextReplacePathWithStrokedPath`, `CGContextDrawImage` at both interpolation qualities,
`CGImageCreateWithImageInRect`, `CGImageCreateWithMaskingColors`, `CGImageCreateWithMask`,
the premultiplied-last big-endian bitmap layout, `CGLayer` with `CGContextDrawLayerInRect`,
`CGPattern` fills, and `CGContextSetShouldAntialias`.

`CGContextClipToMask` matched on both the plain-mask and varying-colour checks. That is worth
noting against the earlier suspicion recorded in `CG-SURVEY.md`: the alpha anomaly seen while
building the gradient path was **not** reproduced by a direct probe, so whatever went wrong
there was in how the gradient code used it, not in `ClipToMask` itself. The gradient path does
not need it either way, so it stays as it is, but the note in `CG-SURVEY.md` overstates the
case.
