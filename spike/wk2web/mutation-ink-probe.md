# Delayed mutation ink probe

This diagnostic is separate from `font-ink.html`; it must not replace or weaken that regression.

`mutation-ink-probe.html` mutates six text nodes at one second, logs one animation-frame callback at two seconds, repaints one sample background at six seconds, then changes the body background at ten seconds. It compares empty/replaced absolute text, an explicitly sized empty block, normal-flow text, a sans-serif span, and text without an explicit geometry query.

Archived candidate `f9c6cfc90` was run in fast mode with a fresh isolated profile on 2026-09-23. Evidence is in `logs/probes/20260923-mutation-ink-f9c6cfc90/`:

- `app.log`: the text mutations produced one update with six dirty rectangles, bounds `670x288+252+118`, before the animation-frame callback. The local-background update was `102x13+252+118`; the body-background update was `960x648+0+0`.
- `text.png`: all six mutated rows already contain the correct text. The static row plus six dynamic samples contain respectively 178, 178, 178, 178, 178, 196, and 178 dark pixels in their text interiors (screen page origin 80,154).
- `local.png`, `body.png`, `final.png`: subsequent local/full repaint phases also show all text.
- `stage.log`: exact archived UI/WEB/GPU artifact verification and unique leased run path.

This result demonstrates a working scheduler, dirty-region transfer, and Cairo text paint for these cases. It **does not clear the original isolated-mutation failure**: changing the normal-flow block in the same task can trigger painting that flushes deferred layer-position work for the absolute blocks.

The leading source hypothesis is a missing schedule when synchronous geometry queries defer layer-position updates. `Element::boundingClientRect()` requests `CanDeferUpdateLayerPositions`; `LocalFrameViewLayoutContext::performLayout()` cancels the pending layout timer; `didLayout()` records layer-position work without scheduling a rendering update when it defers the flush. Absolute self-painting layers rely on that flush to produce their repaint rectangles. A separate normal-flow repaint or video frame can mask the gap.

The isolated discriminator preserves the original single mutation and geometry reads, then requests an animation frame at four seconds without any style or DOM change in that callback. It captures before and after the callback. An empty rendering update making the text appear distinguishes missing scheduling from font rasterization failure.

`mutation-font-raf-probe.html` preserves the original fixture and appends that empty callback. The first attempted run refused an open installed user browser and exited without launching anything; `logs/probes/20260923-font-raf-f9c6cfc90/stage.log` records that refusal. A later idle-box run succeeded: `logs/probes/20260923-223359-font-raf-f9c6cfc90/` contains `before.png`, `after.png`, and `app.log`. The dynamic text was absent before the callback and appeared afterward, with exactly a `102x13+242+550` dirty rectangle and no intervening DOM/style mutation.

The engine fix is `a799eda86`: Tiger's deferred `didLayout()` path requests a coalesced `LayerFlush` rendering update through the existing page scheduler. It does not synchronously repaint or add a periodic repaint timer. The committed `Tools/Scripts/tests/test_tiger_deferred_layer_positions.py` extracts the production methods and checks immediate/deferred layout, merged repaint work, no reentry, detached frames and a disabled-fix negative control.

The matched `a799eda86` candidate passed the unmodified original `fontink` and `ffontink` regressions on Tiger: all 12 rows have actual text pixels, including the isolated dynamic row. `logs/regress/repaint-a799eda86/summary.md` records both modes and the accompanying video lifecycle/file-track gates (4 passed, 0 failed).
