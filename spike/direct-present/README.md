# Tiger cross-process presentation experiment

`python3 spike/direct-present/run.py` builds a small i386 Cocoa host and launches
it on Tiger through the browser's local and remote leases. The host creates its
own window and a WindowServer surface. A separately exec'd process creates an
accelerated CGL context and renders into that surface. All descendants remain
inside the stager's owned process group; the host removes its surface afterward.
The run preserves source/binary hashes, build arguments, logs and a screenshot
under `logs/probes/`. It never stages or installs a browser candidate.

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

## Remaining browser work

This is a feasibility result, not an integrated renderer. Before adoption, the
engine must coordinate surface creation/destruction with its GPU connection,
window movement, resizing, device scale, visibility and process recovery. It
must preserve native controls and menus over the surface, page clipping and
damage, and establish presentation acknowledgements. Full CA scene compositing
must be measured on this drawable; this test uploads one synthetic texture.
Keep the verified bitmap path as a fallback until those behavior checks pass.
