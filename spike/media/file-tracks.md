# File track metadata and selection

Serve `spike/media` over the existing LAN HTTP server and open `file-tracks.html`.
Use the existing `bbb-480p.mp4`: its successfully decoded video stream is index 0,
audio is index 1, and both carry language `und`. No new media asset is needed.
Allow up to 35 seconds on Tiger, in both normal and `TIGER_FAITHFUL=1` modes.

`FILE TRACKS PASS (API + canvas only)` requires one real track of each kind,
IDs `1`/`2`, language metadata, initial selected/enabled states, an opaque-black
canvas image when video is deselected while paused, and exact restoration of the
same paused canvas image without a seek. It then checks audio enabled setters,
unload empties both lists, reload creates fresh tracks without duplicates, and
retained old track setters cannot hide the new picture or change new track flags.
Inspect `window.fileTracksResult` and `[FILE-TRACKS]` console records for details.
The page fails on missing tracks, black initial output, a stalled load/seek,
tainted canvas, or a failed assertion. A RUN title or HOLD is not a suite PASS.

The canvas check exercises the backend's frame choice; it does not certify that
the window's video ring or compositor displayed the same pixels. For viewport
checks, run these query variants separately and inspect screenshots only after
the corresponding `FILE TRACKS HOLD ...` title:

- `?hold=selected`: pause on the original picture at 3 seconds.
- `?hold=black`: the same paused video must become solid black.
- `?hold=restored`: the picture must match the selected shot again.
- `?hold=stale`: the reloaded picture stays visible after old track mutations.

The video rectangle is page `(16,60)` through `(656,420)`. Compare its upper
interior, such as page `(20,64)` through `(650,350)`, to exclude the paused media
controls. With screenshot page origin `(80,154)`, add those offsets to get image
coordinates. Record the screenshot origin if window positioning changes.

For audible behavior, the final PASS state loops while muted. Click **Toggle
element mute**, then **Toggle enabled audio track** twice, allowing the existing
PCM queue to drain between changes. Disabling must silence playback and enabling
must restore it; the element's mute state must remain independent. API/canvas
PASS does not claim audible output or controls correctness. The implementation
keeps decoding deselected video/disabled audio to preserve the existing clock;
alternate container streams remain unadvertised until stream switching exists.

After these pass, repeat the existing `video720native.html` benchmark with
controls enabled and pointer moved outside the bottom bar. Its real video track
now permits WebKit's four-second idle fade; confirm the bar disappears in the
screenshot and inspect presentation telemetry. Also rerun existing MSE playback
checks: this fix registers only file tracks, leaving SourceBuffer registration
unchanged.

Host checks: `python3 WebKit-faithful/Tools/Scripts/tests/test_tiger_file_tracks.py
-v` extracts the production selection/callback methods, compiles a small fake
environment with ASan/UBSan, and verifies paused masking/restoration, changed
frame dimensions, retired callbacks, audio gain gating, and negative controls.
These do not replace matched Tiger build and runtime checks.
