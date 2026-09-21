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

**Build the probe for both platforms, not just Tiger.** The audit track's
`spike/cgbehaviour.c` prints identical `KEY=value` lines on Tiger and the host, so the result
is a diff against modern CoreGraphics rather than a judgement about Tiger numbers. That caught
two broken tests before either found anything: one measuring nothing because layer grouping
only shows against a global alpha, and a y-axis mistake that made every point sample read an
empty pixel on both platforms, which reads as agreement rather than as a broken test. Neither
was visible from Tiger's output alone.

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

### 2. Shadows are lighter, and must not be corrected (1 check)

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

**A shim is possible and must not be shipped.** The audit track ran the blur sweep
(`spike/cgbehaviour.c`, commits 9233271 and fe34d9c) and the ratio is stable: Tiger carries
91.7% of modern's total shadow ink, between 0.905 and 0.949 across blur 0 to 32. By the
criterion stated here that would make an alpha correction shippable. It should not be, and the
sweep is why.

Up to blur 6 the geometry matches exactly and the peak is 255 on both, so the shortfall is
entirely edge antialiasing and close to invisible. From blur 8 up, Tiger's blur saturates: at
16 it covers 70% of modern's area at peak 195 against 128, and at 32 it covers 54% at peak 134
against 41. Those shadows are already too dark and too tight. Scaling alpha up would fix an
invisible error at small radii while making the visible defect worse at large ones.

The 74% figure measured here at blur 2 is a narrower metric than the audit track's 91.7%, which
is total ink over the whole shadow region. A peak or single-pixel reading lands on the part
that saturates, so the sweep's number is the one to trust.

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

### Interpolation quality is binary (audit track)

`CGContextSetInterpolationQuality` accepts all five values and reads them back unchanged, but
the rasterizer collapses them: Default, Low, Medium and High render byte-identically on a 4x4
to 32x32 upscale. Only None is distinct. `CGContextGetInterpolationQualityRange` reports
[0, 0], corroborating from a different direction.

So the None versus High pair in the table above, which matched, was the whole feature rather
than a sample of it. Anywhere WebCore selects Low or Medium to trade quality for speed, Tiger
gives it High. A shim cannot detect the loss by reading the state back, because the setter
stores the value faithfully; only the rasterizer ignores it.

### Font smoothing is inert (audit track)

`spike/fontsmoothtest.c`, commit f7338be. `CGContextSetShouldSmoothFonts` on versus off gives
byte-identical pixels in an opaque bitmap context, and no rendering produces a colour fringe.
`CGContextSetAllowsFontSmoothing` is inert too. `CGContextSetShouldAntialias` is the knob that
works: with it off, inked pixels drop from 615 to 247 and antialiased pixels from 556 to 0.

The probe uses an opaque context deliberately, because CG disables subpixel smoothing for
contexts with an alpha channel even on modern macOS. The claim is bounded: this measures bitmap
contexts, which is what WebCore's canvas and ImageBuffer paths use. A window context could
differ and cannot be tested headlessly.

**A working per-font knob does exist.** `CGFontSetShouldAntialias` is private but exported and
per-font: clearing it takes a glyph run from inked 593 and antialiased 542 down to inked 235
and antialiased 0, while shapes in the same context stay smooth. The flag is bit 0 of a byte at
`font+0x3c`, read back by `CGFontShouldAntialias`. Every result holds identically whether the
glyphs are drawn through `CGContextSelectFont` and `CGContextShowTextAtPoint` or through the
path WebCore actually uses, `ATSFontFindFromName` to `CGFontCreateWithPlatformFont` to
`CGContextShowGlyphsWithAdvances`.

**Consequence here.** `CGContextSetShouldAntialiasFonts` used to forward to
`SetShouldSmoothFonts`, so it only looked like it did something. It is now an explicit no-op,
and it stays inert even though the per-font knob above has exactly the right semantics, because
wiring it up would cost something and buy nothing. WebCore's only call site,
`setCGFontRenderingMode` in `FontCascadeCoreText.cpp:294`, passes `true` unconditionally, and
antialiased glyphs are Tiger's default. Against that, the flag mutates the shared, cached
`CGFont`, so it would leak into every other context using that font, and WebCore usually sets
the font after configuring state, so the context may not have the font yet when the setter
runs. The route is recorded in `cgcompat.c` for the day a caller passes `false`.

The two font antialiasing style accessors stay inert because Tiger has nothing style-shaped to
map them to at all: no `CGContextSetFontRenderingStyle`, and nothing else in its smoothing and
antialias exports beyond the context Should/Allows pairs, their GState backings, the per-font
flag above, a `CGFontAllowsFontSmoothing` that takes no arguments and reads a process-wide
global, and a `__CGFontSmoothingMode` data symbol.

### Fixed here: CGContextDrawTiledImage seamed (audit track)

Tiger exports no `CGContextDrawTiledImage`, so `cgcompat.c` supplies a draw loop. It covered
exactly the right pixels in every clip tried, with inked counts matching the host exactly, but
a fractional tile origin lost about 6% of the alpha to a seam line at each tile boundary, where
the real function stays fully opaque.

The cause is direct: with the origin on the integer lattice the loop byte-matched the host;
only a fractional origin diverged. Adjacent tiles share an edge, and CG was antialiasing each
tile's edge against the backdrop rather than against its neighbour. It fires in practice,
because `GraphicsContextCG.cpp:517` passes a `FloatRect` straight from layout, so any repeated
background at a non-integral position or scale shows faint seams.

Fixed by bracketing the loop with antialiasing off, so a shared edge snaps the same way for
both tiles, closing the seam without leaving a gap. `spike/cgtest.c` asserts that no pixel in
the interior falls below full alpha with a fractional origin; the check was confirmed to fail
with the fix removed.

### Matched, nothing to do (audit track)

Pattern tiling is honoured, on both `CGPatternCreate` and `CGPatternCreateWithImage2`.
NoDistortion renders differently from the two constant-spacing modes on both platforms, and the
two spacing modes agree with each other on both, so the argument is not dropped.
`kCGPatternTilingConstantSpacing`, the value WebCore passes most, behaves as on modern.

Transparency layers group correctly under a non-identity CTM: scaled, rotated and composed
transforms each show the same layer-versus-no-layer alpha drop as modern, within 0.05%. Line
dash phase, `CGContextClipToRects` and `CGImageCreateWithMaskingColors` are identical to modern.

## What matched

Worth recording, because these were the plausible suspects:

`CGContextSetAlpha`, transparency layers (including that a layer composites once rather than
per-operation), `CGContextClipToRect`, `CGContextClipToRects`, non-zero and even-odd fill
rules, `CGContextEOClip`, arc and curve fills, `CGContextSetLineDash`, line caps and joins,
`CGContextReplacePathWithStrokedPath`, `CGContextDrawImage` at both interpolation qualities,
`CGImageCreateWithImageInRect`, `CGImageCreateWithMaskingColors`, `CGImageCreateWithMask`,
the premultiplied-last big-endian bitmap layout, `CGLayer` with `CGContextDrawLayerInRect`,
`CGPattern` fills, and `CGContextSetShouldAntialias`.

`CGContextClipToMask` matched on both the plain-mask and varying-colour checks, which clears
the suspicion recorded earlier in `CG-SURVEY.md`.

The audit track then probed it properly (`spike/clipmasktest.c`, commit 25438a1) and explained
the original misreading. With a DeviceGray mask, destination alpha equals the mask sample
exactly and is identical across white, red, mid grey and black fills. The **colour** channels
are colour times mask, which is just what premultiplied means. Reading a colour channel while
expecting alpha gives exactly the "mask times source colour" that the gradient work reported.
Confirmed across five mask values, four fill colours and a colour ramp.

So the two-bitmap composite in `cgcompat.c` is not forced by `ClipToMask` being broken. It is
kept because it is measured and passing, and the comment there now says so.

**Two real `ClipToMask` failures, both silent** (audit track, same probe). Tiger's
`ClipToMask` accepts only a DeviceGray non-alpha image:

- a `CGImageMaskCreate` stencil clips everything away, at mask sample 0 and 255 alike
- an RGBA image clips everything away, at any alpha

Neither reports an error. Both images are well formed: drawing them with `CGContextDrawImage`
instead renders correctly, so `ClipToMask` is what rejects them. This has a consequence in
WebCore at `GraphicsContextCG.cpp:1078`, `clipToImageBuffer`, which passes an RGBA image and
already carries a FIXME saying it should be grayscale. On Tiger that call does not mask, it
blanks everything drawn afterwards in the clipped region. The fix is a grayscale conversion at
the call site, in WebCore rather than here.
