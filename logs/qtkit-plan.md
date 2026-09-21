# Reviving QTKit for `<video>` on Tiger — porting notes

Read-only analysis. No file under `WebKit/` was touched. Reference source:
`refs/webkit-history/qtkit-mediaplayer/` (the WebKit1 `MediaPlayerPrivateQTKit`
backend at commit `5dfc9ef9c532286804cd5af8eccf6b48c7b41776`, 2018-01-22, the
last commit before "Begin removing QTKit code" removed it). Compared against
the current tree's `Source/WebCore/platform/graphics/MediaPlayerPrivate.h`
(417 lines) and `MediaPlayer.h` (1002 lines), commit `d2f52605` per
`NOTES.md`. `ENABLE_VIDEO` is already forced off for `TIGER` in
`OptionsCocoa.cmake:171` (plan §1.2) — this document is for whenever that
changes, not phase 1.

**Update (same day): the Tiger box was upgraded from QuickTime/QTKit 7.2 to
7.6.4 (build 1327.73, Aug 2009), the last QuickTime ever shipped for Tiger.**
§§0-7 below were written against 7.2 and are kept as-is except where noted;
**§8 has the 7.6.4 diff and supersedes the two rendering-path conclusions in
§0 and §2** — `QTVideoRendererWebKitOnly` is no longer one of the two
unavailable paths, it's back. Read §8 first if short on time.

## 0. tl;dr

The 2018 backend is a reasonable skeleton but not a drop-in: the
`MediaPlayerPrivateInterface` vtable shape and the engine-registration
mechanism were both redesigned since 2018 (§1). Tiger's actual QuickTime is
**7.2**, not 7.0 or 7.6.4 as guessed — checked directly (§2) — which is good
news: QTKit 7.2's `frameImageAtTime:withAttributes:error:` and
`setVisualContext:`/`visualContext` are both present in the box's real QTKit
dylib, gated only by `QTKIT_VERSION_MAX_ALLOWED` (which defaults to 7.2
regardless of target OS version), not by `MAC_OS_X_VERSION_MAX_ALLOWED`. What
*isn't* present is `QTMovieLayer` (confirmed absent from the box's exported
ObjC classes — it needs both `QTKIT_VERSION_MAX_ALLOWED>=7_2` **and**
`MAC_OS_X_VERSION_MAX_ALLOWED>=10_5` in the header, and the real dylib doesn't
export the class regardless) and the private `QTVideoRendererWebKitOnly`
class the 2018 code uses for its non-layer paint path (also confirmed absent
by class-symbol listing). Both of the 2018 backend's two rendering paths are
therefore unavailable on Tiger; a third, `frameImageAtTime:`-based path has
to be written from scratch, and it turns out to be well supported by Tiger's
real QuickTime 7.2 (§4).

## 1. API delta: 2018 `MediaPlayerPrivateQTKit` vs. current `MediaPlayerPrivateInterface`

### 1a. Registration mechanism — the biggest structural change

**2018** (`MediaPlayerPrivateQTKit.mm:174-179`):

```cpp
void MediaPlayerPrivateQTKit::registerMediaEngine(MediaEngineRegistrar registrar)
{
    if (isAvailable())
        registrar([](MediaPlayer* player) { return std::make_unique<MediaPlayerPrivateQTKit>(player); },
            getSupportedTypes, supportsType, originsInMediaCache, clearMediaCache, clearMediaCacheForOrigins, 0);
}
```

`MediaEngineRegistrar` was a function pointer taking six raw function
pointers/lambdas directly: a factory, `getSupportedTypes`, `supportsType`,
`originsInMediaCache`, `clearMediaCache`, `clearMediaCacheForOrigins`, plus a
trailing `0` (an unused 7th argument in 2018's signature — probably a
capabilities bitmask predecessor).

**Current** (`MediaPlayer.h:930-955`): registration is now polymorphic. You
subclass `MediaPlayerFactory` and hand a `unique_ptr<MediaPlayerFactory>` to
`MediaEngineRegistrar` (now `void(std::unique_ptr<MediaPlayerFactory>&&)`):

```cpp
class MediaPlayerFactory : public CanMakeWeakPtr<MediaPlayerFactory>, public CanMakeCheckedPtr<MediaPlayerFactory> {
public:
    virtual MediaPlayerEnums::MediaEngineIdentifier identifier() const = 0;
    virtual Ref<MediaPlayerPrivateInterface> createMediaEnginePlayer(MediaPlayer&) const = 0;
    virtual void getSupportedTypes(HashSet<String>&) const = 0;
    virtual MediaPlayer::SupportsType supportsTypeAndCodecs(const MediaEngineSupportParameters&) const = 0;
    virtual HashSet<SecurityOriginData> originsInMediaCache(const String&) const { return { }; }
    virtual void clearMediaCache(const String&, WallTime) const { }
    virtual void clearMediaCacheForOrigins(const String&, const HashSet<SecurityOriginData>&) const { }
    virtual bool supportsKeySystem(const String&, const String&) const { return false; }
    virtual MediaPlayerScope supportedScope() const { return MediaPlayerScope::Playback; }
};
```

Concrete work: write `MediaPlayerFactoryQTKit final : public MediaPlayerFactory`
(pattern from `MediaPlayerFactoryMediaFoundation` in
`platform/graphics/win/MediaPlayerPrivateMediaFoundation.cpp:144`, or
`MediaPlayerFactoryMediaSourceAVFObjC` in the AVFObjC file — both are ~30-40
LOC shims). It needs a new `MediaEngineIdentifier` enumerator: the list at
`MediaPlayerEnums.h:86` (`AVFoundation`, `AVFoundationMSE`,
`AVFoundationMediaStream`, `AVFoundationCF`, `GStreamer`, `GStreamerMSE`,
`HolePunch`, `MediaFoundation`, `MockMSE`, `CocoaWebM`, `WirelessPlayback`)
has no QuickTime/QTKit entry — add one, `QTKit`.

Registration call site: `MediaPlayer.cpp:326-362` (`buildMediaEnginesVector`)
gates every engine behind a `USE(...)`/`ENABLE(...)` macro
(`#if USE(AVFOUNDATION)`, `#if USE(GSTREAMER)`, `#if USE(MEDIA_FOUNDATION)`,
...). Add a `#if USE(QTKIT)` block (new `USE_QTKIT` macro, Tiger-only,
alongside the `PLATFORM(TIGER)` blocks the rest of this project already adds
to `PlatformUse.h`/`PlatformEnableCocoa.h`) calling
`MediaPlayerPrivateQTKit::registerMediaEngine(addMediaEngine)`.

Signature deltas inside the shimmed methods:
- `getSupportedTypes`: `HashSet<String, ASCIICaseInsensitiveHash>&` (2018) →
  `HashSet<String>&` (current) — drop the second template argument.
- `supportsType` (2018, free function matching `MediaEngineSupportsType`) →
  `supportsTypeAndCodecs` (current, a `MediaPlayerFactory` virtual) — rename
  only, same `MediaEngineSupportParameters` argument shape (verify field names
  haven't drifted; not checked here).
- `originsInMediaCache`: `HashSet<RefPtr<SecurityOrigin>>` (2018) →
  `HashSet<SecurityOriginData>` (current) — `SecurityOriginData` is the
  value-type successor to holding `RefPtr<SecurityOrigin>` directly; trivial
  conversion (`securityOrigin->data()` at each call site).
- The factory's `createMediaEnginePlayer(MediaPlayer&)` returns
  `Ref<MediaPlayerPrivateInterface>` (current) vs. the 2018 lambda returning
  `std::unique_ptr<MediaPlayerPrivateQTKit>` implicitly upcast — switch to
  `adoptRef(*new MediaPlayerPrivateQTKit(player))` or equivalent factory
  helper other backends use.

**~60-90 LOC for the factory + registration wiring.**

### 1b. Renamed pure/override virtuals

| 2018 override | Current equivalent | Change |
|---|---|---|
| `durationMediaTime() const` | `duration() const` | renamed |
| `currentMediaTime() const` | `currentTime() const` | renamed |
| `maxMediaTimeSeekable() const` | `maxTimeSeekable() const` | renamed |
| `setSize(const IntSize&)` | `setPresentationSize(const IntSize&)` | renamed |
| `setVisible(bool)` | `setPageIsVisible(bool)` (+ `setVisibleForCanvas(bool)` defaulting to it) | renamed, and split into two hooks |
| `seek(const MediaTime&)` | `seekToTarget(const SeekTarget&)` → `Ref<MediaTimePromise>` | **renamed and made async** — see §1c |
| `buffered() const` → `std::unique_ptr<PlatformTimeRanges>` | `buffered() const` → `const PlatformTimeRanges&` | return-by-reference now; the 2018 pattern of allocating a fresh `PlatformTimeRanges` per call and returning ownership no longer compiles — the private class must own a member and return a reference to it |

### 1c. Newly required pure virtuals (`= 0`) the 2018 class never implemented

These must get real or no-op bodies for the class to instantiate at all:

| Virtual | Notes |
|---|---|
| `load(const URL&, const LoadOptions&, MediaSourcePrivateClient&) = 0` | Media Source Extensions hook. 2018 had this `#if ENABLE(MEDIA_SOURCE)`-gated and optional; now unconditionally pure. QTKit has no MSE story — give it an empty body (`{ }`), same as the 2018 code already did when the ifdef was active |
| `load(MediaStreamPrivate&) = 0` | WebRTC hook, same story — empty body. 2018 had this optional too |
| `cancelLoad() = 0` | 2018 already implemented this (unaffected, just confirming it's still required) |
| `naturalSize() const = 0`, `hasVideo() const = 0`, `hasAudio() const = 0` | 2018 already implemented, unaffected |
| `setPageIsVisible(bool) = 0` | see rename above, was `setVisible` |
| `seekToTarget(const SeekTarget&) = 0` returning `Ref<MediaTimePromise>` | see below |
| `paused() const = 0`, `play() = 0`, `pause() = 0` | 2018 already implemented, unaffected |
| `didLoadingProgress() const = 0` | 2018 already implemented |
| `buffered() const = 0` returning `const PlatformTimeRanges&` | signature change above |
| `colorSpace() = 0` | **new**, no 2018 equivalent — return e.g. `ColorSpace::SRGB` unconditionally, QTKit has no wide-gamut story worth modeling |
| `load(const URL&)` overload dispatch — `load(const String&)` still has a default body in the current header (`{ }`, non-pure) so the 2018 `load(const String&)` override still slots in as-is, just note `load(const URL&, const LoadOptions&)` now also exists as a non-pure convenience overload that forwards to the string version by default — no action needed unless the caller passes `LoadOptions` QTKit should react to |

`seekToTarget` deserves its own note: this is a structural shift from
synchronous seeking (2018: `seek()` sets `m_seekTo` and fires a `Timer`,
`seekTimerFired()` calls `[m_qtMovie setCurrentTime:]` and reports done via a
callback) to promise-based async seeking. `MediaTimePromise` is WebKit's
`NativePromise`-based promise type (grep
`Source/WebCore/platform/MediaTimePromise.h` — not read in this pass). The
QTKit backend's actual seek operation is itself synchronous
(`-[QTMovie setCurrentTime:]` blocks), so the adaptation is mechanical: keep
the existing seek machinery internally, and have `seekToTarget` return an
already-resolved (or resolved-on-timer-fire) `MediaTimePromise` instead of
using the old `MediaPlayerClient` "seek completed" callback pattern the 2018
code used. Needs a read of one other backend's `seekToTarget` (e.g. the
AVFoundation one) to copy the idiom exactly — not done in this pass, flagged
as a concrete next step.

### 1d. New virtuals worth implementing, not required to compile

| Virtual | Default if unimplemented | Why QTKit could do better |
|---|---|---|
| `nativeImageForCurrentTime()` | `nullptr` | Backs `<canvas>.drawImage(videoElement, ...)`. Trivial to implement once §4's `frameImageAtTime:withAttributes:error:` + `QTMovieFrameImageTypeCGImageRef` path exists (§2) — it already returns roughly the right currency (`CGImageRef`), just wrap it as a `NativeImage` |
| `videoFrameForCurrentTime()` | empty/null `RefPtr<VideoFrame>` | Same underlying data, different wrapper type (`VideoFrame` vs `NativeImage`) — used by newer canvas/WebGL/WebCodecs paths. Tiger has no WebGL and `ENABLE_WEB_CODECS` (default ON for Cocoa per `OptionsCocoa.cmake:171`) should probably join the Tiger off-list regardless of QTKit — skip |
| `bitmapImageForCurrentTimeSync()` / `bitmapImageForCurrentTime()` | default no-op/empty promise | Same frame data again, `ShareableBitmap`-wrapped for cross-process use — **WebKit1/WebKitLegacy has no process split, so this whole family is moot on Tiger**. Leave unimplemented |
| `platformLayer() const` | `nullptr` | Only meaningful if/when the CARenderer host (plan §6) exists; until then, always `nullptr`, matching software-paint-only |
| `supportsAcceleratedRendering() const` | `false` | Same — `false` until CARenderer phase |

### 1e. Removed since 2018 — code to simply drop

`platformMedia() const override` (2018, returns a `PlatformMedia` tagged
union exposing the raw `QTMovie*`) — **`PlatformMedia`/`platformMedia()` no
longer exist anywhere in `MediaPlayerPrivate.h` or `MediaPlayer.h`** (grepped,
zero hits). This was WebKit1's way of handing the embedding app the live
`QTMovie*`/`QTMovieView*` for e.g. `-[WebView _mediaPlayerProxy]`-style APIs.
**Confirmed in §6: zero real callers anywhere in `WebCore` or
`WebKitLegacy`.** Delete the override, saving ~15 LOC.

## 2. What Tiger's QTKit actually exports

Ran `toolchain/bin/tiger-nm -g -arch i386
sysroot/System/Library/Frameworks/QTKit.framework/QTKit` (the box's real
QTKit, mirrored into `sysroot/`, not the SDK stub). Full symbol dump saved
during this session at `/tmp/qtkit-syms.txt` (not committed — regenerate with
the command above if needed).

**Version**: `sysroot/.../QTKit.framework/Resources/Info.plist` says
`CFBundleShortVersionString` **7.2**, and
`sysroot/.../QuickTime.framework/Resources/Info.plist` says **7.2.0**. This
corrects the original guess of "QuickTime 7.6.4" — the box shipped an update
to QuickTime 7.2, not the final 7.6.4 (which is PowerPC/Intel Leopard-era and
was never released for Tiger; 7.2 was Tiger's last major QuickTime rev before
Apple stopped shipping i386 Tiger updates — 7.6.4 requires 10.4.11 *or later*
in Apple's own compatibility notes but was in practice bundled mostly with
Leopard/SL installers; the box's actual installed version is what matters and
it's 7.2).

**Defined ObjC classes** (`.objc_class_name_*` symbols not marked `U`):
`QTMovie`, `QTMovieView`, `QTMovieContentView`, `QTMovieControllerView`,
`QTTrack`, `QTMedia`, `QTFormatDescription`, `QTSampleBuffer`,
`QTVisualContextView`, `QTDataReference`, `QTHotspot`, `QTNode`, `QTStream`,
the `*Enumerator` helper classes, the `QTCapture*` device-capture family
(irrelevant, no `<video>` capture on this port), `QTCompressionOptions`,
`QTAudioCompressionOptions`, `QTVideoCompressionOptions`. **Confirmed
absent**: `QTMovieLayer`, `QTVideoRendererWebKitOnly` — neither name appears
anywhere in the defined-symbol list, confirming both of the 2018 backend's
rendering paths (§0) are unavailable.

**Header-vs-runtime gating, worked out precisely**:
`QTKitDefines.h` in the 10.4u SDK defines `QTKIT_VERSION_MAX_ALLOWED` to
`QTKIT_VERSION_7_2` **unconditionally** (not derived from
`MAC_OS_X_VERSION_MAX_ALLOWED` at all — see the `#ifndef
QTKIT_VERSION_MAX_ALLOWED / #define QTKIT_VERSION_MAX_ALLOWED
QTKIT_VERSION_7_2` block, no `#if` on the OS version). So every `#if
QTKIT_VERSION_MAX_ALLOWED >= QTKIT_VERSION_7_2` gate in `QTMovie.h` is **on
by default** on this SDK, regardless of the `MAC_OS_X_VERSION_MIN_REQUIRED`
pin to 1040 this whole project relies on elsewhere. Concretely, all of these
compile and link today, without any macro surgery:
- `- (void)setVisualContext:(QTVisualContextRef)visualContext;` /
  `- (QTVisualContextRef)visualContext;` (`QTMovie.h:422-423`) — confirmed by
  nm that the underlying methods exist in the box's QTKit 7.2 (ObjC1 fragile
  runtime doesn't export per-selector symbols, but the enclosing category and
  class are present and the header's gate is satisfied; the risk is purely
  whether the *runtime* honors the selector, which the method being declared
  unconditionally in the exact SDK matching this QuickTime version indicates
  it does).
- `- (void *)frameImageAtTime:(QTTime)time withAttributes:(NSDictionary
  *)attributes error:(NSError **)errorPtr;` (`QTMovie.h:318`) — the
  attribute-dictionary keys it takes
  (`QTMovieFrameImageType`,`QTMovieFrameImageTypeCGImageRef`,
  `QTMovieFrameImageTypeNSImage`, `QTMovieFrameImageTypeCIImage`,
  `QTMovieFrameImageTypeCVPixelBufferRef`,
  `QTMovieFrameImageTypeCVOpenGLTextureRef`,
  `QTMovieFrameImageOpenGLContext`, `QTMovieFrameImagePixelFormat`) are
  header-annotated `AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER` (a *separate*,
  OS-version-keyed gate from the QTKit-version gate above — but this
  annotation is only a deployment-target warning, not a compile gate; there's
  no `#if` around these `QTKIT_EXTERN NSString * const ...` declarations).
  **Confirmed present as real, defined (non-`U`) data symbols in the box's
  dylib** by nm: `_QTMovieFrameImageTypeCGImageRef`,
  `_QTMovieFrameImageTypeNSImage`, `_QTMovieFrameImageOpenGLContext`, etc. all
  showed up as `S` (small-data section) symbols, not `U` (undefined). **This
  is Tiger's real, later-updated QuickTime 7.2 exceeding the stock-Tiger
  assumption baked into the SDK's availability annotations** — the annotation
  describes when Apple *introduced* the constant into the public SDK
  (Leopard), not whether this particular QuickTime 7.2 update backports it,
  and empirically it does.
- Chapters API (`hasChapters`, `chapters`, etc., `QTMovie.h:444-453`),
  threading API (`enterQTKitOnThread`, `attachToCurrentThread`, etc.,
  `QTMovie.h:429-440`) — all QTKit-version-gated only, all present.

**Genuinely missing on Tiger 7.2** (present in later QTKit, absent from this
dylib's export list): `QTMovieLayer` (needs `MAC_OS_X_VERSION_MAX_ALLOWED >=
10_5` in addition to the QTKit version gate — Leopard AppKit/CoreAnimation
dependency, not just a QuickTime version bump) and `QTVideoRendererWebKitOnly`
(an Apple-internal class shipped specifically as WebKit's private rendering
helper starting sometime after Tiger — not documented, not in any public SDK
header at all, found only via `NSClassFromString` in WebKit's own source).

**One real gap**: `QTVisualContextRef` **the typedef itself is not declared
anywhere in either the 10.4u or 10.5 SDK snapshots in this checkout**
(`grep -rln QTVisualContextRef sdk/` finds only the two QTKit `.h` files that
*use* the type, never one that *defines* it — it lives in QuickTime's C API,
`QTVisualContext.h`/`ImageCompression.h`, which isn't in either partial SDK
here). Since it's just an opaque pointer (`typedef struct
OpaqueQTVisualContextRef* QTVisualContextRef;` in every real QuickTime SDK),
this is a 1-line local forward-declaration, not a blocker — but note it,
since compiling `MediaPlayerPrivateQTKit.mm` unmodified will fail on this
missing typedef the moment it touches `setVisualContext:`/`visualContext`
(which the 2018 code doesn't actually call — it's declared but unused in that
version; whether Tiger's `frameImageAtTime:`-based rewrite in §4 needs it
depends on which output path is chosen).

## 3. Codec/container reality on Tiger's QuickTime 7.2

QuickTime 7 (all sub-versions from 7.0 on) added first-class H.264 and AAC
support via the Component Manager (`QuickTimeH264.component`,
`AACAudio.component`, historically shipped under
`/System/Library/QuickTime/`) — this predates the 7.2 vs 7.6.4 question
entirely, so the correction in §2 doesn't change this. What Tiger's
QuickTime 7 covers, container/codec-wise:

| Covered | Not covered |
|---|---|
| MOV/MP4 container | WebM/MKV container |
| H.264 (Baseline and Main profile; **not** High profile — QuickTime 7's H.264 decoder predates High-profile hardware acceleration support that arrived with later QuickTime X) | VP8/VP9 |
| MP3, AAC-LC audio | Opus, Vorbis |
| Motion JPEG, Sorenson (legacy codecs, still exported per `QTKitDefines.h`'s codec-type enum, §2) | AV1, HEVC/H.265 (H.265 didn't exist yet in 2005-2009) |

This roughly matches H.264 Baseline/Main + AAC MP4, which is a meaningfully
large slice of *archival* web video (most pre-2015 `<video>` content, and
any modern site that still ships an H.264 fallback source) but **zero** of
WebM/VP9 (YouTube's and most modern streaming's primary format for years) and
zero of AV1 (increasingly the default for new encodes). Practically: a
period-correct H.264/MP4 test video plays; a random 2020s YouTube embed does
not, with or without a working media engine — that's a source-format problem
no amount of WebKit porting fixes.

`atv/extracted/3.0.2` (Apple TV Software's own QuickTime/QTKit, checked per
`atv/REPORT.md`) is confirmed **identical** to Tiger's — "QTKit | 1.0.0 | 1.0.0
| identical | no" in the ATV report's comparison table, and "Video decode is
plain QuickTime. H.264 comes from
`/System/Library/QuickTime/QuickTimeH264.component` driven through QuickTime
and QTKit, both at the same version Tiger has. There is no modern media stack
to harvest." So the ATV firmware extraction — useful for CoreAnimation (plan
§6) — adds nothing here; don't bother diffing its QuickTime components
against Tiger's.

## 4. Getting a frame on screen

### 4a. Compositing off (phase 1 target)

Neither of the 2018 backend's two paths (`QTMovieLayer`,
`QTVideoRendererWebKitOnly`) exists on Tiger (§2), so `paint()` needs a third
path built from what *is* confirmed present: `-[QTMovie
frameImageAtTime:withAttributes:error:]` with
`QTMovieFrameImageType: QTMovieFrameImageTypeCGImageRef` returning a
`CGImageRef` directly (no `NSImage` round-trip, no `-lockFocus`/AppKit
drawing needed — this lines up with the rest of this project's 10.4-safe
`NSImage` avoidance elsewhere, e.g. plan §5.8's drag-image note). Sketch:

```objc
NSDictionary *attrs = @{ QTMovieFrameImageType: (id)QTMovieFrameImageTypeCGImageRef };
NSError *error = nil;
CGImageRef frame = (CGImageRef)[m_qtMovie.get() frameImageAtTime:[m_qtMovie currentTime]
                                                    withAttributes:attrs
                                                             error:&error];
if (frame)
    CGContextDrawImage(context.platformContext(), CGRectMake(...), frame);
```

(`(void *)` return type in the header is QTKit's ObjC1-era way of returning a
toll-free-bridged `CFTypeRef`/`CGImageRef` without importing `<ApplicationServices>`
into the QTKit header — cast on use, as the header comment area around
`QTMovie.h:317-319` implies but doesn't spell out; verify the exact runtime
type with a spike before committing to this, it could also come back as
`NSImage*`/`CIImage*`/`CVPixelBufferRef` depending on how
`QTMovieFrameImageType` is actually honored — the header only documents the
*input* keys, not a compile-time-checked return type, since the method
signature is untyped `void *`).

`repaint()` needs a trigger: `QTMovieTimeDidChangeNotification` fires
sparingly (only on discontinuous jumps, not every frame), so continuous
playback needs either polling on a `Timer` (simplest, matches the 2018 code's
existing `m_seekTimer` idiom) at the target frame rate, or
`QTMovieRateDidChangeNotification`/a `CVDisplayLink`-driven repaint loop
(more correct, more work). **Start with a `Timer` at ~15-24Hz** — Tiger's
Core 2 Duo painting through the software path (plan §5.2) is not going to
sustain 60fps video decode either way, and a modest fixed-rate poll is much
less code than wiring a `CVDisplayLink` for a phase-1 cut.

### 4b. With the CARenderer host (plan §6, later)

Once plan §6.3's `WebCARendererHostView`/`CARenderer` host exists, the
natural upgrade is a plain `CALayer` (not `QTMovieLayer`, which doesn't
exist) whose `contents` property is set to the same `CGImageRef` pulled in
§4a, refreshed on the same timer/notification. This is strictly less work
than building a `QTVisualContextRef`-based OpenGL texture pipeline (the
other theoretically-available path per §2's finding that
`setVisualContext:`/`visualContext` do compile) — the `CGImageRef`-into-
`CALayer.contents` approach reuses all of §4a's code unchanged and just
swaps the destination from `CGContextDrawImage` into a layer property
assignment, avoiding a second, OpenGL-textured, CVPixelBuffer-based render
path that would only make sense if per-frame `CGImageRef` extraction proved
too slow (untested; flag as the thing to benchmark before investing in the
OpenGL path).

## 5. Phased plan and LOC estimates

| Phase | What | LOC | Depends on |
|---|---|---|---|
| P0 | Confirm `frameImageAtTime:withAttributes:error:` actually returns a `CGImageRef` when asked, on real Tiger hardware — a `spike/` program, not WebKit code | ~40 (new spike) | none, can run today |
| P1 | `MediaPlayerFactoryQTKit` + `MediaEngineIdentifier::QTKit` + `MediaPlayer.cpp` registration wiring (§1a) | 60-90 | P0 |
| P2 | Port `MediaPlayerPrivateQTKit.{h,mm}` body: apply the renames (§1b), stub/implement the newly-required pure virtuals (§1c), drop `platformMedia()` (§1e, pending the WebKitLegacy grep) | 150-250 (mostly mechanical edits to the existing 1727+225 LOC, not new code) | P1 |
| P3 | Delete `createQTMovieLayer`/`destroyQTMovieLayer`/`QTMovieLayer` member (~230 LOC gross deletion including the `SOFT_LINK_CLASS(QTKit, QTMovieLayer)` machinery) and `createQTVideoRenderer`/`destroyQTVideoRenderer`/`QTVideoRendererWebKitOnly` member (~150 LOC gross deletion) | −380 net | P2 |
| P4 | New `paint()`/`paintCurrentFrameInContext()` body using `frameImageAtTime:withAttributes:error:` (§4a) + repaint timer | 80-120 | P3, P0 |
| P5 | `seekToTarget` → `MediaTimePromise` adaptation (§1c, worked out concretely in §7) | 40-60 | P2 |
| P6 | `QTVisualContextRef` local typedef (§2, if the CARenderer-era path in §4b ever needs it — not needed for P0-P5) | 1 | only if §4b is pursued |
| P7 | `getSupportedTypes`/`supportsTypeAndCodecs` — reuse the 2018 code's UTI-based type list logic essentially unchanged, just the `HashSet<>` template-argument fix (§1a) | 10-20 | P1 |
| P8 | `ENABLE_VIDEO` flag flip: remove `ENABLE_VIDEO` from the `OptionsCocoa.cmake:171` TIGER off-list; audit what that pulls back in (`ENABLE_MEDIA_SOURCE`/`ENABLE_MEDIA_STREAM`/`ENABLE_ENCRYPTED_MEDIA`/etc. all `WEBKIT_OPTION_DEPEND` on it per `WebKitFeatures.cmake:341-355` — **keep those individually off**, only `ENABLE_VIDEO` itself should flip) | ~5 (a one-line removal plus explicit off-overrides for the dependents) | P1-P7 landed |
| P9 | `WebVideoFullscreenController`/`WebCoreSupport` glue if full-screen `<video>` chrome is wanted — not costed here, see the "not fetched" note in `refs/webkit-history/qtkit-mediaplayer/README.md` | uncosted | after P0-P8 |

**Total for a working inline (non-fullscreen), non-accelerated `<video>` with
audio+seek+poster: roughly 350-560 LOC of edits to the ported 1952-line 2018
file plus ~150 LOC of new registration glue, minus ~380 LOC deleted** — a
smaller net change than it sounds, since most of the 2018 file (load state
machine, notification observers, track enumeration, error/network-state
mapping) needs no changes at all and carries over directly.

### `ENABLE_VIDEO` flag changes, concretely

- `Source/cmake/OptionsCocoa.cmake:171` — currently lists `ENABLE_VIDEO` among
  the TIGER-forced-off options (plan §1.2's "already off" set, confirmed by
  reading that line: `ENABLE_VIDEO ENABLE_WEB_AUDIO ENABLE_WEB_CODECS` all in
  the same forced-off group). Remove `ENABLE_VIDEO` from that list once P1-P7
  land; leave `ENABLE_WEB_AUDIO` and `ENABLE_WEB_CODECS` off (no engine for
  either planned).
- New `USE_QTKIT` (or reuse `PLATFORM(TIGER)` directly at the `#if` in
  `MediaPlayer.cpp`'s `buildMediaEnginesVector`, matching how the rest of
  this project prefers `PLATFORM(TIGER)` gates over inventing new `USE_*`
  macros per plan §1.1's table — recommend following that precedent instead
  of a bespoke `USE_QTKIT`).
- Everything `WEBKIT_OPTION_DEPEND`-ing on `ENABLE_VIDEO`
  (`WebKitFeatures.cmake:341,342,348,350,351,352,354,355`:
  `ENABLE_ENCRYPTED_MEDIA`, `ENABLE_LEGACY_ENCRYPTED_MEDIA`,
  `ENABLE_MEDIA_CONTROLS_CONTEXT_MENUS`, `ENABLE_MEDIA_SESSION`,
  `ENABLE_MEDIA_SOURCE`, `ENABLE_MEDIA_STREAM`,
  `ENABLE_VIDEO_PRESENTATION_MODE`, `ENABLE_VIDEO_USES_ELEMENT_FULLSCREEN`)
  will auto-enable once their dependency (`ENABLE_VIDEO`) is on, unless
  explicitly forced off — add all eight to the TIGER off-list explicitly
  rather than relying on them silently defaulting off, since
  `WEBKIT_OPTION_DEPEND` only means "requires the depended-on option to be
  on to be *available*," not "defaults off when the parent is on."
  `ENABLE_MEDIA_CONTROLS_CONTEXT_MENUS` is probably fine to leave enabled
  (cheap, no new platform surface); the rest should stay off.

## 6. `platformMedia()` — confirmed dead, safe to delete

Grepped for real callers, not just the substring:

```
grep -rn "platformMedia\|PlatformMedia\b" Source/WebKitLegacy/  → zero hits
grep -rln "platformMedia\|PlatformMedia\b" Source/WebCore/      → three files
```

The three `Source/WebCore` hits are all false positives, an unrelated
`platformMedia*` naming coincidence, not the removed `PlatformMedia` tagged
union:

- `platform/playstation/MIMETypeRegistryPlayStation.cpp` — a local function
  named `platformMediaTypes()` (MIME-type-to-extension table for PlayStation's
  media files, nothing to do with `MediaPlayerPrivateInterface`).
- `platform/graphics/avfoundation/objc/WebCoreAVFResourceLoader.{h,mm}` — a
  member `m_platformMediaLoader` of type `Ref<PlatformMediaResourceLoader>`,
  which is a *resource-loading* class (`PlatformMediaResourceLoader`, the
  thing that fetches bytes for an in-progress AVAsset load), unrelated to the
  removed `PlatformMedia` union that used to expose a raw `QTMovie*`/
  `AVPlayer*` handle to the embedder.

Also checked `Source/WebKitLegacy/mac/WebView/WebVideoFullscreenController.{h,mm}`
and `WebView.mm`/`WebViewData.h` specifically, since those are WebKitLegacy's
video-adjacent files (grepped for `QTMovie`, `QTKit`,
`MediaPlayerPrivateQTKit`, `WebVideoFullscreen`, `platformMedia`,
`PlatformMedia`): `WebVideoFullscreenController.{h,mm}` today is purely
`CALayer`/AppKit-window based (a plain `NSWindowController` hosting a
`WebVideoFullscreenOverlayLayer : CALayer`) — **zero** QTKit dependency of
any kind, confirming this class was already migrated off the old
`platformMedia()`-based handle-passing pattern before it was ever removed.
`WebView.mm` has exactly one QuickTime-adjacent hit, a stale comment at
`:3180` about "the QuickTime Cocoa Plug-in" (an NPAPI plugin — NPAPI no
longer exists in this tree at all, plan §5.4), not live code.

**Conclusion: `platformMedia() const override` can simply be deleted** from
the ported `MediaPlayerPrivateQTKit.h`/`.mm`, with no compensating API needed
anywhere in `WebKitLegacy`. Saves the ~15 LOC estimated in §1e; that estimate
stands, just now confirmed rather than flagged.

## 7. `seekToTarget`/`MediaTimePromise` — the concrete adaptation

`MediaTimePromise` (`platform/MediaPromiseTypes.h:35`) is
`NativePromise<MediaTime, PlatformMediaError>` (`platform/PlatformMediaError.h:33`
lists the error enum: `AppendError`, `ClientDisconnected`, `BufferRemoved`,
`SourceRemoved`, `IPCError`, `ParsingError`, `MemoryError`, **`Cancelled`**,
`LogicError`, `DecoderCreationError`, `NotSupportedError`, `NetworkError`,
`NotReady`, `AudioDecodingError`, `VideoDecodingError`, `InvalidState`,
`CDMInstanceKeyNeeded`, ...). `MediaPlayer::seekToTarget` (`MediaPlayer.h:494`)
just forwards to the private interface's override.

Read two existing implementations, `MediaPlayerPrivateMediaFoundation::seekToTarget`
(`platform/graphics/win/MediaPlayerPrivateMediaFoundation.cpp:284-297`) and
`MediaPlayerPrivateGStreamer::seekToTarget`
(`platform/graphics/gstreamer/MediaPlayerPrivateGStreamer.cpp:688-`). Both
follow the same shape, which `WTF/wtf/NativePromise.h` supports directly:

- A member `std::optional<MediaTimePromise::AutoRejectProducer> m_seekPromise;`
  — `AutoRejectProducer` auto-rejects with a default reason if the producer is
  destroyed (object torn down, or superseded by a new seek) without an
  explicit `resolve()`/`reject()` call first.
- `Producer`/`AutoRejectProducer` has `resolve(value)`, `reject(value)`, a
  `.promise()` accessor, and an implicit `operator Ref<PromiseType>()`
  (`NativePromise.h:894,903`) — so `return *m_seekPromise;` or
  `return m_seekPromise->promise();` both work as the function's return
  expression once the producer is `.emplace()`d.
- `MediaTimePromise::createAndResolve(value)` /
  `MediaTimePromise::createAndReject(error)` static helpers exist for the
  already-settled cases (GStreamer uses these for "already at the requested
  position" and "live stream, report current position" — both skip the
  producer machinery entirely).
- If a previous seek is still pending when a new one starts, both backends
  reject the old producer with `PlatformMediaError::Cancelled` before starting
  the new one (`std::exchange(m_seekPromise, std::nullopt)` then
  `->reject(...)`), rather than leaving two seeks in flight.
- Where the two backends differ is only in *when* they resolve: MediaFoundation
  starts an async session transition and resolves later from a COM callback
  (`onSessionStarted()`, `MediaPlayerPrivateMediaFoundation.cpp:889-899`,
  `std::exchange(m_seekPromise, std::nullopt)->resolve(currentTime())`);
  GStreamer resolves from its own bus-message/pad-probe seek-completion
  handler (not fully read in this pass, same idiom).

**QTKit's actual seek operation, per the 2018 code
(`MediaPlayerPrivateQTKit.mm:632-701`, `seek()`/`doSeek()`/`cancelSeek()`/
`seekTimerFired()`), is synchronous in the common case and polls-on-a-timer
in the uncommon case:**

- `doSeek()` (`:655-670`) calls `[m_qtMovie setCurrentTime:qttime]`
  directly — this **blocks until the seek completes** (QTKit's whole API is
  synchronous; there is no async seek primitive to bridge to). It's callable
  immediately whenever `maxMediaTimeSeekable() >= targetTime`, i.e. the movie
  has already buffered/loaded past the target point.
- When the target is beyond what's currently loaded
  (`maxMediaTimeSeekable() < targetTime`), the 2018 code can't call
  `setCurrentTime:` yet — QTKit doesn't have a promise/callback for "notify me
  once buffered this far," so it polls: `m_seekTimer.start(0_s, 500_ms)`
  (`seek():651`) re-checks every 500ms in `seekTimerFired()` (`:682-699`)
  until either the range extends far enough (calls `doSeek()`) or the network
  state gives up (`Empty`/`Loaded` with no further loading expected).

**Concrete adaptation** — keep the existing synchronous `doSeek()` body and
the existing timer-retry machinery for the not-yet-buffered case verbatim
(they need no QTKit-API changes, only the wrapper around them changes), and
replace `seek(const MediaTime&)`'s void return with a promise:

```cpp
// MediaPlayerPrivateQTKit.h
Ref<MediaTimePromise> seekToTarget(const SeekTarget&) override;
// ... keep the private doSeek()/cancelSeek()/seekTimerFired() members as-is,
// they become implementation details seekToTarget drives.
std::optional<MediaTimePromise::AutoRejectProducer> m_seekPromise;

// MediaPlayerPrivateQTKit.mm
Ref<MediaTimePromise> MediaPlayerPrivateQTKit::seekToTarget(const SeekTarget& inTarget)
{
    MediaTime time = std::min(inTarget.time, durationMediaTime());

    // Reject (not silently drop) a seek still in flight when a new one supersedes it,
    // matching the MediaFoundation/GStreamer precedent.
    if (auto previous = std::exchange(m_seekPromise, std::nullopt))
        previous->reject(PlatformMediaError::Cancelled);

    if (!metaDataAvailable())
        return MediaTimePromise::createAndReject(PlatformMediaError::NotReady);

    if (time == currentMediaTime())
        return MediaTimePromise::createAndResolve(time);

    m_seekTo = time;   // existing 2018 member, unchanged meaning
    m_seekPromise.emplace(PlatformMediaError::Cancelled);
    Ref promise = m_seekPromise->promise();

    if (maxMediaTimeSeekable() >= m_seekTo) {
        // Common case: QTKit's setCurrentTime: is synchronous, so doSeek()
        // has already completed the seek by the time it returns below.
        doSeek();
        std::exchange(m_seekPromise, std::nullopt)->resolve(currentMediaTime());
    } else {
        // Rare case: target isn't buffered yet. Reuse the existing 500ms
        // poll loop unchanged; it calls doSeek() once the range catches up.
        // seekTimerFired() (unchanged from 2018) needs one new line at its
        // "seek completed" exit (the `cancelSeek(); updateStates();
        // m_player->timeChanged();` branch) to also resolve m_seekPromise,
        // and its give-up branch (Empty/Loaded network state) to reject it
        // with PlatformMediaError::NetworkError instead of silently
        // abandoning the seek the way the 2018 void-returning version did.
        m_seekTimer.start(0_s, 500_ms);
    }

    return promise;
}
```

This keeps essentially all of the 2018 seek logic (`doSeek()`, the
buffered-range check, the 500ms retry timer) unchanged, and only wraps it:
the two exit points of `seekTimerFired()` that used to just call
`m_player->timeChanged()` need one added line each
(`std::exchange(m_seekPromise, std::nullopt)->resolve(...)` on success,
`->reject(PlatformMediaError::NetworkError)` on give-up) so the promise
representing whichever `seekToTarget()` call is still pending gets settled
from the timer callback instead of being silently forgotten. This runs on
the main thread throughout — QTKit's `NSNotificationCenter`-based callbacks
and `Timer` both dispatch there already, so no thread-hop is needed (unlike
MediaFoundation's COM callback or GStreamer's bus-message thread, both of
which have to bounce back to the main thread before touching `m_seekPromise`;
QTKit's `doSeek()` is already called from the main thread in every case).

Revises the P5 estimate in §5: this is closer to **40-60 LOC** (a rewritten
`seekToTarget()` wrapper plus two one-line additions inside the unchanged
`seekTimerFired()`), not an open-ended unknown — the original "40-80,
read one other backend first" placeholder in §1c/P5 is now resolved.

## 8. QuickTime 7.6.4 update — the box was upgraded, and it changes the rendering-path story

The Tiger box was updated to QuickTime/QTKit **7.6.4** (`CFBundleVersion`
`1327.73`, August 2009 — the last QuickTime release Apple ever shipped for
Tiger). `sysroot/` was re-mirrored
(`ssh tiger 'cd / && tar cf - System/Library/Frameworks/QTKit.framework
System/Library/Frameworks/QuickTime.framework' | tar xf - -C sysroot`, plus
`/System/Library/QuickTime`), the old 7.2 copy kept at `sysroot-old/` for
diffing, and export lists regenerated to
`logs/api/tiger-QTKit-7.2.txt` (454 symbols) and
`logs/api/tiger-QTKit-7.6.4.txt` (453 symbols). Verified directly:
`sysroot/System/Library/Frameworks/QTKit.framework/Resources/Info.plist` and
the `QuickTime.framework` copy both say `CFBundleShortVersionString` 7.6.4,
`CFBundleVersion` 1327.73.

### 8a. The headline change: `QTVideoRendererWebKitOnly` is back

Re-ran `tiger-nm -g -arch i386` against both the old (`sysroot-old/`) and new
(`sysroot/`) `QTKit` binaries and diffed the defined-ObjC-class lists (not
just the C-symbol export lists in `logs/api/`, which don't carry class
names):

```
$ diff <(defined classes, 7.2) <(defined classes, 7.6.4)
...
> QTImageBufferConformer
> QTMovieViewControllerViewTranslationHandler
> QTPixelBufferConverter
> QTVideoRendererWebKitOnly
```

**`QTVideoRendererWebKitOnly` — the private class the 2018 backend's
non-accelerated `paint()` path drives via `-[QTVideoRendererWebKitOnly
drawInRect:]` (§0, §4) — is now a real, defined ObjC class in Tiger's QTKit,
where it was absent in 7.2.** Confirmed the matching notification constant
too: `_QTVideoRendererWebKitOnlyNewImageAvailableNotification` is a new
symbol in `logs/api/tiger-QTKit-7.6.4.txt`, absent from the 7.2 list
(`comm -13` on the sorted export lists, filtered to non-capture symbols).

**`QTMovieLayer` is still absent from both** — confirmed by the same
class-list diff, it appears in neither 7.2 nor 7.6.4's defined classes. This
matches the earlier analysis in §2/§0: `QTMovieLayer` needs
`MAC_OS_X_VERSION_MAX_ALLOWED >= 10_5` in its own header gate on top of the
QTKit-version gate, because it's a Leopard-AppKit-layer-backing feature, not
a QuickTime-version feature — bumping QuickTime alone was never going to
bring it back, and it hasn't.

**This flips the rendering-path recommendation from §0/§4.** With 7.6.4:

- The 2018 backend's **software-renderer path (`MediaRenderingSoftwareRenderer`,
  `createQTVideoRenderer`/`destroyQTVideoRenderer`/`m_qtVideoRenderer`,
  `MediaPlayerPrivateQTKit.mm:376-417`) now has everything it needs and can be
  ported essentially unchanged** — no need for the from-scratch
  `frameImageAtTime:withAttributes:error:` paint path §4a proposed as a
  replacement. That path remains useful as a *fallback* (e.g. for
  `nativeImageForCurrentTime()`, §1d, which wants a single still frame rather
  than a live-updating renderer view) but is no longer required to get
  `paint()` working at all.
- The **movie-layer path (`MediaRenderingMovieLayer`,
  `createQTMovieLayer`/`destroyQTMovieLayer`/`m_qtVideoLayer`) stays dead** —
  `QTMovieLayer` is still unavailable, for the reason above, independent of
  QuickTime version. This path (and its `CALayer`-hosting story) remains
  gated on the plan §6 CARenderer work, not on the QuickTime version.

### 8b. Revised phase table (supersedes P3/P4 in §5)

| Phase | What | LOC | Depends on |
|---|---|---|---|
| P3 (revised) | Delete only `createQTMovieLayer`/`destroyQTMovieLayer`/`m_qtVideoLayer`/`SOFT_LINK_CLASS(QTKit, QTMovieLayer)` (~230 LOC gross deletion, `QTMovieLayer` still absent). **Keep** `createQTVideoRenderer`/`destroyQTVideoRenderer`/`m_qtVideoRenderer`/`QTVideoRendererWebKitOnly` (~150 LOC) — now available, no deletion needed | −230 net (was −380) | P2 |
| P4 (revised) | `paint()`/`paintCurrentFrameInContext()` (`MediaPlayerPrivateQTKit.mm:1254-1291`) port **unchanged** — it already calls `[qtVideoRenderer drawInRect:]`, and `qtVideoRenderer` is now obtainable. The `currentRenderingMode()`/`preferredRenderingMode()` logic (`:452-474`) also needs no change beyond what §1b/§1c already require, since it already prefers `MediaRenderingSoftwareRenderer` whenever `MediaRenderingMovieLayer` isn't available (`preferredRenderingMode()` falls back correctly as written) | ~10-20 (just the §1b/§1c signature-rename mechanics, not new rendering logic) | P3 |

Net effect on the §5 total: roughly **150 fewer LOC to write** than the
original estimate (the §4a `frameImageAtTime:`-based paint path is no longer
on the critical path for P0-P8; §4b's later CARenderer-era note about
preferring `CGImageRef`-into-`CALayer.contents` over an OpenGL/`QTVisualContextRef`
pipeline is now a choice between *three* implemented-and-working sources for
the CA layer's `contents` — `frameImageAtTime:`, the now-available
`QTVideoRendererWebKitOnly`'s underlying frame data if it's introspectable,
or a true `QTVisualContextRef` pipeline — rather than being the only option).
**§4a's spike is still worth doing** (still queued on another agent, per the
team lead) since `nativeImageForCurrentTime()`/§1d and the CARenderer-era
path in §4b both still want it regardless of which path `paint()` itself
ends up using.

### 8c. The rest of the requested symbol checks

| Symbol | In 2018 code? | 7.2 | 7.6.4 | Note |
|---|---|---|---|---|
| `QTMovieOpenForPlaybackAttribute` | yes, `MediaPlayerPrivateQTKit.mm:276` | n/a | n/a | Used as a **raw string literal** (`@"QTMovieOpenForPlaybackAttribute"`), not a soft-linked exported constant — never was checkable via `nm` in either version, and the 2018 code already doesn't assume it's linkable (that's exactly why it's a literal instead of `SOFT_LINK_POINTER`). No version-dependent behavior to report |
| `QTMovieOpenAsyncRequiredAttribute` | **no** — zero occurrences anywhere in the 2018 file | — | — | Not used by the ported backend; nothing to check or change |
| `QTMovieApertureModeAttribute` | yes, `:90-91` (soft-linked) | present | present | No delta — was already available on 7.2 per §2's original finding (QTKit-version-gated at 7.2, not OS-version-gated) |
| `QTMovieRateChangesPreservePitchAttribute` | yes, `:81` | present | present | No delta |
| `QTMovieHasApertureModeDimensionsAttribute` | not directly referenced by name in the 2018 `.mm` (only in the SDK header, §2) | present | present | No delta |
| `QTMovie` `loadedRanges` / `QTMovieLoadedRangesDidChangeNotification` | yes, `:77,99,354-355,880-881,1149,1661` | `-loadedRanges` guarded by `respondsToSelector:` (`:880`); **the notification constant itself is absent as an exported symbol in *both* 7.2 and 7.6.4** (`nm`, zero hits) | same | The 2018 code was already written defensively for this — the method-existence check means it degrades gracefully regardless of QuickTime version, and since the notification constant isn't exported in the version actually on this box either, `loadedRangesChanged:` (`:1661`) simply never fires here and `maxMediaTimeLoaded()`'s `loadedRanges`-based branch (`:880-881`) is the only place buffered-range data reaches WebCore. No action needed, no regression — same behavior in 7.2 and 7.6.4 |
| `QTMovieView` changes | not used for rendering by the 2018 backend at all (only `QTMovie`/`QTVideoRendererWebKitOnly`/`QTMovieLayer`) | — | new class `QTMovieViewControllerViewTranslationHandler` appeared, capture-view-adjacent classes churned | Irrelevant to this port — the 2018 backend never instantiates `QTMovieView` (that's the higher-level, controller-chrome-included widget; WebKit always used the lower-level `QTMovie` object directly) |
| 7.6.x error/notification constants generally | — | 454 exported symbols | 453 exported symbols, **117 added / 118 removed** (`comm -13`/`comm -23` on the sorted lists) | The overwhelming majority of churn is unrelated to anything the 2018 backend uses: a new `QTTimeFormatter` CF-style API (`kQTTimeFormatter*`, `QTTimeFormatterCreate*`), a new private `QTUI*` movie-controller-skin widget-drawing API (`QTUIWidget*`, `QTUIState*`, all clearly the modern QuickTime Player movie-controller chrome, nothing WebKit would touch), and a batch of `QTVisualContext`⟷`QTImageConsumer` attribute-bridging functions (`QTConvertImageConsumerAttributeToVisualContextAttribute` and siblings) that only matter if the CARenderer-era `QTVisualContextRef` path (§4b, §2's flagged gap) is ever pursued instead of the `CGImageRef`-based one. Removed symbols are almost entirely legacy `NSDataDataHandler`/`NSObjectDataHandler`/MP3-importer-patch internal plumbing — none of it referenced by the 2018 `MediaPlayerPrivateQTKit.mm` |

### 8d. Codec components under `/System/Library/QuickTime`

Now mirrored (previously absent from `sysroot/`, flagged as a gap in the
original "Not done in this pass" list — partially closed): `sysroot/System/Library/QuickTime/`
contains `QuickTimeH264.component`, `QuickTimeMPEG4.component`,
`QuickTimeMPEG.component`, `QuickTime3GPP.component`,
`QuickTimeComponents.component`, `AppleVAH264HW.component` (hardware-assisted
H.264, irrelevant on this GPU/era but present), `AppleProResDecoder.component`,
`ApplePixletVideo.component`, `QuickTimeStreaming.component`,
`QuickTimeFireWireDV.component`, plus capture-device components
(`QuickTimeIIDCDigitizer.component`, `QuickTimeUSBVDCDigitizer.component`,
irrelevant, no `<video>` capture on this port) and `QuickTimeVR.component`
(irrelevant). `QuickTimeH264.component`, `QuickTimeMPEG4.component`, and
`QuickTimeComponents.component` all report `CFBundleShortVersionString`
7.6.4 / `CFBundleVersion` 1327.73, matching `QTKit.framework` exactly — same
release, not independently versioned.

**Not independently re-verified by disassembly**: whether `QuickTimeH264.component`
7.6.4 actually decodes H.264 High Profile (not just Baseline/Main as §3
stated for the general QuickTime-7-era baseline). This is a documented Apple
claim for the QuickTime 7.6 release specifically — 7.6 was the update that
added support for playing H.264 content up to 1080p and improved
profile/level coverage for the contemporary MacBook Pro/iMac hardware
refresh — but confirming the exact profile/level bitmask this specific
component enforces would need either disassembly or an actual High-profile
test file played on the box, neither done here. Treat §3's Baseline/Main
statement as the safe floor, and High-profile support as *plausible but
unconfirmed* for 7.6.4 specifically (it does not change §3's WebM/VP9/AV1/HEVC
conclusion either way — none of those become available regardless of H.264
profile coverage). AAC-LC was already covered per §3; AAC-HE (used by some
lower-bitrate streaming content) was not specifically checked and the
component's `CFBundleVersion` string alone doesn't resolve it.

## 9. The accelerated route: QuickTime's OpenGL visual context, for the CARenderer-host era

`spike/qtrenderertest.m` (commit `b09f4fd`) confirmed `QTVideoRendererWebKitOnly`
is real-time at 320x240 (~15fps matching a 15fps source, `drawInRect:` costing
~30-38ms/draw) but the decoder itself can't keep up at 720p — only ~2
`NewImageAvailable` notifications/second, `drawInRect:` costing 220-390ms/draw
when it does fire. That spike measured the same software CPU-bound path §4a
originally proposed as a fallback (`frameImageAtTime:`) and the one §8a found
already works (`QTVideoRendererWebKitOnly`) — both feed a CPU-side pixel
buffer through `NSGraphicsContext`/`CGContextDrawImage`. Team lead reports
QuickTime Player on Tiger reached ~24fps at 720p H.264 on comparable
hardware, which only happens through the GPU-resident path this section
covers — **confirming the bottleneck is the CPU buffer readback + `drawRect:`
composite, not the decoder itself.** The decoder is fine at 720p; walking
each frame through main memory and AppKit's software blitter is what isn't.

### 9a. The API, confirmed present on Tiger 7.6.4

The C-level `QTVisualContext` API lives in `QuickTime.framework`, not
`QTKit.framework` (the earlier `logs/api/tiger-QTKit-7.6.4.txt` export list,
being QTKit-only, doesn't carry these symbols — that's why the initial grep
against it came back empty; re-ran `tiger-nm -g -arch i386` directly against
`sysroot/System/Library/Frameworks/QuickTime.framework/QuickTime` instead).
**All of the following are present as defined (`T`) symbols**:

```
_QTOpenGLTextureContextCreate
_QTVisualContextCreate
_QTVisualContextRetain / _QTVisualContextRelease
_QTVisualContextSetImageAvailableCallback
_QTVisualContextSetIsNewImageAvailableCallback
_QTVisualContextIsNewImageAvailable
_QTVisualContextNewImageAvailable
_QTVisualContextCopyImageForTime
_QTVisualContextTask
_QTVisualContextGetAttribute / SetAttribute / GetProperty / SetProperty
_QTVCInitializeOpenGLTextureContext
```

(plus the pixel-buffer-context sibling API, `QTPixelBufferContextCreate` /
`QTVCInitializePixelBufferContext`, for a CPU-side alternative not needed
here.) `CVOpenGLTextureRef`'s consumer API is confirmed present in
`CoreVideo.framework` (version 1.4.1, matches `logs/ca-hosting-design.md`'s
existing `CVDisplayLink` finding): `_CVOpenGLTextureCacheCreate`,
`_CVOpenGLTextureCacheCreateTextureFromImage` (not actually needed — the
visual-context path hands back a ready `CVOpenGLTextureRef` directly, no
cache round-trip required), `_CVOpenGLTextureGetTarget`,
`_CVOpenGLTextureGetName` (the two calls that matter: they give the raw GL
texture target/name to bind for drawing), `_CVOpenGLTextureRetain`/`Release`.

On the `QTKit.framework`/`QTMovie` side, `-setVisualContext:`/`-visualContext`
were already confirmed present in §2 (gated only by `QTKIT_VERSION_MAX_ALLOWED`,
which defaults to 7.2 regardless of OS version — true at both 7.2 and 7.6.4,
unaffected by the §8 upgrade). §2 also already flagged the one real gap:
**`QTVisualContextRef` itself isn't declared in either SDK snapshot in this
checkout** — a 1-line local `typedef struct OpaqueQTVisualContextRef*
QTVisualContextRef;` forward-declaration closes it, same conclusion as
before, now with a concrete use for it.

### 9b. The pipeline

1. `QTOpenGLTextureContextCreate(kCFAllocatorDefault, cglContext,
   cglPixelFormat, attributes, &visualContext)` — bind the visual context to
   the **same `CGLContextObj`** `logs/ca-hosting-design.md` §2.2 already
   creates for the `CARenderer` host's `NSOpenGLView` (`WebCARendererHostView`).
   One shared GL context for both CA's own rendering and QuickTime's texture
   output — required for the texture handle to be usable without a
   cross-context share/copy.
2. `[m_qtMovie setVisualContext:visualContext]` (bridging the C ref through
   the ObjC method — the `QTVisualContextRef`/`QTVisualContextRefID` divide
   is exactly the toll-free-bridged relationship the 10.4u SDK header comment
   implies but doesn't spell out, consistent with §4a's note about
   `frameImageAtTime:`'s untyped `void *` return needing the same kind of
   verification. Not independently spiked in this pass — flagged below).
3. Per frame (driven by `QTVisualContextSetImageAvailableCallback` or by
   polling `QTVisualContextIsNewImageAvailable` from the same timer/display-
   link tick `logs/ca-hosting-design.md` §2.4 already drives the `CARenderer`
   frame from — **reuse that driver, don't add a second one**): call
   `QTVisualContextTask(visualContext)` to pump QuickTime's internal state,
   then if a new image is available, `QTVisualContextCopyImageForTime(visualContext,
   kCFAllocatorDefault, NULL, &cvImageBuffer)` and `CVOpenGLTextureGetTarget`/
   `CVOpenGLTextureGetName` to get the GL texture target/name to bind.
4. Draw it. **This is not a `CALayer.contents` assignment** — Tiger has no
   IOSurface (10.6+) to back a zero-copy `CALayer.contents`/GL-texture bridge,
   and copying the `CVOpenGLTextureRef` back to a `CGImageRef` for
   `CALayer.contents` would reintroduce exactly the CPU round-trip this whole
   section exists to avoid. Instead, draw the texture as a textured quad
   directly in `WebCARendererHostView`'s own GL code, in the **same CGL
   context and the same frame** as the `CARenderer` pass from
   `logs/ca-hosting-design.md` §2.3:
   ```
   CGLSetCurrentContext(cgl);
   [CATransaction flush];
   glClear(GL_COLOR_BUFFER_BIT);
   // draw layers stacked below the <video> element's z-order, if any, via CARenderer
   glEnable(textureTarget); glBindTexture(textureTarget, textureName);
   // draw the video's quad at its layer's screen-space rect
   glDisable(textureTarget);
   // draw layers stacked above the <video> element, if any, via CARenderer
   [renderer render];   // as before
   [renderer endFrame];
   [[self openGLContext] flushBuffer];
   ```
   This matches `logs/ca-hosting-design.md` §4.4's own placeholder
   ("Video — separate track... Not part of this design") — §9 is exactly
   that missing track. **Caveat, not costed here**: a `<video>` element that
   participates in complex stacking (transformed/opacity ancestors above and
   below it in the same compositing context, or other elements painting
   *through* transparency over the video) needs the layer tree partitioned
   into "everything below the video's z-order" / "everything above," each
   rendered as a separate `CARenderer` pass around the raw GL quad draw — a
   real complication for correctness in the general case, though the common
   case (`<video>` sitting in its own block, nothing overlapping it) works
   with a single before/after split as sketched above. `CARenderer` doesn't
   support partial-scene rendering by z-index natively; splitting the layer
   tree into two `CARenderer` instances (or two render passes with different
   root sublayer sets) is the mechanism, not costed in this pass.

### 9c. Can this replace `QTMovieLayer`?

**Yes, and it's not a coincidence — Apple's own `QTMovieLayer` (10.5+,
confirmed still absent from Tiger 7.6.4's QTKit in §8a) is understood to be
built on exactly this mechanism internally**: a `CALayer` subclass that
installs a `QTVisualContext` (specifically the pixel-buffer or IOSurface
variant depending on OS version, not the raw OpenGL-texture variant used
here) and feeds each `QTVisualContextCopyImageForTime` result to the layer's
`contents` on newer OS versions where IOSurface-backed `CALayer.contents`
makes that a zero-copy operation. **This is stated from how QuickTime/WebKit's
public documentation and the general shape of the API describe the
relationship, not from disassembling `QTMovieLayer` itself** — no
`QTMovieLayer` binary exists on this Tiger box to disassemble, per §8a, so
this can't be verified against Tiger's own copy either way. The practical
upshot either way: whether or not the internal mechanism is literally
identical, **the video-track design in §9b is the correct Tiger-native
replacement for what `QTMovieLayer` would have provided** — same visual
result (a live-updating video image composited into the CA scene), reached
through the one GL-texture-based mechanism confirmed to exist on this
specific QuickTime version, with the CPU-round-trip removed. Once this
lands, `MediaPlayerPrivateQTKit`'s `MediaRenderingMovieLayer` rendering mode
(§8a/§0, `createQTMovieLayer`/`destroyQTMovieLayer`/`m_qtVideoLayer`, still
recommended for deletion since the literal `QTMovieLayer` class stays
unavailable) is superseded by a *third* rendering mode this plan hasn't
previously named — call it `MediaRenderingVisualContext` — gated on whether
the CARenderer host (`logs/ca-hosting-design.md`) is active, falling back to
`MediaRenderingSoftwareRenderer` (§8a, `QTVideoRendererWebKitOnly`) when
compositing is off, exactly mirroring the existing `currentRenderingMode()`/
`preferredRenderingMode()` logic's shape (§8b) but with a new best tier
added above the old two.

**Not costed in this pass**: LOC estimate for `MediaRenderingVisualContext`
itself (a new rendering mode plus the `WebCARendererHostView` quad-draw
integration in §9b) — this depends on the CARenderer host existing first
(plan §6, `logs/ca-hosting-design.md` phase 4.1+), so it's a phase *after*
P0-P9 in §5's table, not a revision to any existing phase there.

## Not done in this pass

- Did not spike-test §9's `QTOpenGLTextureContextCreate`/`setVisualContext:`/
  `QTVisualContextCopyImageForTime` pipeline on real Tiger hardware — §9's
  symbol-presence check (via `nm`) confirms the API is linkable, not that it
  behaves as documented at runtime on this specific QuickTime 7.6.4 build.
  Natural next spike once `spike/CAHost`/`logs/ca-hosting-design.md`'s
  `NSOpenGLView` host exists to bind the texture into.
- Did not spike-test `frameImageAtTime:withAttributes:error:`'s actual
  runtime return type on real Tiger hardware (§4a/P0) — the header alone
  doesn't prove it returns a `CGImageRef` and not an `NSImage`/`CIImage`
  despite asking via `QTMovieFrameImageType`. (Being spiked separately on
  the box by another agent as of this writing.)
- Did not check whether `/System/Library/QuickTime/*.component` (the actual
  H.264/AAC codec components) are present in this project's `sysroot/`
  mirror — the mirror currently has no `sysroot/System/Library/QuickTime/`
  directory at all. If codec components aren't mirrored, add them (they're
  needed at runtime regardless of anything WebKit-side).
- Did not fully read `MediaPlayerPrivateGStreamer::seekToTarget`'s bus-message
  seek-completion handler (only its first ~35 lines and the producer-setup
  idiom) — not needed for the QTKit adaptation above since QTKit's seek
  completion detection (the existing `seekTimerFired()`/network-state check)
  is already fully specified by the 2018 code being ported, but flagged in
  case a future pass wants the GStreamer completion-detection idiom for
  comparison.
