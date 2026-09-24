# Native 720p presentation evidence, f9c6cfc90

The matched candidate `f9c6cfc9078a9a9ebfbcfc44a52bb742e6411e3e` achieves
26.97 distinct full-video window paints/s in fast mode and 15.10/s in faithful
mode. Neither meets the 29/s acceptance threshold. These are certified 1280×720
source and display regions in a controlled fixture, not physical scanout counts
or live YouTube streaming evidence.

Evidence: `logs/bench/native-f9c6cfc90/{n720,fn720}.log`, `.video-paints.json`,
`.png`, and `candidate.json`. Compare faithful mode with
`logs/bench/native-f69874944/fn720.{log,video-paints.json}`. Both runs use the same
immutable frame parser SHA-256 `6033377c62ccb91ad3f04a5da1a19f16a38056d8af311c8ff3d9b9081b7d4f36`,
8 seconds of warmup and a 30-second measurement window. Source/display dimensions
are 1280×720. No device or engine changes were made during this analysis.

| Result | Previous faithful f69874944 | Current faithful f9c6cfc90 | Current fast f9c6cfc90 |
| --- | ---: | ---: | ---: |
| Distinct full-video window paints / 30 s | 386 | 453 | 809 |
| Distinct paints/s | 12.87 | 15.10 | 26.97 |
| Paint gap p95, ms | 94.723 | 82.245 | 67.342 |
| Maximum gap including interval edges, ms | 188.721 | 128.779 | 93.179 |
| Duplicate full paints | 0 | 0 | 60 |
| Sequence skips between full paints | 510 | 445 | 90 |
| Uncertified partial-paint attempts | 0 | 0 | 53 |

Faithful throughput increased 17.4%; the bootstrap notification fix removes the
previous fast-mode stall. Decoder probes remain around 30 fps with zero reported
dropped frames in both modes. Decoder cadence therefore does not explain the
remaining presentation deficit.

## Fast mode: drawing into Cocoa's window backing is the main wait

`TigerWebView::drawVideoSinks` logs `receive` only after the owned ring snapshot
and CGImage construction. It then clips/transforms the CGContext, calls
`CGContextDrawImage`, restores state and logs the window-paint certification.
Pairing those markers by ring and sequence during the exact certified interval
gives 869 full-paint spans, mean **29.19 ms**, median **29.757 ms**, p95
**32.408 ms**, maximum **43.025 ms**. Their total elapsed time is **25.367 s**.
The 53 partial-paint spans average **26.80 ms**, totaling another **1.420 s**.
These spans include certification overhead and are elapsed calls, not CPU-only
or GPU timer measurements. They exclude the snapshot/allocation work before the
receive marker.

Of 116 steady UI main-thread samples in that interval, **96** include the same
wait chain: `mach_msg_trap` → `_CGSSynchronizeWindowBackingStore` →
`_CGSLockWindow` → `CGSDeviceLock` → `CGContextDrawImage` →
`TigerWebView::drawVideoSinks`. The UI binary hash matches the candidate record;
its addresses `0x55387`/`0x54e33` resolve to `drawVideoSinks`/`paint`. Actual Tiger
CoreGraphics resolves `0x90399836`, `0x9032bb49`, `0x9033b5de`, `0x90336ac1` to the
four CoreGraphics entries above. Several other samples are in
`CGSConvertARGB8888toRGBA8888`. UI cumulative CPU is about 21% of one core, despite
spending most elapsed drawing time in this backing-store synchronization.

The 43 receive replacements are accepted ring snapshots replaced before a full
certified paint; **receive is not an IPC-arrival timestamp** in this path. The
90 sequence skips likewise must not be labeled decoder drops. Duplicate and
partial paints still cost substantial drawing time, but skipping them requires
preserving damage/exposure and native control behavior.

## Faithful mode: reuse helps preparation; readback and copies remain expensive

The following costs are means from existing probes over launch seconds 10–40
(the nearby steady 30-second interval, not a new acceptance gate). Timing phases
can include driver waits; they are not additive CPU counters across processes.

| Existing probe, ms per operation | Previous faithful | Current faithful |
| --- | ---: | ---: |
| GPU layer application: video preparation | 23.179 | 4.780 |
| GPU layer application: total | 23.328 | 4.909 |
| CARenderer frame phase | 23.229 | 28.410 |
| GL pixel readback | 10.104 | 12.592 |
| CPU row reversal | 2.173 | 2.398 |
| GPU frame total, excluding prior layer application | 35.963 | 43.891 |
| WEB measured GPU round trip | 62.976 | 53.024 |
| UI backing-store row copy | 9.996 | 9.991 |
| UI backing-store draw | 4.322 | 4.422 |

Current video-preparation median is **3.31 ms**, versus **23.37 ms** previously;
this is consistent with provider-release-gated reuse removing fresh-buffer
preparation overhead. The snapshot copy itself is still required for ownership.
The aggregate browser gain is smaller because current rendering/readback costs
are higher. Their exact cause is not isolated by these logs; changes in
throughput, contention and driver waits remain possible explanations.

Current CARenderer render-phase p95 is **41.82 ms**; readback p95 is **21.23 ms**.
After layer application, `WCSceneCA::render` still renders to a pbuffer, calls
`glReadPixels` for 3,686,400 bytes, reverses rows, and sends a shared bitmap. The UI
then copies that bitmap before drawing it. Bitmap-cache allocation cost itself
is effectively zero in these steady probes. WEB main-thread sampling is mostly
waiting, so optimizing page JavaScript is not the priority for this fixture.

## Next bounded experiment

The highest-value next implementation is diagnostic direct presentation of the
existing complete CA scene into the UI-owned imported surface, with generation
checks, geometry/resize/teardown acknowledgement, native-widget eligibility and
the current bitmap path as fallback. That route can remove faithful readback,
row reversal and UI copy/draw, and avoid fast mode's repeated CoreGraphics window
backing synchronization. Keep complete scene/control composition; a bare video
overlay would not establish correctness.

Standalone direct-CA pool results (`spike/direct-present/README.md`) establish
feasibility on this device. They do not guarantee that the real browser's CA tree
will fit 33.3 ms. Measure the browser path separately, including initial-frame
cost, retained-provider lifetime, source pixel size, distinct-frame gaps,
controls/clipping and GPU restart. Retain the current strict native-720 gate.

## Benchmark exit-status interpretation

These historical runs wrote a combined `.exit-status`: their value 1 records
failed performance acceptance, while the root run supervisor confirmed both
staging invocations completed. The original console message attributing that
value to `stage-app.sh` was misleading. New runs record `.stage-exit-status` and
`.gate-exit-status` separately; `.exit-status` remains the overall command result.
Rebuilt reports label historical nonzero combined statuses as staging unknown,
even when a failed video gate is available. Existing logs and exit records are
not rewritten to manufacture missing provenance.
