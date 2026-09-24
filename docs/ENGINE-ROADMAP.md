# Engine integration and performance

Target: Core 2 Duo T7500, 6 GB, Mac OS X 10.4.11. The first video target is
720p at 30 presented frames per second with working controls. Full pages, with
their scripts and content enabled, are the performance baseline.

Keep modern WebCore, JavaScriptCore and networking in 64-bit processes. Use the
32-bit Cocoa host for native UI, event interpretation, text services, menus and
presentation. Keep browser chrome work outside this phase. The eventual product
has one adaptive rendering path; software/accelerated switches remain useful for
diagnosis and comparison during integration.

## Implementation order

1. **Establish a reproducible candidate.** Preserve all pending branches and dirty
   work, build every process from the same committed engine tree, compare the
   actual preprocessed wire contracts, and stage immutable artifacts. Exercise the
   baseline before attributing failures to a pending topic.
2. **Measure presentation.** Count distinct displayed video frames, dropped and
   repeated frames, input-to-paint latency, memory and time in decode, render,
   readback and UI presentation. Decoder FPS alone cannot prove 720p30. Compare
   deterministic local pages first, then Twitter/X, NYT, The Verge and YouTube.
3. **Remove serial rendering waits.** Review and measure the pending two-frame
   video pipeline. Make shared video buffers safe for retained image providers.
   Then evaluate direct GPU presentation to remove readback and copies while
   retaining correct transforms, clipping, opacity, overlap and video controls.
4. **Keep input responsive under script load.** Advance scrolling outside the web
   main thread with coordinated hit testing, wheel routing and scroll state.
   Avoid using site restrictions to conceal main-thread saturation.
5. **Integrate native behavior.** Bring in scrollbar and IME work as separate
   verified changes. Test text selection, marked text, dead keys, undo, command
   shortcuts, focus, spelling, menus, overflow scrollers and transformed controls.
   Prefer Cocoa event interpretation and services over duplicate editing logic.
6. **Recover without losing state.** Verify web/network/GPU crash recovery,
   pressure handling and navigation cleanup; preserve the user's profile and
   isolate test profiles. Test restarts with the same wire contract as the UI.
7. **Make conformance executable.** Finish a useful WebKitTestRunner path, starting
   with real text tests. Unsupported event/pixel operations must report failure.
   Expand WPT/layout/media coverage before enabling broader APIs.
8. **Expand modern capabilities.** Add codecs, graphics, audio and browser APIs in
   vertical slices with actual behavior and conformance results. API existence or
   an immediately rejected promise is not implemented support. Track DRM, device
   access and hardware limits separately from engine implementation gaps.

For every platform gap, investigate in this order: Tiger public API; Tiger
private API; faithful backport using Apple source or verified disassembly;
WebKit/open-source implementation; new implementation. Verify ABI and behavior
on Tiger before trusting a same-named symbol. Correctness and performance both
belong in the acceptance criteria.

## Preserved source state

The outer integration branch is `integration/engine-baseline-20260923`.
`WebKit-integration` is an isolated inner worktree on
`tiger-integration-20260923`, initially based on `46ce7e847`.
The source inventory and original dirty-patch hashes are in
[`integration-handoff.json`](integration-handoff.json). The local handoff bundle
preserves every Tiger branch relative to upstream `d2f526058`; it is a recovery
artifact, not a published remote.

The original topic branches are retained. The features checkpoint is WIP,
not an integrated feature claim. WKTR has additional isolated fixes on
`tiger-wktr-integration-20260923`; its functional test gates are still pending.
Do not push inner branches to the inner `origin`: that remote is upstream WebKit.

## Build and run

Run from the outer repository. The Mini mirror uses the same absolute root.
`tools/mini-build.sh` holds a Mini-wide lease from sync through verified download,
waits for legacy Ninja jobs, synchronizes installed toolchains and dependencies,
reconfigures every time, and preserves complete logs under `logs/mini-build/`.

```sh
tools/mini-build.sh WebKit-integration tiger-web-integration TigerWebProcess TigerNetworkProcess
tools/mini-build.sh WebKit-integration tiger-ui-integration TigerWK2App TigerBrowser2
tools/mini-build.sh WebKit-integration tiger-gpu-integration TigerGPUProcess

export UIDIR=build/tiger-ui-integration
export WEBDIR=build/tiger-web-integration
export GPUDIR=build/tiger-gpu-integration
python3 tools/tiger-artifacts.py verify --ui "$UIDIR" --web "$WEBDIR" --gpu "$GPUDIR"
tools/regress.sh example fexample
tools/regress.sh
```

Do not edit the selected engine worktree or synchronized build inputs during a
build. Publication fails if their content changes. `build-manifest.json` records
the source, configuration, dependency hashes, executable hashes and wire evidence.
Old binaries without manifests are not retroactively certified.

Staging uses `tiger-eth`, a local lock and a Tiger-side lease. Each run has its own
directory under `/Users/shg/wk2/runs/`, process group, logs and screenshots. The
default scratch profile is `/Users/shg/wk2/home`; set `RUN_HOME_NEW=1` for a fresh
profile. Tests wait for an idle box and never terminate the user's browser.
Diagnostics are copied back even after a launch failure.

```sh
RUN_HOME_NEW=1 APP=TigerBrowser2 \
  spike/wk2web/stage-app.sh https://www.nytimes.com/ 60
INSTALL=0 tools/make-bundle.sh
# After the candidate passes and the user's app is closed:
tools/make-bundle.sh
```

Installation uses a temporary sibling and rename, retains the previous bundle,
and rolls back a failed promotion. `--all` in the regression harness also selects
pending-topic checks; it is deliberately broader than the integrated baseline.

## Evidence and limits

The initial implementation adds build provenance and safe staging, not a claim
that all rendering or web features are complete. Existing measurements in
`NOTES.md` show software 480p30 and approximately 20 FPS for the initial faithful
video path. Neither is evidence of end-to-end 720p30. Record new candidate source
IDs, test logs and measured results below as integration progresses.

### 2026-09-23 integration evidence

The original baseline `46ce7e847` was rebuilt on the Mini for all three process
configurations. Its immutable artifacts are in `build/baselines/46ce7e847/`.
All 14 established regressions passed on Tiger; logs are in
`logs/regress/baseline-46ce7e847-{smoke,rest}/`. Cookie testing passed all 24
assertions, including a second launch with the same isolated test profile.

The new hosted-control fixture exposed three baseline defects: an input outside
its overflow clip intercepts clicks; a hidden input intercepts clicks; and an
offset iframe's input accepts text but does not display it. The latter now has
an explicit screenshot ink check in addition to the DOM value assertion. The
native-control integration fixes these three defects; both rendering modes passed
the initial and full hide/reveal/edit/iframe-scroll cycles on candidate `f69874944`.
The author-colored scrollbar checks exposed a separate disabled preference,
subsequently fixed and verified on candidate `f9c6cfc90` below.

Rendering candidate `44eab1c9a` includes two bounded in-flight video updates,
safe pinned-ring snapshots with owned CGImage pixels, and distinct window-paint
telemetry. All three Mini builds and wire/dependency verification passed. Its
artifacts are retained in `build/candidates/44eab1c9a/`. Fast and faithful smoke
screenshots matched the baseline exactly. Both 18-second video lifecycle
workloads passed their API/event/progress checks, including resize,
pause/resume, detach/reinsert and player replacement. This is not a frame-integrity
or presentation-rate claim.

The shared ring's bounds, ownership, concurrent publication, replacement and
abandoned-reader recovery probes passed in both i386 and x86_64 executables on
Tiger (83,328 concurrent snapshots combined). See
`build/handoff/video-ring-tiger-results.txt`. Host sanitizer coverage also passed.

Native 720p benchmark results for `44eab1c9a` are retained in
`logs/bench/perf-44eab1c9a/` and **do not meet the target**:

- Fast mode decoded at approximately 30 FPS but produced only one certified full
  video window paint in the 30-second measurement interval. Partial control-strip
  paints were correctly excluded. A pre-existing repaint-notification latch was
  never cleared by the fast direct sink. The correction is integrated separately
  as `cbc81bf0f`, with deterministic publication/acknowledgement race tests in
  `6f7da45df`. A second bootstrap defect and its later validation are recorded below.
- Faithful mode completed approximately 11.8 GPU renders/second in the measured
  8–38 second interval. Median rendering, readback and UI-copy costs were roughly
  23, 10 and 11 ms respectively. Visible media controls correctly prevented
  certification of unobscured full video frames. Missing file-video track
  registration also prevents WebKit's controls from enabling auto-hide; this is
  an implementation gap, not grounds to relax the benchmark.

Native benchmarks now snapshot their parser from the exact candidate Git commit,
record its hash and enforce source/display dimensions, a 30-second interval,
minimum 29 unique window paints/second and maximum 100 ms gap. Receipt, decoding,
partial paints and physical display scanout are kept distinct.

The [cross-process surface experiment](../spike/direct-present/README.md) proves
that Tiger can render from a separate GPU process directly into an owned Cocoa
window using its native surface-sharing protocol. Full 1280×720 texture-upload,
draw and flush work had p95 5.782 ms in the isolated probe. This establishes a
concrete route to eliminate readback and UI copies, but full scene composition,
native-view overlap, surface lifecycle and browser integration remain to be
validated before adoption. The subsequent native-overlap probe below resolves
the Cocoa overlay question.

### Candidate progression

Candidate `f69874944` built all three configurations with matching source,
dependencies and wire contracts; artifacts are in `build/candidates/f69874944/`.
`logs/regress/native-f69874944/summary.md` records eight passing checks and six
failures:

- Native scrollbars and hosted-control initial/cycle checks pass in both modes.
  Screenshots confirm visible iframe text, alongside DOM value/hit assertions.
- Both video lifecycle checks pass their API/event checks.
- All four author-color screenshots fail because the default CSS preference was
  disabled. The supported preference is now enabled in `e1f2ec605`.
- File-track canvas checks fail despite visible video. The Cairo shareable-image
  implementation was missing, so remote canvas image transport dropped the
  drawing operation. `c737b0e10` implements copied and retained image ownership.
  The harness also incorrectly selected faithful rendering for both file-track
  rows; that selection is fixed and regression-tested.

`logs/bench/native-f69874944/` still fails both native 720p gates. Faithful mode
now hides controls correctly and delivers 386 certified full-window frames in
30 seconds: **12.87 FPS**, with p95 gap 94.7 ms and maximum gap 188.7 ms. Fast mode
still delivers only one full frame because its repaint latch becomes stuck at
sink activation. `e99f9a447` fixes that bootstrap transition, with a failing-old-code
negative control. These numbers are window-paint evidence, not physical scanout.

The separate direct-presentation experiment now measures complete CA scene work.
Reusing provider-owned pixel buffers reduced steady mean work from 30.00 to
20.80 ms and p95 from 31.26 to 22.23 ms. Each CGImage still owns its pixels until
release; three slots were allocated and reused 237 times, without fallback.
This is a synthetic surface workload, not end-to-end video. The corresponding
production pool is integrated in `8fe858515`; its browser benchmark is recorded below.

The [native-surface probe](../spike/native-surface/README.md) also demonstrates
that a GPU surface below a transparent Cocoa backing preserves real NSTextField
editing/selection, focus rings, NSScroller, NSClipView clipping and opaque native
views. Moving it above the backing supplies the negative control: those native
pixels become occluded although API state still agrees. Browser integration must
still implement trusted GPU connection identity, geometry generations, surface
retirement, complete redraws and recovery before replacing readback presentation.

The latest complete engine candidate is `f9c6cfc90`. In addition to the preceding
fixes, it separates native ATS indices from FreeType's resource-container indices.
The [manifest probe](../spike/fontmanifest.md) preserves all 176 native indices and
matches all 174 supported native faces using public ATS structural tables. Fifty-one
indices differ between the APIs. A narrow FreeType resource-attribute fix also
makes Courier New open correctly; the old archive fails the same actual Tiger
font opens while Monaco/Courier controls remain unchanged. No font bytes are
redistributed. The new browser gate requires both nonzero layout and visible
sample glyphs for all twelve font rows.

All three configurations of `f9c6cfc90` built with matching source, dependencies
and wire contracts. Its immutable artifacts are in `build/candidates/f9c6cfc90/`.
At the user's request it was installed in
`/Users/shg/Applications/TigerBrowser.app` on Tiger, retaining the previous app as
`TigerBrowser.app.previous-20260923-215631-9b63aa04112f`. This is a development
installation with the following known test failures, not a release qualification.
All 74 installed regular bundle files were verified against the local candidate.

`logs/regress/fixes-f9c6cfc90/summary.md` records seven passing checks and three
failures. All four author-color checks and both default-scrollbar checks pass.
The fast file-track test now passes the canvas, selection, audio, unload/reload
and retired-callback checks. In faithful mode, restoring the paused video leaves
black successfully but fails exact canvas frame equality; its cause remains under
investigation. Both font-ink checks show all eleven static rows correctly, while
the dynamically inserted final row has nonzero layout but no visible paint.
The strict pixel gates remain unchanged.

`logs/bench/native-f9c6cfc90/` records **809 distinct full-window frames in 30
seconds (26.97 FPS)** in fast mode and **453 (15.10 FPS)** in faithful mode. Both
remain below the 29 FPS gate. Fast mode's maximum gap is 93.18 ms; faithful mode's
is 128.78 ms. The bootstrap correction restores continuous fast presentation;
the owned-buffer pool improves faithful presentation from 12.87 FPS. These are
local 1280×720 window-paint measurements, not YouTube streaming or physical scanout.
The [measured performance note](native-video-f9c6cfc90.md) identifies Cocoa backing
store synchronization as the dominant sampled fast-path wait. Direct GPU
presentation remains the next major performance change.

WebKitTestRunner's UI and WEB targets compile in `WebKit-wktr-integration`.
Its first functional test exposed Tiger's legacy `realpath(path, NULL)` behavior:
the i386 implementation crashed, and native legacy realpath also accepted missing
final paths on both ABIs. The compatibility wrapper now uses native canonicalization
and public `stat` validation. All 23 target checks pass on each ABI. Both runner
halves rebuilt successfully at `098406951` with the repaired dependency. The
next runtime attempt exposed test-runner GPU preferences requesting an unstaged
helper; software defaults avoid that failure. Reusing upstream Cocoa's bounded
NSRunLoop pumping resolves the subsequent storage-cleanup wait. The next runtime
gate exposed a missing opt-in to JSC testing configuration in the Tiger launcher
and WEB64 argument parser; the isolated correction is being rebuilt with a built
artifact check for the parser switch. The real JavaScript text,
consecutive-test/reset and HTTP-cookie gates remain pending. Successful builds
alone do not establish a functioning conformance runner.

Pending IME work remains isolated. It has fixed-width range sentinels, ordered
synchronous queries, asynchronous spelling cancellation and a verified native
persistent Learn Word path. Composition state, blur/navigation cleanup and full
input-method keyboard-event propagation still need integration and validation.

### Delivery milestones and estimate

Keep the modern engine and reuse native/older platform implementations at their
interfaces. Replacing the engine with an old whole-browser fork would make modern
web compatibility another major project. New code should be limited to missing
platform services, safe cross-process integration and demonstrated defects.

The practical acceptance milestone is usable X/Twitter, NYT and Verge browsing,
plus sustained YouTube 720p playback near 30 FPS, with working controls and native
editing. A page-load screenshot is only an initial check. Acceptance also needs
logged-in sessions where applicable, long scrolling, navigation/back, network
recovery, repeated seeks/quality changes, audio/video sync and memory stability.

The integrated FFmpeg backend currently advertises MP4/QuickTime/M4V and
AAC/MP3, with H.264/AAC/MP3 codec strings. WebM/VP9/AV1/Opus are not yet exposed
through this backend. Broader format support needs integration and validation;
available decoder libraries alone do not establish browser support. The current
MediaCapabilities smoothness estimate also needs measured width/bitrate/profile
and presentation constraints rather than a height/frame-rate-only heuristic.

Native editing acceptance must include selection synchronization, cancellable
`beforeinput`, keyboard/composition event ordering and script-controlled inputs.
The current hosted-field bridge carries text/input/change/focus, so Cocoa visual
fidelity does not establish all DOM editing semantics. DOM clipboard integration
and partial clipping/transforms remain separate gaps. Faithful composition also
still flattens preserve-3d and omits filter/backdrop-filter application. These are
correctness work alongside direct presentation.

Several complete API families remain disabled in the integrated configuration:
WebAudio, WebGL, WebRTC, WebCodecs, encrypted media and OffscreenCanvas. Pending
features and recovery/memory-pressure worktrees are isolated WIP. The estimates
below cover the named browsing/playback milestone; they do not promise every
modern web feature. Sustained-use gates must include constrained memory, process
restart and profile reuse before daily-driver claims.

The provisional planning range is **2–4 months of focused engineering** for that
functional milestone and **6–12 months** for a dependable daily driver, assuming
sustained work and regular access to the build and target machines. This is not a
measured delivery forecast. Revise it after integrated direct presentation, the
complete YouTube streaming path and representative site sessions pass. Hardware
codec limits and changing live-site requirements remain significant uncertainty.
