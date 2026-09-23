# Author-colored scrollbar regression

Run `scrollbar-colors.html` in both fast and faithful modes after rebuilding a matched
UI/web/GPU candidate. The normal stager copies this fixture. Coordinates assume the default
960 × 648 page viewport at 100% zoom, with all scroll positions reset to zero. There are no
network dependencies. This is a visual/input regression; no runtime result is claimed yet.

The fallback deliberately uses simple WebCore-painted tracks/thumbs, without arrow buttons.
Its geometry, hit testing, thumb drag and page steps come from `ScrollbarThemeComposite`.
Automatic colors retain native HITheme artwork and hosted root NSScrollers.

1. Initially the root vertical/horizontal thumbs are purple `#a014b4`, tracks and corner
   gold `#f8dc78`. Sample thumb (952, 10), track (952, 610), and corner (952, 640).
   There must be no Aqua arrow pair or NSScroller covering these colors.
2. The left overflow box has blue `#1644c8` thumbs and peach `#ffcca0` tracks/corner;
   the thin box has green `#008020` thumbs and pale green `#cdfac8` tracks/corner.
   The iframe has red `#c8281e` thumbs and pale blue `#c8dcfa` tracks/corner.
   The middle overflow box must retain Aqua artwork and paired arrows throughout.
3. Click each author-colored vertical track below its thumb: left box (252, 230),
   thin box (754, 230), iframe (252, 470). The corresponding title fields `over`,
   `thin`, and `frame` must report positive vertical scroll positions. The other
   panes and root must stay at zero until separately used. Click the middle box's
   lower arrow at (532, 236); `auto` must scroll by a line.
4. Click Reset scroll (590, 34). Drag the left box's thumb from (252, 95) to (252, 175);
   only `over` must scroll. Reset again, then drag the root thumb from (952, 50) to
   (952, 250). Root vertical scroll must increase, and the colored thumb must follow.
   Reset again and drag the root horizontal thumb from (50, 640) to (250, 640);
   root horizontal scroll must increase.
5. Reset. Click Native auto (200, 34), wait one rendering update: both root scrollbars
   must become Aqua NSScrollers with their normal arrows. Root scrolling must work.
   Click Author colors (70, 34): the NSScrollers must disappear and purple/gold painting
   must return without stale native views capturing input. Repeat this transition twice.
6. Click Other colors (330, 34): root thumbs become red and tracks/corner pale blue
   immediately, without moving or resizing the window. Click Current color (460, 34):
   the thumb must resolve `currentColor` to `#4d168b`; the track/corner must use the
   translucent cyan color. Return to Native auto and verify native artwork again.

Allow one second after each style/scroll change. The title and console log pane scroll
positions; screenshots verify colors and absence of a hosted root NSScroller.

An initial-state plus root-input run, after selecting the matched build through `UIDIR`,
`WEBDIR`, and `GPUDIR`:

```sh
APP=TigerBrowser2 \
APP_ENV="TIGER_CONSOLE=1 TIGER_SCRIPT='wait 6; drag 952,50 952,250; wait 2'" \
  sh spike/wk2web/stage-app.sh file:///Users/shg/wk2/share/scrollbar-colors.html 12
```

Repeat with `TIGER_FAITHFUL=1` inside `APP_ENV`; complete all transition and per-pane checks
before integration. Keep `scrollbars.html` passing to protect automatic-color native geometry.
