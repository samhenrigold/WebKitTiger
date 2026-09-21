# Hosting Core Animation compositing in WebKitLegacy on Mac OS X 10.4.11

Design for milestone **M8** of `logs/webcore-plan.md` (§6). Written after
`spike/CAHost` proved the whole shape on the real box; every claim about CA 1.6
behaviour below is something that spike demonstrated, not something inferred from
headers. Read `atv/REPORT.md` for how the framework was obtained and
`spike/CAHost/CAHost.m` for the working reference implementation.

Status of the tree this targets: `USE_CA` is **off** today and
`WebChromeClient::allowedCompositingTriggers()`
(`Source/WebKitLegacy/mac/WebCoreSupport/WebChromeClient.h:178-188`) returns `0`,
so `RenderLayerCompositor` never builds a `GraphicsLayer`. This document
describes turning that back on, in stages.

---

## 0. The three facts that shape everything

1. **Tiger AppKit has no layer-backed `NSView`.** `setWantsLayer:` does not exist
   as a symbol or a string in Tiger's AppKit, and the WindowServer speaks no CA
   render-server protocol, so `CAContext contextWithCGSConnection:options:` will
   not composite into a window even though the selector is in the binary. The
   modern host path in
   `Source/WebKitLegacy/mac/WebView/WebHTMLView.mm:6132` (`-attachRootLayer:`),
   which does `[hostingView setLayer:]` + `[hostingView setWantsLayer:YES]`,
   cannot work as written.
2. **`CARenderer` over a raw CGL context does work**, hardware accelerated on the
   GeForce 8600M. That is also how Apple itself shipped CA on Darwin 8:
   `BackRow.framework` in the Apple TV image contains `CARenderer`,
   `rendererWithCGLContext:options:` and `setLayer:` and no `CAContext` strings at
   all.
3. **`CATiledLayer` is present but useless here.** Its class exists, it responds
   to `setTileSize:`, `setLevelsOfDetail:`, `setLevelsOfDetailBias:` and
   `+shouldDrawOnMainThread`, and its delegate `-drawLayer:inContext:` is called.
   The tiles it produces never reach a `CARenderer`-driven render tree: the page
   stays blank, no error is raised, and scrolling requests no further tiles.
   This is fine, because **WebCore does not use `CATiledLayer` any more** —
   `TileGrid.cpp:68` creates its tile container with
   `createCompatibleLayer(PlatformCALayer::LayerType::LayerTypeLayer, nullptr)`
   and its tiles are ordinary layers. WebCore's own tiling is exactly the manual
   grid `spike/CAHost` had to build by hand.

---

## 1. `PlatformCALayerTiger`

### 1.1 Shape

Do **not** fork `PlatformCALayerCocoa`. Add
`Source/WebCore/platform/graphics/ca/tiger/PlatformCALayerTiger.{h,mm}` as a
sibling implementation of the same `PlatformCALayer` interface
(`Source/WebCore/platform/graphics/ca/PlatformCALayer.h`, 412 lines), and select
it from `GraphicsLayerCA::createPlatformCALayer`
(`GraphicsLayerCA.cpp:344`) at build time. `PlatformCALayerCocoa.mm` is 1394
lines of which roughly 400 are post-Snow-Leopard; a copy that strips those is
easier to keep honest than a file with forty `#if PLATFORM(TIGER)` blocks in it,
and it never has to be merged with upstream.

Keep `Source/WebCore/platform/graphics/cocoa/WebLayer.mm` (`WebSimpleLayer`,
`-drawInContext:` at `:132`, calling
`PlatformCALayer::drawLayerContents` at `:59`) as is. That file is CA 1.0 API
throughout and compiles against the Leopard headers unchanged. It is the software
paint entry point and it is the piece `spike/CAHost` modelled with
`-drawLayer:inContext:`.

### 1.2 Maps directly, no work

These `PlatformCALayer` methods have a one-to-one CA 1.6 selector, verified
present in the Apple TV binary and exercised by the spike:

| Group | Methods |
|---|---|
| Geometry | `bounds`/`setBounds`, `position`/`setPosition`, `anchorPoint`/`setAnchorPoint`, `transform`/`setTransform`, `sublayerTransform`/`setSublayerTransform`, `masksToBounds`/`setMasksToBounds`, `geometryFlipped`/`setGeometryFlipped` (see 1.4), `isDoubleSided`/`setDoubleSided`, `cornerRadius`/`setCornerRadius` |
| Tree | `superlayer`, `removeFromSuperlayer`, `setSublayers`, `removeAllSublayers`, `appendSublayer`, `insertSublayer`, `replaceSublayer`, `adoptSublayers`, `setMaskLayer` |
| Display | `setNeedsDisplay`, `setNeedsDisplayInRect`, `contents`/`setContents`, `clearContents`, `setContentsRect`, `setMinificationFilter`, `setMagnificationFilter` |
| Appearance | `isOpaque`/`setOpaque`, `isHidden`/`setHidden`, `backgroundColor`/`setBackgroundColor`, `setBorderWidth`, `setBorderColor`, `opacity`/`setOpacity`, `setName`, `setShadowPath` |
| Timing | `setSpeed`, `setTimeOffset`, `addAnimationForKey`, `removeAnimationForKey`, `animationForKey` |

`CAShapeLayer` and `CATransformLayer` both exist, so `LayerTypeShapeLayer`
(`shapePath`, `shapeWindRule`, `kCAFillRuleNonZero`/`EvenOdd`) and
`LayerTypeTransformLayer` map directly too.

### 1.3 Needs adaptation

| `PlatformCALayer` member | Modern implementation | Tiger |
|---|---|---|
| `setContentsScale` / `contentsScale` | `PlatformCALayerCocoa.mm:977-989`, also calls `setRasterizationScale:` | Neither selector exists. Store the value in a member, return it from `contentsScale()`, and make the setter a no-op on the layer. Tiger has no HiDPI display, so the value is always 1.0 in practice; keep the member so `GraphicsLayerCA::updateContentsScale` (`GraphicsLayerCA.cpp:4441-4470`) and `TileGrid`'s `setContentsScale(deviceScaleFactor())` (`TileGrid.cpp:73,107`) keep working. |
| `setAcceleratesDrawing` / `acceleratesDrawing` | `drawsAsynchronously` (`:790-796`) | Selector absent. Member-only; getter returns false. WebCore only uses it as a hint. |
| `setIsBackdropRoot` | `setShouldRasterize:` (`:681-684`) | Selector absent. No-op. |
| `LayerTypeBackdropLayer` | `CABackdropLayer` (`:244-245`), `setWindowServerAware:` (`:290-295`) | Class absent. Gate `backdrop-filter` off entirely and fall back to `LayerTypeWebLayer`; there is no window-server-aware compositing on Tiger anyway. |
| `contentsFormat` / `updateContentsFormat` | `setContentsFormat:`, `setWantsExtendedDynamicRangeContent:`, `setToneMapMode:` (`:1191-1208`) | All absent. Hard-code `ContentsFormat::RGBA8`; the whole method body compiles out with `ENABLE(PIXEL_FORMAT_RGBA16F)` off. |
| `setAntialiasesEdges` | `setEdgeAntialiasingMask:` (`:1021-1024`) | **Present.** Keep as is. |
| `setFilters` / `copyFiltersFrom` | `PlatformCAFilters::setFiltersOnLayer` in `PlatformCAFiltersCocoa.mm` (607 lines) | `CAFilter` and `filterWithType:` exist and `[CAFilter filterWithType:@"gaussianBlur"]` works (`spike/catest.m`). Port the file but audit each `kCAFilter*` name against the binary's strings; unknown names must degrade to no filter rather than crash. Lower priority than everything else: land it after 4.3. |
| `setContents` with an `IOSurface` | `:839-849`, `PlatformCALayer.mm:236` | `HAVE(IOSURFACE)` stays **off**. Tiger has no IOSurface and the Apple TV image has none either. `CGImageRef` contents is the only path, which is what `spike/CAHost` used. |
| `setDelegatedContents` | defaults + IOSurface | Leave at the base-class defaults. |
| `isSeparated`, `isSeparatedPortal`, `appleVisualEffectData`, `setTonemappingEnabled`, `setNeedsDisplayIfEDRHeadroomExceeds` | `HAVE(CORE_ANIMATION_SEPARATED_LAYERS)` etc. | All already behind `HAVE_*` that are off. Nothing to write. |
| `enumerateRectsBeingDrawn` | `[m_layer regionBeingDrawn]` → `CGSRegionObj` (`:1354-1361`) | The `CGS*` region SPI **is** exported by Tiger's CoreGraphics (`atv/REPORT.md`), but `-regionBeingDrawn` is a CA 1.6-era private selector that must be probed with `respondsToSelector:` before use. If absent, fall back to enumerating the single clip bounding box, which is what `PlatformCALayer::drawLayerContents` does for one rect. |
| `setUsesWebKitBehavior:` / `setSortsSublayers:` | `:334-340` | Very modern SPI, absent. Skip — and see 1.5, because the sublayer-sorting behaviour those control is exactly what bites on Tiger. |
| `CASpringAnimation` | `PlatformCAAnimationCocoa.mm:177,186,337` | Class absent. Degrade to `CABasicAnimation` with a `kCAMediaTimingFunctionEaseOut` timing function, behind a single helper so the substitution is in one place. |
| `CAPresentationModifier(Group)` | not referenced in `platform/graphics/ca/` at all | Nothing to do. |
| `LayerPool` / `PlatformCALayerContentsDelayedReleaser` | `LayerPool.cpp`, `...DelayedReleaser.mm` | Plain C++/MRR, no post-SL API. Port unchanged. |

### 1.4 `geometryFlipped`

`setGeometryFlipped:` exists and works, and **there is no `-geometryFlipped`
getter** — calling it raises `NSInvalidArgumentException: selector not
recognized`. `PlatformCALayer::geometryFlipped()` must read it with
`[m_layer valueForKey:@"geometryFlipped"]` (KVC works; the property key is in the
binary) or, simpler and cheaper, track it in a member the way
`PlatformCALayerTiger::setGeometryFlipped` writes it.

Second, non-obvious: on this build **the flip cascades to the entire subtree, not
just direct sublayers.** Setting it once on the host's root layer makes every
descendant lay out top-left with y growing downward, and painted contents stay
right way up. That matches what `-[WebHTMLView attachRootLayer:]` does today at
`WebHTMLView.mm:6164-6165` (`setGeometryFlipped:YES` on the pre-Mountain-Lion
path) and means `WebLayerHostingFlippedView` (`WebHTMLView.mm:690-701`, whose
only job is `-isFlipped` → YES) stays useful for AppKit geometry even though it
will no longer be layer-backed.

`contentsAreFlipped` **is** present and returns NO, so the drawing callback must
flip the CTM itself. `PlatformCALayer::drawLayerContents`
(`PlatformCALayerCocoa.mm:1284`) already handles orientation via the
`GraphicsContext` state saver and the caller's transform; the flip belongs in
`-[WebSimpleLayer drawInContext:]`, which is where 2009's
`+[WebLayer drawContents:ofLayer:intoContext:]` put it and where
`spike/CAHost`'s `-drawLayer:inContext:` put it.

### 1.5 3D transforms need a `zPosition` — mandatory, not cosmetic

The single most surprising CA 1.6 behaviour found: **sibling layers that carry a
non-affine `CATransform3D` are depth sorted rather than painted in sublayer
order.** Half of any rotation's z range is negative, so a rotated layer renders
*behind* its own opaque siblings and looks like it has vanished. Ruled out in the
spike: `doubleSided`, `masksToBounds`, `geometryFlipped`, the sign of `m34`, and
wrapping in a `CATransformLayer`. An affine (z-axis) rotation never shows the
problem. Setting `zPosition` fixes it.

The good news is that the hook already exists.
`PlatformCALayerCocoa::setPosition(const FloatPoint3D&)`
(`PlatformCALayerCocoa.mm:632-638`) already writes
`[m_layer setZPosition:value.z()]`, and `position()` (`:626`) reads it back. The
bad news is that `GraphicsLayerCA.cpp` **never sets a non-zero z** — the only
writer of `GraphicsLayer::m_zPosition` is `GraphicsLayer.cpp:799` and nothing in
the CA path plumbs it through.

So `PlatformCALayerTiger::setTransform` must, after setting the transform, give
the layer a `zPosition` derived from the transform when the transform is
non-affine:

```
// PlatformCALayerTiger::setTransform(const TransformationMatrix& t)
[m_layer setTransform:t];
if (!t.isAffine())
    [m_layer setZPosition:m_explicitZPosition ? m_explicitZPosition : kTigerCA3DLift];
```

A constant lift (the spike used 200) is enough for a flat page where composited
3D content should sit above the document tiles. If sibling 3D layers ever need to
interleave correctly with each other, the right value is the transformed
z-extent, but that is a refinement, not a blocker. Record this as a known
divergence from upstream in the file header, because it is the kind of thing that
looks like a bug when someone diffs against `PlatformCALayerCocoa.mm`.

### 1.6 Tiling

Use WebCore's own `TileController`/`TileGrid` unchanged
(`ca/TileController.cpp` 940 lines, `ca/TileGrid.cpp` 878 lines,
`ca/TileCoverageMap.cpp` 187 lines). They already build tiles out of plain
`LayerTypeLayer` layers, already evict, already do coverage rects. The only Tiger
touch points are:

- `cocoa/WebTiledBackingLayer.mm:62` calls `[super setRasterizationScale:]` and
  `:102` overrides `-setDrawsAsynchronously:`. Both selectors are absent; delete
  those two lines in the Tiger variant.
- `PlatformCALayerCocoa.mm:325` sets `[m_layer setValue:@YES forKey:@"isTile"]`.
  Harmless KVC on an arbitrary key; keep it.
- `TileController::tileSize()` defaults to `kDefaultTileSize`, which is 512
  (`ca/TileController.h:58`, also the default `m_marginSize` at `:265`). The spike
  measured **4.1 to 5.2 ms to paint a 256 px tile** of text-heavy content on this
  hardware; a 512 px tile is four times the area, so budget 16 to 20 ms, which is
  a dropped frame. Set the Tiger default to 256 and revisit with real pages.

Memory, measured on the box for a 620x4000 page in a 645x480 viewport:

| | Resident |
|---|---|
| 256 px tiles, 9 live | 16.5 MB |
| 256 px tiles, 12 live, after a 1785 px scroll | 31.9 MB |
| One 620x4000 layer, no tiling | 24.0 MB |
| Single paint of that whole page | 93.7 ms |

Tiling wins on latency, not on bytes. On a 6 GB machine the byte count is not the
constraint; the 93 ms hitch is.

### 1.7 Implicit animations must be switched off

Every layer WebCore repositions or shows and hides needs its actions disabled or
CA cross-fades it. Upstream does this with
`[m_layer setDelegate:[WebActionDisablingCALayerDelegate shared]]`
(`PlatformCALayerCocoa.mm:321`,
`Source/WebCore/platform/graphics/cocoa/WebActionDisablingCALayerDelegate.mm`).
That class is pure CA 1.0 and works on Tiger, but note that in the host the same
object cannot also be the drawing delegate — the spike hit this. Use the
`WebActionDisablingCALayerDelegate` for layers whose contents WebCore sets
directly, and for tile layers set `actions` explicitly with `NSNull` for
`contents`, `position`, `bounds`, `onOrderIn` and `onOrderOut`.

---

## 2. The host

### 2.1 The view

Replace the `layerHostingView` created in `-[WebHTMLView attachRootLayer:]`
(`WebHTMLView.mm:6132-6166`) with a new
`Source/WebKitLegacy/mac/WebView/WebCARendererHostView.{h,mm}`:

```objc
@interface WebCARendererHostView : NSOpenGLView {
    CARenderer *_renderer;
    CALayer *_rootLayer;          // the WebRootLayer container
    NSTimer *_frameTimer;         // or a CVDisplayLink, see 2.4
    BOOL _needsRender;
}
- (void)setRootLayer:(CALayer *)layer;
- (void)setNeedsRenderInRect:(CGRect)dirty;
@end
```

`-attachRootLayer:` keeps its current structure: lazily create the host view with
`NSViewWidthSizable | NSViewHeightSizable`, `addSubview:`, create the
`WebRootLayer` container (`WebHTMLView.mm:702-716`, whose `-renderInContext:`
NOOP stays correct), `addSublayer:` the layer WebCore passed, post
`_WebViewDidStartAcceleratedCompositingNotification`, and
`setGeometryFlipped:YES` on the container. The two lines that change are
`setLayer:` / `setWantsLayer:YES` (`:6155-6156`), which become
`[hostView setRootLayer:container]`. `-detachRootLayer` (`:6168-6176`) drops the
renderer and removes the view.

Keep `WebLayerHostingFlippedView` for AppKit-side geometry, or make
`WebCARendererHostView` override `-isFlipped` → YES itself and delete the extra
view. Prefer the latter: one fewer view in the hierarchy, and `NSOpenGLView`
surface placement is easier to reason about without an intermediate.

### 2.2 CGL context lifecycle

- Pixel format: `NSOpenGLPFADoubleBuffer`, `NSOpenGLPFAAccelerated`,
  `NSOpenGLPFAColorSize 32`, `NSOpenGLPFADepthSize 24`. Falling back to software
  if `initWithAttributes:` returns nil is worth doing, because the spike's
  `carendertest` already has that fallback and CA renders correctly on the
  software renderer, just slowly.
- In `-prepareOpenGL`: `CGLSetParameter(cgl, kCGLCPSwapInterval, &one)`,
  `CGLSetCurrentContext(cgl)`, then
  `[CARenderer rendererWithCGLContext:cgl options:nil]` **retained** (the class
  method returns an autoreleased object).
- **`CGLSetCurrentContext` before every frame.** AppKit and any other GL client
  in the process can and will switch the current context between frames.
- On `-reshape` and on window resize: `glViewport`, then
  `glMatrixMode(GL_PROJECTION)` / `glLoadIdentity` /
  `glOrtho(0, W, 0, H, -1, 1)` exactly as `CARenderer.h` prescribes, then set the
  root layer's bounds inside a transaction with `kCATransactionDisableActions`,
  then `[renderer setBounds:]`. Getting only two of those three produces content
  that is clipped or stretched with no error.

### 2.3 The frame

```objc
CGLSetCurrentContext(cgl);
[CATransaction flush];                 // mandatory; see below
glClear(GL_COLOR_BUFFER_BIT);
[_renderer beginFrameAtTime:CACurrentMediaTime() timeStamp:NULL];
[_renderer addUpdateRect:dirty];       // mandatory
[_renderer render];
[_renderer endFrame];
[[self openGLContext] flushBuffer];
```

`[CATransaction flush]` is not optional. CA normally commits from a run-loop
observer that AppKit installs for layer-backed views; without it the render tree
stays empty and every frame renders black with no error and no exception. This is
the single most likely way for a first integration to appear completely broken.

`addUpdateRect:` is likewise mandatory — `CARenderer` only draws the accumulated
update region. Union WebCore's damage (what arrives through
`PlatformCALayer::setNeedsDisplayInRect`, ultimately
`GraphicsLayer::setNeedsDisplayInRect`) with `[renderer updateBounds]`, which is
CA's own accumulated damage for animations WebCore does not know are running.
`spike/CAHost` passes the whole view bounds every frame, which is correct but
wasteful; real damage plumbing is worth doing once frame time matters.

Measured cost on the box, 645x480, GeForce 8600M:

| | ms/frame |
|---|---|
| `flush` + `beginFrame`/`addUpdateRect`/`render`/`endFrame`, at rest | 0.44 to 0.59 |
| same, during a 900 px/s scroll of a tiled page | 0.87 |
| `flushBuffer` (vsync wait) | the rest of the frame |

Core Animation is not the budget. The swap is.

### 2.4 What drives the frame

`[renderer nextFrameTime]` returns `+infinity` when nothing is animating, the
current frame time when a continuous animation is running, and a specific time
otherwise. Use it: render on demand, and only schedule the next frame when
`nextFrameTime` is finite or WebCore has posted damage. A free-running 60 Hz timer
keeps a Core 2 Duo busy for nothing.

Prefer `CVDisplayLink` (CoreVideo 1.4.1 is on Tiger) over `NSTimer` for the
animating case, but note the display link callback runs on its own thread, so it
must only signal the main thread; all CA work stays on the main thread.
`spike/CAHost` used an `NSTimer` on `kCFRunLoopCommonModes` (the constant
`NSRunLoopCommonModes` does not exist on Tiger; the CoreFoundation one does) and
that is a perfectly good first implementation.

This driver is the same one §5.2 of the plan needs and replaces
`Source/WebKitLegacy/mac/WebView/WebViewRenderingUpdateScheduler.mm` (197 lines),
whose `registerCACommitHandlers()` uses
`[CATransaction addCommitHandler:forPhase:]`, a post-Snow-Leopard API that does
not exist here. The Tiger scheduler calls `-[WebView _updateRendering]`
(`WebView.mm:8800-8810`) and then `-[WebView _flushCompositingChanges]`
(`:8839-8847`) from its own tick, in that order, before the `CARenderer` frame.

### 2.5 How the software and composited paths coexist

They already do, and the existing structure survives:

- `-[WebHTMLView viewWillDraw]` (`WebHTMLView.mm:1592-1603`) runs
  `_web_updateLayoutAndStyleIfNeededRecursive` then `_flushCompositingChanges`.
  Unchanged.
- `-[WebHTMLView drawRect:]` (`:3891-3975`) keeps painting the non-composited
  document through `-drawSingleRect:` (`:3845-3889`) into the window's graphics
  context. When compositing is on, WebCore stops painting the composited subtrees
  into that context and the host view's GL surface covers the region it owns.
- The block at `:3956-3970` that does `disableScreenUpdatesUntilFlush` +
  `[CATransaction flush]` when `_needsOneShotDrawingSynchronization`
  (`WebView.mm:8776-8785`) is exactly the synchronisation point we need, and it
  already calls the right thing. Extend it to also force one `CARenderer` frame
  so the GL surface and the window backing store change in the same screen
  update.
- `-[WebHTMLView _setAsideSubviews]` (`:1560-1591`) must keep the host view in
  the subview list, same as it keeps `layerHostingView` today, "otherwise the
  layers flash".
- Root layer content: WebCore paints it through
  `-[WebSimpleLayer drawInContext:]` →
  `PlatformCALayer::drawLayerContents` → `PlatformCALayerClient::platformCALayerPaintContents`
  → `GraphicsLayerCA::paintGraphicsLayerContents` → `FrameView::paint`. That
  whole chain is CA 1.0 API and needs no Tiger variant. `spike/CAHost` stands in
  for it with `paintPage()` behind `-drawLayer:inContext:` and confirms the
  mechanics: flip the CTM, translate by the tile's origin so the paint code works
  in page coordinates, take the dirty rect from `CGContextGetClipBoundingBox`.
- **One process-wide requirement:** `dlopen` the rebundled private QuartzCore
  before the first `CALayer` message, since nothing links it at build time and
  the fragile runtime resolves classes lazily. Its install name must be
  `@executable_path/../Frameworks/QuartzCore.framework/Versions/A/QuartzCore`
  (Tiger's dyld has no `@rpath`). `spike/CAHost/rebundle.sh` is the reference.

---

## 3. Coordinates, hit testing and scrolling

### 3.1 Coordinates

Three spaces, and they must be kept straight:

| Space | Origin | Who uses it |
|---|---|---|
| AppKit view | top-left, because `WebHTMLView` is flipped | events, `-drawRect:`, `NSScroller` |
| GL / `CARenderer` | bottom-left, set by the `glOrtho` in 2.2 | the renderer's own root layer |
| Page / layer | top-left, because the root layer is `geometryFlipped` | everything WebCore hands us |

The host's root layer gets `anchorPoint` (0,0), `position` (0,0), bounds equal to
the view bounds, `masksToBounds:YES`, and `setGeometryFlipped:YES`. That single
flip makes the layer space agree with WebCore's, and because it cascades (1.4)
nothing below has to be flipped again.

Converting an AppKit event point to a page point is then
`page.y = (viewHeight - view.y) + scrollOffset.y` with `page.x = view.x`, which
is what `-[CAHostView pagePointForViewPoint:]` does in the spike.

### 3.2 Hit testing

`-[CALayer hitTest:]` exists, works, and honours the root layer's bounds origin,
so a scrolled tree hit-tests correctly. Verified in the spike: after scrolling
1785 px, page (500,1900) returns the composited overlay, page (100,1885) returns
the tile that covers it, and a point past the page width returns the root layer.

This matters less than it sounds. WebCore hit-tests the render tree, not the
layer tree; `-[WebHTMLView hitTest:]` already bails when the hit view is the
layer host (`WebHTMLView.mm:1748`) so events fall through to the document. Keep
that behaviour with the new view class. CA hit testing is useful for debugging
and for the eventual event-region work, not for normal input.

### 3.3 Scrolling

Two candidate mechanisms, and the choice is not ours to make freely:

1. **Move the host's root layer bounds origin.** Cheap: 0.87 ms/frame measured,
   no tile repainted, no layer invalidated. This is what `spike/CAHost` does.
2. **Let WebCore scroll.** `RenderLayerCompositor` builds a scrolling layer tree
   and `GraphicsLayerCA` repositions the content layer; `TileController` revalidates
   tiles against the new coverage rect.

Use (2) — the WebCore path — because WebKitLegacy on Mac has no asynchronous
scrolling and the main-thread scroll already moves the layers. (1) is the right
model only for the host's own idea of the visible rect, which it needs to pass to
`TiledBacking::setVisibleRect` so tile coverage tracks the scroll.

One sign trap worth writing down: under `geometryFlipped`, scrolling **down**
means **increasing** `bounds.origin.y`. The unflipped intuition gives the opposite
sign and moves the content off screen silently, with no error and no visual clue
other than a blank window.

---

## 4. Phase plan

Each step is independently shippable and independently revertible.

### 4.0 Today — compositing off

`USE_CA 0`, `USE_CORE_IMAGE 0`,
`WebChromeClient::allowedCompositingTriggers()` returns `0`,
`platform/graphics/ca/*` excluded from the build,
`platform/graphics/tiger/GraphicsLayerTiger.cpp` supplies the
`GraphicsLayer::create` that `GraphicsLayerCA.cpp:326` would have. No change.

### 4.1 Composited root layer, software-painted tiles

Turn on the minimum that produces one composited root.

| Flag | From | To |
|---|---|---|
| `USE_CA` (`PlatformUse.h:62`) | 0 | **1** |
| `HAVE_IOSURFACE` and satellites (`PlatformHave.h:379,391,395,399,403,409,905`) | off | **stay off** |
| `USE_CORE_IMAGE` (`:66`) | 0 | stay 0 until 4.3 |
| `ENABLE_PIXEL_FORMAT_RGBA16F` | off | stay off |
| `HAVE_CORE_ANIMATION_SEPARATED_LAYERS`, `HAVE_CORE_MATERIAL` | off | stay off |
| `WebChromeClient::allowedCompositingTriggers()` (`WebChromeClient.h:178`) | `0` | **`AnimatedOpacityTrigger`** alone. One trigger is enough to make `RenderLayerCompositor::enableCompositingMode` build the root layer, and opacity exercises the root and the tiles without touching 3D, which is not safe until 4.2. Enum at `page/ChromeClient.h:506-516`. |

Build back in: `GraphicsLayerCA.cpp`, `PlatformCALayer.mm`, the new
`PlatformCALayerTiger.mm`, `TileController.cpp`, `TileGrid.cpp`,
`TileCoverageMap.cpp`, `LayerPool.cpp`, `TransformationMatrixCA.cpp`,
`cocoa/WebLayer.mm`, `cocoa/WebActionDisablingCALayerDelegate.mm`,
`cocoa/WebTiledBackingLayer.mm` (minus the two lines in 1.6). Keep
`PlatformCAAnimationCocoa.mm` and `PlatformCAFiltersCocoa.mm` **out**.

Gate: a `file://` page scrolls, painted entirely through tiles, pixel-identical
to the software path. No animation, no transform.

### 4.2 Accelerated sublayers: transforms, opacity, animations

Add `PlatformCAAnimation.cpp` and a `PlatformCAAnimationTiger.mm` (the
`CASpringAnimation` degradation from 1.3). Widen
`allowedCompositingTriggers()` to `ThreeDTransformTrigger | AnimationTrigger |
AnimatedOpacityTrigger | CanvasTrigger`. Land the `zPosition` rule from 1.5 *before* enabling the 3D
trigger, or every `translate3d`/`rotate3d` element will appear to vanish.

Gate: a CSS `transform: rotate3d` animation runs on the GPU and is visible.

### 4.3 Filters

`PlatformCAFiltersCocoa.mm` with a `kCAFilter*` audit, `USE_CORE_IMAGE` on,
`FilterTrigger` added (`page/ChromeClient.h:512`). `backdrop-filter` stays off permanently (no
`CABackdropLayer`).

### 4.4 Video — separate track

`AVPlayerLayer` does not exist on Tiger; video is QuickTime/QTKit. Not part of
this design. `LayerTypeAVPlayerLayer` stays unreachable, and `VideoTrigger` stays off.

---

## 5. Size and risk

### 5.1 Estimated new or modified lines

| Piece | LOC | Note |
|---|---|---|
| `ca/tiger/PlatformCALayerTiger.{h,mm}` | ~1100 | `PlatformCALayerCocoa` minus ~400 lines of post-SL paths, plus ~150 of member-backed substitutes |
| `WebKitLegacy/mac/WebView/WebCARendererHostView.{h,mm}` | ~450 | `spike/CAHost/CAHost.m` is 700 lines including its fake page and instrumentation; the host itself is about 450 |
| `WebHTMLView.mm` edits (`attachRootLayer:`, `detachRootLayer`, `_setAsideSubviews`, the `drawRect:` sync block) | ~80 | mostly deletions |
| Tiger rendering-update scheduler replacing `WebViewRenderingUpdateScheduler.mm` | ~200 | shared with plan §5.2, not new cost here |
| `ca/tiger/PlatformCAAnimationTiger.mm` (4.2) | ~600 | `PlatformCAAnimationCocoa.mm` is 598 |
| `PlatformCAFiltersCocoa.mm` audit (4.3) | ~100 changed of 607 | |
| `cocoa/WebTiledBackingLayer.mm` Tiger variant | ~10 changed of 156 | |
| Build-system and flag changes | ~60 | |
| **Total through 4.2** | **~2400 new, ~150 changed** | against ~14700 lines of upstream `ca/` that get reused unmodified |

That is consistent with the plan's 15 to 25 engineer-day estimate for M8, and the
reuse ratio is the point: roughly six lines of upstream code reused per line
written.

### 5.2 Risks, worst first

1. **`CATiledLayer` is dead and `TileController` has never run on a
   `CARenderer`.** The spike proved plain-`CALayer` tiles composite correctly and
   that `TileGrid` already builds exactly that, but `TileController`'s coverage
   and revalidation logic has never been exercised without a window server. This
   is the largest unknown in 4.1. Mitigation: bring `TileController` up against
   the spike harness before wiring it into WebCore.
2. **The depth-sort surprise (1.5).** Cheap to fix once known, invisible and
   maddening if forgotten. It cost most of a session in phase 2. Write the
   comment.
3. **`[CATransaction flush]` and `addUpdateRect:` being forgotten** in any new
   code path produces a silently black window. Put both in one place in the host
   and never call the renderer from anywhere else.
4. **The forgotten-getter class of bug.** `geometryFlipped`, and anything else
   whose setter exists and getter does not, raises at runtime rather than failing
   to compile, because the Leopard headers declare properties the binary only
   half implements. Every CA selector `PlatformCALayerTiger` calls that is not on
   the verified list in 1.2 should be `respondsToSelector:`-probed once at startup
   and logged, not discovered in the field.
5. **Availability annotations.** The Leopard QuartzCore headers mark the entire CA
   API `AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER`, which is a hard error at a
   10.4 deployment target. Every translation unit that imports them must
   `#undef` that macro first, as `spike/CAHost/CAHost.m` does. This belongs in
   one compat header, not in forty files.
6. **Paint cost, not composite cost.** 4.1 to 5.2 ms per 256 px tile of
   text-heavy content means a full-viewport repaint at 645x480 costs 40 to 50 ms.
   Tiling converts that into a series of smaller hitches; it does not make it
   fast. Any regression here will look like a compositing problem and will not be
   one.
7. **Class-name collision.** The rebundled framework still defines 207 `CI*`
   classes that duplicate Tiger's own QuartzCore. Nothing in the base system pulls
   QuartzCore in implicitly (AppKit, Quartz, QTKit and ApplicationServices all
   link it zero times), but any future dependency that does will produce
   duplicate-class chaos. Never install into `/System`; keep the private install
   name.

---

## Reference implementation

`spike/CAHost/` on the target box: `CAHost.m` (the host and a fake 620x4000
tiled page), `rebundle.sh` (the private framework), `Makefile`,
`phase2-before.png` / `phase2-after.png` / `phase2-resized.png` (scrolling and
resize on the 10.4.11 machine). Environment switches in that binary:
`CAHOST_AUTO` scripted scroll with timings, `CAHOST_PLAIN` one whole-page layer
instead of tiles, `CAHOST_CATILED` the `CATiledLayer` path that does not
composite, `CAHOST_RESIZE` programmatic resize.
