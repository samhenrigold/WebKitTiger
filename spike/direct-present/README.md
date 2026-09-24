# Tiger cross-process presentation experiment

`python3 spike/direct-present/run.py` builds a small i386 Cocoa host and launches
it on Tiger through the browser's local and remote leases. The host creates its
own window and a WindowServer surface. A separately exec'd process creates an
accelerated CGL context and renders into that surface. All descendants remain
inside the stager's owned process group; the host removes its surface afterward.
The run preserves source/binary hashes, build arguments, logs and a screenshot
under `logs/probes/`. It never stages or installs a browser candidate.

GL remains the default. `python3 spike/direct-present/run.py --mode ca` instead
uses the same bundled QuartzCore as `WCSceneCA`. The runner freezes the framework
into the local result directory, records its binary and file hashes, and copies
it under the unique leased remote run. The child logs the loaded framework path;
the result parser rejects a system or unrelated framework. `--build-only` builds
and records provenance without contacting Tiger. Parser checks can be run with
`python3 spike/direct-present/test_run.py`.
Add `--frame-storage pool` in CA mode to reuse at most three private pixel
allocations, exclusively after their previous CGDataProvider release callbacks.
The default `--frame-storage malloc` creates fresh storage for every image.

The protocol is taken from Tiger's own implementation, rather than inferred from
newer macOS behavior:

- The 10.4 SDK's `usr/include/Xplugin.h` documents surface export/import and
  `xp_attach_gl_context` for separate clients.
- Actual Tiger 10.4.11 `/usr/lib/libXplugin.1.dylib`, copied to
  `build/handoff/libXplugin-tiger.dylib`, confirms the private calls. Disassembly is
  preserved beside it. `xp_export_surface` sets the owned window's
  `SecondaryOwner` property to the receiving client's CGS connection ID using a
  signed 32-bit CFNumber, then sets sharing state 2. The exported pair is the
  native window and surface IDs.
- `xp_attach_gl_context` calls `CGLSetSurface` with the receiver's own connection
  ID and that pair. `CGSSetSurfaceBounds` takes a CGRect by value on i386.

This experiment applies that same protocol to a Cocoa-owned window. It only
grants the child access to the window created for the experiment.

## Result on the target

Run `20260923-201847-surface-1c0da599c5` used the NVIDIA GeForce 8600M GT OpenGL
renderer. All surface creation, sharing, attachment and removal calls returned
success. The screenshot confirms visible rendering in the Cocoa window.

240 changing 1280×720 BGRA texture uploads, draws and drawable flushes completed
in 7.972 seconds at a requested 30 Hz. Work cost averaged 5.569 ms, with p95
5.782 ms and a 50.833 ms maximum on startup frame 0. These are completed GL
operations, **not video frame delivery or physical scanout measurements**.
The 30.105 operations/second calculation includes an immediate first frame.

The earlier 300-frame run overlapped the stager's screenshot and reported a
1.382-second stall. The revised workload finishes its 240 timed frames before
the screenshot, then retains the surface for inspection. Do not compare these
timings with a workload that captures the screen during the timed interval.

## CARenderer result

Run `20260923-203028-surface-ca-5bed5f8836` rendered 240 fresh, owned 1280×720
BGRA CGImages through CARenderer into the imported drawable. Each frame allocates
and copies a private pixel buffer, creates a provider-backed CGImage, commits it
to a video layer, renders the **whole** scene, and calls `CGLFlushDrawable`. The
provider frees its bytes after CA releases the image. Full redraws are intentional:
the probe does not assume that swapped drawable buffers preserve prior pixels.

The scene uses WCSceneCA's geometry-flipped host and disabled implicit animations.
A 200×72 opaque magenta sibling at (40,40) stays above the video image. The saved
screenshot confirms that ordering and the image's top-down orientation. This is
CA sibling ordering; it does not test AppKit controls over a WindowServer surface.

| Measurement | Result |
| --- | ---: |
| Elapsed / completed drawable flushes per second | 8.065 s / 29.758 |
| Mean / p95 total work | 33.604 ms / 32.387 ms |
| Maximum work | 788.629 ms, startup frame 0 |
| Mean allocation + copy + CGImage construction | 11.501 ms |
| Mean CA commit + full scene render | 18.212 ms |
| Mean drawable flush + GL error check | 3.882 ms |

The framework binary SHA-256 was
`ae24133c5536c1ba386a812eda25b2391cdbdd3d9312016e7fae14e559c85cb0`.
Runtime `dladdr` resolved to that run's copied framework. Attachment, explicit
`CGLClearDrawable` detachment, and surface removal all returned success.

Measurement finished 8.405 seconds after parent startup, before the 8.5-second
deadline and the runner's 10-second screenshot. The workload stops at that
deadline; the parser rejects fewer than 240 completed frames, late completion,
missing teardown, or a mismatched mode/framework. Timings are **drawable flush
completions, not decoded video delivery or physical scanout**.

This initial result demonstrates full CA compositing on the drawable, with little
steady-frame headroom and substantial cold-start cost. The owned-buffer experiment
below isolates the preparation cost and recovers headroom. An integration must
still include ring acquisition, decoding, IPC and the lifecycle checks below.

The committed evidence is in
`logs/probes/20260923-203028-surface-ca-5bed5f8836/`: `app.log`, `result.json`,
`provenance.json`, and `shot.png`. Probe source snapshots, executables and copied
frameworks remain local build artifacts.

## Provider-owned storage reuse

Two subsequent leased trials used identical scene/image/render behavior and added
per-frame timing for allocation, memcpy, CGImage creation, CA transaction/flush,
CA drawing, and drawable flush. These are CPU-side elapsed call durations; they
can include driver waits and are not GPU-only or thread-CPU counters. Per-frame
lines are written **after** the timed loop. The table explicitly excludes the
first 30 frames from both trials (210 measured steady frames each).

| Stage / work duration | Fresh allocation | Provider-released pool |
| --- | ---: | ---: |
| Allocation/acquisition mean | 0.0264 ms | 0.0026 ms |
| memcpy mean | 11.2662 ms | 2.2389 ms |
| CGImage construction mean | 0.0127 ms | 0.0116 ms |
| CA transaction mean | 0.0786 ms | 0.0809 ms |
| CA full-scene render mean | 14.7869 ms | 14.6044 ms |
| Drawable flush mean | 3.8159 ms | 3.8404 ms |
| Total work mean | 30.0006 ms | 20.8003 ms |
| Total work p95 | 31.257 ms | 22.225 ms |
| Total work maximum | 35.856 ms | 23.015 ms |

The pool made three allocations and reused released slots 237 times. No fallback
allocation was needed; peak occupancy was three, with two providers still holding
slots at the end of measurement. Every frame still received a **fresh CGImage**
over its private copied pixels. The screenshot preserves the same overlay and
orientation. The full 240-frame pool run completed at 30.047 flushes/s, with an
all-frame work p95 of 22.420 ms. Startup frame 0 still cost 775.511 ms; pooling
does not fix cold CA renderer setup.

Most of the 9.2 ms steady-frame saving appears in memcpy, not the malloc call.
This is consistent with avoiding first-write faults/zeroing on fresh destination
pages, but this experiment did not collect page-fault counters to prove that
mechanism. It establishes a useful end-to-end gain in this synthetic scene,
not native-720 browser acceptance or a scanout rate.

`PixelBufferPool.h` uses Tiger's public malloc and pthread APIs. Checkout reserves
a slot under a mutex; only the provider release callback returns it. If all three
slots remain retained, the producer allocates independent fallback storage and
never waits or overwrites live pixels. Closing drops the owner's reference while
outstanding providers retain the pool until their callbacks finish. Host tests
cover retained-byte integrity, exhaustion, cross-thread release, reuse, changed
size, and close with outstanding callbacks:

```sh
clang -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -pthread \
  spike/direct-present/test_pixel_pool.c -o /tmp/tiger-pixel-pool-test
/tmp/tiger-pixel-pool-test
```

The same test passes with `-fsanitize=thread`; nine Python parser checks pass.
Both CA variants and the default GL mode cross-compile. Committed trial evidence
(`app.log`, `result.json`, `provenance.json`, `shot.png`) is under:

- `logs/probes/20260923-204030-surface-ca-malloc-dc42a1c31b/`
- `logs/probes/20260923-204111-surface-ca-pool-94f68ad825/`

## Remaining browser work

This is a feasibility result, not an integrated renderer. Before adoption, the
engine must coordinate surface creation/destruction with its GPU connection,
window movement, resizing, device scale, visibility and process recovery. It
must preserve native controls and menus over the surface, page clipping and
damage, and establish presentation acknowledgements. CA mode measures a synthetic
image layer and opaque sibling, not the engine's complete page composition.
Keep the verified bitmap path as a fallback until those behavior checks pass.
