# Core Animation for Tiger, from first-gen Apple TV firmware

Verdict up front: **a complete i386 Core Animation exists in Apple TV Software 3.0.2 and it runs
on our 10.4.11 box.** It loads with zero unresolved symbols, instantiates layers and animations,
and actually rasterizes a layer tree through `CARenderer` into an OpenGL context. Tested live on
`tiger`, both spikes pass.

The "LayerKit / LK-prefixed classes" lead is **false for this hardware**. Every layer class is
CA-prefixed. The only LK symbols anywhere are five coding-proxy helpers
(`LKCGColorCodingProxy`, `LKCGImageCodingProxy`, `LKNSArrayCodingProxy`,
`LKNSDictionaryCodingProxy`, `LKNSValueCodingProxy`) — archiver leftovers, not the API.

## Images obtained

Apple's `mesu.apple.com` and `appldnld.apple.com` return 403 for every historical Apple TV path;
those downloads are gone. The Internet Archive item `Apple_TV_1_Software` carries the full set.
Downloaded from `https://archive.org/download/Apple_TV_1_Software/`:

| File | Version | Build | Size |
|---|---|---|---|
| `atv-1.0.dmg` | Apple TV 1.0 | 8N5107 | 207 MB |
| `atv-2.4.dmg` | Apple TV 2.4 | 8N5880 | 115 MB |
| `atv-3.0.2.dmg` | Apple TV 3.0.2 | 8N6014 | 246 MB |

Each `.dmg` is a bootable HFS+ root filesystem directly — no nested `OSBoot.img`, no `.pkg`,
no payload to unpack. `hdiutil attach -nobrowse -readonly` is the whole extraction step.

All three report `ProductName = Apple TV OS`, `ProductVersion = 10.4.7`. So this is Darwin 8,
one point release *behind* our target, built for i386. That is why the binaries are usable.

## What each image contains

**1.0 (March 2007) has no Core Animation at all.** Its `QuartzCore.framework` is version 1.4.11,
215 ObjC classes, all `CI*` — Core Image and Quartz Composer, same shape as the stock Tiger
framework. `BackRow.framework` (the Front Row UI) links it but gets no layers from it. Nothing in
`/System`, `/Applications` or `/usr/lib` contains the string `CALayer` or `LKLayer`. There is no
`LayerKit.framework` and no `CoreSurface`.

**2.4 (2009)** already has Core Animation: 32 CA classes, including `CALayer`, `CARenderer`,
`CAOpenGLLayer`, `CATiledLayer`, `CAFilter`, plus a `CAPDFLayer` and `CAScriptContext` that were
dropped later. No `CAShapeLayer`, `CAGradientLayer`, `CALayerHost` or `CAReplicatorLayer`.

**3.0.2 (built 30 Jan 2010)** is the one to use. `QuartzCore.framework` is version **1.6.0
(build 222.0)**, a single 4.5 MB i386 Mach-O carrying Core Image *and* Core Animation together,
374 ObjC classes, 43 of them CA. That version number lines up with Snow Leopard's QuartzCore,
compiled against a Darwin 8 SDK.

CA classes in 3.0.2:

```
CAAnimation CAAnimationGroup CABasicAnimation CABoxLayoutManager CACGPathCodingProxy
CACGPathCodingSegment CACGPatternCodingProxy CACodingProxy CAConstraint
CAConstraintLayoutManager CAContext CAContextImpl CAEmitterCell CAEmitterLayer CAFilter
CAGradientLayer CAKeyframeAnimation CALayer CALayerArray CALayerHost CAMatchMoveAnimation
CAMediaTimingFunction CAMediaTimingFunctionBuiltin CAMLParser CAMLWriter CAOpenGLLayer
CAPropertyAnimation CARenderer CARenderObject CAReplicatorLayer CAScrollLayer
CAScrollLayoutManager CAShapeLayer CASlotProxy CASublayerEnumerator CATableLayoutManager
CATextLayer CATiledLayer CATransaction CATransformLayer CATransition CAValueFunction
CAWrappedLayoutManager
```

The C-level `CACF*` render API (`CACFLayerCreate`, `CACFAnimationCreate`, `CARenderOGLRender`,
`CABackingStoreCreate`, `CAImageQueueCreate`, the `CATransform3D*` math) is all exported too.
That is the same private surface the Windows WebKit port used to drive CA directly.

Extracted to `/Users/shg/Developer/WebKitTiger/atv/extracted/3.0.2/QuartzCore.framework`.
It is self-contained: no extra private framework has to come with it.

## Dependency verdict

Every dependency is a stock Tiger library, and every compatibility version is satisfied by what is
actually on the 10.4.11 box (`sysroot/`):

| Dependency | ATV requires (compat) | Tiger 10.4.11 has (current) | |
|---|---|---|---|
| CoreVideo | 1.2.0 | 1.4.1 | ok |
| CoreFoundation | 150.0.0 | 368.31.0 | ok |
| Foundation | 300.0.0 | 567.36.0 | ok |
| ApplicationServices | 1.0.0 | 22.0.0 | ok |
| OpenGL | 1.0.0 | 1.0.0 | ok |
| libGLImage.dylib | 1.0.0 | 1.0.0 | ok |
| IOKit | 1.0.0 | 275.0.0 | ok |
| Accelerate (weak) | 1.0.0 | 4.0.0 | ok |
| libxml2.2 | 9.0.0 | 9.16.0 | ok |
| libobjc.A | 1.0.0 | 227.0.0 | ok |
| libstdc++.6 | 7.0.0 | 7.4.0 | ok |
| libgcc_s.1 | 1.0.0 | 1.0.0 | ok |
| libSystem.B | 1.0.0 | 88.3.9 | ok |

Symbol check: the binary has 1137 undefined symbols. Resolving them against every Mach-O in
`sysroot/{usr/lib,System/Library/Frameworks,System/Library/PrivateFrameworks}` plus the
framework's own exports leaves **zero unresolved**. Notably Tiger's CoreGraphics already exports
the whole private `CGS*` surface CA wants (`CGSBindSurface`, `CGSNewRegionWithRect`,
`CGSSetSurfaceOpacity`, `CGSDeviceCreate`, ...) and the private CoreText entry points it uses for
`CATextLayer`.

**One real hazard: the install name collides.** The framework's `LC_ID_DYLIB` is
`/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore`, exactly Tiger's own
QuartzCore, and 207 of its class names (`CIFilter`, `CIImage`, `CIContext`, ...) are duplicates of
Tiger's. If both images ever end up in one process you get duplicate-class chaos. In our favour,
nothing in the base system pulls QuartzCore in implicitly — AppKit, Quartz, QTKit and
ApplicationServices all link it zero times. Still, before shipping, run `install_name_tool -id` to
give it a private path next to WebKit and load it explicitly. Never install it into the box's
`/System`; nothing here did.

## Live results on the 10.4.11 box

Two spikes, both built with `tiger-clang -fobjc-runtime=macosx-fragile-10.4` and run over ssh.
Neither links QuartzCore at build time; both `dlopen` the framework from `/tmp`.

`spike/catest.m` — API smoke test. All 17 checks pass. `CALayer`, `CATransaction`,
`CABasicAnimation`, `CAFilter`, `CATiledLayer`, `CATransformLayer`, `CAShapeLayer` all resolve;
`LKLayer` does not. `CACurrentMediaTime()` returns a live clock. Layer creation, `setBounds:`,
`setBackgroundColor:` with a real `CGColorRef`, `setOpacity:`, `setName:`/`name` round trip,
`addSublayer:`/`sublayers`, `CABasicAnimation animationWithKeyPath:@"opacity"` +
`addAnimation:forKey:`, `CATransaction begin`/`commit`, and
`[CAFilter filterWithType:@"gaussianBlur"]` all work.

`spike/carendertest.m` — does it actually draw. Creates an accelerated CGL pbuffer context,
builds a `CARenderer` with `rendererWithCGLContext:options:`, attaches a green 256x256 root layer,
renders, and reads the pixel back:

```
ok   CGL pbuffer context, renderer=NVIDIA GeForce 8600M GT OpenGL Engine
ok   CARenderer rendererWithCGLContext:options: -> 0x37bbd0
     readback sanity (expect 0,0,255): 0,0,255,255
ok   beginFrame/render/endFrame, glError=0x0
     center pixel RGBA = 0,255,0,255

PASSED: layer actually rasterized green
```

Hardware-accelerated on the GeForce 8600M. Two things are required and are not obvious:

- **`[CATransaction flush]` must be called explicitly.** CA normally commits from a run-loop
  observer. Headless, the render tree stays empty and you render black with no error.
- **`addUpdateRect:` must be called**, and the root layer needs a real `position`; the default
  anchor point puts it mostly off-origin.

## How this lines up with what WebCore calls

Classes WebCore's `platform/graphics/ca` messages, against 3.0.2:

| Present | Missing |
|---|---|
| `CALayer`, `CAFilter`, `CATransaction`, `CAPropertyAnimation`, `CAAnimationGroup`, `CAMediaTimingFunction`, `CAKeyframeAnimation`, `CABasicAnimation`, `CAValueFunction`, `CATransformLayer`, `CAShapeLayer`, `CALayerHost` | `CASpringAnimation`, `CAPresentationModifier`, `CAPresentationModifierGroup`, `CABackdropLayer` |

Selector coverage is better than the class list suggests. Of the CALayer/CAAnimation API WebCore
drives, present and working: `setBackgroundColor:`, `setBorderColor:`, `setBorderWidth:`,
`setContents:`, `setAnchorPoint:`, `setPosition:`, `setBounds:`, `setTransform:`,
`setSublayerTransform:`, `setMasksToBounds:`, `setOpacity:`, `setHidden:`, `setGeometryFlipped:`,
`setDoubleSided:`, `setFilters:`, `setCompositingFilter:`, `setBackgroundFilters:`, `setMask:`,
`setNeedsDisplayInRect:`, `setEdgeAntialiasingMask:`, `setContentsGravity:`,
`setMinificationFilter:`, `setMagnificationFilter:`, `setCornerRadius:`, `setShadowOpacity:`,
`addAnimation:forKey:`, `removeAnimationForKey:`, `animationKeys`, `presentationLayer`,
`setDelegate:`, `setActions:`, `setSublayers:`, `insertSublayer:atIndex:`, `setZPosition:`,
`setSpeed:`, `setTimeOffset:`, `setBeginTime:`, `setFillMode:`, `setRemovedOnCompletion:`,
`setTimingFunction:`, `setKeyTimes:`, `setValues:`, `setFromValue:`, `setToValue:`, `setAdditive:`,
`setValueFunction:`.

Absent, all of them post-Snow-Leopard additions:

- `contentsScale` / `setContentsScale:` — 10.6. No HiDPI; hard-code scale 1.0. Tiger has no Retina
  displays anyway, so this is a stub, not a gap.
- `shouldRasterize` / `setRasterizationScale:` — 10.6. Only a performance hint; ignore.
- `drawsAsynchronously` — 10.8. Ignore.
- `setAllowsEdgeAntialiasing:`, `setContentsFormat:`, `setWantsExtendedDynamicRangeContent:`,
  `setToneMapMode:` — modern. Stub out with the filters/EDR code paths compiled off.

None of these are load-bearing for a compositing WebKit on this hardware.

## The structural problem that remains

Tiger's AppKit has **no layer-backed views**. `setWantsLayer:` does not exist as a string anywhere
in `sysroot/.../AppKit`, and Tiger's WindowServer speaks no CA render-server protocol, so
`CAContext contextWithCGSConnection:options:` will not composite into a window even though the
selector exists in the binary. The `CARenderer` path is what works, and it is the path the spike
proves.

That means the integration shape is: put the page's layer tree under a `CARenderer`, drive it from
an `NSOpenGLView`'s draw, and call `[CATransaction flush]` plus `addUpdateRect:` ourselves each
frame. This is close to what the Windows CA port did, so there is precedent in WebKit's own
history for wiring `PlatformCALayer` to a manually-driven renderer rather than to AppKit.

## Headers

The framework ships no headers, but `sdk/MacOSX10.5.sdk` already has the Leopard QuartzCore
headers (`CALayer.h`, `CAAnimation.h`, `CATransaction.h`, `CARenderer.h`, `CATransform3D.h`,
`CAMediaTiming.h`, ...). Leopard is CA 1.x, the same generation as this binary, so those headers
are the right declarations to compile against. Anything they declare that this build lacks is the
short missing-selector list above.

## Reproducing

```bash
cd /Users/shg/Developer/WebKitTiger/atv
bash dl.sh                                    # re-download all three images
hdiutil attach -nobrowse -readonly -mountpoint /tmp/atv302 atv-3.0.2.dmg
cp -R /tmp/atv302/System/Library/Frameworks/QuartzCore.framework extracted/3.0.2/
hdiutil detach /tmp/atv302
```

Spikes, built and run from the project root:

```bash
toolchain/bin/tiger-clang -fobjc-runtime=macosx-fragile-10.4 -o build/catest spike/catest.m \
  -framework Foundation -framework ApplicationServices -ltigercompat
toolchain/bin/tiger-clang -fobjc-runtime=macosx-fragile-10.4 -o build/carendertest \
  spike/carendertest.m -framework Foundation -framework ApplicationServices -framework OpenGL \
  -ltigercompat
scp -O -r atv/extracted/3.0.2/QuartzCore.framework tiger:/tmp/
scp -O build/catest build/carendertest tiger:/tmp/
ssh tiger '/tmp/catest && /tmp/carendertest'
```

---

# Inventory: what else in Apple TV 3.0.2 is newer than Tiger 10.4.11

Short answer: **almost nothing.** The Apple TV is a 10.4.7 (Darwin 8) system, three point releases
*behind* our target. Apple backported exactly one component forward, QuartzCore, and left the rest
of the OS at or below Tiger's level. Two other pieces are genuinely newer but not usable.

## Version comparison

`ATV` is `tiger-otool -L` current_version on the 3.0.2 image; `Tiger` is the same read on our
`sysroot/` mirror of the real 10.4.11 box.

| Component | ATV 3.0.2 | Tiger 10.4.11 | Newer? | Usable |
|---|---|---|---|---|
| **QuartzCore** (Core Animation) | **1.6.0 / build 222.0** | 1.4.11 (Core Image only) | **yes, by a generation** | **yes — see first half of this report** |
| CFNetwork | 250.0.0 | 129.22.0 | yes, a lot | no (missing CF symbols) |
| CoreVideo | 1.5.0 | 1.4.1 | slightly | yes, but pointless |
| WebKit / JavaScriptCore | 528.18.0 | 523.12.0 | yes | not as a dependency; valuable as reference |
| CoreFoundation | 369.13.0 | 368.31.0 | marginally | no (system-wide swap) |
| CoreGraphics | 258.74.0 | 258.77.0 | no, older | no |
| CoreText | 1.0.0 (244 exports) | 1.0.0 (243 exports) | no | no |
| ImageIO | 1.0.0 | 1.0.0 | no | no |
| ATS | 184.6.1 | 184.13.1 | no, older | no |
| ColorSync | 4.4.6 | 4.4.8 | no, older | no |
| Foundation | 567.32.0 | 567.36.0 | no, older | no |
| AppKit | 824.42.0 | 824.44.0 | no, older | no |
| Security | 29002.0.0 | 29774.0.0 | no, older | no |
| libSystem.B | 88.3.3 | 88.3.9 | no, older | no |
| libobjc.A | 227.0.0 | 227.0.0 | identical | no |
| libicucore.A | 32.0.0 | 32.0.0 | identical | no |
| libxml2.2 | 9.16.0 | 9.16.0 | identical | no |
| libsqlite3.0 | 9.6.0 | 9.6.0 | identical | no |
| libz.1 | 1.2.3 | 1.2.3 | identical | no |
| OpenGL | 1.0.0 | 1.0.0 | identical | no |
| QTKit | 1.0.0 | 1.0.0 | identical | no |
| GraphicsServices (private) | 14.0.0, 247 exports | **absent** | ATV-only | only as WebKit 528's dependency |
| libdispatch | **absent** | absent | — | — |
| Blocks runtime | **absent** | absent | — | — |
| IOSurface | **absent** | absent | — | — |
| CoreMedia / VideoToolbox | **absent** | absent | — | — |

## Runtime capability checks

**libobjc is the same ObjC 1 runtime we already have.** Version 227.0.0, byte-identical in the
ways that matter. None of `objc_setAssociatedObject`, `objc_allocateClassPair`, `object_getClass`,
`class_addMethod`, `method_exchangeImplementations`, `objc_retain` or `objc_msgSendSuper2` are
exported. No help for the `compat/objc2compat.m` track.

**No libdispatch, no Blocks, no IOSurface, no CoreMedia, no VideoToolbox.** `/usr/lib/libdispatch*`
does not exist, and libSystem exports neither `dispatch_async` nor `_NSConcreteStackBlock` nor
`_Block_copy`. `compat/dispatch/` and `compat/blockclasses.m` remain the only path.

**Video decode is plain QuickTime.** H.264 comes from
`/System/Library/QuickTime/QuickTimeH264.component` driven through QuickTime and QTKit, both at
the same version Tiger has. There is no modern media stack to harvest.

## CoreText and CoreGraphics: no gain, as suspected

Dumped both with `tiger-nm` and diffed against our existing gap lists.

| | WebCore calls currently missing | ATV provides |
|---|---|---|
| CoreText | 66 (`logs/api/missing-CT.txt`) | **0** |
| CoreGraphics | 144 (`logs/api/missing-CG.txt`) | **0** |

ATV CoreText exports 244 symbols against Tiger's 243, and ATV CoreGraphics 3572 against Tiger's
3570. The only four real CoreGraphics additions are `CGSCustomUIVersion`,
`CGSDisplayColorSyncUpdate`, `CGSDisplayConfigurationOrigin` and `CGSRunningMacBuddy` — Apple TV
product plumbing, nothing WebCore wants.

Even setting the symbol count aside, both are unloadable from a private path. Their install names
are the stock `ApplicationServices.framework/Frameworks/...` paths, and ApplicationServices loads
Tiger's copies into every graphical process before we get a say. There is no version of this that
works. **Not usable. Close the lead.**

## The other two newer pieces

**CFNetwork 250.0.0 — not usable.** Nearly double Tiger's 129.22.0, so it looked promising. It has
479 undefined symbols; 477 resolve against Tiger, but two do not: `__CFSocketRead` and
`__CFSocketSetSocketReadBufferAttrs`, private entry points added in CoreFoundation 369. Tiger has
368.31, so it will not load. Swapping CoreFoundation too is not on the table, since every process
links it. Moot anyway: we already have curl 8.14 with TLS 1.2 and SNI working on the box.

**CoreVideo 1.5.0 — loadable but pointless.** Zero unresolved symbols against Tiger, all
dependencies satisfied. But it is one minor version ahead of Tiger's 1.4.1 and only matters for
video paths we are not building. Copied to `extracted/3.0.2/` in case the media track wants it.

## The real prize: Apple shipped GraphicsLayerCA on this exact stack

The image contains **WebKit 528.18.0** (Safari 4 era) built for Darwin 8 i386, and its WebCore
links QuartzCore 1.6.0 and contains `GraphicsLayerCA_property`, `CALayer(%p) GraphicsLayer(%p)`,
`WebLayer`, `WebLayerAdditions`, `WebTiledLayer`, `CATiledLayer`, `CATransformLayer`,
`CAPropertyAnimation`, `CAKeyframeAnimation`, `CAValueFunction`, `CAMediaTimingFunction`.

That is WebCore's accelerated-compositing layer, the direct ancestor of our
`platform/graphics/ca`, shipping in production against this very QuartzCore on Darwin 8. Our
integration question already has a worked answer in binary form. Kept at
`extracted/3.0.2/WebKit.framework` (20 MB) purely as reference, with its private dependency
`extracted/3.0.2/GraphicsServices.framework` (100 KB, 247 exports, resolves fully against Tiger).
We are not linking a 2009 WebKit; we are reading how it did the hosting.

## The window server situation, and why it confirms the CARenderer plan

**The Apple TV has no window server.** `/System/Library/CoreServices/WindowServer` is a 119-byte
shell script that prints a TODO and calls `exit 0`:

```sh
#!/bin/sh
# TEMPORARY HACK til we have updated /etc/ttys and SystemStarter resources
# See Radar 3221126.
exit 0
```

So Core Animation on this device never composited through a window server, and its CoreGraphics
has no render-server additions over Tiger's. `BackRow.framework`, the front end, contains exactly
`CARenderer`, `rendererWithCGLContext:options:` and `setLayer:` — and no `CAContext` strings at
all. ATV's AppKit does link QuartzCore, unlike Tiger's, but exports zero layer-backed-view
strings; there is no `setWantsLayer:`.

Apple drove Core Animation on Darwin 8 by handing a layer tree to a `CARenderer` over a raw CGL
context, with no window server and no layer-backed views. That is precisely the path
`spike/carendertest.m` proved on our box. The plan in the first half of this report is not a
workaround, it is the shipping design.

## Kept in `atv/extracted/3.0.2/` (55 MB)

| Directory | Why |
|---|---|
| `QuartzCore.framework` | the deliverable: Core Animation 1.6.0 for Darwin 8 i386 |
| `WebKit.framework` | reference: GraphicsLayerCA 528.18 against that QuartzCore |
| `GraphicsServices.framework` | WebKit 528's ATV-private dependency |
| `BackRow.framework` | reference: the `CARenderer` + CGL hosting pattern |
| `CoreVideo.framework` | 1.5.0, marginally newer, for the media track if wanted |
