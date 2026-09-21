# WebCore + WebKitLegacy/mac on Tiger — porting plan

Scope: get `Source/WebCore` and `Source/WebKitLegacy/mac` (WebKit1, the `WebView`
API) compiling, linking and running on Intel Mac OS X 10.4.11, i386, Cocoa port
flavour, `ENABLE_WEBKIT=OFF`, C-loop JSC, no JIT, software painting.

Written 2026-09-20 against WebKit `d2f52605`. Read `NOTES.md` and
`logs/wkcmake-journal.md` first; this document assumes the WTF/JSC work described
there. Everything here is planning only — no file under `WebKit/` was modified.

## Three corrections to prior assumptions, up front

1. **Tiger's CoreText is usable.** A survey note elsewhere in this project claims
   "CoreText is 10.5+, Tiger has only ATSUI" and calls `USE_CORE_TEXT` a
   load-bearing blocker. That is wrong. Tiger ships a private
   `CoreText.framework` with 243 exports covering the whole
   `CTFont` / `CTFontDescriptor` / `CTTypesetter` / `CTLine` / `CTRun` surface.
   Of the 66 functions WebCore calls that are missing, **19 have no live call
   site or are already version-gated, 18 are pure renames, 15 can be stubbed
   with cosmetic loss, 11 map onto ATS or CoreGraphics, and exactly 1
   (`CTFontShapeGlyphs`) is real work** — and that one has a supported
   workaround already in the tree. `USE_CORE_TEXT` stays **ON**.

2. **Tiger's ImageIO is complete.** `logs/api/tiger-ImageIO.txt` is a zero-byte
   file, so the `missing-CG.txt` diff counted all of `CGImageSource*` /
   `CGImageDestination*` as missing. They are not: Tiger's
   `ApplicationServices/ImageIO.framework` exports 271 symbols including
   `CGImageSourceCreateWithData`, `CGImageSourceCreateIncremental`,
   `CGImageSourceUpdateData`, `CGImageSourceCreateImageAtIndex`,
   `CGImageSourceGetStatusAtIndex`, `CGImageSourceCopyPropertiesAtIndex` and
   226 `kCG*` constants. **The entire image-decoding path works.** 20 of the 144
   "missing CG" entries evaporate on this fact alone. Regenerate
   `logs/api/tiger-ImageIO.txt` with the command in §3.2.

3. **The curl backend is not a drop-in.** Modern WebKit deleted
   `ResourceHandleCurl` when WinCairo moved to the NetworkProcess.
   `Source/WebCore/platform/network/ResourceHandle.cpp:312` now reads
   `#if USE(SOUP) || USE(CURL)` and defines `start()`/`cancel()` as
   `ASSERT_NOT_REACHED()`. `platform/network/curl/` still has the transport
   (`CurlContext`, `CurlRequest`, `CurlRequestScheduler`) but no WebKit1 handle.
   The plan in §2 is therefore *not* "swap cocoa for curl" — it is "keep the
   Cocoa platform-data types and write one new file".

---

# 1. Feature switches

## 1.1 Where each switch lives

| File | Role |
|---|---|
| `Source/cmake/WebKitFeatures.cmake` | defines every `ENABLE_*` option and its cross-port default |
| `Source/cmake/OptionsCocoa.cmake:165-196` | the existing `if (TIGER)` `WEBKIT_OPTION_DEFAULT_PORT_VALUE(... PRIVATE OFF)` list |
| `Source/WTF/wtf/PlatformEnableCocoa.h:1245-1255` | `#if PLATFORM(TIGER)` block, for `ENABLE_*` that Cocoa turns on unconditionally with no CMake option |
| `Source/WTF/wtf/PlatformHave.h:2046-2054` | `#if PLATFORM(TIGER)` block, for `HAVE_*` |
| `Source/WTF/wtf/PlatformUse.h:53-100` | `USE_CG` / `USE_CORE_TEXT` / `USE_CA` / `USE_CORE_IMAGE` / `USE_CF` / `USE_FOUNDATION` / `USE_APPKIT`, all bare `#if PLATFORM(COCOA)` |
| `Source/WTF/Scripts/Preferences/UnifiedWebPreferences.yaml` | runtime defaults; `acceleratedCompositingEnabled` lives here now, **not** in `Settings.yaml` |

`__MAC_OS_X_VERSION_MIN_REQUIRED` is pinned at 1040 by
`compat/include/sdk-fill/Availability.h`, so every `#if __MAC_OS_X_VERSION_MIN_REQUIRED >= <modern>`
in `PlatformHave.h` / `PlatformEnableCocoa.h` is already false. Only the gates
keyed on bare `PLATFORM(COCOA)` / `PLATFORM(MAC)` need attention, and those are
what the two `PLATFORM(TIGER)` blocks exist for.

## 1.2 Already off (no action)

The `OptionsCocoa.cmake:165` list already forces off, among others:
`ENABLE_WEBGL`, `ENABLE_WEBGPU`, `ENABLE_VIDEO`, `ENABLE_WEB_AUDIO`,
`ENABLE_MEDIA_SOURCE`, `ENABLE_MEDIA_STREAM`, `ENABLE_MEDIA_RECORDER`,
`ENABLE_WEB_RTC`, `ENABLE_ENCRYPTED_MEDIA`, `ENABLE_LEGACY_ENCRYPTED_MEDIA`,
`ENABLE_VIDEO_PRESENTATION_MODE`, `ENABLE_PICTURE_IN_PICTURE_API`,
`ENABLE_WIRELESS_PLAYBACK_TARGET`, `ENABLE_AVF_CAPTIONS`, `ENABLE_AV1`,
`ENABLE_SPEECH_SYNTHESIS`, `ENABLE_GAMEPAD`, `ENABLE_WEBXR`,
`ENABLE_MODEL_ELEMENT`, `ENABLE_APPLE_PAY`, `ENABLE_PAYMENT_REQUEST`,
`ENABLE_PDFKIT_PLUGIN`, `ENABLE_ASYNC_SCROLLING`,
`ENABLE_ACCESSIBILITY_ISOLATED_TREE`, `ENABLE_WEB_AUTHN`,
`ENABLE_WRITING_TOOLS`, `ENABLE_OFFSCREEN_CANVAS`, `ENABLE_SERVICE_CONTROLS`,
`ENABLE_TELEPHONE_NUMBER_DETECTION`, `ENABLE_RESOURCE_USAGE`,
`ENABLE_MEMORY_SAMPLER`, `ENABLE_CONTENT_FILTERING`, `ENABLE_VARIATION_FONTS`,
`ENABLE_REMOTE_INSPECTOR`, `ENABLE_SHAREABLE_RESOURCE`,
`ENABLE_SANDBOX_EXTENSIONS`, `ENABLE_GPU_PROCESS`. `ENABLE_C_LOOP` is ON.

`ENABLE_NETSCAPE_PLUGIN_API` no longer exists anywhere in the tree — NPAPI was
removed upstream. `ENABLE_PDF_PLUGIN` and `ENABLE_PDFKIT_PLUGIN` default OFF and
are WebKit2-only.

## 1.3 Additions to the `OptionsCocoa.cmake:165` TIGER list

| Option | Default | Why for Tiger |
|---|---|---|
| `ENABLE_ATTACHMENT_ELEMENT` | ON for Cocoa | `<attachment>` is Mail-specific. Turning it off deletes `Source/WebCore/rendering/AttachmentLayout.mm` (the only user of `CTFrameGetLineOrigins`, `CTFramesetterSuggestFrameSizeWithConstraints`, `CTLineGetBoundsWithOptions`, `CTLineGetTrailingWhitespaceWidth`, `CTFontDescriptorCreateWithTextStyleAndAttributes`) — 5 CoreText gaps for one line |
| `ENABLE_FULLSCREEN_API` | ON | no fullscreen on Tiger; drops `WebCoreFullScreenPlaceholderView.mm` (`NSVisualEffectView`), `WebFullScreenController.mm` (584 LOC), `WebKitFullScreenListener.mm` |
| `ENABLE_NOTIFICATIONS` | ON | drops `WebNotificationClient.mm` (190), `WebNotification.mm` (210) |
| `ENABLE_GEOLOCATION` | ON | drops `WebGeolocationClient.mm` (242), `WebGeolocationPosition.mm` (88); CoreLocation is 10.6 |
| `ENABLE_DEVICE_ORIENTATION` | OFF already | confirm; drops `WebDeviceOrientation*.mm` |
| `ENABLE_SMOOTH_SCROLLING` | ON | pulls `ScrollAnimatorMac` momentum paths; leave ON initially but expect it back if `ScrollbarsControllerMac` is dropped (§4) |
| `ENABLE_DARK_MODE_CSS` | OFF already | confirm OFF; pairs with the `NSAppearance` no-op |
| `ENABLE_MATHML` | ON | leave ON — pure layout code, no platform surface |
| `ENABLE_CONTENT_EXTENSIONS` | OFF already | confirm |
| `ENABLE_DRAG_SUPPORT` | ON for Mac | **leave ON.** It also gates text selection by mouse. `DragImageCocoa.mm` needs the manual `CTTypesetter` loop in §3.1 |

`ENABLE_DATA_DETECTION` has **no CMake option** — it is hardcoded to 1 at
`PlatformEnableCocoa.h:306-307` for `PLATFORM(MAC)`. Add
`#undef ENABLE_DATA_DETECTION` / `#define ENABLE_DATA_DETECTION 0` to the
`PLATFORM(TIGER)` block at `PlatformEnableCocoa.h:1245`.

## 1.4 `USE_*` switches — the compositing decision

| Macro | Site | Tiger | Consequence |
|---|---|---|---|
| `USE_CG` | `PlatformUse.h:53` | **ON** | CoreGraphics is the whole 2D backend. Non-negotiable. |
| `USE_CORE_TEXT` | `PlatformUse.h:58` | **ON** | see correction 1 above and §3.1 |
| `USE_CF`, `USE_FOUNDATION`, `USE_APPKIT` | `:91`, `:99`, `:104` | **ON** | Tiger has all three |
| `USE_CA` | `PlatformUse.h:62` | **OFF** | see below |
| `USE_CORE_IMAGE` | `PlatformUse.h:66` | **OFF** | CoreImage exists on Tiger but WebCore's uses are `CIFilter`-through-`CAFilter` paths that go with CA |
| `USE_ANGLE_EGL` | `OptionsCocoa.cmake:274` | already OFF | |
| `USE_LIBWEBRTC` | `OptionsCocoa.cmake:236` | already FALSE | |

`USE_CA` is referenced in exactly **19 files**, and `GraphicsLayer.cpp:133` /
`GraphicsLayerContentsDisplayDelegate.h:30` already carry
`#if !USE(CA) && !USE(COORDINATED_GRAPHICS)` fallbacks. Turning it off is clean.

### What compositing-off actually does in modern WebCore

The `ENABLE(ACCELERATED_COMPOSITING)` flag was removed upstream — zero hits in
the tree. Compositing is unconditional code that is switched at runtime. Traced
through `Source/WebCore/rendering/RenderLayerCompositor.cpp`:

- `cacheAcceleratedCompositingFlags()` (`:610`) reads
  `settings->acceleratedCompositingEnabled()` and ANDs it with
  `chrome().client().allowedCompositingTriggers()` (`:616-619`) into
  `m_hasAcceleratedCompositing` (`:644`).
- `canBeComposited()` (`:3327`) returns false when that flag is false, so
  `RenderLayer::ensureBacking()` (`:2431`) is never reached and **no
  `RenderLayerBacking` and no `GraphicsLayer` is ever constructed**.
- `enableCompositingMode()` (`:593`) is the only caller of `ensureRootLayer()`
  (`:600`). Its three entry points are all gated on `hasAcceleratedCompositing`
  or on `forceCompositingMode` (itself gated at `:632`/`:686`, and
  `ForceCompositingMode` carries `excludeFrom: [WebKitLegacy, WebCore]` in the
  preferences YAML) or on page-overlay count. So `m_rootContentsLayer` is never
  created.
- `attachRootLayer()` (`:5316`) returns early at `if (!m_rootContentsLayer)`,
  therefore **`WebChromeClient::attachRootGraphicsLayer`
  (`Source/WebKitLegacy/mac/WebCoreSupport/WebChromeClient.mm:883`) is never
  called**, and `-[WebHTMLView attachRootLayer:]`
  (`WebHTMLView.mm:6132`, which creates the `layerHostingView` and the
  `WebRootLayer : CALayer` at `WebHTMLView.mm:702`) never runs.

The cheapest hard kill switch is `WebChromeClient::allowedCompositingTriggers()`,
an inline `final` at `Source/WebKitLegacy/mac/WebCoreSupport/WebChromeClient.h:178-188`
— return `0` and the runtime pref cannot re-enable it.

### What still has to compile and link

| File | LOC | Verdict |
|---|---|---|
| `platform/graphics/GraphicsLayer.{cpp,h}` | 1180 | **must compile.** Self-sufficient with `USE_CA` off via the `:133` fallback |
| `platform/graphics/GraphicsLayerFactory.h` | 46 | header only; `ChromeClient::graphicsLayerFactory()` already defaults to `nullptr` (`page/ChromeClient.h:464`) and WebKitLegacy does not override it |
| `rendering/RenderLayerBacking.cpp` | 5501 | **must compile**, never constructed |
| `rendering/RenderLayerCompositor.cpp` | 6411 | **must compile**, runs and does nothing |
| `platform/graphics/ca/GraphicsLayerCA.cpp` | 5470 | **exclude.** But it holds the only Cocoa definition of `Ref<GraphicsLayer> GraphicsLayer::create(GraphicsLayerFactory*, GraphicsLayerClient&, Type)` (`:326`) — the other two are `texmap/GraphicsLayerTextureMapper.cpp:35` and `texmap/coordinated/GraphicsLayerCoordinated.cpp:74`. Replace with a ~20-line `platform/graphics/tiger/GraphicsLayerTiger.cpp` defining it as `RELEASE_ASSERT_NOT_REACHED()` |
| `ca/PlatformCALayer.mm` | 377 | exclude |
| `ca/cocoa/PlatformCALayerCocoa.mm` | 1394 | exclude — needs `CALayer`, `CATiledLayer`, `CABackdropLayer`, `AVPlayerLayer` |
| `ca/cocoa/PlatformCAAnimationCocoa.mm` | 598 | exclude — `CASpringAnimation` is 10.11 |
| `ca/PlatformCAAnimation.cpp` | 154 | exclude |
| `ca/cocoa/PlatformCAFiltersCocoa.mm` | 607 | exclude — `CAFilter` SPI |
| `ca/TileController.cpp` / `TileGrid.cpp` / `TileCoverageMap.cpp` | 940 / 878 / 187 | exclude |
| `ca/cocoa/WebTiledBackingLayer.mm` | 156 | exclude |
| `ca/LayerPool.cpp` | 155 | exclude |
| `ca/TransformationMatrixCA.cpp` | 69 | exclude, and guard the `operator CATransform3D` declaration in `platform/graphics/transforms/TransformationMatrix.h` (it is `USE(CA)`-guarded already) |
| `ca/FrameProcessIndicators.cpp` | — | exclude |

Source lists to edit: `Source/WebCore/SourcesCocoa.txt:405-420` and
`Source/WebCore/PlatformCocoa.cmake:429-444`. **Net: one 20-line stub replaces
~10,900 lines of code that cannot be built against Tiger's QuartzCore.**

## 1.5 `HAVE_*` additions to `PlatformHave.h:2046`

`HAVE_IOSURFACE` (`:379`) is already undefined there, but the satellites are
keyed independently and will leak:

`HAVE_IOSURFACE_ALPHA_CHANNEL_MODE` (`:391`),
`HAVE_IOSURFACE_COREIMAGE_SUPPORT` (`:395`),
`HAVE_IOSURFACE_ACCELERATOR` (`:399`),
`HAVE_IOSURFACE_SET_OWNERSHIP` (`:403`),
`HAVE_IOSURFACE_SET_OWNERSHIP_IDENTITY` (`:409`),
`HAVE_IOSURFACE_RGB10` (`:905`),
`HAVE_LOSSLESS_COMPRESSED_IOSURFACE_CG_SUPPORT` (`:1761`),
`HAVE_CORE_ANIMATION_RENDER_SERVER` (`:301`),
`HAVE_CORE_ANIMATION_FRAME_RATE_RANGE` (`:319`),
`HAVE_CORE_ANIMATION_SEPARATED_LAYERS` / `_PORTALS` (`:717-718`).

Two one-line wins that remove three CoreText symbols with zero behavioural loss
(both are bare `#if PLATFORM(COCOA)` with no version check):

- `PlatformHave.h:888` `HAVE_CORE_TEXT_SBIX_IMAGE_SIZE_FUNCTIONS` → off. Kills
  `CTFontGetSbixImageSizeForGlyphAndContentsScale` and the
  `CTFontCopyTable(kCTFontTableSbix)` probe at `FontCoreText.cpp:911`.
- `PlatformHave.h:1373` `HAVE_CTFONTMANAGER_CREATEMEMORYSAFEFONTDESCRIPTORFROMDATA`
  → off. The `#else` already returns `nullptr`.
- `PlatformHave.h:889` `HAVE_WOFF_SUPPORT` is in the same block if WOFF becomes a
  problem.

Files to exclude that are pure IOSurface: `platform/graphics/cg/ImageBufferIOSurfaceBackend.cpp`,
`cg/IOSurfacePool.cpp`, `cocoa/IOSurface.mm`. `ImageBufferCGBitmapBackend.cpp` is
the software backend and stays.

---

# 2. Networking: curl under the NSURLRequest API

## 2.1 The shape

WebKitLegacy's public API vends `NSURLRequest*` / `NSURLResponse*` / `NSError*`
to the embedder through `WebResourceLoadDelegate`, `WebPolicyDelegate` and
`WebDataSource`. Those must keep working. The good news is that WebCore already
**synthesizes** those objects from plain fields rather than owning CFNetwork
state: `ResourceResponse::initNSURLResponse()` in
`platform/network/cocoa/ResourceResponseCocoa.mm` builds an `NSHTTPURLResponse`
out of `m_url` / `m_httpStatusCode` / `m_httpHeaderFields`.

So the plan is **keep every Cocoa platform-data type and replace exactly one
file**. The seam is `ResourceHandle`: `Source/WebCore/loader/ResourceLoader.cpp:285`
is its only construction site.

```
Cocoa (keep)                          curl (new)
ResourceRequest  m_nsRequest    ──────► CurlRequest via ResourceHandleCurl.mm
ResourceResponse m_nsResponse   ◄────── ResourceResponse(CurlResponse&)
ResourceError    m_platformError ◄───── ResourceError(int curlCode, URL, Type)
```

### Dependency count in WebKitLegacy, measured

| Accessor | Hits | Files |
|---|---|---|
| `nsURLRequest(...)` | 13 | `WebCoreSupport/WebFrameLoaderClient.mm` (11), `WebView/WebDataSource.mm` (2) |
| `nsURLResponse()` | 12 | `WebFrameLoaderClient.mm`, `WebDataSource.mm`, `WebHTMLView.mm`, `WebResource.mm` |
| `nsError()` | 0 | reached via `operator NSError*()` |
| NSURLRequest → ResourceRequest | 1 | `WebFrameLoaderClient.mm:374` |

**26 sites, 4 files, one reverse conversion.** None of them has to change.

## 2.2 What exists in `platform/network/curl` (7226 LOC, 45 files)

| File | LOC | Role |
|---|---|---|
| `CurlContext.{cpp,h}` | 989 / 351 | `CurlGlobal`, `CurlContext` singleton, `CurlShareHandle`, `CurlMultiHandle`, `CurlHandle` (every `curl_easy_setopt`, header/body callbacks, metrics, error mapping) |
| `CurlRequest.{cpp,h}` | 693 / 171 | the transfer object: `create(ResourceRequest&, CurlRequestClient&)`, `resume()`, `cancel()`, `setUserPass()`, `setAuthenticationScheme()` |
| `CurlRequestClient.h` | 49 | 5 pure virtuals — `curlDidSendData` / `DidReceiveResponse` / `DidReceiveData` / `DidComplete` / `DidFailWithError`. **This is the interface the new handle implements.** |
| `CurlRequestScheduler.{cpp,h}` | 273 / 89 | worker thread + multi-handle pump, calls back via `callOnMainThread` |
| `CurlResponse.h` | 76 | POD: url, status, headers, `CertificateInfo`, `NetworkLoadMetrics` |
| `CurlFormDataStream.{cpp,h}` | 182 / 65 | `FormData` → upload read callback |
| `CurlMultipartHandle.*` | 331 / 97 / 42 | `multipart/x-mixed-replace` |
| `CurlSSLHandle.{cpp,h}` / `CurlSSLVerifier.{cpp,h}` | 106+108 / 84+52 | CA bundle path, allowed/ignored cert hosts, verify callback |
| `OpenSSLHelper.{cpp,h}` | 412 / 42 | X509 → `CertificateInfo` |
| `CertificateInfoCurl.cpp` + `curl/CertificateInfo.h` | 70 / 68 | `Vector<Certificate>` of DER blobs |
| `AuthenticationChallengeCurl.cpp` | 136 | builds a challenge from `CurlResponse`'s `WWW-Authenticate` |
| `ResourceResponseCurl.cpp` | 145 | `ResourceResponse(CurlResponse&)` ctor |
| `ResourceErrorCurl.cpp` | 92 | `ResourceError(int curlCode, URL, Type)`, `isCertificationVerificationError()` |
| `CurlProxySettings.{cpp,h}` | 154 / 96 | proxy URL parsing |
| `CookieJarDB.{cpp,h}` | 671 / 111 | SQLite cookie store — **zero callers in this tree** |
| `CookieUtil.{cpp,h}` | 190 / 45 | `Set-Cookie` parsing → `Cookie` |
| `CurlStream.*` / `CurlStreamScheduler.*` | 201+91 / 194+73 | `CURLOPT_CONNECT_ONLY` socket for WebSockets — **not needed**, `SocketStreamHandle` is WK2-only now |
| `DNSResolveQueueCurl.{cpp,h}` | 60 / 47 | prefetch stub |
| `PublicSuffixStoreCurl.cpp` | 74 | needs **libpsl**, a new dependency — exclude and keep the Cocoa `PublicSuffixStore` |

**Absent, and this is the gap:** `ResourceHandleCurl`,
`CurlResourceHandleDelegate`, `CookieJarCurl`, `NetworkStorageSessionCurl`,
`CurlCacheManager`, `CurlDownload`, `SocketStreamHandleImplCurl`,
`SynchronousLoaderClientCurl`. The checkout is a depth-1 clone
(`git log | wc -l` = 1), so `git show` cannot recover them. Fetch a WebKit source
tarball from the r250000 era (2019-2021) for the reference implementation.

## 2.3 The platform-data split, decided

| Type | Decision | Work |
|---|---|---|
| `ResourceRequest` | **keep Cocoa** (`cf/ResourceRequest.h:120` `m_nsRequest`, `cocoa/ResourceRequestCocoa.mm` 404 LOC) | strip `_CFURLRequest` SPI at `:136,186,275,278,282,286,379,394`, `_setProperty:forKey:` at `:298,299,326`, `_CFURLRequestSetStorageSession`, `_initWithCFURLRequest:`, `CFURLCacheCopyResponseForRequest`. **≈ −60 LOC** |
| `ResourceResponse` | **keep Cocoa** (`cocoa/ResourceResponseCocoa.mm` 202 LOC) | one `[m_nsResponse _setMIMEType:]` call. Replace with a private `NSHTTPURLResponse` subclass overriding `-MIMEType`. **≈ +20 LOC** |
| `ResourceError` | **keep Cocoa** (`cocoa/ResourceErrorCocoa.mm` 335 LOC) | replace `kCFErrorDomainCFNetwork` (`:160,213`) with `NSURLErrorDomain` plus a private domain; add a `ResourceError(int curlCode, ...)` ctor. **≈ 40 LOC** |
| `Credential` | keep Cocoa (`cocoa/CredentialCocoa.{h,mm}` 70/141) | none |
| `ProtectionSpace` | keep Cocoa (`cocoa/ProtectionSpaceCocoa.{h,mm}` 62/223) | none — `platform/network/ProtectionSpace.h:31` dispatches `#if PLATFORM(COCOA) ... #elif USE(CURL)` and COCOA wins |
| `CertificateInfo` | **swap to curl** | exclude `cocoa/CertificateInfoCocoa.mm` (50) and `cf/CertificateInfoCFNet.cpp` (145); take `curl/CertificateInfoCurl.cpp`. LibreSSL already hands you the chain |
| `AuthenticationChallenge` | **hybrid** | keep the Cocoa class shape (`Panels/WebPanelAuthenticationHandler.m` needs `NSURLAuthenticationChallenge`), populate it from `AuthenticationChallengeCurl`'s `WWW-Authenticate` parser. **≈ 100 LOC bridge** |
| `NetworkStorageSession` | **keep Cocoa.** Note the class was renamed: it is `platform/network/CookieStorageSession.{h,cpp}` + `cocoa/CookieStorageSessionCocoa.mm` (478) + `cf/CookieStorageSessionCFNet.cpp` (96). The header is `#if PLATFORM(COCOA)` at lines 42, 69, 82, 110, 121, 128, 137 | audit the 478-line Cocoa file for `_CFHTTPCookieStorage*` SPI — **the largest unaudited risk in this section** |
| Cookies | **keep `NSHTTPCookieStorage`** (Tiger 10.2+), skip `CookieJarDB` | see §2.6 |

## 2.4 Files to exclude and add

Remove from `Source/WebCore/SourcesCocoa.txt:700-726` and
`Source/WebCore/PlatformCocoa.cmake:526-549`:

| Path | LOC | Why |
|---|---|---|
| `platform/network/cocoa/ResourceHandleCocoa.mm` | 665 | replaced |
| `platform/network/cocoa/WebCoreResourceHandleAsOperationQueueDelegate.{mm,h}` | 434 | `NSURLConnection` delegate |
| `platform/network/cocoa/WebCoreNSURLSession.{mm,h}` | 1077 | `NSURLSession`, media-only |
| `platform/network/cf/FormDataStreamCFNet.mm` | 555 | `CFReadStream` upload → `CurlFormDataStream` |
| `platform/network/cf/ResourceRequestCFNet.{cpp,h}` | 133 | `_CFURLRequest`; inline the two priority helpers |
| `platform/network/cf/DNSResolveQueueCFNet.{cpp,h}` | 206 | → `DNSResolveQueueCurl.cpp` |
| `platform/network/cf/CertificateInfoCFNet.cpp` + `cf/CertificateInfo.h` | 145 | → curl |
| `platform/network/cocoa/CertificateInfoCocoa.mm` | 50 | → curl |
| `platform/network/cocoa/RangeResponseGenerator.mm` | 343 | media-only |
| `platform/network/cocoa/NetworkLoadMetrics.mm` | 125 | `NSURLSessionTaskMetrics` → `CurlRequest::networkLoadMetrics()` |
| `platform/network/cocoa/SynchronousLoaderClient.mm` | 45 | `NSURLConnection` sync path |
| `loader/cocoa/SubresourceLoaderCocoa.mm` | 47 | `NSCachedURLResponse` |
| `loader/cocoa/DiskCacheMonitorCocoa.mm` | — | CFNetwork disk cache |

Keep: `ResourceRequestCocoa.mm`, `ResourceResponseCocoa.mm`,
`ResourceErrorCocoa.mm`, `CredentialCocoa.mm`, `CredentialStorageCocoa.mm`,
`ProtectionSpaceCocoa.mm`, `AuthenticationCocoa.mm`, `CookieCocoa.mm`,
`CookieStorageSessionCocoa.mm`, `FormDataStreamCocoa.mm`,
`WebCoreURLResponse.mm`, `UTIUtilities.mm`, `BlobDataFileReferenceCocoa.mm`,
`mac/NetworkStateNotifierMac.cpp`.

Add: `Source/WebCore/platform/Curl.cmake` already exists and is complete; Win
includes it at `PlatformWin.cmake:4`, PlayStation at `PlatformPlayStation.cmake:1`.
There is **no `PlatformWinCairo.cmake`** and **no `OptionsWinCairo.cmake`** in
this tree — WinCairo folded into the Win files, and `USE_CURL` is set directly
per port (`OptionsWin.cmake:122`, `OptionsPlayStation.cmake:271`), not in
`WebKitFeatures.cmake`.

For Tiger: `include(platform/Curl.cmake)` in `PlatformCocoa.cmake` under
`if (TIGER)`, minus `CurlStream*` and `PublicSuffixStoreCurl.cpp`; plus
`SET_AND_EXPOSE_TO_BUILD(USE_CURL ON)` in the TIGER block of
`OptionsCocoa.cmake`; plus a `platform/network/tiger/CurlSSLHandleTiger.cpp`
(~30 LOC, copy `platform/network/playstation/CurlSSLHandlePlayStation.cpp:60`)
for the CA bundle path.

## 2.5 The `USE(CF)` + `USE(FOUNDATION)` + `USE(CURL)` hybrid

`USE(CFURLCONNECTION)` **no longer exists** — 0 hits in WebCore, 0 in
WebKitLegacy. It was removed upstream; the CFNetwork dependency is now expressed
as unguarded `#import <pal/spi/cf/CFNetworkSPI.h>` inside the cocoa/cf files.

`USE_CF` (`PlatformUse.h:91`) and `USE_FOUNDATION` (`:99`) are hardcoded under
`#if PLATFORM(COCOA)` and are not CMake options.

Only **24** `USE(CURL)` occurrences exist in all of WebCore, 18 of them inside
`platform/network/curl/`. The six outside:

| Site | Effect with `USE_CURL=1, PLATFORM(MAC)` |
|---|---|
| `platform/network/ResourceHandle.cpp:312` | **conflict** — `#if USE(SOUP) \|\| USE(CURL)` defines `~ResourceHandle`/`start`/`cancel` as `ASSERT_NOT_REACHED()`, duplicate-symbol with the Cocoa/new impl. Fix: `&& !PLATFORM(COCOA)`, or moot once `ResourceHandleCocoa.mm` is gone and `ResourceHandleCurl.mm` defines them |
| `platform/network/SynchronousLoaderClient.cpp:106` | same pattern, same fix |
| `platform/network/ProtectionSpace.h:31` | `#elif` — COCOA wins, no conflict |
| `platform/network/DNSResolveQueue.cpp:32` | `#elif` — no conflict |
| `platform/MIMETypeRegistry.cpp:694` | `#if USE(CURL)` — harmless extra |
| `platform/WebCorePersistentCoders.cpp:401` | `#elif` — no conflict |

**The interaction surface is two preprocessor lines.** Nothing assumes
"`USE(CF)` implies CFNetwork" symbolically.

## 2.6 Cookies

`CookieJarDB` has zero callers. Its schema (`CookieJarDB.cpp:52-67`) is a single
`Cookie` table keyed `(name, domain, path)` with `domain_index` and `path_index`.
Using it means writing a `CookieStorageSessionCurl.cpp` against the
COCOA-guarded API in `CookieStorageSession.h` (un-guarding a dozen methods),
wiring the `#else` branch of `loader/CookieJar.cpp:202`, and adding a path hook
in WebKitLegacy — ~400 LOC.

**Skip it.** Tiger has `NSHTTPCookieStorage` (10.2+). Keep
`CookieStorageSessionCocoa.mm`, and in the new `ResourceHandleCurl.mm` do what
the old handle did: read the `Cookie:` header from the storage before
`CurlRequest::resume()`, feed `Set-Cookie:` back after `curlDidReceiveResponse`.
**≈ 60 LOC.**

Two WebKitLegacy cookie sites:
- `WebView/WebPreferences.mm:2707,2711` uses
  `CFHTTPCookieStorageSetCookieAcceptPolicy` (CFNetwork SPI, unlikely in 129.22).
  Replace with `-[NSHTTPCookieStorage setCookieAcceptPolicy:]` (public, 10.2).
- `WebPreferences.mm:2007-2017` reads
  `[[NSHTTPCookieStorage sharedHTTPCookieStorage] cookieAcceptPolicy]` — fine.

## 2.7 Other WebKitLegacy networking touch points

| File | What | Verdict |
|---|---|---|
| `WebCoreSupport/WebFrameLoaderClient.mm` | the boundary; `:297,306` create a `WebDownload` with `[WebDownload _downloadWithLoadingConnection:handle->connection() ...]` | **adapt** — only `:306` breaks. ≈10 LOC |
| `Misc/WebDownload.mm` (272) | wraps `NSURLDownload`; `:251 -_initWithLoadingConnection:` takes `NSURLConnection*` | **adapt** — stub `_downloadWithLoadingConnection:` so `:297` always takes the fresh `initWithRequest:` path (restarts the download; acceptable) |
| `WebCoreSupport/WebFrameNetworkingContext.mm` (106) | `sourceApplicationAuditData`, `scheduledRunLoopPairs()`, `<pal/spi/cf/CFNetworkSPI.h>` | **adapt** — drop the SPI import, return `nullptr`. ≈15 LOC |
| `WebCoreSupport/NetworkStorageSessionMap.{h,cpp}` (45/109) | session-ID map | keep |
| `WebCoreSupport/WebResourceLoadScheduler.{cpp,h,mm}` (420/152/110) | WK1 scheduler, platform-neutral | keep |
| `WebView/WebView.mm` | `:3236 [NSURLConnection canHandleRequest:]`; `:3434 [[NSURLCache sharedURLCache] cachedResponseForRequest:]`; `:8392-8611 +_setCacheModel:` using `_CFURLCacheCopyCacheDirectory` / `[NSURLCache _CFURLCache]` | **adapt** — gut the `_CFURLCache` SPI from `_setCacheModel:`, replace `canHandleRequest:` with a scheme whitelist. ≈60 LOC |
| `Misc/WebNSURLExtras.mm`, `Misc/WebNSURLRequestExtras.m` | IDN/percent-encoding, plain accessors | keep |
| `Panels/WebPanelAuthenticationHandler.m` | `NSURLCredentialStorage` | keep |
| `Plugins/*` | pass `NSURLRequest`/`NSURLResponse` | exclude (§5) |

## 2.8 LOC estimate

| Bucket | LOC | Confidence |
|---|---|---|
| `platform/network/curl/ResourceHandleCurl.mm` — `start`/`cancel`/`platformSetDefersLoading`/`loadResourceSynchronously`/`continueWillSendRequest`/`receivedCredential` + the 5 `CurlRequestClient` methods + cookie glue | 550-750 | medium — the 2019-era original was ~600 plus a 270-line delegate |
| Auth bridge (Cocoa `AuthenticationChallenge` ← `CurlResponse`) | ~100 | medium |
| Synchronous-load message pump + `platformBadResponseError` | 60-100 | high |
| `CurlSSLHandleTiger.cpp` | ~30 | high |
| `ResourceError(int curlCode, ...)` on the Cocoa class | ~50 | high |
| `NSHTTPURLResponse` MIME-type subclass | ~25 | high |
| **New code total** | **~815-1055** | |
| WebCore edits (2 guards, `ResourceHandleInternal.h` swap `RetainPtr<NSURLConnection> m_connection` → `RefPtr<CurlRequest>`, SPI stripping) | ~130 touched, ~105 net deleted | |
| WebKitLegacy edits (`WebFrameLoaderClient.mm`, `WebFrameNetworkingContext.mm`, `WebView.mm`, `WebPreferences.mm`, `WebDownload.mm`) | ~100 | |
| Build system (`PlatformCocoa.cmake`, `SourcesCocoa.txt`, `OptionsCocoa.cmake`) | ~55 | |
| **Total** | **~1100-1350 written, ~230 edited** | |

### Honest unknowns

- `cocoa/CookieStorageSessionCocoa.mm` (478 LOC) was not line-audited for
  `_CFHTTPCookieStorage*` SPI. If it is SPI-heavy, add 150-400 LOC and
  `CookieJarDB` becomes competitive after all.
- **Redirects.** `CurlRequest` can follow internally (`CURLOPT_FOLLOWLOCATION`)
  or surface each hop. The old handle disabled follow and drove them manually so
  `WebFrameLoaderClient::dispatchWillSendRequest` (`:355`, with the
  `nsURLRequest` round-trip at `:374`) fires per hop. Getting this right is
  fiddly and is not costed above.
- **No disk cache.** `CurlCacheManager` is gone and `NSURLCache` is excluded, so
  every load hits the network. Correct, but painful on a Core 2 Duo over the
  web. Consider reviving `CurlCacheManager` from the same old tarball later.
- **Thread model.** `CurlRequestScheduler` spawns a worker thread and posts back
  with `callOnMainThread`. curl 8.14 + LibreSSL on a 2005 pthreads/CFRunLoop is
  untested.

---

# 3. Text and graphics: the CoreText and CoreGraphics gaps

## 3.1 CoreText — 66 missing functions, classified

Tiger CoreText is a private framework with 243 exports
(`logs/api/tiger-CT.txt`). Classification of `logs/api/missing-CT.txt`:

| Class | Count | Meaning |
|---|---|---|
| **A** | 19 | already gated off, comment-only, or no live call site — free |
| **A\*** | 2 | one-line `PlatformHave.h` flip (`:888`, `:1373`) |
| **B** | 18 | rename or trivial remap onto a Tiger CT export |
| **C** | 11 | implementable via ATS or CoreGraphics |
| **D** | 1 | `CTFontShapeGlyphs` — real work, with a workaround |
| **E** | 15 | stub with acceptable, named loss |

### The class-B renames (mechanical, mostly `#define`)

| Modern | Tiger | Sites |
|---|---|---|
| `CTFontDescriptorCreateCopyWithAttributes` | `CTFontDescriptorCopyWithAttributes` | 6 |
| `CTFontDescriptorCreateCopyWithFeature` | `CTFontDescriptorCopyWithFeature` | 1 |
| `CTFontDescriptorCreateMatchingFontDescriptors` | `CTFontDescriptorCopyMatchingFontDescriptors` | 1 |
| `CTFontDescriptorCreateMatchingFontDescriptor` | first element of the above + empty guard | 1 |
| `CTFontCreateUIFontForLanguage` | `CTFontCreateUIFontForLocale` | 7 |
| `CTFontDescriptorCreateForUIType` | `CTFontCopyFontDescriptor(CTFontCreateUIFontForLocale(...))` | 6 |
| `CTFontDescriptorCreateLastResort` | `CTFontDescriptorCreateWithNameAndSize(CFSTR("LastResort"), 0)` | 3 |
| `CTFontGetGlyphCount` | `CTFontGetNumberOfGlyphs` | 1 |
| `CTFontCopyFullName` | `CTFontCopyName(font, kCTFullNameKey)` | 3 |
| `CTFontCopyGraphicsFont` | `CGFontRetain(CTFontGetGraphicsFont(f, nullptr))` | 1 |
| `CTFontHasTable` | `!!adoptCF(CTFontCopyTable(f, tag, kCTFontTableOptionNoOptions))`, cached on the `Font` | 2 |
| `CTFontGetPhysicalSymbolicTraits` | `CTFontGetSymbolicTraits` | 1 |
| `CTFontGetGlyphsForCharacterRange` | fill a `Vector<UniChar>` + `CTFontGetGlyphsForCharacters` | 1 |
| `CTFontCreateWithFontDescriptorAndOptions` | `CTFontCreateWithFontDescriptor`, drop options | 1 |
| `CTFontDescriptorCreateWithAttributesAndOptions` | `CTFontDescriptorCreateWithAttributes`, drop options | 2 |
| `CTFontDescriptorCreateCopyWithSymbolicTraits` | `{kCTFontTraitsAttribute: {kCTFontSymbolicTrait: t}}` + `CTFontDescriptorCopyWithAttributes` | 2 |
| `CTLineGetBoundsWithOptions` | `CTLineGetTypographicBounds` decomposition (both call sites pass only `0` or `ExcludeTypographicLeading`) | 2 |
| `CTFontCopyDefaultCascadeListForLanguages` | `CTFontCopyDefaultCascadeList` — **loses per-language reordering** | 1 |

### The four areas that matter

**(a) Complex text — `CTFontShapeGlyphs`, the one class-D item.**
One live call site: `platform/graphics/coretext/FontCoreText.cpp:665`, inside
`Font::applyTransforms()` (declared `platform/graphics/Font.h:132`). It is the
*fast path* shaper: `WidthIterator` fills a `GlyphBuffer` with nominal cmap
glyphs, then `applyTransforms` mutates it in place for kerning, GSUB ligatures,
mark positioning and RTL reordering.

Do **not** reimplement it. `ComplexTextController`
(`platform/graphics/ComplexTextController.cpp` + the CoreText backend at
`coretext/ComplexTextControllerCoreText.mm`) shapes via
`CTTypesetter` / `CTLine` / `CTRun`, and **Tiger exports that whole API**. WebKit
already ships the switch for ports without `applyTransforms`:

- `platform/graphics/Font.cpp:569` — the `#if !USE(CORE_TEXT)` no-op
  `applyTransforms` returning `makeGlyphBufferAdvance()`.
- `platform/graphics/FontCascade.cpp:686-694` —
  `FontCascade::shouldUseComplexTextControllerForSimpleText()`, currently
  `#if PLATFORM(GTK) || PLATFORM(WPE)`, which forces `CodePath::Complex` whenever
  `enableKerning() || requiresShaping()`.

Fix: `#if`-out the `CTFontShapeGlyphs` body of `Font::applyTransforms` for Tiger,
and widen the guard at `FontCascade.cpp:686` to include Tiger. Cost is throughput,
not correctness.

Remaining work in `ComplexTextControllerCoreText.mm` is three small items:
`CTTypesetterCreateWithUniCharProviderAndOptions` (`:220`) → Tiger's
`CTTypesetterCreateWithUniCharProvider`, with the one option WebCore passes
(`kCTTypesetterOptionForcedEmbeddingLevel`) expressed as explicit bidi override
characters U+202D/U+202E + U+202C around the text, discarding the three
zero-width control glyphs; `CTRunGetInitialAdvance` (`:64`) → `CGSizeZero`;
`CTRunGetBaseAdvancesAndOrigins` (`:93`) → dead branch, since Tiger's
`CTRunGetStatus` predates `kCTRunStatusHasOrigins`.

`CTRunGetGlyphsSpan` / `CTRunGetAdvancesSpan` / `CTRunGetStringIndicesPtrSpan`
are **false positives** — they are WebCore-local `static` helpers at
`ComplexTextControllerCoreText.mm:39,47,55` wrapping the `*Ptr` forms Tiger has.

**(b) Text painting — `CTFontDrawGlyphs`, the riskiest single item.**
Two calls at `coretext/FontCascadeCoreText.cpp:284,287`. Replace with
`CTFontApplyToContext(font, ctx)` (Tiger exports it; it sets the CG font and
text matrix), then `CGContextSetTextPosition` + `CGContextShowGlyphsWithAdvances`.
Tiger has no `CGContextShowGlyphsAtPositions`, so the absolute `CGPoint positions[]`
WebCore builds must be differenced into `CGSize advances[]`
(`advance[i] = pos[i+1] − pos[i]`), with the pen set to `positions[0]` first.
This is exactly what pre-CoreText `FontMac.mm` did. Get it wrong and every glyph
on every page is misplaced.

**(c) Font fallback — the load-bearing remap.**
`cocoa/FontCacheCoreText.cpp:783` (`lookupFallbackFont`, reached from
`Font::systemFallbackFontForCharacterCluster`) and `:994` call
`CTFontCreateForCharactersWithLanguageAndOption`. Tiger has
`CTFontCreateForString(CTFontRef, CFStringRef, CFRange)`. Three losses:
no `language` parameter (zh-Hans/zh-Hant/ja disambiguation gone), no
`kCTFontFallbackOptionSystem` (irrelevant — Tiger has no
`AllowUserInstalledFonts::No` privacy mode), and **no `coveredLength` out-param**.
That last one must be reconstructed by running `CTFontGetGlyphsForCharacters` on
the returned font and counting leading non-zero glyphs; `FontRanges` uses it to
decide how much of a cluster the fallback consumed, and a wrong value splits
clusters visibly.

Cascade list: `cocoa/SystemFontDatabaseCoreText.cpp:173` → Tiger's
`CTFontCopyDefaultCascadeList`. Do **not** substitute `CTFontCopyFontCascadeList`
— that is the font's own cascade attribute, not the system default.
`removeCascadeList()` at `:159` only needs the descriptor rename.

**(d) Web fonts from bytes — `FontCustomPlatformDataCoreText.cpp`.**
`CTFontManagerCreateFontDescriptorFromData` at `:125` is the entire `@font-face`
path. Replace with `CGDataProviderCreateWithCFData` → `CGFontCreateWithDataProvider`
→ `CTFontCreateWithGraphicsFont` → `CTFontCopyFontDescriptor`; all four exist on
Tiger. Keep `ATSFontActivateFromMemory(kATSFontContextLocal, ...)` +
`ATSFontGetPostScriptName` + `CTFontDescriptorCreateWithNameAndSize` in reserve
for TrueType collections and fonts `CGFontCreateWithDataProvider` rejects.

**The real blocker in that file is not CoreText.** `:95-106` calls
`FPFontCreateFontsFromData` / `FPFontCreateMemorySafeFontsFromData` /
`FPFontCopySFNTData` from **FontParser.framework**, which Tiger also lacks.
Replace `extractFontCustomPlatformDataSystemParser` with a pass-through returning
`buffer.createCFData()` unchanged — WebCore already does WOFF decoding and its
own validation upstream.

### System/UI fonts and CSS generic families

`CTFontDescriptorCreateForCSSFamily` (`SystemFontDatabaseCoreText.cpp:326`)
resolves `serif`/`sans-serif`/`cursive`/`fantasy`/`monospace`. Tiger exports the
direct ancestors — `CTFontDescriptorCreatePerLanguageAndCSSKey`,
`CTFontDescriptorCreateCSSFontDictionaryPerLanguage`, and the keys
`kCTFontDescriptor{Serif,SanSerif,Monospace,Cursive,Fantasy}FamilyKey`. The
signature has to be reverse-engineered from the Tiger binary (the Hopper MCP
tools are available in this session). Fallback if that goes badly: hardcode
Times / Helvetica / Courier / Apple Chancery / Papyrus.

`CTFontManagerCopyAvailableFontFamilyNames`
(`FontCacheCoreText.cpp:205`, `FontCache::systemFontFamilies`) → Tiger **does**
export `_CTFontDescriptorCopyAvailableFontFamilyNames` (first line of
`tiger-CT.txt`), same shape. Fallback: `ATSFontFamilyIteratorCreate`/`Next`/
`ATSFontFamilyGetName`.

Dynamic Type (`CTFontDescriptorCreateWithTextStyle`,
`CTFontDescriptorGetTextStyleSize`) → return `nullptr` / default size. Callers
already fall through. Tiger has no Dynamic Type.

`CTFontCreatePathForGlyph` (`FontCoreText.cpp:789`, used by SVG text-on-path,
`-webkit-text-stroke` and canvas text paths) → `CGFontGetGlyphPath(CGFontRef,
const CGAffineTransform*, CGGlyph)`, which Tiger exports. Note the unit
difference: `CGFontGetGlyphPath` returns em-space, so pass
`CTFontGetMatrix(font)` concatenated with `scale(size / CTFontGetUnitsPerEm(font))`.

### `platform/graphics/coretext/` file inventory

Build wiring: `WebCore/PlatformCocoa.cmake:74` feeds `SourcesCocoa.txt` into the
unified-sources generator, so **`SourcesCocoa.txt` is authoritative**, not the
explicit `WEBCORE_SOURCES` block in `PlatformCocoa.cmake` (which omits
`DrawGlyphsRecorder.cpp`, `SystemFontDatabaseCocoa.mm`,
`SystemFontDatabaseCoreText.cpp`).

| File | `SourcesCocoa.txt` | Verdict |
|---|---|---|
| `FontCoreText.cpp` | `:505` | largest surface — 13 of the 66. Edits for classes B/E plus the `applyTransforms` stub-out |
| `FontCascadeCoreText.cpp` | `:504` | **riskiest** — the `CTFontDrawGlyphs` replacement (§3.1b) |
| `ComplexTextControllerCoreText.mm` | `:502` (`@nonARC`) | becomes the primary shaper; 3 small edits |
| `FontCustomPlatformDataCoreText.cpp` | `:506` | `@font-face` + the FontParser problem |
| `FontPlatformDataCoreText.cpp` | `:507` | live: 3 edits. Dead-in-WebKit1 (only reachable from the IPC deserializer `FontPlatformData::create(const Attributes&)`): 8 more — consider `#if`-ing the whole serialization block out |
| `DrawGlyphsRecorder.cpp` | `:503` | 2 edits. Cannot be excluded — `displaylists/DisplayListRecorder.{h,cpp}` hard-depends on it and is in the unconditional `Sources.txt` |
| `GlyphPageCoreText.cpp` | `:508` | 1 edit (`CTFontGetVerticalGlyphsForCharacters` → fall through) |
| `SimpleFontDataCoreText.cpp` | `:509` | 1 edit (`CTParagraphStyleSetCompositionLanguage` → no-op) |
| `DrawGlyphsRecorder.h`, `SimpleFontDataCoreText.h` | headers | as-is |

Nothing in this directory should be excluded. The exclusion candidates are
`rendering/AttachmentLayout.mm` (via `ENABLE_ATTACHMENT_ELEMENT=OFF`, §1.3) and
the `FontPlatformData::Attributes` serialization block.

## 3.2 CoreGraphics — 144 "missing", of which ~60 are noise

First, regenerate the ImageIO export list, which is currently an empty file:

```
toolchain/bin/tiger-nm -g -arch i386 \
  sysroot/System/Library/Frameworks/ApplicationServices.framework/Frameworks/ImageIO.framework/Versions/A/ImageIO \
  | grep -v ' U ' | awk '{print $NF}' | sed 's/^_//' | sort -u > logs/api/tiger-ImageIO.txt
```

That yields **271 symbols, 226 of them `kCG*` constants**, and removes 20 entries
from `missing-CG.txt`.

### Noise to drop from the list (~32 entries)

Types and macros, not symbols: `CGAffineTransform`, `CGFloat`, `CGPoint`,
`CGRect`, `CGSize`, `CGColorSpaceRef`, `CGRectMake`, `CGPointMake`, `CGSizeMake`,
`CGCeiling`, `CGFloor`, `CGRound`, `CGFAbs`, `CGFloatMin`, `CGContextStateSaver`
(a WebCore class, `platform/graphics/cg/CGContextStateSaver.h`),
`CGSubimageCacheWithTimer` (also WebCore's own,
`platform/graphics/cg/CGSubimageCacheWithTimer.cpp`).

Similarly, most of the `missing-kCG` diff is **C enumerators declared in the
10.4u SDK headers**, not dylib exports. Verified: `kCGBlendModeNormal`,
`kCGLineCapButt`, `kCGPathFill` and friends are all in
`sdk/MacOSX10.4u.sdk/.../CoreGraphics.framework/Headers/CGContext.h:34,43,46`.
Only the `CFStringRef` constants (`kCGColorSpaceSRGB`, `kCGImageProperty*`,
`kCGImageSource*`) are real exports, and the ImageIO ones are present.

### Real gaps, classified

| Group | Tiger has | Fix |
|---|---|---|
| **Gradients** — `CGGradientCreateWithColorComponentsAndOptions`, `CGGradientCreateWithColorsAndOptions`, `CGContextDrawLinearGradient`, `CGContextDrawRadialGradient`, `CGContextDrawConicGradient` | `CGShadingCreateAxial`, `CGShadingCreateRadial`, `CGShadingCreateCustom2`, `CGContextDrawShading`, the whole `CGFunctionCreate`/`CGFunctionEvaluate` API | **Rewrite `platform/graphics/cg/GradientRendererCG.{h,cpp}`** (~270 LOC, self-contained). Its `Gradient = RetainPtr<CGGradientRef>` typedef (`.h:52`) becomes `RetainPtr<CGShadingRef>` plus the stop table; `drawLinearGradient`/`drawRadialGradient` (`.cpp:254,259`) become `CGShadingCreateAxial`/`Radial` + `CGContextDrawShading`. The `drawsBefore`/`drawsAfterEndLocation` options map onto `CGShadingCreateAxial`'s extend flags. **Conic gradients have no Tiger equivalent** — approximate with a custom `CGFunction` over a radial, or gate `conic-gradient()` off in CSS. The file already has a `createGradientBySampling` path (`.cpp:213`) that pre-samples stops into components, which is exactly the shape a `CGFunction` evaluator wants. **≈ 250 LOC rewritten** |
| **Paths** — `CGPathCreateWithRect`, `CGPathCreateWithRoundedRect`, `CGPathAddRoundedRect`, `CGPathAddContinuousRoundedRect`, `CGPathAddUnevenCornersRoundedRect`, `CGPathCreateCopyByTransformingPath`, `CGPathCreateMutableCopyByTransformingPath`, `CGPathGetPathBoundingBox`, `CGPathCreateCopyByIntersectingPath` | `CGPathCreateMutable`, `CGPathAddRect`, `CGPathAddArcToPoint`, `CGPathAddCurveToPoint`, `CGPathApply`, `CGPathCreateMutableCopy`, **`CGPathGetGeometricBoundingBox`** | All shims, in `platform/graphics/cg/PathCG.cpp` (`:188`, `:245`, `:261`, `:275`, `:532`, `:644`). `CGPathGetPathBoundingBox` → `CGPathGetGeometricBoundingBox` is an exact rename. The transforming copies are a `CGPathApply` callback re-emitting through `CGPathAddCurveToPoint` with the matrix applied — ~40 LOC, write once, reuse for both. Rounded rects are four arcs; continuous (squircle) and uneven corners degrade to plain rounded. `CGPathCreateCopyByIntersectingPath` has no equivalent and no cheap fallback — find its caller and gate |
| **Color spaces** — `CGColorSpaceCreateWithName(kCGColorSpaceSRGB)`, `CGColorSpaceGetModel`, `CGColorSpaceGetName`, `CGColorSpaceGetBaseColorSpace`, `CGColorSpaceCopyPropertyList`, `CGColorSpaceCreateWithPropertyList`, `CGColorSpaceCreateExtended`, `CGColorSpaceIsWideGamutRGB`, `CGColorSpaceSupportsOutput`, `CGColorSpaceUsesExtendedRange` | `CGColorSpaceCreateWithName` **exists**, with `kCGColorSpaceGenericRGB` / `GenericGray` / `GenericCMYK` / `UserRGB` etc.; `CGColorSpaceGetColorSpaceModel`; `CGColorSpaceCreateWithICCData`; `CGColorSpaceCreateICCBased` | `platform/graphics/cg/ColorSpaceCG.{cpp,h}` is the single point. `kCGColorSpaceSRGB` (10.5) → build sRGB from an embedded ICC profile via `CGColorSpaceCreateWithICCData`, or accept `kCGColorSpaceGenericRGB` (Tiger's generic RGB is close to sRGB with a 1.8 gamma — visible but not wrong). `CGColorSpaceGetModel` → `CGColorSpaceGetColorSpaceModel` (rename). Wide-gamut/extended-range predicates → `false`. Display-P3, Rec.2020, linear-sRGB, XYZ → map all to generic RGB and let `ColorConversion.cpp` do the math in software |
| **Colors** — `CGColorCreateSRGB`, `CGColorCreateGenericGray`, `CGColorGetConstantColor(kCGColorClear/Black/White)` | `CGColorCreate(colorSpace, components)` | 3 one-line inlines in `platform/graphics/cg/ColorCG.cpp` |
| **Font rendering hints** — `CGContextSetAllowsFontSubpixelPositioning`, `...Quantization`, the `Get` forms, `CGContextSetShouldSubpixelPositionFonts`, `...QuantizeFonts`, `CGContextSetShouldAntialiasFonts`, `CGContextSetFontAntialiasingStyle`, `CGContextGetFontAntialiasingStyle`, `CGFontGetGlyphAdvancesForStyle`, `CGFontRenderingGetFontSmoothingDisabled`, the `kCGFontRenderingStyle*` constants | `CGContextSetShouldSmoothFonts`, `CGContextSetShouldAntialias`, `CGContextSetAllowsAntialiasing`, `CGFontGetGlyphAdvances` | **No-op every setter, return a fixed value from every getter.** Tiger's CG antialiases and smooths on its own; WebCore's subpixel-positioning machinery just does not apply. `CGFontGetGlyphAdvancesForStyle` → `CGFontGetGlyphAdvances`. Concentrated in `platform/graphics/cg/GraphicsContextCG.cpp` and `platform/graphics/coretext/FontCascadeCoreText.cpp` |
| **`CGContextBeginTransparencyLayerWithRect`** | `CGContextBeginTransparencyLayer` | shim: clip to the rect first, then begin the layer |
| **`CGContextDrawTiledImage`** | `CGPatternCreate` + `CGContextFillRect` | `platform/graphics/cg/PatternCG.cpp` already builds `CGPattern`s; route the tiled-image path through it. ~30 LOC |
| **`CGBitmapContextCreateWithData`** | `CGBitmapContextCreate` | drop the release-callback argument; free the buffer from the `ImageBuffer` destructor instead. `platform/graphics/cg/ImageBufferCGBitmapBackend.cpp` |
| **`CGContextDrawPathDirect`**, `CGContextStrokeArc` | `CGContextAddPath`+`CGContextDrawPath`, `CGContextAddArc`+`CGContextStrokePath` | trivial |
| **`CGFontCopyFamilyName`**, `CGFontGetGlyphsForUnichars` | `CTFontCopyFamilyName` on a wrapping `CTFont`; `CTFontGetGlyphsForCharacters` | trivial |
| **`CGDataProviderCreateMultiRangeDirectAccess`**, `CGDataProviderSetProperty` | `CGDataProviderCreateDirectAccess`, `CGDataProviderCreateWithCFData` | the multi-range form is an incremental-decode optimisation; fall back to the whole-buffer provider |
| **`CGPatternCreateWithImageTransformStep`** | `CGPatternCreate` | drop the step argument |
| **EDR / tone mapping / HDR** — `CGContextGet/SetEDRTargetHeadroom`, `Get/SetContentToneMappingInfo`, `CGImageApplyHDRGainMap`, `CGImageCreateWithContentHeadroom`, `CGImageGetContentHeadroom`, `CGImageGetHDRGainMapHeadroom`, `CGImageGetContentAverageLightLevelNits`, `CGImageCreatePixelBufferAttributesForHDRTarget` | nothing | **gate.** All are `HAVE(*)`-guarded or reachable only through HDR image paths. Add the corresponding `HAVE_` macros to `PlatformHave.h:2046` |
| **IOSurface** — all `CGIOSurfaceContext*` (9), `CGImageCreateFromIOSurface` | nothing | **gate** via `HAVE_IOSURFACE` (already off) and its satellites (§1.5) |
| **Lockdown mode** — `CGEnterLockdownModeForFonts`, `CGEnterLockdownModeForPDF`, `CGIsInLockdownModeForPDF` | nothing | gate |
| **Image blocks / providers** — `CGImageBlock*` (4), `CGImageProviderCreate`, `CGImageProviderGetSize`, `CGImageCreateWithImageProvider` | nothing | gate; these are the async-decode path |
| **Window server / CGS / CGL** — `CGSCopyConnectionProperty`, `CGSHWCaptureWindowList`, `CGSPackages*` (2), `CGLQueryRendererInfo`, `CGLDescribeRenderer`, `CGLDestroyRendererInfo`, `CGDisplayModeGetPixelsWide/High`, `CGEventCopyIOHIDEvent`, `CGContextCreateWithDelegate`, `CGContextSetOwnerIdentity` | Tiger exports a large private `CGS*` surface but not these | gate. `platform/graphics/cg/CGWindowUtilities.cpp` and the WebGL renderer query go with `ENABLE_WEBGL=OFF` |
| **`CGStyleCreateGaussianBlur`, `CGStyleCreateColorMatrix`** | `CGStyleCreate`, `CGStyleCreateShadow`, `CGStyleCreateFocusRing` (Tiger has the `CGStyle` family!) | interesting — Tiger's `CGStyle` covers shadows and focus rings, which is most of what WebCore wants it for. Gaussian blur and colour matrix are 10.7 additions; route those through the software filter path in `platform/graphics/filters/` |
| **PDF** — `CGPDFContextClose`, `CGPDFDictionaryGetNameString`, `CGPDFDocumentIsTaggedPDF`, `CGPDFPageLayoutGetAreaOfInterestAtPoint`, `CGContextDrawPDFPageWithAnnotations` | `CGPDFContext*` mostly present | `PDFDocumentImage.cpp` and `ImageBufferCGPDFDocumentBackend.cpp`. Low priority — exclude both initially |
| **ImageIO modern options** — `kCGImageSourceShouldCacheImmediately`, `kCGImageSourceUseHardwareAcceleration`, `kCGImageSourceDecodeRequest`, `kCGImageSourceSkipMetadata`, `kCGImageSourceEnableRestrictedDecoding`, `kCGImagePropertyGroups`, `kCGImageAuxiliaryData*`, `CGImageSourceGetPrimaryImageIndex`, `CGImageSourceCopyAuxiliaryDataInfoAtIndexWithOptions` | the core `CGImageSource` API, all of it | `platform/graphics/cg/ImageDecoderCG.cpp`. The unknown keys are safe to define as `CFSTR("...")` and pass — CG ignores unrecognised option keys (line `:68` already does exactly this for `kCGImageSourceEnableRestrictedDecoding`). `CGImageSourceGetPrimaryImageIndex` (`:460`) → `0`. The HDR gain-map (`:650-652`) and stereo-group (`:867`, `:985`) blocks → gate. **This file needs perhaps 30 lines of change for a fully working decoder.** |

**Estimate: ~600 LOC of CG shim, concentrated in five files** —
`GradientRendererCG.cpp` (rewrite), `PathCG.cpp`, `ColorSpaceCG.cpp`,
`GraphicsContextCG.cpp`, `ImageDecoderCG.cpp` — plus a
`platform/graphics/tiger/CGCompat.h` for the rename `#define`s.

---

# 4. AppKit and Foundation gaps

## 4.1 The shape of the problem

Of 147 NS classes WebCore/WebKitLegacy message, 37 are missing on Tiger
(`comm` of `used-NSclasses.txt` against `tiger-NSclasses.txt`; `NSApp` is a false
positive — it is a global `id` at
`sdk/MacOSX10.4u.sdk/.../NSApplication.h:42`, not a class). **26 of the 36 are
plain gate-offs** for features already being cut. Only two are real work.

But the class list is the smaller problem. The bigger one is **selectors on
classes that do exist**, and the single largest item there is a pure rename.

## 4.2 The highest-leverage items, in order

| # | Item | Kind | Clears | Effort |
|---|---|---|---|---|
| 1 | **`NSEvent` 10.12 renames** — 17 `NSEventType*`, 9 `NSEventModifierFlag*`, `NSEventMaskAny` | ~30 `#define`s in nscompat | **189 occurrences, ~20 files.** Every occurrence in the tree uses the new spelling and **zero** use the old one, so the mapping is total and mechanical | 30 lines |
| 2 | **SDK lightweight generics** — `__covariant` type parameters on `NSArray`/`NSDictionary`/`NSSet` (+mutable) | patch `sdk/MacOSX10.4u.sdk` Foundation headers | **268 occurrences across 86 files** (217 `NSArray<`, 25 `NSDictionary<`, 11 `NSSet<`, 7 `NSMutableArray<`, 4 `NSMutableDictionary<`, 2 `NSMutableSet<`, 2 `NSOrderedSet<`) | **18 one-line edits in 3 files**: `NSArray.h` lines 12,19,56,79,89,115; `NSDictionary.h` 11,19,38,61,68,77; `NSSet.h` 11,19,35,54,61,73 |
| 3 | **Pasteboard constants** — `NSPasteboardType*` (28 sites), `NSPasteboardName*` (7), `UTType.identifier` → `kUTType*` (44) | extend `WebCore/platform/mac/LegacyNSPasteboardTypes.h` | **~79 sites, 10 files** | ~60 lines |
| 4 | **`NSAppearance` / `LocalDefaultSystemAppearance` no-op** | gate | **78 call sites, 18 files** | ~15 lines |
| 5 | **`NSWindowStyleMask*` renames** | `#define` | 13 sites, 8 files | 5 lines |
| 6 | **30 `NSAccessibility*` string constants** | append to `CocoaAccessibilityConstants.h` | 6 files | ~30 lines |
| 7 | **`NSEventPhase` + `-phase`/`-momentumPhase`/`-scrollingDelta*`/`-hasPreciseScrollingDeltas`** | NSEvent category returning legacy values | 5 files | ~40 lines |
| 8 | **`NSColor` category** — 12 colour swaps | shim | 6 files | ~50 lines |
| 9 | **`NSView backingScaleFactor` → 1.0, `convertRect{To,From}Backing:` → identity, `NSWindow occlusionState` → Visible** | category | 13 sites, 5 files | ~20 lines |
| 10 | **Foundation remainders** — `+dataWithContentsOfURL:options:error:`, `-base64EncodedStringWithOptions:`, `-containsString:`, `-URLByDeletingLastPathComponent`, `-[NSURL fileSystemRepresentation]` | shim | 7 files | ~40 lines |
| 11 | **`#define __kindof`** to nothing | header | 17 sites | 1 line |

**Items 1-5 are ~130 lines and clear roughly 630 of ~900 individual gap sites.**

On item 2, the SDK patch: 18 lines versus 268 edits across 86 WebKit files.
Generics are erased by clang with no runtime or ABI consequence, so this is free.
`NSOrderedSet` does not exist on Tiger at all, but its two uses are in
`platform/ios/WebItemProviderPasteboard.mm`, which is not built.

## 4.3 Three genuine rewrites

### (a) `ScrollbarThemeMac` — the one real rewrite, ~150 new LOC

`WebCore/platform/mac/ScrollbarThemeMac.mm` drives the private 10.7
`NSScrollerImp` / `NSScrollerImpPair` (overlay scrollbars), declared in
`WebCore/PAL/pal/spi/mac/NSScrollerImpSPI.h:61-165`. Key sites:

| Line | Use |
|---|---|
| `:57-106` | `WebSnapshotScrollerImpDelegate` (needs `NSScrollerImpDelegate` + `CALayer`) |
| `:169` | `NSPreferredScrollerStyleDidChangeNotification` |
| `:220-241` | `scrollerImpForScrollbar()` |
| `:282` | `+[NSScrollerImp scrollerWidthForControlSize:scrollerStyle:]` → `scrollbarThickness` |
| `:293` | `recommendedScrollerStyle() == NSScrollerStyleOverlay` |
| `:341-343` | `knobMinLength` + inset arithmetic → `hasThumb` |
| `:482` | `knobMinLength` → `minimumThumbLength` |
| `:603-609` | `rectForPart:`, `drawKnobSlotInRect:highlight:`, `drawKnob` → **the whole paint path** |

Much of the Tiger-era code is still in the file and intact. **Keep**:
`nativeTheme` (`:176-180`), button constants (`:183-189`), `buttonRepaintRect`
(`:350-366`), `backButtonRect` (`:368-400`), `forwardButtonRect` (`:402-440`),
`trackRect` (`:442-476`), `handleMousePressEvent` (`:488-515`),
`scrollbarPartToHIPressedState` (`:522-538`), `WebScrollbarPrefsObserver`
(`:126-172` minus `:169` — and note the `AppleAquaScrollBarVariantChanged`
observer at `:167` is already there).

**Delete**: `:57-106`, `:169`, `:196-241`, `:296-312`, `:540-620`, `:655-696`.
**Rewrite**: `scrollbarThickness` → `+[NSScroller scrollerWidthForControlSize:]`
(present, `sdk/MacOSX10.4u.sdk/.../NSScroller.h:70-71`); `usesOverlayScrollbars`
→ `false`; `hasThumb` → constant minimum; `minimumThumbLength`.
**Add**: ~150 LOC of `HIThemeDrawTrack(&info, nullptr, ctx,
kHIThemeOrientationNormal)` paint with `HIThemeTrackDrawInfo.kind =
kThemeScrollBarMedium/Small`, plus `HIThemeDrawTrackTickMarks`. Both are in
Tiger's `HIToolbox.framework/Headers/HITheme.h` (grep with `grep -a`, the headers
are ISO-8859 encoded). Arrow placement from `AppleScrollBarVariant` in
`NSUserDefaults` replaces the hardcoded `ScrollbarButtonsNone` at `:260`.

**Drop from the build**: `platform/mac/ScrollbarMac.{h,mm}`,
`platform/mac/NSScrollerImpDetails.{h,mm}`,
`platform/mac/ScrollbarsControllerMac.{h,mm}`,
`PAL/pal/spi/mac/NSScrollerImpSPI.h`,
`page/scrolling/mac/Scroller{Pair,}Mac.*`.
**Two gate edits**: `platform/Scrollbar.cpp:56-60` takes the `#else` branch, and
`platform/ScrollbarsController.cpp:34` widens so the generic no-op controller is
used (every method in `ScrollbarsController.h:59-131` has a working default).
`ScrollAnimatorMac.mm` touches no `NSScrollerImp` — keep it.

### (b) Pasteboard — collapse to single-item, ~22 real sites

`WebCore/platform/mac/LegacyNSPasteboardTypes.h` (99 lines) already wraps every
Tiger constant in `legacy*PasteboardTypeSingleton()` inlines (`:35-93`) and is
already `#import`ed by all ten consumers (`PasteboardMac.mm:35`,
`PasteboardCocoa.mm:29`, `PlatformPasteboardMac.mm:34`, `DragDataCocoa.mm:31`,
`PasteboardWriter.mm:31`, `WebHTMLView.mm:116`, `WebView.mm:196`,
`WebPDFView.mm:60`, `WebNSPasteboardExtras.mm:45`).

Every Tiger constant is confirmed present in `logs/api/tiger-AppKit.txt`:
`NSStringPboardType` (`:2476`), `NSHTMLPboardType` (`:2160`),
`NSRTFPboardType` (`:2407`), `NSRTFDPboardType` (`:2404`),
`NSTIFFPboardType` (`:2519`), `NSURLPboardType` (`:2553`),
`NSFilenamesPboardType` (`:2077`), `NSFilesPromisePboardType` (`:2078`),
`NSColorPboardType` (`:1915`), `NSFontPboardType` (`:2107`),
`NSPDFPboardType` (`:2307`).

**The pre-10.6 path is still live** in
`WebCore/platform/mac/PlatformPasteboardMac.mm:331-360` — `declareTypes:` (`:344`),
`setData:forType:` (`:355`), `setString:forType:` (`:359`), with readers
`propertyListForType:` (`:191`), `setPropertyList:forType:` (`:456,482,484,486`),
`addTypes:owner:` (`:409,418,435`), `[NSURL URLFromPasteboard:]` (`:402`). And
`write(Vector<PasteboardCustomData>&)` at `:611` **already short-circuits to it
when `itemData.size() == 1`** (`:613`). Make that branch unconditional and the
10.6 code is dead.

Real work: `clearContents` (10.6, 3 sites) → `declareTypes:@[] owner:nil`;
`-[NSURL initWithPasteboardPropertyList:ofType:]` (2 sites) →
`+[NSURL URLFromPasteboard:]`; `_setExpirationDate:` SPI (3 sites) → delete;
`NSPasteboardItem`/`pasteboardItems`/`writeObjects:` (~12 sites) → collapse;
SVG UTI (`kUTTypeScalableVectorGraphics` is 10.7) → hardcode
`@"public.svg-image"` at `PlatformPasteboardMac.mm:390,531` and
`PasteboardCocoa.mm:78`. Gate out `PasteboardWriter.{h,mm}` (`NSPasteboardWriting`)
and `WebSharingServicePickerController.mm`.

`WebPlatformStrategies.mm:114-205` needs **zero** work — 20 in-process
one-line forwarders. `PasteboardItemInfo.h:44-98` is a pure-C++ POD.
Tiger's C UTI API (`UTTypeCreatePreferredIdentifierForTag`,
`kUTTagClassNSPboardType`) is complete, so `PasteboardMac.mm:615,657,658`
compiles unchanged.

### (c) Accessibility — port, do not stub, ~250-400 LOC

`WebAccessibilityObjectWrapperBase.h:72` is
`@interface WebAccessibilityObjectWrapperBase : NSObject {` — **no
`<NSAccessibility>` formal protocol, no `NSAccessibilityElement`.** This is the
one big Cocoa subsystem WebKit never migrated. It is the classic Tiger informal
protocol throughout `accessibility/mac/WebAccessibilityObjectWrapperMac.mm`:
`accessibilityActionNames` (`:458`), `accessibilityAttributeNames` (`:664`),
`accessibilityAttributeValue:` (`:2526`), `accessibilityFocusedUIElement`
(`:2754`), `accessibilityHitTest:` (`:2764`), `accessibilityIsAttributeSettable:`
(`:2847`), `accessibilityIsIgnored` (`:2892`), `accessibilityPerformAction:`
(`:3144`), `accessibilitySetValue:forAttribute:` (`:3211`),
`accessibilityAttributeValue:forParameter:` (`:4393`).

Of 547 unique `NSAccessibility*` identifiers, 384 are WebKit's own `#define`s
(mostly `accessibility/cocoa/CocoaAccessibilityConstants.h`) and 271 exist in
`tiger-AppKit.txt`. **30 genuine misses**, nearly all plain `@"AXFoo"` strings
that can be added as `#define`s alongside the 384 already there: the table/grid
group (`VisibleCells`, `ColumnHeaderUIElements`, `RowHeaderUIElements`,
`ColumnCount`, `RowCount`, `SelectedCells`, `RowIndexRange`, `ColumnIndexRange`,
`CellForColumnAndRowParameterized`, `CellRole`), misc attributes
(`ValueDescription`, `Required`, `PlaceholderValue`, `UnknownOrientationValue`,
`LevelIndicatorRole`, `DisclosureTriangleRole`), subroles (`Toggle`, `Timeline`,
`Switch`), and notifications (`SelectedCellsChanged`, `RowExpanded`,
`RowCollapsed`, `AnnouncementRequested`, `AnnouncementKey`, `PriorityKey`,
`PriorityHigh`, `PriorityLow`).

Two cannot be faked and must be `#if`-ed out:
`NSAccessibilityPostNotificationWithUserInfo` (10.7,
`AXObjectCacheMac.mm:226`) → fall back to `NSAccessibilityPostNotification`
dropping the user info; `NSAccessibilityCustomAction` (10.13,
`WebAccessibilityObjectWrapperMac.mm:4623-4637`) and
`NSAccessibilityRemoteUIElement` (10.9, 5 sites) → gate.

**There is no `ENABLE(ACCESSIBILITY)` gate left upstream** (0 hits), so stubbing
means *reintroducing* one across ~15-20 cross-platform call points — ~150 LOC
across 12 files, and you lose VoiceOver. Porting is barely more work.

## 4.4 NSTextInputClient — zero delta, 6 deletions

`WebHTMLView` **never migrated**. `WebHTMLView.mm:934` reads
`@interface WebHTMLView (WebNSTextInputSupport) <NSTextInput>`, and
`NSTextInputClient` has **0 occurrences** in `WebKitLegacy/mac`. All twelve
implemented methods match the Tiger signatures in
`sdk/MacOSX10.4u.sdk/.../NSInputManager.h:14-49` verbatim — `insertText:`
(`:6621` / `.h:16`), `doCommandBySelector:` (`:6561`/`:17`),
`setMarkedText:selectedRange:` (`:6503`/`:19`), `unmarkText` (`:6456`/`:21`),
`hasMarkedText` (`:6440`/`:22`), `conversationIdentifier` (`:6434`/`:23`,
`long` vs `NSInteger` — identical on i386), `attributedSubstringFromRange:`
(`:6400`/`:27`), `markedRange` (`:6378`/`:31`), `selectedRange` (`:6362`/`:35`),
`firstRectForCharacterRange:` (`:6328`/`:39`), `characterIndexForPoint:`
(`:6302`/`:43`), `validAttributesForMarkedText` (`:6263`/`:47`). No
`...replacementRange:` / `...actualRange:` variant exists anywhere.

Six deletions:

| Site | Fix |
|---|---|
| `WebHTMLView.mm:174` | drop `#import <pal/spi/mac/NSTextInputContextSPI.h>` |
| `WebHTMLView.mm:203,221` | drop `@class NSTextInputContext` + the `NSResponder(inputContext)` declaration |
| `WebHTMLView.mm:4164` | take the `NSInputManager` `#else` branch 20 lines below |
| `WebHTMLView.mm:6278-6281` | delete the `-inputContext` override; Tiger TSM asks `textStorage` (`:6283`) |
| `WebHTMLView.mm:6263-6275` | drop `NSTextAlternativesAttributeName` (10.8) and `NSTextInsertionUndoableAttributeName`; the other four constants exist on Tiger |
| `WebCoreSupport/WebEditorClient.mm:397` | `[[NSTextInputContext currentInputContext] discardMarkedText]` → `[[NSInputManager currentInputManager] markedTextAbandoned:]`, the pattern already at `WebHTMLView.mm:6806` |

**The real hazard in this file is elsewhere**: `_updateSecureInputState`
(`WebHTMLView.mm:6747`) uses `TISCreateASCIICapableInputSourceList` and
`TSMSetDocumentProperty` with `kTSMDocumentEnabledInputSourcesPropertyTag`.
**Text Input Sources is 10.5+.** Gate that method.

## 4.5 NSEvent, NSView, NSWindow, NSColor — the real deltas

Beyond the 189 renames (§4.2 item 1), the genuinely new NSEvent API is ~35 lines,
90% of it in `WebCore/platform/mac/PlatformEventFactoryMac.mm`: `NSEventPhase`
typedef + 7 enumerators and `-phase`/`-momentumPhase` (10.7, `:209-238`),
`-hasPreciseScrollingDeltas` / `-scrollingDeltaX/Y` (10.7, `:682-684`),
`-isDirectionInvertedFromDevice` (10.7, `:800`), `NSEventTypePressure` + `-stage`
(10.10, `:55,78,156,727,757`), `-CGEvent` (10.5, `:714`). Stub `NSEventPhase` as
an enum returning `NSEventPhaseNone`, route `deltaX * lineHeight` into the
non-precise path (`:686+`), force `m_force = 0`. There are **zero** uses of
`NSEventTypeGesture/Magnify/Swipe`, `magnification`, `deviceDeltaX/Y` or
`associatedEventsMask`. `-_scrollCount` is declared in `NSEventSPI.h:52` with
**0 real calls** — delete.

NSView/NSWindow: `backingScaleFactor` (4 sites: `PlatformScreenMac.mm:201`,
`WebView.mm:6892,6894,6895`) → `1.0`; `NSTrackingArea` (2 sites:
`WebHTMLView.mm:1828,4666`) → the Tiger
`addTrackingRect:owner:userData:assumeInside:` path **already live at
`WebHTMLView.mm:1761`**; `occlusionState` (1 site, `WebView.mm:4257`) → always
visible; `wantsUpdateLayer`/`updateLayer` (`WebFrameView.mm:517,522`) → delete,
Tiger AppKit never asks. `convertRect{To,From}Backing:` has 8 hits but **all are
WebKit's own implementations** of the `NSScrollerImp` delegate, not AppKit calls,
and go away with §4.3a. `inLiveResize` (12 sites) and `viewWillDraw` (9) exist on
Tiger.

NSColor: Tiger's `NSColor.h` has everything the old theme used, including
`colorUsingColorSpace:` (`:178`, since `NSColorSpace` shipped in 10.4). Component
getters take `float`, which is `CGFloat` on i386 — a non-issue. Twelve swaps in
`platform/graphics/mac/ColorMac.mm` and `rendering/mac/RenderThemeMac.mm`:
`unemphasizedSelectedContentBackgroundColor` → `+secondarySelectedControlColor`,
`selectedContentBackgroundColor` → `+alternateSelectedControlColor`,
`unemphasizedSelectedTextColor` → `+selectedTextColor`, `findHighlightColor` →
calibrated yellow, `quaternary`/`tertiaryLabelColor` → calibrated whites,
`+colorWithSRGBRed:` → `+colorWithCalibratedRed:` (all three call sites are
black/white/clear, so colorimetry is moot). Two structural, both in
`ColorMac.mm`: `+colorWithCGColor:` (10.8, `:43`, the whole `Color`→`NSColor`
cache) → `CGColorGetComponents` + `+colorWithCalibratedRed:`, and the
`NSColorSpace.deviceRGBColorSpace` dot-syntax at `:80` →
`colorUsingColorSpaceName:NSDeviceRGBColorSpace`, which the fallback at `:82-87`
already uses. `controlAccentColor`, `labelColor`, `linkColor` and the
`system*Color` family have **0 uses**.

`NSAppearance`: 62 references across 23 files, but
`platform/mac/LocalDefaultSystemAppearance.mm:32-55` is the whole story — a
save/set/restore of `+currentDrawingAppearance` (10.14), `+setCurrentAppearance:`,
`NSAppearanceNameDarkAqua` and `-appearanceByApplyingTintColor:`. It is
referenced 78 times, always as an RAII scope guard. **No-op the ctor/dtor, keep
`m_usingDarkAppearance = false`, and all 78 sites compile unchanged.**

---

# 5. WebKitLegacy/mac specifics

## 5.1 Directory inventory and keep/exclude

| Directory (`Source/WebKitLegacy/mac/`) | files | LOC | Tiger | Why |
|---|---|---|---|---|
| `WebView` | 38 | 34,800 | **keep, trim** | the API surface |
| `DOM` | 131 | 19,317 | **keep** | the ObjC DOM is part of the `WebView` API (`-[WebFrame DOMDocument]`, editing delegates, `WebElementDictionary`). Huge but mechanical and platform-neutral |
| `WebCoreSupport` | 29 | 8,864 | **keep, trim** | the client implementations |
| `Misc` | 30 | 4,032 | **keep** | mostly NS category extensions |
| `History` | 7 | 3,139 | **keep** | `BinaryPropertyList.cpp` (844) is self-contained C++ |
| `Plugins` | 5 | 1,834 | **exclude** | §5.4 |
| `WebInspector` | 5 | 1,027 | **exclude** | §5.5 |
| `Storage` | 6 | 778 | **keep** | localStorage/WebSQL glue; pairs with `Source/WebKitLegacy/Storage/*.cpp` |
| `DefaultDelegates` | 4 | 717 | **keep** | tiny, and the non-nil fallbacks are required |
| `Panels` | 2 | 468 | **keep** | plain NSPanel auth sheet, Tiger-safe |
| `Carbon`, `Workers`, `Accessibility` | — | — | **absent** | these directories no longer exist upstream |
| `icu` | 0 | 0 | — | contains only a README |

Within `WebView`, drop: `WebPDFView.mm` (1509) + `WebPDFRepresentation.mm` (109)
+ `WebPDFDocumentExtras.mm` (149), `WebImmediateActionController.mm` (601),
`WebFullScreenController.mm` (584), `WebVideoFullscreenController.mm` (350),
`WebNotification.mm` (210), `WebDeviceOrientation*.mm` (209),
`WebMediaPlaybackTargetPicker.mm` (126), `WebGeolocationPosition.mm` (88),
`WebIndicateLayer.mm` (73).

Within `WebCoreSupport`, drop: `WebInspectorClient.mm` (902),
`WebGeolocationClient.mm` (242), `TextIndicatorWindow.mm` (195),
`WebNotificationClient.mm` (190), `CorrectionPanel.mm` (124),
`WebPaymentCoordinatorClient.mm` (112), `WebSelectionServiceController.mm` (99),
`WebAlternativeTextClient.mm` (96), `WebKitFullScreenListener.mm` (75),
`WebMediaKeySystemClient.mm` (53). Note `WebApplicationCache.mm` and
`WebApplicationCacheQuotaManager.mm` are already 0 bytes upstream.

Within `Misc`, drop `WebSharingServicePickerController.mm` (256, `NSSharingService`
is 10.8).

Build files: `Source/WebKitLegacy/CMakeLists.txt` (54, the generic non-Cocoa
list), `PlatformCocoa.cmake` (1732), `Sources.txt` (31), `SourcesCocoa.txt` (188,
the unified list), `SourcesCMakeCocoa.txt` (152, CMake-only additions registered
at `PlatformCocoa.cmake:47-55`). `PlatformCocoa.cmake:57-75` and `:118-134` add
the non-unified `.m` files; `:78-110` drives the preferences codegen
(`GeneratePreferences.rb` → `WebViewPreferencesChangedGenerated.mm`,
`WebPreferencesInternalFeatures.mm`, `WebPreferencesExperimentalFeatures.mm`,
`WebPreferencesDefinitions.h`).

**Kept total: ~72k LOC of `.mm`/`.m`/`.cpp`.**

## 5.2 The software paint path

| Step | Site |
|---|---|
| 1 | `WebView/WebHTMLView.mm:3891` `-drawRect:` — `getRectsBeingDrawn:count:`, `_restoreSubviews`, union-vs-individual heuristic (`cRectThreshold` 10, `cWastedSpaceThreshold` 0.75) |
| 2 | `WebHTMLView.mm:3933,3936,3939` → `-drawSingleRect:` |
| 3 | `WebHTMLView.mm:3845` `-drawSingleRect:` — saves gstate, `NSRectClip`, `[(WebClipView *)superview setAdditionalClip:]` |
| 4 | `WebHTMLView.mm:3862` → `[[self _frame] _drawRect:rect contentsOnly:YES]` |
| 5 | `WebView/WebFrame.mm:666` — builds a `GraphicsContextCG` from `[[NSGraphicsContext currentContext] CGContext]`, sets `PaintBehavior`, calls `view->paintContents(context, enclosingIntRect(rect))` |
| 6 | `LocalFrameView::paintContents` → `RenderView::paint` |

`-[WebFrame _paintBehaviorForDestinationContext:]` already ORs in
`PaintBehavior::FlattenCompositingLayers | Snapshotting` — the software path is
designed to flatten. There is no `_web_drawRect` in the tree.
`WebHTMLView.mm:6900` also calls `drawSingleRect:` from `-drawLayer:inContext:`
(`:6178`), which is the CA path and goes away.

**One hard CA dependency remains in `mac/WebView/`:**
`WebViewRenderingUpdateScheduler.mm` (197 LOC) uses
`[CATransaction addCommitHandler:forPhase:]` (10.14) at `:105-118` to drive the
rendering update. Replace with a `CFRunLoopObserver` or an `NSTimer` at the
display refresh rate. This is required even with compositing off, because it is
what pumps `Page::updateRendering()`.

## 5.3 WebPreferences

`WebView/WebPreferences.mm` is 3325 lines; `+initialize` is `:305-359` but almost
everything now comes from
`INITIALIZE_DEFAULT_PREFERENCES_DICTIONARY_FROM_GENERATED_PREFERENCES` (`:313`),
generated from `Source/WTF/Scripts/Preferences/UnifiedWebPreferences.yaml`.
**Change defaults in that YAML's `WebKitLegacy:` override branch, not in
`WebPreferences.mm`.** Keys are `#define`s in
`WebView/WebPreferenceKeysPrivate.h`.

| Concern | Key (`WebPreferenceKeysPrivate.h`) | Tiger value |
|---|---|---|
| Cache model | `WebKitCacheModelPreferenceKey` `:182` | force `WebCacheModelDocumentViewer` (512MB-2GB boxes) |
| Accelerated compositing | `WebKitAcceleratedCompositingEnabledPreferenceKey` `:98` | **`false`** (yaml default is `true`) |
| WebGL | `WebKitWebGLEnabledPreferenceKey` `:107` | **`false`** |
| Accelerated 2D canvas | `WebKitAccelerated2dCanvasEnabledPreferenceKey` `:109` | `false` |
| Web Audio | `WebKitWebAudioEnabledPreferenceKey` `:106` | `false` |
| Back/forward cache | `WebKitUsesPageCachePreferenceKey` `:82` | **`false` initially** — it multiplies resident memory and hides teardown bugs. Turn on once stable |
| Offline app cache | `WebKitOfflineWebApplicationCacheEnabledPreferenceKey` `:92` | `false` (`WebKitApplicationCacheTotalQuota` is already `@0` at `:324`) |
| Local storage / WebSQL | `WebKitLocalStorageEnabledPreferenceKey` `:61`, `WebKitDatabasesEnabledPreferenceKey` `:60`, `WebKitWebSQLEnabledPreferenceKey` `:168` | keep on — SQLite-backed and cheap |
| Developer extras | `WebKitDeveloperExtrasEnabledPreferenceKey` `:84` | `false`, forced by §5.5 |
| Full screen | `WebKitFullScreenEnabledPreferenceKey` `:112` | `false` |
| Service controls | `WebKitServiceControlsEnabledPreferenceKey` `:150` | `false` |
| Gamepads | `WebKitGamepadsEnabledPreferenceKey` `:149` | `false` |
| SubtleCrypto | `WebKitSubtleCryptoEnabledPreferenceKey` `:159` | `false` — `WebCryptoClient.mm` uses CommonCrypto newer than 10.4 |
| Spelling/grammar/autocorrect | `WebContinuousSpellCheckingEnabled` `:73`, `WebGrammarCheckingEnabled` `:74`, `WebAutomaticSpellingCorrectionEnabled` `:80`, `WebAutomaticQuoteSubstitutionEnabled` `:76`, `WebAutomaticLinkDetectionEnabled` `:77`, `WebAutomaticDashSubstitutionEnabled` `:78`, `WebAutomaticTextReplacementEnabled` `:79`, `WebKitAsynchronousSpellCheckingEnabledPreferenceKey` `:113` | all `NO` — every one is 10.5/10.6 `NSSpellChecker` |
| Smart insert/delete | `WebSmartInsertDeleteEnabled` `:71` | keep `true` |
| Private browsing | `WebKitPrivateBrowsingEnabledPreferenceKey` `:70` | `NO` (already `:321`) |

There is **no font-smoothing preference any more** (`WebKitFontSmoothingLevel`
was removed; smoothing is now `FontSmoothingMode` in CSS/Settings), and **no
plugins or Java preference** (both removed upstream).

## 5.4 Plugins

`ENABLE_NETSCAPE_PLUGIN_API` does not exist — NPAPI is gone from modern WebKit.
`ENABLE_PDF_PLUGIN` (`WebKitFeatures.cmake:267`) and `ENABLE_PDFKIT_PLUGIN`
(`:268`) already default OFF and are WebKit2-only.

Drop from `Source/WebKitLegacy/SourcesCocoa.txt`:
`mac/Plugins/WebBasePluginPackage.mm` (394),
`mac/Plugins/WebPluginContainerCheck.mm` (192),
`mac/Plugins/WebPluginController.mm` (641),
`mac/Plugins/WebPluginDatabase.mm` (468); and from `SourcesCMakeCocoa.txt`:
`mac/Plugins/WebPluginPackage.mm` (139). Remove
`${WEBKITLEGACY_DIR}/mac/Plugins` from the include dirs at
`PlatformCocoa.cmake:33`.

**Keep all 1228 lines of `Source/WebCore/plugins/`.** `PluginData` and
`PluginInfoProvider` are not optional: `Page` holds a `Ref<PluginInfoProvider>`
and `navigator.plugins`/`navigator.mimeTypes` bind to `DOMPluginArray` /
`DOMMimeTypeArray` unconditionally. They degrade to empty lists on their own.

`Source/WebKitLegacy/mac/WebCoreSupport/WebPluginInfoProvider.mm` (75) is
**already effectively a stub**: `pluginInfo()` (`:52`) has a dead
`return plugins;` at `:63` before the loop. Only `refreshPlugins()` (`:47`)
touches `WebPluginDatabase` — delete that one line.

`WebFrameLoaderClient::createPlugin` (`WebCoreSupport/WebFrameLoaderClient.mm:1713`,
~100 lines) **has a non-plugin path**: the first branch (`:1728-1746`) asks the UI
delegate for `webView:plugInViewWithArguments:` and wraps the returned `NSView`
in a `PluginWidget`. Returning `nullptr` unconditionally is safe —
`Source/WebCore/loader/SubframeLoader.cpp:428-430` handles a null widget and falls
back to `<object>` fallback content. `redirectDataToPlugin` (`:1814`) becomes a
no-op.

## 5.5 WebInspector — OFF, but the backend is not separable

`mac/WebInspector/` is 5 sources / 1027 LOC (`WebNodeHighlightView.mm` 342,
`WebNodeHighlight.mm` 295, `WebInspector.mm` 188, `WebNodeHighlighter.mm` 104,
`WebInspectorFrontend.mm` 98), plus `WebCoreSupport/WebInspectorClient.mm`
(**902 LOC**), `cf/WebCoreSupport/WebInspectorClientCF.cpp`,
`WebCoreSupport/LegacyWebPageDebuggable.cpp`,
`WebCoreSupport/LegacyWebPageInspectorController.cpp`.

It needs a second full `WebView` hosting the frontend
(`WebInspectorClient.mm:84,514`), and the `com.apple.WebInspectorUI` resource
bundle (`:232` `localizedStringsURL`, `:532`). **`Source/WebInspectorUI/` does not
exist in this checkout**, so the frontend HTML/JS is not even present. It also
imports `SecurityInterface/SFCertificatePanel.h` (`:47`), an API Tiger's
SecurityInterface does not have.

**`ENABLE_INSPECTOR_*` is not separable from the backend.** There is no
`ENABLE_INSPECTOR` and no `ENABLE_WEBINSPECTORUI` option. `WebKitFeatures.cmake`
has only `ENABLE_INSPECTOR_ALTERNATE_DISPATCHERS` (`:229`, OFF, and
`WEBKIT_OPTION_DEPEND`s on `ENABLE_REMOTE_INSPECTOR` at `:347`),
`ENABLE_INSPECTOR_EXTENSIONS` (`:230`, OFF), `ENABLE_INSPECTOR_TELEMETRY`
(`:231`, OFF) and `ENABLE_REMOTE_INSPECTOR` (`:277`, **ON** by default — already
forced OFF in the Tiger block).

`Source/WebCore/inspector/` is **84 `.cpp` files, 35,643 LOC**, none of it behind
a flag. There is no `InspectorController.cpp` any more — it is
`PageInspectorController` plus `InspectorInstrumentation.h`, whose hooks are
compiled unconditionally into `Document`, `FrameLoader`, `ResourceLoader`,
`ScriptExecutionContext` and the DOM. The JSC side
(`Source/JavaScriptCore/inspector/`, 30+ files) is likewise mandatory:
`JSGlobalObject` holds an agent set.

**Recommendation: OFF.** Drop the eight WebKitLegacy files. This saves ~2400 LOC
of WebKitLegacy and costs `-[WebInspector show:]`, the node-highlight overlay,
and makes `WebKitDeveloperExtrasEnabledPreferenceKey` inert. You still compile
and link all 35,600 lines of `WebCore/inspector/` plus the JSC inspector — the
second-largest chunk of dead weight in the build after the DOM bindings.
`Page::inspectorController()` still has to return something, so keep
`LegacyWebPageInspectorController.cpp` if removing it breaks the link.

## 5.6 Spellcheck, dictation, data detectors

`EditorClient` is a pure-virtual interface, so every method must exist. The
no-op form of `WebCoreSupport/WebEditorClient.mm` (1306 LOC):

| Function | Line | No-op |
|---|---|---|
| `isContinuousSpellCheckingEnabled` | `:206` | `return false;` |
| `isGrammarCheckingEnabled` | `:218` | `return false;` |
| `spellCheckerDocumentTag` | `:228` | `return 0;` |
| `isAutomaticSpellingCorrectionEnabled` | `:590` | `return false;` |
| `toggleAutomaticQuoteSubstitution` / `LinkDetection` / `DashSubstitution` / `TextReplacement` / `SmartLists` | `:555,565,575,585,605` | `{ }` |
| `checkSpellingOfString` | `:952` | `*misspellingLocation = -1; *misspellingLength = 0;` |
| `checkGrammarOfString` | `:968` | `*badGrammarLocation = -1; *badGrammarLength = 0;` |
| `checkTextOfParagraph` | `:908` and `:1074` (two `#if`-split definitions) | `return { };` |
| `shouldEraseMarkersAfterChangeSelection` | `:936` | `return true;` |
| `ignoreWordInSpellDocument` / `learnWord` | `:942,947` | `{ }` |
| `getGuessesForWord` | `:1109` | `guesses.clear();` |
| `updateSpellingUIWithGrammarString` / `WithMisspelledWord` / `showSpellingUI` | `:1080,1090,1095` | `{ }` |
| `spellingUIIsShowing` | `:1104` | `return false;` |
| `requestCheckingOfString` / `requestExtendedCheckingOfString` / `requestCandidatesForSelection` | `:1272,1295,1134` | `{ }` |
| `handleRequestedCandidates` / `handleAcceptedCandidateWithSoftSpaces` / `didCheckSucceed` | `:1168,1201,1259` | **delete** — `NSTextCheckingResult` candidates are 10.12 |

That removes every `NSSpellChecker` dependency except
`-checkSpellingOfString:startingAt:`, which does exist on 10.4 if you want real
spellcheck later.

`ENABLE_DATA_DETECTION` is hardcoded to 1 at `PlatformEnableCocoa.h:306-307`;
zero it in the `PLATFORM(TIGER)` block. `ENABLE_WRITING_TOOLS` is already OFF.
Drop `CorrectionPanel.mm`, `WebAlternativeTextClient.mm`,
`WebSharingServicePickerController.mm`, `WebSelectionServiceController.mm`.

## 5.7 Printing — Tiger-compatible as written

The whole path is the 10.2-era `NSView` printing protocol.
`WebView/WebHTMLView.mm`: `-_web_setPrintingModeRecursive:adjustViewSize:`
(`:1441`), `-_web_setPrintingModeRecursive` (`:1449`),
`-_web_clearPrintingModeRecursive` (`:1454`),
`-_web_setPrintingModeRecursiveAndAdjustViewSize` (`:1459`),
`-_setPrinting:minimumPageLogicalWidth:...` (`:4689`, declared `:930`),
`-adjustPageHeightNew:top:bottom:limit:` (`:4727`),
`-_scaleFactorForPrintOperation:` (`:4748`),
`-_provideTotalScaleFactorForPrintOperation:` (`:4773`),
`-_delayedEndPrintMode:` (`:4793`), plus `-knowsPageRange:` and `-rectForPage:`
in the `:4780-4900` block. `WebView/WebFrameView.mm`:
`-printOperationWithPrintInfo:` (`:1089`), `-canPrintHeadersAndFooters` (`:1080`),
`-documentViewShouldHandlePrint` (`:1102`), `-printDocumentView` (`:1111`).
`Misc/WebNSPrintOperationExtras.m` (54) uses `NSPrintScalingFactor`, `paperSize`
and the margin accessors — all 10.0.

One gap: `-_provideTotalScaleFactorForPrintOperation:` (`:4773`) is an AppKit
private callback that only newer AppKit invokes. On Tiger it never fires, so
shrink-to-fit scaling is lost. Compensate by applying
`_scaleFactorForPrintOperation:` inside `beginDocument`. `CGFloat` is `float` on
i386, which the `adjustPageHeightNew:` signature already assumes.

## 5.8 Drag and drop — both paths are already in the tree

Destination (`WebView/WebView.mm`): `-draggingEntered:` (`:6320`),
`-draggingUpdated:` (`:6333`), `-draggingExited:` (`:6350`),
`-prepareForDragOperation:` (`:6366`), `-performDragOperation:` (`:6371`) are all
10.0. **Delete `:6398-6420`**, which uses
`-[NSDraggingInfo enumerateDraggingItemsWithOptions:...]` (10.7) with
`NSFilePromiseReceiver` (10.12); fall through to `:6423`
`dragController().performDragOperation(DragData{...})`.

Source (`WebView/WebHTMLView.mm`): `-dragImage:at:offset:event:pasteboard:source:slideBack:`
(`:4244`, calls `super` at `:4256`), `-draggingSourceOperationMaskForLocal:`
(`:4287`), `-draggedImage:endedAt:operation:` (`:4300`),
`-namesOfPromisedFilesDroppedAtDestination:` (`:4339`) — **all 10.0, this is the
Tiger API, keep it**.

`WebCoreSupport/WebDragClient.mm` (299) contains both paths side by side. Keep
the UI-delegate hook at `:175-178` and the
`[topHTMLView dragImage:at:offset:event:pasteboard:source:slideBack:]` call at
`:184`; delete the function that uses
`[[NSDraggingItem alloc] initWithPasteboardWriter:]` (`:197`) and
`beginDraggingSessionWithItems:event:source:` (`:209`), and route all callers to
`:184`. `createPasteboardWriter` goes with it (`NSPasteboardWriting` is 10.6).

`Source/WebCore/platform/mac/DragImageMac.mm` no longer exists; it merged into
`platform/cocoa/DragImageCocoa.mm` (337). Audit that file for `NSImage` drawing:
the 10.4-safe primitives are `-lockFocus` / `-compositeToPoint:operation:`, not
`-drawInRect:fromRect:operation:fraction:respectFlipped:hints:` (10.6). It also
holds the `CTFramesetter` drag-label layout described in §3.1.

## 5.9 Accessibility in WebKitLegacy — ~30 lines, no changes needed

There is no `mac/Accessibility/` directory. The whole WebKitLegacy layer is
forwarding: `WebHTMLView.mm:4991` `-accessibilityAttributeValue:` (intercepts
`NSAccessibilityChildrenAttribute`, returns `[[self _frame] accessibilityRoot]`,
else `super` at `:5000`), `:5005` `-accessibilityFocusedUIElement`, `:5013`
`-accessibilityHitTest:`, `:5027` `-_accessibilityParentForSubview:`, `:2523`
`-accessibilityRootElement`; and `WebView/WebFrame.mm:2197`
`-[WebFrame accessibilityRoot]`, the real entry point, which calls
`AXObjectCache::accessibilityEnabled()` (`:2199`) and
`setEnhancedUserInterfaceAccessibility(...)` (`:2203`).

**`WebFrameView` overrides nothing.** Every API used here is 10.2/10.4. The work
is all in `Source/WebCore/accessibility/mac/` (§4.3c).

## 5.10 WebKit2 contamination — none

Grepped `mac/`, `WebCoreSupport/`, `Storage/`, `cf/` for `ENABLE(WEBKIT2)`,
`WebProcess`, `<WebKit/`, `IPC::`, `WKPage`, `WebPageProxy`. Four hits, none
real: two `OriginAccessPatternsForWebProcess::singleton()` (a misleadingly named
**WebCore** type in `page/OriginAccessPatterns.h`, at
`Plugins/WebPluginContainerCheck.mm:101` and `WebView/WebFrame.mm:2082`), one
`WTF::IOSApplication::isWebProcess()` (`WebResourceLoadScheduler.cpp:288`, a
bundle-ID check returning false), and
`WebView/WebHTMLViewForTestingMac.h:30`'s `#import <WebKit/WebHTMLView.h>`, where
`<WebKit/...>` is the *legacy* umbrella and the file is in no source list.
**Nothing to exclude on this axis.**

---

# 6. Later phase: compositing through CARenderer

Do not attempt this until milestone M5. It is orthogonal to everything above and
the software path has to work first.

## 6.1 What we have

Per `atv/REPORT.md`: Apple TV Software 3.0.2 ships an i386 `QuartzCore.framework`
1.6.0 (build 222.0) with 43 CA classes, extracted to
`atv/extracted/3.0.2/QuartzCore.framework`. It loads on the real 10.4.11 box with
**zero unresolved symbols**; `spike/catest.m` (17 checks) and
`spike/carendertest.m` (CARenderer over a CGL pbuffer, GPU-rendered pixel
readback on a GeForce 8600M) both pass.

Constraints, all proven:
- **Tiger AppKit has no layer-backed views.** `setWantsLayer:` does not appear as
  a string anywhere in Tiger's AppKit, and the window server speaks no CA render
  protocol, so `CAContext contextWithCGSConnection:options:` will not composite
  into a window even though the selector exists.
- The only path is a `CARenderer` inside an `NSOpenGLView`, driven manually.
  Two non-obvious requirements: **`[CATransaction flush]` must be called
  explicitly** (CA normally commits from a run-loop observer; headless the render
  tree stays empty and you render black with no error), and **`addUpdateRect:`
  must be called**, with a real `position` on the root layer.
- The install name collides with Tiger's own QuartzCore, and 207 Core Image class
  names are duplicates. Run `install_name_tool -id` to give it a private path
  next to WebKit and load it explicitly. Never install into the box's `/System`.
- Headers: use `sdk/MacOSX10.5.sdk`'s QuartzCore headers (Core Animation 1.x, the
  same generation).
- Missing classes: `CASpringAnimation`, `CAPresentationModifier(Group)`,
  `CABackdropLayer`. Missing selectors are all post-10.6: `contentsScale`,
  `shouldRasterize`, `drawsAsynchronously`, `setAllowsEdgeAntialiasing:`,
  `setContentsFormat:`, EDR/tone-map setters. None load-bearing.

Apple shipped `GraphicsLayerCA` on this exact stack: the same image carries
WebKit 528.18.0 for Darwin 8 i386, whose WebCore links QuartzCore 1.6.0 and
contains `GraphicsLayerCA_property`, `WebLayer`, `WebTiledLayer`, `CATiledLayer`,
`CATransformLayer`, `CAPropertyAnimation`, `CAKeyframeAnimation`,
`CAValueFunction`. Kept at `atv/extracted/3.0.2/WebKit.framework` as reference —
we are reading how it did the hosting, not linking it. `BackRow.framework` in the
same image contains exactly `CARenderer`, `rendererWithCGLContext:options:` and
`setLayer:`, and **no `CAContext` strings at all**, confirming that Apple itself
drove CA on Darwin 8 through a raw CGL `CARenderer`.

## 6.2 What WebKitLegacy's layer hosting touches today

| Site | What it does |
|---|---|
| `Source/WebKitLegacy/mac/WebCoreSupport/WebChromeClient.mm:883` | `attachRootGraphicsLayer(LocalFrame&, GraphicsLayer*)` — calls `-[WebHTMLView attachRootLayer:]` / `detachRootLayer` |
| `Source/WebKitLegacy/mac/WebView/WebHTMLView.mm:6132` | `-attachRootLayer:` — creates the `layerHostingView` (an `NSView` with `setWantsLayer:YES`) and installs the root `CALayer` |
| `WebHTMLView.mm:702` | `WebRootLayer : CALayer` — the host layer, overrides `-renderInContext:` |
| `WebHTMLView.mm:6156,6172` | `setWantsLayer:` / `-layer` / `setLayer:` |
| `WebHTMLView.mm:6178` | `-drawLayer:inContext:` → `[view drawSingleRect:rect]` — the CA drawing callback |
| `WebView/WebViewRenderingUpdateScheduler.mm:105-118` | `[CATransaction addCommitHandler:forPhase:]` to drive `Page::updateRendering()` |
| `WebCore/platform/graphics/ca/PlatformCALayerCocoa.mm` | `WebLayer`, `WebSimpleLayer` — the `CALayer` subclasses WebCore instantiates; `WebTiledLayer` is the historical `CATiledLayer` subclass, now `WebTiledBackingLayer.mm` driven by `TileController` |

## 6.3 What a CARenderer host would need

The shape is the Windows `CACFLayerTreeHost` design, for which there is precedent
in WebKit's own history.

1. **A `WebCARendererHostView : NSOpenGLView`** replacing `layerHostingView`. It
   owns a `CARenderer` created with
   `[CARenderer rendererWithCGLContext:options:]` from its `NSOpenGLContext`'s
   `CGLContextObj`, sets `renderer.layer` to the root layer WebCore hands over,
   and in `-drawRect:` does
   `[CATransaction flush]` → `[renderer beginFrameAtTime:timeStamp:]` →
   `[renderer addUpdateRect:bounds]` → `[renderer render]` →
   `[renderer endFrame]` → `[[self openGLContext] flushBuffer]`.
2. **A display link substitute.** Tiger has CoreVideo 1.4.1 with
   `CVDisplayLink`, which is the right driver; fall back to a 60Hz `NSTimer` if
   it misbehaves. This same driver replaces
   `WebViewRenderingUpdateScheduler.mm`'s `CATransaction` commit handler, so the
   §5.2 work is a prerequisite and not wasted.
3. **Dirty-rect plumbing.** `CARenderer` needs `addUpdateRect:` per frame.
   `[renderer updateBounds]` gives CA's own accumulated damage; union it with
   WebCore's `GraphicsLayer::setNeedsDisplayInRect` damage.
4. **Re-enable the CA files from §1.4** — `GraphicsLayerCA.cpp`,
   `PlatformCALayerCocoa.mm`, `PlatformCAAnimationCocoa.mm`,
   `PlatformCAFiltersCocoa.mm`, the tiling set — compiled against the 10.5 SDK's
   QuartzCore headers, with the missing selectors from `atv/REPORT.md` stubbed:
   `contentsScale` hardcoded to 1.0 (Tiger has no Retina), `shouldRasterize`
   ignored, `drawsAsynchronously` ignored, `CASpringAnimation` degraded to
   `CABasicAnimation` with an ease-out timing function, `CABackdropLayer` and
   `backdrop-filter` gated off.
5. **Load the framework explicitly** with `dlopen` of the renamed private path
   before the first `CALayer` message, since the ObjC classes are resolved
   lazily on the fragile runtime and nothing must link it at build time.
6. **`allowedCompositingTriggers()`** in `WebChromeClient.h:178-188` goes from
   `0` back to the real mask, and `acceleratedCompositingEnabled` back to `true`.

Uncosted but substantial. Expect this phase to be comparable in size to the whole
of §3.

---

# 7. Build order and milestones

Effort figures are engineer-days of focused work, assuming the toolchain and
compat layers hold up. They exclude debugging on the target box, which
historically doubles everything.

| # | Milestone | Gate | Prerequisites | Effort |
|---|---|---|---|---|
| **M0** | **WTF + JSC link and `jsc -e 'print(1+1)'` runs on the box** | the journal's "Next session" list | `object_isClass` in objc2compat; the C-loop offlineasm build | in flight |
| **M1** | **Feature switches land; WebCore configures** | `cmake -DENABLE_WEBCORE=ON` completes, DerivedSources generate | §1 in full: `USE_CA 0`, `USE_CORE_IMAGE 0`, the CA exclusions + `GraphicsLayerTiger.cpp` stub, the `HAVE_*` additions, the new `ENABLE_*` off-list, `ENABLE_DATA_DETECTION 0`. Plus the SDK generics patch (§4.2 item 2) and the nscompat items 1, 3, 4, 5, 11 — do these **before** compiling anything, they are what stop the first 600 errors | 3-4 d |
| **M2** | **WebCore compiles** (every TU produces an object) | `ninja WebCore` reaches 100% of compile steps | §3 in full (CoreText B/C/E remaps, the CG shim, `GradientRendererCG` rewrite, `PathCG`, `ColorSpaceCG`, `ImageDecoderCG`), §4.3a-c, the remaining nscompat items. Expect the long tail: unified-source batches mean one bad file blocks twenty | **15-20 d — the single largest milestone** |
| **M3** | **WebCore archives and its symbols resolve** | link a trivial `.cpp` against `libWebCore.a` + the frameworks with no undefined symbols | shakes out every stub written blind in M2. Historically this is where `CTFontShapeGlyphs`-shaped decisions get revisited | 3-5 d |
| **M4** | **WebKitLegacy compiles and links** | `libWebKitLegacy.a` archives; a link test resolves | §5 (exclusions, `WebEditorClient` no-ops, drag/print/pasteboard edits, `WebViewRenderingUpdateScheduler` run-loop driver) + §2 minus the network transport (stub `ResourceHandle::start()` to fail every load) | 5-7 d |
| **M5** | **`about:blank` in a window on Tiger** | a ~200-line test app creates a `WebView`, loads `about:blank`, and paints white with no crash | the whole software paint path (§5.2), `ScrollbarThemeMac` (§4.3a), `RenderThemeMac`, `ColorMac`, event plumbing. **This is the real proof point** — it exercises layout, painting, fonts, and the run loop end to end without touching the network | 5-10 d |
| **M6** | **A local `file://` page with text, a font and an image renders correctly** | side-by-side screenshot against Safari 2 on the same box | CoreText painting (§3.1b) and ImageDecoderCG under real load. Text metrics bugs surface here, not at M5 | 5-8 d |
| **M7** | **A real page over HTTPS** | `https://example.com` loads and renders | §2 in full: `ResourceHandleCurl.mm`, the auth bridge, redirects, cookies | 8-12 d |
| **M8** | *(optional)* **Compositing through CARenderer** | a CSS-animated `transform: translate3d` runs on the GPU | §6 | 15-25 d |

**Total to M7: roughly 45-70 engineer-days.**

## Ordering notes

- **Do the SDK generics patch and the NSEvent renames before writing a single
  other line.** Together they are ~50 lines and remove ~450 of the ~900 gap
  sites. Everything else is easier once the error output is readable.
- **Turn `USE_CA` off before attempting to compile `platform/graphics/`**, not
  after. Otherwise you will spend days on `PlatformCALayerCocoa.mm`, which cannot
  be made to work.
- **M2 benefits from parallelism across the existing agents**, because the CG,
  CoreText, AppKit and networking edits touch disjoint directories. The shared
  files are `PlatformHave.h`, `PlatformUse.h`, `OptionsCocoa.cmake`,
  `SourcesCocoa.txt` and `PlatformCocoa.cmake` — coordinate on those five.
- **The riskiest item is not the largest.** `CTFontDrawGlyphs` (§3.1b) is about
  30 lines and will silently misplace every glyph on every page if the
  position-to-advance differencing is wrong. Write a standalone spike that draws
  a known string through both paths on a 10.5 box and diffs the pixels before
  trusting it.
- **The biggest unknown is `cocoa/CookieStorageSessionCocoa.mm`** (478 LOC, not
  line-audited for CFNetwork SPI). Audit it early in M4; if it is SPI-heavy the
  `CookieJarDB` decision in §2.6 flips and M7 grows by a week.
- **Keep `ENABLE_ATTACHMENT_ELEMENT=OFF` honest.** It is worth five CoreText gaps
  and a file, but confirm nothing in the editing code path hard-depends on
  `<attachment>` before committing to it.

## Commands to re-run before starting

```bash
cd /Users/shg/Developer/WebKitTiger

# Fix the empty ImageIO export list (§3.2) — this changes the CG plan materially.
toolchain/bin/tiger-nm -g -arch i386 \
  sysroot/System/Library/Frameworks/ApplicationServices.framework/Frameworks/ImageIO.framework/Versions/A/ImageIO \
  | grep -v ' U ' | awk '{print $NF}' | sed 's/^_//' | sort -u > logs/api/tiger-ImageIO.txt

# Dump ATS for the class-C CoreText replacements (§3.1d).
toolchain/bin/tiger-nm -g -arch i386 \
  sysroot/System/Library/Frameworks/ApplicationServices.framework/Frameworks/ATS.framework/Versions/A/ATS \
  | grep -v ' U ' | awk '{print $NF}' | sed 's/^_//' | sort -u > logs/api/tiger-ATS.txt

# Regenerate missing-CG against CG + ImageIO together.
# Note used-CG.txt is "<count> <symbol>", so strip the count first.
comm -23 <(awk '{print $2}' logs/api/used-CG.txt | sort -u) \
         <(cat logs/api/tiger-CG.txt logs/api/tiger-ImageIO.txt | sort -u) \
  > logs/api/missing-CG2.txt
```

`missing-CT.txt` does not need regenerating — the classification in §3.1 already
supersedes it.
