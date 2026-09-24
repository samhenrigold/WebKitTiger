# Native Cocoa above an imported Tiger GPU surface

This is an isolated feasibility probe, derived from the committed
`7566cf6:spike/direct-present` source and runner. It does not enable an engine
path or change a browser bundle. The runner uses the existing local and remote
Tiger leases, unique run directory, owned process-group cleanup, source/binary
hashes and screenshot collection.

Run `python3 spike/native-surface/run.py` for the intended ordering. Run
`python3 spike/native-surface/run.py --order above` for the deliberate control
experiment in which the drawable should cover the native controls. `--build-only`
does not contact Tiger. `python3 spike/native-surface/test_run.py -v` checks API
log validation, screenshot criteria and negative cases locally.

The 32-bit Cocoa parent creates a buffered window with `setOpaque:NO` and a clear
background. Its custom content view is nonopaque and clears damaged backing
pixels with `NSCompositeCopy`. Real subviews draw normally above that transparent
page area: an editable NSTextField, an NSScroller, and a second NSTextField whose
document is clipped by NSClipView. A magenta rectangle drawn into the Cocoa
backing store makes its ordering unambiguous.

The separate renderer imports the parent's owned surface using the previously
verified SecondaryOwner protocol. It selects the public CGL surface-order
parameter, draws 180 changing frames, and retains the final picture across the
screenshot. This finite workload establishes visible GPU output; it is not a
video presentation or throughput benchmark. The parent uses Tiger's public
`TransformProcessType` and `SetFrontProcess` to become a foreground app, then
uses the real field editor to insert text and select the first six characters.
The log requires an actual key window and field editor. This tests native edit
methods and visible selection, not physical keyboard shortcut routing or DOM IPC.

The screenshot checker requires the GPU checker pattern, native magenta marker,
white field interiors with text, blue selection, native scroller, and clipping
at the NSClipView's right edge. The above-window experiment must hide those native
pixels while showing the GPU pattern. Blank, absent and cropped screenshots fail.
The logged window origin is used instead of assuming the browser's coordinates.

## Target result

The below-window run `20260923-205047-native-surface-below-6ec8e10c68` passed
all API and screenshot checks on the Tiger machine's NVIDIA GeForce 8600M GT.
The log records surface-order readback `-1`, a key window, the real field editor,
successful text replacement, selection `(0,6)`, and successful drawable/surface
teardown. The screenshot shows the Cocoa focus ring, selected text, scroller,
magenta native view and partially clipped field above the imported GPU image.
Every pixel sampled beyond the clip's right edge belongs to the GPU pattern.

The opposite-order run `20260923-205223-native-surface-above-e69931e7c7` also
completed successfully. With public order readback `1`, its screenshot has GPU
pixels where the native magenta rectangle and both text fields were, and the
scroller is covered. Cocoa still reports the same key field editor and selection.
This control demonstrates that API state alone is insufficient: the below-window
ordering is what makes the native drawing visible.

The evidence directory contains `app.log`, `shot.png`, `result.json` and
`provenance.json`. Source snapshots and executables are local build artifacts.
An earlier launch correctly failed the key-window requirement until the probe
adopted TigerBrowser2's public foreground-process setup. A later functional run
showed the intended native pixels but completed only 239 of the inherited 240
frames before its deadline; that run was not accepted. The final probe uses a
complete 180-frame workload with startup headroom and a longer screenshot hold.

## Primary API and ABI evidence

- Tiger SDK `OpenGL.framework/Headers/CGLTypes.h:124` documents
  `kCGLCPSurfaceOrder = 235`: `1` above the window and `-1` below it.
  `NSOpenGL.h:162` exposes the same public behavior. `OpenGL.h:75` declares
  `CGLSetParameter(ctx, parameter, const long*)`; on this i386 SDK GLint is long.
- Actual Tiger OpenGL binary at
  `sysroot/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL`,
  SHA-256 `fd107933fd9f78fb4c9900f183b3e99508a0b31251f6a63cc2ff84a84047e482`:
  CGLSetParameter's order case calls
  `CGSOrderSurface(connection, window, surface, requestedOrder, -1)` at
  `0x931eb9a0`. CGLSetSurface reapplies the stored order with the same last
  argument at `0x931ecd17`. The public parameter therefore supplies the correct
  window-backing reference; do not substitute the `relative=0` used when ordering
  Xplugin surfaces among sibling surfaces.
- The other concrete route is surface shaping. Tiger SDK `usr/include/Xplugin.h`
  describes `XP_SHAPE`, rectangle lists, and `xp_configure_surface`. In actual
  Tiger libXplugin, `xp_configure_surface` calls
  `CGSShapeSurface(connection, window, surface, CGRectByValue, region)` at
  `0x9a85b25c`. On i386 the rectangle is four 32-bit floats after three 32-bit IDs,
  followed by the region pointer. `_xp_make_cg_region` constructs the region with
  `CGSNewRegionWithRect(const CGRect*, outRegion)` and
  `CGSUnionRegionWithRect(region, const CGRect*, outRegion)`. The copied binary
  and disassembly are `build/handoff/libXplugin-tiger.*`; binary SHA-256 is
  `df419aec66022404fbf5186dca0d3aa9e1010c2e470a344ecdebddb8dbecc7e9`.
  Xplugin's header explicitly reserves that library for X11, so it supplies ABI
  evidence rather than a new runtime dependency. Do not infer the ABI of the
  differently named `CGSSetSurfaceShape` from its symbol alone.
- Public `NSWindow.h:432` supports `addChildWindow:ordered:` and also exposes
  nonopaque windows and ignored mouse events. A child drawable below an opaque
  parent would still be obscured. Moving controls into a separate window above
  the drawable introduces first-responder, key-window and IME ownership changes.
  The below-backing route preserves their existing window and is the smaller
  experiment.

## Browser integration boundaries

The ordinary opaque backing-store paint must not cover the page's direct
drawable. Toolbar and status-bar backing must remain intentionally opaque.
Native controls remain above the GPU scene only when hosting is semantically
valid: arbitrary CSS transforms, opacity, partial DOM clipping and page elements
painted over controls still require correct eligibility or a WebCore fallback.
This probe tests an actual NSClipView, not the engine's pending DOM clip protocol.

Surface creation, geometry, owner connection, destruction and GPU restart need
generation checks and an acknowledged fallback to the current bitmap path.
Window moves/resizes, occlusion, minimize/restore, sheets, menus, focus rings,
IME candidate windows and display changes still need live tests. Making holes
around controls in an above-window surface is less attractive: it must also
preserve the translucent focus-ring edges and the page pixels beneath them.
Neither surface regions nor child windows are enabled by this probe.
