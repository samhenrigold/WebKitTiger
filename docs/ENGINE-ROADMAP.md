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
