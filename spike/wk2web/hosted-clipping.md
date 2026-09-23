# Hosted control clipping regression

`hosted-clipping.html` exercises real AppKit controls, so screenshots and input delivery
are the checks; DOM geometry alone cannot detect a leaked NSView. Run both fast and
`TIGER_FAITHFUL=1` modes with a matched UI/web/GPU candidate containing the fix. The normal
stager copies this HTML fixture. All coordinates below are page coordinates at the default
960 × 648 viewport, 100% zoom. No network service or media file is required.

Initial state: only the iframe's top field is visible. The overflow field at (30, 220),
the visibility-hidden field at (350, 80), and the iframe's bottom field at (352, 370)
must have no hosted view. The blue panels model content/video underneath these locations.

1. Click (60, 232), (380, 92), and (380, 382). The title must contain
   `over=0 hidden=1 frameY=0 hits=1,1,1 values=///`.
2. Click the visible iframe field at (380, 242), type `frame`. It must accept input and
   report `values=//frame/`. Its root position is outside the iframe's local viewport
   dimensions, catching the old coordinate mismatch.
3. Click Reveal field at (70, 34), then the field at (60, 102), type `clip`.
   Expect `over=130` and `values=clip//frame/`. Click Hide field at (190, 34), then
   (60, 232). Expect `over=0 hits=2,1,1`; no ghost field may intercept that click.
   Reveal again: the field must retain `clip`.
4. Click Visible at (390, 34), then (380, 92), type `vis`. Click Hidden at (520, 34),
   then (380, 92). Expect `hidden=1 hits=2,2,1 values=clip/vis/frame/`.
5. Click Frame bottom at (390, 184), then its now-visible field at (380, 252), type `lower`.
   Expect `frameY=130 values=clip/vis/frame/lower`. Click Frame top at (520, 184), then
   the frame underlay at (380, 382). Expect `frameY=0 hits=2,2,2` and all values retained.

Allow one second after each scroll/visibility change for a rendering update. AppKit field
clicks use the normal TigerBrowser2 hit-test path; page actions are links so they do not
introduce additional hosted controls. The console logs every relevant input/focus/change
event and the same state as the title (`TIGER_CONSOLE=1`).

A reproducible initial-state/input-delivery run, after the candidate has been built and
selected through `UIDIR`, `WEBDIR`, and `GPUDIR`:

```sh
APP=TigerBrowser2 \
APP_ENV="TIGER_CONSOLE=1 TIGER_SCRIPT='wait 6; click 60,232; wait 1; click 380,92; wait 1; click 380,382; wait 1; click 380,242; wait 1; type frame; wait 2'" \
  sh spike/wk2web/stage-app.sh file:///Users/shg/wk2/share/hosted-clipping.html 17
```

Repeat with `TIGER_FAITHFUL=1` inside `APP_ENV`. Inspect the returned log for
`hits=1,1,1 values=//frame/` and the returned screenshot for only the iframe's top field.
Complete steps 3–5 before integration; a screenshot-only pass is insufficient.

This bounded fix suppresses hidden and wholly clipped controls and compares frame/root
viewports in the same coordinate space. Partial clipping (including a child subrect inside
a partially clipped iframe), transforms, opacity and page paint order still require a
hosted-view clipping/eligibility design with a drawn-control fallback. No runtime result
is claimed by this fixture until the matched candidate is built and these checks run.
