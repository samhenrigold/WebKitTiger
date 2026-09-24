# Control fidelity and presentation, 24 September 2026

This work follows installed candidate `f9c6cfc90`. It keeps modern upstream
WebKit's CSS and control behavior, with Tiger implementations at the platform
boundaries. The reported Amazon select, iHeart volume slider and Apple purchase
radio controls supplied the reproduction patterns.

## Installed candidate

`7b2ce2201` is installed at `/Users/shg/Applications/TigerBrowser.app` on Tiger.
All three Mini builds have matching source, dependencies and wire contracts.
All 74 regular bundle files and four symlinks match the local tested bundle;
see `logs/install-7b2ce2201.log` and `logs/installed-7b2ce2201.json`.
Rollback is retained at
`/Users/shg/Applications/TigerBrowser.app.previous-20260924-004335-65aebead2ea9`.

Twenty functional browser checks pass: four popup gates, six control gates, six
font/video gates and four existing faithful page/scroll/control/navigation checks.
The latter two suites are recorded in
`logs/regress/image-edges-7b2ce2201/summary.md` and
`logs/regress/engine-7b2ce2201/summary.md`. The exact paused-frame canvas and pixel
thresholds were unchanged. The normal rendering paths are installed; direct
presentation remains opt-in. This qualification does not certify every modern
website, CSS feature or experimental recovery path.

The subsequent `b6e98a730` build adds opt-in preparation/flush timing diagnostics.
It is a separate matched archive for investigation, not the installed candidate.

## Native control eligibility

The old deterministic fixture created twelve unwanted native widgets alongside
nine ordinary or transitioning controls. Native hosting now uses the computed
appearance and checks clipping, visibility, ancestor and iframe effects, and
custom shadow parts. WebCore painting uses the same decision, avoiding both
duplicate controls and missing fallback painting. Fractional coordinates alone
do not disqualify an ordinary native control.

Styled selects preserve the author's box and text and use the upstream Mac
decoration behavior. Removing their native view exposed an existing missing
popup-menu proxy. The new proxy uses Tiger's public `NSPopUpButtonCell` tracking
without painting a native button over the page. It preserves duplicate option
indices, disabled items, separators, Escape and cancellation during navigation.
Tiger's public `dismissPopUp` does not terminate its nested tracking loop; a
verified private AppKit menu association identifies the owned Carbon menu for
public `CancelMenuTracking`. Unrelated menus cannot be canceled through it.

The bitmap provider also stopped ordering an auxiliary 1024×1024 window over the
browser. Its offscreen cell rendering follows upstream's `WebControlWindow`
pattern. Eight native bitmap hashes remain unchanged on Tiger while the extra
visible window is gone.

Candidate `75a29da0e` passes the eligibility fixture in both rendering modes and
the four existing clipping/edit/hide/reveal/iframe-scroll gates. Screenshots and
native-widget counts agree: nine initial widgets, eight while the transitioning
control is custom, and nine after restoring its native appearance. Evidence:

- `logs/probes/controls-75a29da0e/RESULT.md`
- `logs/regress/controls-75a29da0e/summary.md`
- `logs/probes/20260923-231359-control-window-initialized/`

The matched `7b2ce2201` build additionally passes all four native-popup browser
gates: original selection and expanded lifecycle in both modes. Actual Tiger menu
screenshots show the menu and preserved author styling. Trusted selection events
use the correct option indices with no DOM navigation-key fallback; Escape,
reopening and navigation cancellation pass. Driver snapshots, hashes and visual
review are recorded in `logs/probes/styled-select-lifecycle-7b2ce2201/RESULT.md`.

The fully custom range has its author-styled thumb. A range with native appearance
on the host can still have a native thumb: upstream `SliderThumbElement` explicitly
sets that used appearance. The exact fixture produces the same result in host
WKWebView; this case was not changed to diverge from upstream.

These fixture results establish the reported classes of defect, not complete
live-site or Cocoa editing compatibility. Arbitrary sibling overlap, transformed
native controls, composition and script-controlled editing need further work.

## CoreAnimation layer geometry

The scene adapter assigned WebKit's top-left position directly to CoreAnimation's
anchor position. A 200×45 overlay at CSS (80,125) consequently appeared at
(-20,102.5), clipped against the left edge. The correction follows upstream
`GraphicsLayerCA`: `CA position = WC position + anchor × size`.

WC position, anchor, size and bounds origin are retained across partial property
updates. WC base layers use their specified default half-size anchor; scene-owned
tile and video helper layers keep zero anchors. The actual production constructor
and update method pass native CALayer tests with sanitizers, covering 1,024 partial
update combinations, anchor resets, nonzero bounds origins and affine transforms.
This change does not add transform-origin-z or preserve-3d support.

Screenshots carry the target display's ICC profile. Geometry checks normalize that
profile and derive expected colors independently from the fixture. Raw channel
differences alone were not evidence of a CSS color defect; color management remains
separate from the demonstrated layer displacement.

## Correctness that also avoids unnecessary work

Deferred layout repaints now schedule the next rendering update. The dynamic
font-ink gate paints all twelve rows in both modes.

The faithful paused-video canvas test exposed Tiger CoreGraphics image-edge
antialiasing: an opaque 640×360 image scaled to 32×18 acquired partially transparent
edges, which changed on repeated source-over draws. The narrow correction disables
edge antialiasing for axis-parallel images with pixel-aligned device edges. It
preserves clipping, interpolation, fractional/rotated coverage and saved state.
Native Tiger probes reproduce the original failure and pass with the correction.
All six unchanged font/video gates pass on `75a29da0e`, including exact paused-frame
restoration: `logs/regress/image-edges-75a29da0e/summary.md`.

Cookie lookup now uses an indexed host-suffix query. On Tiger's 10,000-cookie
fixture, 200 SQL lookups decreased from about 2355 ms to 141.5 ms, about 16.6×.
All 24 browser cookie assertions and relaunch persistence pass. This is a query
measurement, not a whole-page loading speed claim.

Video image providers reuse their owned image while the published sequence is
unchanged. Ownership, replacement and concurrent-publication checks cover retained
images. The profiler's unsafe stack reads were also replaced with bounded Mach
copies and its thread-port rights are released; both target ABIs pass mapped,
unmapped, protected and partial-read probes.

## Measured video performance

The workload uses a local 1280×720 video displayed at 1280×720, eight seconds of
warmup and thirty seconds of measurement. The gate requires at least 29 unique
frames/second and no gap above 100 ms. It distinguishes decoding, GPU flushes,
live UI acceptance, window painting and physical scanout. These results do not
establish YouTube streaming support or physical display scanout.

| Path and candidate | Unique frames / 30 s | FPS | Maximum gap |
| --- | ---: | ---: | ---: |
| Fast, `f9c6cfc90` | 815 | 27.17 | 94.54 ms |
| Fast, `75a29da0e` | 813 | 27.10 | 95.40 ms |
| Faithful, `f9c6cfc90` | 464 | 15.47 | 131.41 ms |
| Faithful, `75a29da0e` | 464 | 15.47 | 129.46 ms |
| Direct faithful, `89c598d83` (drawable acceptances) | 790 | 26.33 | 102.79 ms |
| Direct faithful, `7b2ce2201` (drawable acceptances) | 807 | 26.90 | 112.46 ms |
| Direct faithful, `b2fcc886c` (drawable acceptances) | 895 | 29.83 | 122.25 ms |

The fast comparison shows no measurable FPS improvement. Both faithful rows use
the same second pointer movement after startup so WebKit's media controls hide.
Earlier runs with only one early movement retained controls and were correctly
rejected as occluded. The direct run extends total lifetime to fifty seconds so
the final screenshot occurs after the unchanged measurement window. A preserved
earlier run stalled during screen capture and cannot isolate steady throughput.
Every row still fails the unchanged performance gate.

Evidence is retained under `logs/bench/native-unprofiled-{f9c6cfc90,75a29da0e}/`,
`logs/bench/faithful-latepointer-{f9c6cfc90,75a29da0e}/` and
`logs/bench/direct-latepointer-captureafter-{89c598d83,7b2ce2201,b2fcc886c}/`.
The `7b2ce2201` screenshot starts 4.275 seconds after the measured interval;
the capture helper records the same monotonic clock used by frame telemetry.

Direct presentation reuses the existing WC/CoreAnimation scene on a Tiger native
cross-process surface. It removes readback and the UI bitmap copy. GPU identity,
surface generation, complete first flush, detach acknowledgement and ownership
restoration are checked. The surface must be explicitly ordered before export;
the original omission produced accepted frames with no visible scene. Actual
screenshots, not counters alone, exposed that defect.

This path remains off by default. It currently requires a fully visible,
untransformed 1× view without native child views. Inserting a native child must
restore bitmap painting before mutating the hierarchy: Tiger calls
`didAddSubview:` before assigning that child's window, making synchronous painting
from that callback unsafe. The new pre-insertion hooks cover both insertion APIs.

The `7b2ce2201` target lifecycle run verifies bitmap and direct scene geometry,
changing tiles, a static canvas, native-control insertion, bitmap fallback,
editing and navigation with visible pixel checks. Resize remains untested because
target accessibility automation is disabled. GPU-exit recovery fails: the surface
retires and the retained bitmap stays visible, but a new scene is not painted.
Direct presentation therefore remains experimental. Evidence is in
`logs/probes/direct-lifecycle-7b2ce2201/20260924-004542-794611/`.
Its original results are preserved alongside `checker-reanalysis.json`: the
navigated empty promoted panel needed a CSS-color reference rather than the
DeviceRGB raster reference. Geometry and tolerances were unchanged; nine checker
tests retain missing-content, displaced-content and stale-recovery negatives.

## Next performance work

The direct-path UI acknowledgement is normally below one millisecond and UI CPU
is 1.1–1.2 percent in steady samples. The remaining cost is predominantly
GPU scene rendering/flush and video preparation. Rendering/flush alone averaged
23.69 ms in the measured interval; video preparation averaged 10.10 ms, with
p95 17.58 ms. Variable preparation can push the
total beyond the 33.3 ms frame budget. Split preparation timing and buffer-pool
evidence should guide the next change.

The diagnostic `b6e98a730` run isolates a startup-dependent retention problem.
All three reusable video buffers are checked out before direct activation, and
all 684 preparations in the measured interval allocate independent storage.
Snapshot filling averages 15.15 ms, scene rendering 19.67 ms and drawable flushing
5.69 ms. The queried swap interval is already zero, with no GL/flush errors.
This run delivers 22.77 accepted frames/second and also fails the unchanged gate.
Different startup buffering confounds comparison with the clean run, so the
difference cannot be attributed to logging overhead. Evidence is in
`logs/bench/direct-video-prep-b6e98a730/`.

An isolated Tiger probe confirms the inactive bitmap renderer retains those image
providers after layer detachment. Releasing the renderer frees obsolete images;
when its factory autorelease remains pending, that release is postponed until
the factory pool drains. Destroying the old GL context releases no additional
providers. All subsequent frames reuse cached storage, a recreated bitmap renderer
draws the unchanged scene, and all twelve provider callbacks occur exactly once
at final teardown. Both cases pass native pixel and ownership checks; see
`logs/probes/ca-renderer-retention-20260924-010908-800bcbfcf782/`.

Candidate `b2fcc886c` implements that bounded correction. It retires the inactive
renderer only after a successful full direct flush, drains its factory autorelease
while the GL context is alive, and preserves the scene and retained UI bitmap.
Bitmap fallback recreates its target lazily and forces a complete redraw.
Direct frames no longer recreate an unused bitmap target. Actual-helper sanitizer
tests cover six creation/attachment failure points, retries and repeated
transitions; omitting either retirement or the factory pool fails negative
controls. This fixes ownership without increasing the buffer cache or changing
the swap interval.

The full-browser `b2fcc886c` run delivers 895 unique frames in 30 seconds
(29.83 accepted frames/second, p95 gap 41.5 ms), up from 807 at `7b2ce2201`.
It still fails the unchanged gate on a single 122.25 ms gap against the 100 ms
limit; the FPS floor of 29 is met. Decoder output stays at 30.0 FPS with no drops.
The screenshot starts 4.29 seconds after the interval and shows the playing video.
A diagnostic rerun confirms the ownership fix: all 893 preparations reuse cached
storage, at most three buffers are checked out, and snapshot filling falls from
15.15 ms to 2.92 ms mean (p95 5.98 ms). Scene rendering averages 13.49 ms and
drawable flushing 5.01 ms, with zero GL or flush errors. That rerun reaches
29.77 frames/second with a 116.69 ms maximum gap and also fails. The remaining
work is the isolated long gap, not steady-state throughput. Evidence is in
`logs/bench/direct-latepointer-captureafter-b2fcc886c/` and
`logs/bench/direct-video-prep-b2fcc886c/`.

The [native video contents research](native-video-queues.md) identifies two
existing API paths and older upstream adapters to probe: public CoreVideo texture
caching through `CAOpenGLLayer`, and private `CAImageQueue` contents. The bundled
framework exports the necessary symbols, but correct composition and frame lifetime
under our `CARenderer` still require target proof. No zero-copy claim follows from
symbol availability.

Preserve correctness gates while improving that path: full CSS layer geometry,
native-view insertion, navigation, GPU loss and resize must all keep visible
content. Screen capture belongs outside timed windows. Then validate sustained
live-site interaction, scrolling, streaming, seeking and memory behavior before
enabling direct presentation by default.
