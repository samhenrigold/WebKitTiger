# spike/CAHost

Core Animation on Mac OS X 10.4.11, using the Apple TV 3.0.2 QuartzCore
(`atv/extracted/3.0.2`). Every program here runs on the box; the design these
feed is `logs/ca-hosting-design.md`.

`rebundle.sh` builds the private framework the whole directory links against:
it copies the Apple TV QuartzCore, drops in the Leopard headers, renames the
CoreImage classes (`decollide.py`, see below) and sets a private install name.
Run it once before `make`.

| Target | What it proves |
|---|---|
| `make` (CAHost) | A CARenderer inside an NSOpenGLView composites a tiled 620x4000 page. Tiling, scrolling by the viewport's bounds origin, hit testing, resize. |
| `make CAVideo` | QuickTime's OpenGL visual context feeding GPU textures into the same GL context, with CA layers composited over the video. |
| `make CAVideoNoCA` | The same without Core Animation linked. Isolates the CoreImage class collision. |
| `make CAWidgets` | **Superseded.** Real Aqua controls as live NSViews over the page. |
| `make CASceneTest` | The scene applier: a WC-shaped layer-tree delta applied to real CALayers, with tile pixels from an mmap'd region. |

## CAWidgets is superseded

It hosted live on-screen AppKit controls above the GL surface. That cannot be
right in the end: page content that must draw *over* a control has nowhere to go,
because an NSView is always above the layer tree it sits on. WebKit already
solves this by serializing native controls as `ControlPart` / `ControlStyle`
value types and drawing them with real `NSCell`s wherever the pixels are, so the
32-bit render process will draw them into the page's own backing store.

Its measurements still stand and are worth keeping: scrolling 40 live controls
costs 10.4 ms/frame, and the AppKit quirks it found (surface order, the
transparent hole, invalidation of vacated rects, the full-keyboard-access gate)
apply to any AppKit drawing over a GL surface.

## decollide.py

The Apple TV QuartzCore carries Core Image and Core Animation in one binary and
duplicates 207 `CI*` class names from Tiger's own QuartzCore. Once it is loaded,
`+[CIFilter filterWithName:]` stops resolving no matter which image loaded first,
and any QuickTime playback in the process throws. `decollide.py` renames every
whole `CI*` string in the private copy to `ZI*`: same length, so it is an
in-place byte patch, and self-consistent because every reference inside that one
binary is patched too. `rebundle.sh` runs it.
