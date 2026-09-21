# WinCairo's GPU-process 2D remoting, as a recipe for our two builds

Read-only extraction from the tree at `WebKit/` (d2f52605). Everything below is quoted with
file and line. Nothing under `WebKit/` was modified.

Two corrections to the brief before anything else, because both change the plan:

- **The Windows port in this checkout is Skia, not Cairo.** `OptionsWin.cmake:91` sets
  `USE_SKIA PRIVATE ON`, and `OptionsWin.cmake:166-174` takes Cairo only in the `else()`
  arm. There is no `OptionsWinCairo.cmake`. So "WinCairo still has cairo locally" is no
  longer true upstream; the port that proves non-Cocoa 2D remoting now proves it with Skia
  underneath. This does not weaken the precedent, but it does mean we cannot copy a
  Cairo-shaped configuration from it.
- **`RemoteDisplayListRecorder` is not the wire.** `RemoteGraphicsContext` is.
  `RemoteDisplayListRecorder.messages.in` declares one message and is in **no** build
  system: absent from `WebKit_MESSAGES_IN_FILES` and from `DerivedSources.make`, and
  nothing includes `RemoteDisplayListRecorderMessages.h`. The 103 drawing messages live on
  `RemoteGraphicsContext` and `RemoteDisplayListRecorder` inherits them by the
  `: RemoteGraphicsContext` superclass syntax.

## 1. What the port turns on, and what that gets

```
Source/cmake/OptionsWin.cmake:105   ENABLE_GPU_PROCESS   PRIVATE ON
Source/cmake/OptionsWin.cmake:100   ENABLE_WEBGL         PUBLIC  ON
Source/cmake/OptionsWin.cmake:123   USE_GRAPHICS_LAYER_TEXTURE_MAPPER ON
Source/cmake/OptionsWin.cmake:124   USE_GRAPHICS_LAYER_WC             ON
Source/cmake/WebKitFeatures.cmake:227  ENABLE_GPU_PROCESS defaults OFF
```

`USE_GRAPHICS_LAYER_WC` is the switch that matters, and it does three things beyond
compiling the `wc` directories:

- `Source/WebKit/WebProcess/WebPage/DrawingArea.cpp:78-79` selects `DrawingAreaWC`.
- `Source/WebKit/Shared/WebPreferencesDefaultValues.cpp:330-332` makes
  `defaultUseGPUProcessForDOMRenderingEnabled()` return true, so **DOM rendering goes to
  the GPU process by default** with no preference plumbing of our own.
- `UnifiedWebPreferences.yaml:6414` and `:6460` default canvas and WebGL rendering to the
  GPU process for the same ports.

The preferences themselves: `UseGPUProcessForDOMRenderingEnabled`
(`UnifiedWebPreferences.yaml:6417-6426`, `sharedPreferenceForWebProcess: true`),
`UseGPUProcessForCanvasRenderingEnabled` (`:6404-6415`),
`UseGPUProcessForWebGLEnabled` (`:6451-6462`), `UseGPUProcessForMediaEnabled`
(`:6438-6449`). **There is no `displayListEnabled` preference**; `RenderingMode::DisplayList`
is chosen by callers.

### The source lists are unconditional

`Source/WebKit/Sources.txt` contains no preprocessor conditionals at all. The guarding is
inside the `.cpp` files with `#if ENABLE(GPU_PROCESS)`.

GPU side, `Sources.txt:32-48`: `ImageBufferShareableAllocator`, `RemoteDisplayListRecorder`,
`RemoteGraphicsContext`, `RemoteGraphicsContextGL[FunctionsGenerated]`, `RemoteImageBuffer`,
`RemoteImageBufferGraphicsContext`, `RemoteImageBufferSet`, `RemoteRenderingBackend`,
`RemoteResourceCache`, `RemoteSnapshot[Recorder]`, `ScopedRenderingResourcesRequest`,
`ScopedWebGLRenderingResourcesRequest`, `ShareablePixelBuffer`, `SharedFont`.

Web side, `Sources.txt:826-839`: `ImageBufferRemoteDisplayListBackend`,
`ImageBufferRemotePDFDocumentBackend`, `ImageBufferShareableBitmapBackend`,
`PrepareBackingStoreBuffersData`, `RemoteDisplayListRecorderProxy`,
`RemoteGraphicsContextProxy`, `RemoteImageBufferProxy`, `RemoteImageBufferSetProxy`,
`RemoteNativeImageProxy`, `RemoteSnapshotRecorderProxy`, `RemoteRenderingBackendProxy`,
`RemoteResourceCacheProxy`.

`RemoteSerializedImageBufferProxy` is not a file: it is a class inside
`WebProcess/GPU/graphics/RemoteImageBufferProxy.h`, driven by `MoveToSerializedBuffer` /
`MoveToImageBuffer` (`RemoteRenderingBackend.messages.in:69-70`).

Message generation is unconditional too, `Source/WebKit/CMakeLists.txt:196-201` and
`:354-356`. The only port-specific file is `Source/WebKit/Platform/WC.cmake`, pulled in from
`PlatformWin.cmake:8`.

### Transport

`Platform/Sources.txt:20-24`: `StreamClientConnection`, `StreamConnectionBuffer`,
`StreamConnectionWorkQueue`, `StreamServerConnection`. The buffer classes
(`StreamConnectionBuffer`, `StreamClientConnectionBuffer`, `StreamServerConnectionBuffer`,
`StreamConnectionEncoder`) have **zero preprocessor conditionals**. `StreamServerConnection`
has no `PLATFORM(COCOA)` at all. `StreamClientConnection.cpp:31-33` has exactly one, a
CoreFoundation include. `StreamConnectionWorkQueue.cpp:144-146` wraps an autorelease pool in
`USE(FOUNDATION)`.

`IPC::Semaphore` (`Platform/IPC/IPCSemaphore.h:34-41`, `:70-85`, `:90-97`) is a three-way
Cocoa / Windows / Unix split with a live `#else` arm. `SharedMemory`
(`Source/WebCore/platform/SharedMemory.h:65-71` and friends) likewise.

`ShareableBitmap` (`Source/WebCore/platform/graphics/ShareableBitmap.h`) has **no
`PLATFORM(COCOA)` anywhere** — its fifteen conditionals are all `USE(CG)` / `USE(CAIRO)` /
`USE(SKIA)`. That is the 2D-library problem, not a Cocoa problem, and section 2 is where it
bites.

## 2. Turning off local raster in the web process

### The routing

`WebCore::ImageBuffer::create` (`Source/WebCore/platform/graphics/ImageBuffer.cpp:80-121`)
consults the client hook **first**, at `:84-87`, and only falls through to the local switch
at `:89-117` if it declines.

The hook is on `GraphicsClient`, not `ChromeClient` directly:
`Source/WebCore/platform/GraphicsClient.h:66` declares
`createImageBuffer(const FloatSize&, RenderingMode, RenderingPurpose, float, const ColorSpace&, ImageBufferFormat)`
pure virtual, with `:69` `sinkIntoImageBuffer`. `ChromeClient.h:468` re-declares it
returning `nullptr`. WebKit overrides at
`WebProcess/WebCoreSupport/WebChromeClient.cpp:1175-1188`, whose body is the whole decision:

```
1177:  if (WebProcess::singleton().shouldUseRemoteRenderingFor(purpose))
1181:      return ...ensureRemoteRenderingBackendProxy()->createImageBuffer(...)
1184:  // ShareableSnapshot / ShareableLocalSnapshot -> local ImageBufferShareableBitmapBackend
1187:  return nullptr;
```

`WebProcess::shouldUseRemoteRenderingFor` (`WebProcess.cpp:2579-2596`) maps
`RenderingPurpose` to the three preference flags; `ShareableLocalSnapshot` and `Unspecified`
are always local.

**The pivot is one function.** `RemoteRenderingBackendProxy.cpp:258-263`:
`canMapRemoteImageBufferBackendBackingStore()` returns
`!shouldUseRemoteRenderingFor(RenderingPurpose::DOM)`. When DOM rendering is remoted the web
process does not map the backing store, so `getPixelBuffer` goes over IPC
(`RemoteImageBufferProxy.cpp:343-364`, IPC arm at `:359`) and the recorder
(`RemoteDisplayListRecorderProxy`, a `RemoteGraphicsContextProxy`, returned from
`RemoteImageBufferProxy.cpp:386-389`) is what page painting draws into.

`GraphicsContext::platformContext()` defaults to `nullptr`
(`Source/WebCore/platform/graphics/GraphicsContext.h:83`) and
`PlatformGraphicsContext.h:54-55` has a real `#else using PlatformGraphicsContext = void;`,
so `GraphicsContext`, `NullGraphicsContext` and the recorder proxy all compile with **no 2D
library at all**. That is the good news.

### What still needs local raster, with call sites

| Need | Where | Status with remoting on |
|---|---|---|
| canvas `getImageData` / `putImageData` | `CanvasRenderingContext2DBase.cpp:2692`, `:2709`, `:2765` | **fully remoted** over IPC as a plain `PixelBuffer` |
| canvas `toDataURL` / `toBlob` | `HTMLCanvasElement.cpp:693-723`, `:730-760` → `ImageUtilities.cpp:92-96` | **needs local raster**: `platformEncodeData` (`ImageUtilities.h:95`) exists only for CG, Cairo and Skia |
| — its worst call | `ImageUtilities.cpp:48` | creates an `ImageBuffer` with **no `GraphicsClient`**, so the hook is skipped and it lands on `ImageBufferPlatformBitmapBackend` |
| stroke hit testing | `Path::strokeContains` (`Path.cpp:411`); callers `CanvasRenderingContext2DBase.cpp:1333-1350`, `RenderSVGShape.cpp:96,101,284`, `SVGRenderSupport.cpp:485` | **needs a real platform context**; e.g. `PathCairo.cpp:435` constructs `GraphicsContextCairo` then `cairo_in_stroke` |
| image decoding to `NativeImage` | `ImageDecoder.cpp:50-76`; `ScalableImageDecoder.h:136` | **always local, never remoted**; `PlatformImage.h:43-49` has no `#else` |
| text measurement and font metrics | no `RemoteFont*` exists in `WebProcess/GPU/graphics/` | **always local**; fonts are serialized *to* the GPU process for drawing only |

Two more traps worth naming: `RemoteImageBufferProxy.cpp:315` (`sinkIntoBufferForDifferentThread`)
and `:340` (`filteredNativeImage`) both create local unaccelerated buffers or platform images.

### The compile-time wall

`ImageBufferShareableBitmapBackend.h:32-38` and `:47-53` are a three-way `USE(CG)` /
`USE(CAIRO)` / `USE(SKIA)` `using` with **no `#else`**, so line 55 fails to compile with
none of the three. `ImageBufferPlatformBackend.h:28-47`, `PlatformImage.h:43-49` and
`PlatformPath.h:34-42`, `:46-55` are the same shape.

This matters more than it looks, because `RemoteRenderingBackendProxy.cpp:289` selects
`ImageBufferShareableBitmapBackend` for `RenderingMode::Unaccelerated` **even on the remote
path**, and `DrawingAreaWC.cpp:415` passes exactly that mode for its tiles.

`NullGraphicsContext` (`Source/WebCore/platform/graphics/NullGraphicsContext.h`, header-only)
is a real used class: `paintingDisabled()` returns true at `:57`, every drawing entry is an
empty final override, and it is instantiated at `LocalFrameView.cpp:5760`, `:5771`, `:6359`,
`ContentfulPaintChecker.cpp:42`, `RenderImageResource.cpp:134`, `ShapeOutsideInfo.cpp:283`,
`RenderLayerBacking.cpp:2376`, `SVGRenderTreeAsText.cpp:466`, `StyleFilterImage.cpp:154`,
and as a member of `NullImageBufferBackend.h:55`,
`ImageBufferRemoteDisplayListBackend.h:59` and `ImageBufferRemotePDFDocumentBackend.h:59`.
`NullImageBufferBackend` is **not** selectable from `RenderingMode`; it is the GPU process's
failed-allocation placeholder, instantiated only at `RemoteRenderingBackend.cpp:366` and
`RemoteImageBufferSet.cpp:158`.

### Recommendation

**Keep a local software raster in the 64-bit web process, and never let it reach the
display.** Not Cairo: build `ImageBufferShareableBitmapBackend` against our own minimal
backend so the three-way `using` has a fourth arm, or add the missing `#else`.

The reasoning is that the four remaining needs are not optional and not remotable as the
tree stands. Image decoding has no GPU-process path at all, font metrics have none,
`Path::strokeContains` needs a platform path, and `toDataURL` needs an encoder. Trying to
run with `NullGraphicsContext` everywhere would take all four out of service, and two of
them, decoding and metrics, are load-bearing for any page.

The cheapest honest shape is a scratch-only raster: a `PlatformImage` that is a plain
32-bit buffer, a `PlatformPath` from WebCore's own path geometry, and the cross-platform
`ScalableImageDecoder` family for decode. Page painting still records into
`RemoteDisplayListRecorderProxy` and never touches it. That keeps one rule easy to enforce
and to test: **the web process may allocate pixels, but it may never present them.**

## 3. The two gated messages and the three encodings

Message counts, all wrapped whole in `#if ENABLE(GPU_PROCESS)`:

| File | Messages | Conditional |
|---|---|---|
| `GPUProcess/graphics/RemoteGraphicsContext.messages.in` | 103 | 5 |
| `GPUProcess/graphics/RemoteRenderingBackend.messages.in` | 41 | 3 |
| `GPUProcess/graphics/RemoteImageBuffer.messages.in` | 13 | 1 |
| `GPUProcess/graphics/RemoteImageBufferSet.messages.in` | 4 | 1 |
| `WebProcess/GPU/graphics/RemoteRenderingBackendProxy.messages.in` | 2 | 0 |
| `WebProcess/GPU/graphics/RemoteImageBufferProxy.messages.in` | 1 | 0 |

`RemoteResourceCache` and `RemoteResourceCacheProxy` have **no** `.messages.in`: they are
plain caches driven by the `Cache*` / `Release*` messages on `RemoteRenderingBackend`.

**The two CoreGraphics-gated drawing messages**, which is what the survey counted:

```
RemoteGraphicsContext.messages.in:125  #if USE(CG)
RemoteGraphicsContext.messages.in:126      ApplyStrokePattern() StreamBatched
RemoteGraphicsContext.messages.in:127      ApplyFillPattern() StreamBatched
RemoteGraphicsContext.messages.in:128  #endif
```

Also off for us, and fine: `RemoteGraphicsContext.messages.in:135-139`
(`PLATFORM(COCOA) && ENABLE(VIDEO)`: `DrawVideoFrame`, `SetSharedVideoFrameSemaphore`,
`SetSharedVideoFrameMemory`) and `RemoteRenderingBackend.messages.in:57-60`
(`PLATFORM(COCOA)`: `PrepareImageBufferSetsForDisplay[Sync]`). The one message that exists
**only** for the WC ports is `RemoteRenderingBackend.messages.in:65-67`,
`Flush(IPC::Semaphore semaphore) NotStreamEncodable` under `USE(GRAPHICS_LAYER_WC)` — we
want that one.

### The three encodings, exactly

All three are in `Source/WebKit/Shared/WebCoreArgumentCoders.serialization.in` except the
font one.

**1. `ColorSpace`, `:1200-1228`.** Three arms. With CG it is a `RetainPtr<CGColorSpaceRef>`;
with Skia an `sk_sp<SkColorSpace>`; **with neither it is already the neutral form we want**:

```
1206: [Nested] enum class WebCore::PlatformColorSpace::Name : uint8_t {
1207:         SRGB
1208:         , LinearSRGB
1209: #if ENABLE(DESTINATION_COLOR_SPACE_DISPLAY_P3)
1210:         , DisplayP3, LinearDisplayP3
1212: };
1215: [AdditionalEncoder=StreamConnectionEncoder] class WebCore::PlatformColorSpace {
1216:     WebCore::PlatformColorSpace::Name get();
1217: };
```

and `:1221-1228` picks the validated form for `USE(CG) || USE(SKIA)` and the plain one
otherwise. **So the neutral encoding already exists and is reached by having neither CG nor
Skia.** The work is not writing it, it is making the 32-bit replay side use the same arm
while still having CG locally. That side must compile this coder with CG *off* and map the
enumerator to a real `CGColorSpaceRef` at the point of use.

**2. `ShareableBitmapConfiguration`, `:6538-6551`.** Neutral core is size, colour space,
pixel format, headroom, opacity and three validated integers. The CG arm adds two fields:

```
6547: #if USE(CG)
6548:     CGBitmapInfo m_bitmapInfo;
6549:     std::optional<WebCore::ShareableGainMap> m_shareableGainMap;
6550: #endif
```

Note it embeds `WebCore::ColorSpace` at `:6540`, so it inherits problem 1.

**3. `FontPlatformDataAttributes`**, declared `Source/WebCore/platform/graphics/FontPlatformData.h:107-154`,
coded `Source/WebKit/Shared/WebCoreFont.serialization.in:86-102`. Four arms. The neutral core
is `FontMetadata` (`FontPlatformData.h:95-105`): point size, orientation, width variant, text
rendering mode, two synthetic flags and metrics overrides — **no font identity at all**. The
CoreText arm adds `serializableAttributes()`, `m_options`, `m_url`, `m_psName`; the Skia arm
adds family name, style and HarfBuzz features; the `PLATFORM(WIN) && USE(CAIRO)` arm adds a
`LOGFONT`.

This is the one that needs new work rather than a switch. Neither existing neutral arm
carries enough to name a font, so the recording side must send something the replay side can
resolve. The 32-bit side is authoritative for metrics in the decided architecture, so the
natural encoding is an index into the font manifest it generated, plus `FontMetadata`.

## 4. The layer delta, and what a Core Animation applier must do

One message carries an entire frame's compositing update:

```
GPUProcess/graphics/wc/RemoteWCLayerTreeHost.messages.in:31
    Update(struct WebKit::WCUpdateInfo updateInfo) -> (std::optional<WebKit::UpdateInfo> updateInfo)
```

The whole subsystem is **four messages**: that one, `CreateWCLayerTreeHost` and
`ReleaseWCLayerTreeHost` (`GPUConnectionToWebProcess.messages.in:62-63`), and
`UpdateGeometryWC` (`WebProcess/WebPage/DrawingArea.messages.in:54`).

`WCUpdateInfo` (`WebProcess/WebPage/wc/WCUpdateInfo.h:124`) is six fields: viewport, a remote
context identifier, the root layer id, `addedLayers`, `removedLayers`, and
`Vector<WCLayerUpdateInfo> changedLayers`.

`WCLayerUpdateInfo` (`:78`) is an id, an `OptionSet<WCLayerChange> changes` bitset of 29 bits
(`:46`), and then every property: children, mask and replica ids, position, anchor point,
size, bounds origin, seven booleans, solid and debug-border colours, opacity, border width,
repaint count, contents rect, a `BackgroundChanges` block carrying colour plus
`Vector<WCTileUpdate>`, transform and children transform, filters and backdrop filters and
their rect, contents clipping rect, a `PlatformLayerChanges` block for WebGL buffers, and a
host identifier.

**The delta trick is in the serialization, and it is reusable verbatim.**
`WCUpdateInfo.serialization.in:80` marks the bitset `[OptionalTupleBits]` and every
subsequent member carries `[OptionalTupleBit=WebKit::WCLayerChange::X]`, so the generated
coder writes the bitset and then only the fields whose bit is set. Tiles ride as
`WCBackingStore`, one shared-memory handle each
(`WCBackingStore.serialization.in:25`).

Production side: `GraphicsLayerWC` (`GraphicsLayerWC.h:41`) notes a dirty bit in every setter
(`noteLayerPropertyChanged`, `.h:119`, field `.h:136`), and
`GraphicsLayerWC.cpp:542` `flushCompositingStateForThisLayerOnly()` builds the update with
`.changes = std::exchange(m_uncommittedChanges, { })` at `:548`, paints dirty tiles at `:594`,
and hands it to the observer at `:633`. `DrawingAreaWC` accumulates
(`DrawingAreaWC.cpp:398`, `:404`, `:407-410`) and ships one frame in `sendUpdateAC()` at
`:257`, swapping the accumulator at `:290`.

Consumption side is one function: `WCScene::update` (`WCScene.cpp:99`), a dirty-bit `if`
chain at `:110-249` over a `HashMap<PlatformLayerIdentifier, std::unique_ptr<Layer>>`
(`WCScene.h:63`).

**What our Core Animation applier has to implement**, reading that chain:

- an id → `CALayer` registry with add and remove (`:105-108`, `:251-252`);
- **children as wholesale replacement**, not incremental: `:112-116` maps the id vector and
  calls `setChildren`. There is no insert, remove or reorder on the wire, which makes a
  `sublayers` assignment the exact analogue and is a real simplification;
- mask and replica resolution (`:117-128`). CA has `mask`; it has **no replica**, so that one
  needs faking or dropping;
- roughly 25 property setters (`:129-150`, `:190-197`) that map nearly one-to-one onto
  `CALayer`: position, anchorPoint, bounds, masksToBounds, opacity, transform,
  sublayerTransform, doubleSided, contentsRect, backgroundColor, borderColor, borderWidth;
- tiles (`:151-177`). CA has no sparse tiled backing store, so either composite tiles into
  one `contents` image or make a sublayer per tile;
- backdrop (`:198-218`) → `CABackdropLayer`, and filters → `CIFilter`;
- the WebGL content-buffer and remote-frame paths (`:219-248`), both of which funnel into
  `setContentsLayer`, the CA analogue being "someone else's layer goes here";
- **not** `applyAnimationsRecursively` (`:255`): Core Animation runs animations itself, which
  removes a tick loop rather than adding one.

**Not reusable**, and worth cutting early: `WCSceneContext` entirely (GL context,
`makeContextCurrent`, `swapBuffers`, `createTextureMapper`), `WCSharedSceneContextHolder`,
the paint and present block `WCScene.cpp:257-299` (`glViewport`, `beginPainting`,
`rootLayer->paint`, `glReadPixels`), `TextureMapperFPSCounter`, and the
`TextureMapperPlatformLayer` payload type in `WCContentBuffer` and the two managers. One leak
to note: `WCTileUpdate::index` is `TextureMapperSparseBackingStore::TileIndex`
(`WCUpdateInfo.h:40`), so the wire format drags in a texture-mapper header unless we
typedef our own.

Reusable unchanged: `WCUpdateInfo.h`, both `.serialization.in` files, the `Update` message,
`GraphicsLayerWC`, `WCLayerFactory`, `WCTileGrid`, `WCBackingStore`, `DrawingAreaWC`'s
accumulation, and the id-keyed registry pattern.

## 5. Concrete list for our two builds

### TIGER_WEB, x86_64, recording only

Options: `ENABLE_GPU_PROCESS=ON`, `USE_GRAPHICS_LAYER_WC=ON` (which gives DOM, canvas and
WebGL remoting by default via `WebPreferencesDefaultValues.cpp:330-332` and
`UnifiedWebPreferences.yaml:6414`, `:6460`), `USE_CG=OFF`, `USE_SKIA=OFF`, `USE_CAIRO=OFF`,
`USE_CORE_TEXT=OFF`, `ENABLE_WEBGL=OFF` initially, `ENABLE_VIDEO` off on this path.

Compiles: `Sources.txt:826-839` web-side proxies, `Platform/Sources.txt:20-24` stream
transport, `WebProcess/WebPage/wc/*` from `Platform/WC.cmake:15` and the serialization
inputs at `:35-36`, `DrawingArea.cpp:78-79` selecting `DrawingAreaWC`.

Must be written: a fourth arm for `ImageBufferShareableBitmapBackend.h:47-53` and
`ImageBufferPlatformBackend.h:38-47`; `PlatformImagePtr` and `PlatformPathImpl` `#else` arms
(`PlatformImage.h:43-49`, `PlatformPath.h:34-55`); `platformEncodeData`
(`ImageUtilities.h:95`); a `GraphicsClient` for the client-less `ImageBuffer::create` at
`ImageUtilities.cpp:48`; the font-identity encoding in `WebCoreFont.serialization.in:86-102`.

### TIGER_UI, i386, replay plus Core Animation

Options: `ENABLE_GPU_PROCESS=ON` and the GPU-side sources, `USE_CG=ON` and `USE_CORE_TEXT=ON`
locally, but **the three coders above compiled in their neutral arm** so the wire matches the
64-bit side.

Compiles: `Sources.txt:32-48` GPU-side, the same stream transport, `RemoteWCLayerTreeHost`
from `Platform/WC.cmake:4` and its message at `:31`.

Must be written: the Core Animation scene applier replacing `WCScene.cpp:99-299`, roughly the
section-4 list; a colour-space mapper from `PlatformColorSpace::Name` to `CGColorSpaceRef`;
a font resolver from the manifest index back to a real `CTFontRef`.

### The ordering that falls out

`ColorSpace` is embedded in image-buffer creation, colour-space transforms and every
shareable-bitmap configuration, so its neutral arm has to be settled before anything renders
at all. The font encoding can wait until text, and the tile-to-`contents` decision until the
first composited frame.

## Not covered

WebGL (`RemoteGraphicsContextGL*`, and the `USE(GRAPHICS_LAYER_WC)` arm in
`RemoteGraphicsContextGLWC.cpp`), video (`PLATFORM(COCOA) && ENABLE(VIDEO)` messages),
WebGPU, PDF backends, and the `ENABLE(RE_DYNAMIC_CONTENT_SCALING)` messages at
`RemoteImageBuffer.messages.in:45-47` and `RemoteImageBufferSet.messages.in:35-37`. Also not
covered: whether `DrawingAreaProxyWC` (`UIProcess/wc/`, 121 lines) is worth reusing for the
non-composited path, which the survey's UI-process estimate should probably absorb.
