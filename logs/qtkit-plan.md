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
Before dropping it silently, grep `Source/WebKitLegacy/mac` for any call site
that still expects it (not done in this pass — flagged). If nothing calls
it, delete the override, saving ~15 LOC.

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
| P5 | `seekToTarget` → `MediaTimePromise` adaptation (§1c) — read one other backend's implementation first | 40-80 | P2 |
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

## Not done in this pass

- Did not grep `Source/WebKitLegacy/mac` for callers of `platformMedia()`
  (§1e) — needed before deleting that override.
- Did not read `MediaTimePromise.h` or another backend's `seekToTarget`
  implementation (§1c/P5) — needed before writing the seek adaptation.
- Did not spike-test `frameImageAtTime:withAttributes:error:`'s actual
  runtime return type on real Tiger hardware (§4a/P0) — the header alone
  doesn't prove it returns a `CGImageRef` and not an `NSImage`/`CIImage`
  despite asking via `QTMovieFrameImageType`.
- Did not check whether `/System/Library/QuickTime/*.component` (the actual
  H.264/AAC codec components) are present in this project's `sysroot/`
  mirror — the mirror currently has no `sysroot/System/Library/QuickTime/`
  directory at all. If codec components aren't mirrored, add them (they're
  needed at runtime regardless of anything WebKit-side).
