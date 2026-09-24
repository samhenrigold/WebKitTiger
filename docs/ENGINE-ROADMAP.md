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
`tiger-tests`; it is not yet a passing test runner. Do not push inner branches to
the inner `origin`: that remote is the upstream WebKit repository.

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
native-control integration contains targeted fixes and separate author-colored
scrollbar tests; these require their own matched build and device validation.

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
  `6f7da45df`; its device validation is pending.
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
validated before adoption.

WebKitTestRunner now builds against the current rendering engine in the separate
`WebKit-wktr-integration` worktree rather than an older incompatible video-ring
tree. Its WEB test process and network helper build passed; the UI runner and
functional text/reset/HTTP gates are still in progress. Pending IME work also has
fixed-width range sentinels, ordered synchronous queries, explicit asynchronous
spelling cancellation and a verified Tiger-private persistent Learn Word path.
It remains isolated until compilation and Cocoa composition-state checks pass.
