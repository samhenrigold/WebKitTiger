# TigerBrowser

The 32-bit UI-process app shell for the WebKit2-shaped split (see
`NOTES.md` "DIRECTION SET BY THE USER" and "WEBKIT2 SPLIT SURVEY"). It used
to host a classic WebKit1 `WebView`; **2026-09-20 it was merged with
`spike/CAHost`'s Core Animation compositor host** and now hosts a
`TigerPageView` (CARenderer over an `NSOpenGLView`, same phase-2 tiled
CALayer page as CAHost) instead. No real web content and no 64-bit content
process are wired up yet — the page view paints a stub placeholder (a
checkerboard tile grid + a banner showing the current URL) so the window's
scrolling, resizing, hit-testing, menus and keyboard access can all be
exercised and built on before the content process exists.

## Structure

One file, `TigerBrowser.m`, no nib, MRR, fragile ObjC runtime. Classes, top
to bottom in the file:

- **`checkFrameworksLoaded()`** — a plain C function, not a class. Walks
  `_dyld_image_count()`/`_dyld_get_image_name()` at launch and aborts loudly
  (`NSRunAlertPanel` + `exit(1)`) if the bundled, decollided
  `Contents/Frameworks/QuartzCore.framework` isn't among the loaded images.
  Called first thing in `main()`, before any window is built.
- **`TigerPageView : NSOpenGLView <NSTextInput>`** — the compositor host.
  Owns the `CARenderer`, the `viewport`/`page` `CALayer` pair (`viewport` is
  `masksToBounds` + `geometryFlipped`, matching CAHost), a manual grid of
  256px tile `CALayer`s (`buildTileGrid`/`updateTiles`, `TILE_SIZE`/
  `PAGE_WIDTH`/`PAGE_HEIGHT` `#define`s at the top of the file), the
  `-drawLayer:inContext:` delegate that calls `paintStubPage()`, scrolling
  (`-setScrollY:`, `-scrollerAction:`, `-scrollWheel:`), hit-testing
  (`-mouseDown:` logs view point → page point → hit layer name), and the
  Edit-menu responder-chain actions (`-cut:`/`-copy:`/`-paste:`/
  `-selectAll:`/`-undo:`/`-redo:`, logging via `-logEditCommand:` — no real
  text model exists yet). Also implements the full `NSTextInput` protocol
  (`-keyDown:` → `-interpretKeyEvents:`, `-doCommandBySelector:`,
  `-insertText:`, `-setMarkedText:selectedRange:`, `-unmarkText`,
  `-hasMarkedText`, `-markedRange`, `-selectedRange`,
  `-firstRectForCharacterRange:`, `-characterIndexForPoint:`,
  `-conversationIdentifier`, `-validAttributesForMarkedText`,
  `-attributedSubstringFromRange:`) — see "Text input spike" below for the
  design and findings. `-startTextInputSelfTest`/`-fireNextSelfTestEvent:`
  drive a scripted sequence of synthetic key events for exercising it
  without interactive input.
- **`TigerFindBar : NSView`** — a small view (label + `NSTextField` + Done
  button) that `TigerBrowserController` shows/hides for Cmd-F. Its field's
  action (`-performFind:` on the controller) just logs the search text.
- **`TigerBrowserController`** — owns the window and all the chrome: the
  toolbar row (back/forward/reload buttons, address field), the find bar,
  the status field, the `TigerPageView` + its `NSScroller` sibling (an
  `NSOpenGLView`'s surface composites over whatever's under it, so per CAHost
  the scroller can't overlap it — it's a separate view docked to the right
  edge), and a simple in-memory `_history`/`_historyIndex` array so
  back/forward/reload have real (if stub) behavior to drive. Also owns the
  View-menu stub actions (`-zoomActualSize:`/`-zoomIn:`/`-zoomOut:`, logged
  only). Sets up the full-keyboard-access tab chain explicitly
  (`-setNextKeyView:` across back → forward → reload → address field → page
  view → scroller → back), since Tiger's `NSWindow` doesn't infer one from
  view geometry.
- **`TigerBrowserAppDelegate`** — thin: builds the controller, forwards
  `openLocation:`/`performFindPanelAction:`/zoom actions from the menu bar
  to it.
- **`main()`** — `NSApplication` setup, the frameworks check, then builds
  the menu bar by hand: File (Open Location, Cmd-L), Edit (Undo/Redo/Cut/
  Copy/Paste/Select All, all target-`nil` so they dispatch through the
  responder chain to `TigerPageView`, plus Find..., Cmd-F), View (Actual
  Size/Zoom In/Zoom Out).

Shared helpers near the top of the file (`makeColor`, `makeCheckerImage`,
`paintStubPage`) are adapted directly from `spike/CAHost/CAHost.m`'s phase-2
code — same technique, trimmed of CAHost's video/animation/auto-scroll
benchmark extras that don't belong in the shell.

## Files

- `TigerBrowser.m` — the whole app.
- `Makefile` — builds `build/TigerBrowser.app`. Depends on
  `spike/CAHost/Frameworks/QuartzCore.framework` (runs CAHost's
  `rebundle.sh` if it isn't already built) and copies it into
  `Contents/Frameworks/`, matching CAHost's install-name convention
  (`@executable_path/../Frameworks/QuartzCore.framework/...`). Links
  `-framework Cocoa -framework OpenGL -framework ApplicationServices
  -framework QuartzCore` (from the bundled framework's `-F` search path) —
  no more `-framework WebKit`.
- `Info.plist` — bundle metadata, `LSMinimumSystemVersion` 10.4, unchanged.
- `testpages/` — left over from the WebView era (see "Archived" below); not
  used by the current shell, kept for whenever real content loading returns.
- `textinput-selftest.log` — full transcript from a run of the text-input
  self-test on the box (`TIGERBROWSER_TEXTINPUT_TEST=1`); see "Text input
  spike" below.

## Build

```
cd spike/TigerBrowser
make
```

`tiger-otool -L` on the built binary confirms the private install name:

```
@executable_path/../Frameworks/QuartzCore.framework/Versions/A/QuartzCore (compatibility version 1.2.0, current version 1.6.0)
```

## Running on the box

```
scp -O -r build/TigerBrowser.app tiger:/Users/shg/
ssh tiger '/Users/shg/TigerBrowser.app/Contents/MacOS/TigerBrowser \
    "https://example.com/test-page" > /tmp/tb.log 2>&1 & sleep 3; screencapture -x /tmp/tb.png'
```

## Screenshot

`screenshot-shell.png` (2026-09-20, on the 10.4.11 box): toolbar with back/
forward/reload buttons and the loaded URL in the address field, window title
following the URL, the `TigerPageView` showing the checkerboard tile grid
with the blue URL banner at the top of the page, the `NSScroller` sibling on
the right, and the status field at the bottom reading
`Loaded (stub): https://example.com/test-page`. (The crash-report dialog
visible in the screenshot is an unrelated teammate's `CAVideo` test running
concurrently on the same shared box — not TigerBrowser.)

## What works

- Frameworks-loaded check passes: the bundled, decollided QuartzCore is
  correctly bound (confirmed via the `_dyld_get_image_name` dump this check
  logs). Tiger's *system* QuartzCore is also loaded in the same process —
  apparently a transitive AppKit dependency, not something this app links
  directly — but since `decollide.py` already renamed the private copy's
  `CI*` Core Image class names to `ZI*`, the two coexisting doesn't collide
  (this app never touches Core Image/QuickTime anyway).
- CARenderer hosts the tiled page exactly as in CAHost phase 2: 48 tiles
  (4x12 at 256px) painted on demand via `-drawLayer:inContext:`, correct
  `geometryFlipped` top-left page coordinates, checkerboard + URL banner
  render correctly on screen.
- Toolbar, address bar (loads on Enter), status bar, window title tracking,
  back/forward/reload against the in-memory history stack, Cmd-L to focus
  the address field, Cmd-F to open/close the find bar, Edit/View menus all
  built and wired.
- No crashes, no missing selectors, no fragile-ABI issues merging CAHost's
  CA-hosting code into the browser-chrome shell.

## Known rough edges

- Interactive verification of scrolling/hit-testing via UI-scripted clicks
  (`osascript`/System Events) was unreliable on the shared Tiger box during
  this session — a teammate's concurrently-running `CAWidgets`/`CAVideo`
  test windows kept stealing frontmost/key-window status between my
  `activate` and `click` calls, and stderr redirected to a file on the box
  is fully buffered (not line-buffered), so `-mouseDown:`'s hit-test log
  didn't appear until process exit. The scrolling/hit-testing code itself is
  the same method bodies as CAHost's own already-validated implementation
  (`-setScrollY:`, `-scrollerAction:`, `-mouseDown:`), not new code, so this
  is a test-environment contention issue on a shared box, not a sign the
  functionality doesn't work — but it wasn't independently re-screenshotted
  mid-scroll in this session. Worth a clean re-verification pass once the
  box isn't shared with concurrent GUI tests.
- Back/forward/reload only manipulate the address bar, window title and page
  banner text (via the stub history array) — there's no real navigation or
  content yet, by design (that's the 64-bit content process's job later).

## Text input spike (2026-09-20, logs/textinput-plan.md)

`TigerPageView` now adopts Tiger's `NSTextInput` informal protocol (declared
in `NSInputManager.h`, not `NSTextInputClient` — that's 10.6+) and logs
every call instead of building the `Vector<KeypressCommand>`/sending IPC the
real UI process eventually will. Logging goes to a real file opened with
`setvbuf(..., _IOLBF, 0)` (`tiLogOpen()`/`tiLog()` near the top of the file,
default path `/tmp/tigerbrowser-textinput.log`, override with
`TIGERBROWSER_TEXTINPUT_LOG`) — stderr piped over ssh on this box is fully
block-buffered, which silently dropped log lines in an earlier spike (the
scrolling section above); a dedicated line-buffered file sidesteps that
entirely.

**Two-tier EditorState answer, implementing the plan's §3a recommendation
(2026-09-20 update, superseding the single-cache description this section
originally had).** `TigerPageView` keeps two copies of a tiny fake document
(`selectedRange`, `markedRange`, a document string): `_pending*` (what the
current keydown's `insertText:`/`setMarkedText:`/`unmarkText` calls have
produced so far) and `_applied*` (the last state actually confirmed by the
simulated content-process round trip). A `BOOL _inInterpretKeyEvents`, set
for exactly the duration of `-interpretKeyEvents:` inside `-keyDown:`, picks
which tier the six query methods (`hasMarkedText`/`markedRange`/
`selectedRange`/`firstRectForCharacterRange:`/`attributedSubstringFromRange:`/
`characterIndexForPoint:`, via the shared `-tiCurrentSelectedRange`/
`-tiCurrentMarkedRange`/`-tiCurrentDocumentText` accessors) read from:

- **While `_inInterpretKeyEvents` is set** (a query from inside the IME's own
  `doCommandBySelector:`/`insertText:`/`setMarkedText:` callback, verifying
  its own just-issued edit before returning): answers from **pending** —
  same call stack, nothing has been dispatched anywhere yet, so this is
  exactly accurate, not just "less stale."
- **Otherwise**: answers from **applied** — the last state confirmed by the
  simulated round trip (`performSelector:withObject:afterDelay:0.05`,
  standing in for the real async IPC reply), possibly stale by up to one
  round trip. `doCommandBySelector:` deliberately never mutates either tier
  (per the task: "record... do not execute" — the real content process's
  `WebCore::Editor` would own that).
- **`applied` also updates independent of any keydown**:
  `-simulateNonKeyboardSelectionChange:` (a repeating timer during the
  self-test) moves `_appliedSelectedRange` directly, standing in for the
  real UI process's proactive `EditorState` push landing for reasons that
  have nothing to do with typing — an in-page focus jump being the
  canonical example flagged in §3a.

This is a materially different design from the plan's original single-cache
sketch: the in-`interpretKeyEvents:` case needs no staleness tolerance at
all, because on Tiger's fully synchronous `NSInputManager` model nothing is
ever sent anywhere until `-interpretKeyEvents:` returns (§3a's finding that
this makes Tiger's version of upstream's "IME polls immediately after its
own edit" race trivially solvable, unlike upstream's own async
`NSTextInputContext` model, which genuinely can't avoid it).

**No way to type over ssh**, so `TigerPageView startTextInputSelfTest`
(gated behind `TIGERBROWSER_TEXTINPUT_TEST=1`, fires 1.5s after the window
appears) drives a scripted sequence of synthetic `NSEvent`s built with
`+[NSEvent keyEventWithType:...]` straight into `-keyDown:`, in-process — no
`osascript`/System Events UI-scripting and no window-focus dependency,
avoiding the shared-box contention noted above entirely. Full transcript:
`textinput-selftest.log` (checked in). Screenshot: `screenshot-textinput.png`.

### Command vocabulary observed vs. the plan's expectations

Matches the plan closely, once the synthetic events were built correctly
(see "Tiger quirks" below):

| Input | `NSTextInput` call(s) |
|---|---|
| Plain character ("H", "i") | `insertText:` with a plain `NSString` |
| Return | `doCommandBySelector: insertNewline:` |
| Tab | `doCommandBySelector: insertTab:` |
| Escape | `doCommandBySelector: cancelOperation:` |
| Shift-Left/Right | `moveLeftAndModifySelection:` / `moveRightAndModifySelection:` |
| Option-Left/Right | `moveWordLeft:` / `moveWordRight:` |
| Cmd-Left/Right | `moveToBeginningOfLine:` / `moveToEndOfLine:` |
| Delete (backspace) | `deleteBackward:` |
| Forward Delete | `deleteForward:` |
| Ctrl-A / Ctrl-E (Emacs) | `moveToBeginningOfParagraph:` / `moveToEndOfParagraph:` |
| Ctrl-K (Emacs) | `deleteToEndOfParagraph:` |
| Ctrl-D (Emacs) | `deleteForward:` — same selector the physical Forward Delete key produces |

Every one of these arrived as `doCommandBySelector:` with the exact
`NSStringFromSelector` names `WebHTMLView.mm`'s existing implementation
already expects per `webkitlegacy-plan.md` §1.4 — nothing unrecognized, no
Tiger-specific renamed selector. `hasMarkedText`/`markedRange`/
`selectedRange`/`firstRectForCharacterRange:` were never called during any
of this — they're IME-candidate-window-driven, not typing-driven, so a
plain US-layout sequence with no live composition session never exercises
them; consistent with the plan's §1b framing that they're an independent
concern from the per-keystroke flow.

### Two-tier answer: self-test assertions (2026-09-20 update)

The self-test now asserts the three behaviors §3a's recommendation implies,
via a non-aborting `tiAssert()` (logs PASS/FAIL, keeps running — a crash on
the first failure would hide every assertion after it). One run: **30/30
passed, 0 failed** (`textinput-selftest.log`):

1. **A query issued from inside `insertText:` sees the just-inserted
   character.** Asserted inside `-insertText:` itself, right after mutating
   `_pending*`: calls `-selectedRange` and checks it matches the range
   `_pending*` was just set to. Example from the log: inserting `'H'`
   advances `selectedRange` from `{44,0}` to `{45,0}`, and the in-callback
   query sees `{45,0}` immediately — not the old `{44,0}`.
2. **A query issued between the keydown returning and the simulated round
   trip landing sees the OLD applied state, and never blocks.** Asserted in
   `-keyDown:` right after `-interpretKeyEvents:` returns (so
   `_inInterpretKeyEvents` is back to `NO`, meaning this query reads
   `applied`): the result must equal the applied state as it was *before*
   this keydown, timed with `-timeIntervalSinceNow` and asserted `< 1ms`
   (observed: consistently ~0.05ms — a local struct read, unsurprisingly
   fast, but confirms the design never does a blocking wait here). Same
   `'H'` example: right after the keydown returns, `selectedRange` still
   reads `{44,0}` (the pre-keystroke value), not `{45,0}`.
3. **A query issued after the simulated round trip lands sees the applied
   state.** Asserted in `-applyPendingEditorState` right after copying
   pending → applied: `selectedRange` now matches the just-applied value.
   Same example: 50ms after the keydown, `selectedRange` reads `{45,0}`.

The non-keyboard selection-change timer also fired throughout the run
(`-simulateNonKeyboardSelectionChange:`, every 0.6s, logged as "simulated
non-keyboard selection change (e.g. in-page focus jump, no keydown
involved)"), confirming `applied` updates correctly outside the keydown path
entirely, interleaved with the scripted typing in the log without disturbing
any of the 30 assertions above.

### Tiger quirks and gotchas found

- **Synthetic `NSEvent`s for arrow/function keys need the real
  Unicode private-use glyph in `characters`, not an empty string.** The
  first pass of this self-test passed `characters:@""` for the arrow keys
  and Forward Delete, and Tiger's `interpretKeyEvents:` responded by calling
  `insertText:""` for *all* of them — Shift/Option/Cmd-arrows included —
  instead of any movement/selection selector, and Cmd-arrows produced no
  callback at all. Fixed by supplying `NSLeftArrowFunctionKey`/
  `NSRightArrowFunctionKey`/`NSDeleteFunctionKey` (the `0xF700`-range
  OpenStep private-use constants from `NSEvent.h`) as the event's
  `characters`/`charactersIgnoringModifiers`, after which every arrow/delete
  combination produced the correct selector (table above). Conclusion:
  `NSInputManager`'s `interpretKeyEvents:` dispatches from the **already-
  resolved** `characters` string (the same string a live keyboard-layout
  translation would have produced before AppKit ever builds the `NSEvent`)
  — it does not itself re-derive a command from `keyCode` + modifier flags.
  A hand-built `NSEvent` has to supply that resolved string explicitly.
- **The dead-key test (Option-E then E, expected to compose é via
  `setMarkedText:`) did not produce marked text.** Both key presses came
  through as plain `insertText:` calls (an empty string for the Option-E
  dead-key event, then `"e"`) — no `setMarkedText:selectedRange:`, no
  composition session. Given the quirk above, this makes sense: a real dead
  key's marked-text behavior comes from the system's keyboard-layout/TSM
  machinery resolving `keyCode 14 + NSAlternateKeyMask` against the current
  input source's dead-key table — something that happens **before** an
  `NSEvent` reaches `-keyDown:` on real hardware, not something
  `interpretKeyEvents:` re-derives from a synthetic event's `keyCode` alone.
  Reproducing genuine dead-key/IME composition would need events built the
  way the team lead's alternate suggestion described — `CGEventPost`/
  `CGEventCreateKeyboardEvent` through the actual HID event tap — which
  goes through the real TSM pipeline but reintroduces the frontmost-window/
  shared-box focus contention this in-process approach was chosen to avoid.
  Flagged, not resolved, in this spike.
- **`NSTextInput`'s `aString` parameter is genuinely polymorphic** — the
  protocol comment says `insertText:`/`setMarkedText:` can receive either an
  `NSString` or `NSAttributedString`; this self-test only ever observed
  plain `NSString` (no live IME session ever ran, matching the point
  above), so the `NSAttributedString` branch in `-tiDescribeString:` is
  exercised by inspection of the protocol contract, not confirmed live in
  this session.
- **`NSMarkedClauseSegmentAttributeName`/`NSTextAlternativesAttributeName`/
  `NSTextInsertionUndoableAttributeName`** (the three attribute constants
  `webkitlegacy-plan.md` §1.4 already flagged as 10.5+-only) are confirmed
  absent from the 10.4u SDK's `NSAttributedString.h` by direct grep, not
  just assumed — `validAttributesForMarkedText` returns only
  `NSUnderlineStyleAttributeName` (10.0+, confirmed present) as a result.
- **No async escape hatch exists anywhere in this protocol** — every one of
  the four query methods is a synchronous return, confirmed directly against
  `NSInputManager.h`'s declarations (`conversationIdentifier` even returns
  bare `long`, not `NSInteger`, since `NSInteger` postdates Tiger). This
  matches the plan's §3 finding exactly and is why the cache design above
  exists at all.

## Native menus spike: `<select>` popup and context menu (2026-09-20)

Implements what `WebPopupMenuProxyMac` (`<select>`) and `WebContextMenuProxyMac`
(right-click) do, driven from a serialized item list the way one would arrive
from a web process. The serialized form is literally plain `NSDictionary`
items (`tiItem()`/`tiSeparatorItem()`/`tiSubmenuItem()` helpers near the top
of the file) — no custom class needed, since a dictionary already *is* the
serialized shape (`title`, `enabled`, `checked`, `separator`, `submenu`,
array order = index).

- **`-showSelectPopupWithItems:selectedIndex:pageRect:`** (`TigerPageView`)
  mirrors `WebPopupMenuProxyMac::populate`/`showPopupMenu`: a real
  `NSPopUpButtonCell` (`initTextCell:pullsDown:NO`, `usesItemFromMenu:NO`,
  `autoenablesItems:NO`), populated item-by-item, `selectItemAtIndex:`, then
  `-performClickWithFrame:inView:` — the public-API equivalent of the
  private `PAL::popUpMenu()` SPI upstream uses, confirmed present in the
  10.4u SDK's `NSPopUpButtonCell.h` alongside `attachPopUpWithFrame:inView:`/
  `dismissPopUp`. This one call does attach+track+dismiss together and
  **returns only once the user picks something or cancels** — all keyboard
  handling (arrows, type-select, Return, Escape) comes from AppKit's own
  native `NSMenu` tracking loop, not anything this file implements. The
  chosen index comes back as the method's return value (stands in for "a
  callback" — the self-test treats it as one, matching how a real UI
  process would receive it as the async reply to a `ShowPopupMenu` message).
- **`-showContextMenuWithItems:atPageRect:`** mirrors `WebContextMenuProxyMac`:
  a real `NSMenu`, built recursively for submenus (`tiBuildMenuRecursive`),
  separators via `+[NSMenuItem separatorItem]`, checked state via
  `-setState:`, shown via `+[NSMenu popUpContextMenu:withEvent:forView:]`
  with a synthesized right-mouse-down event. Since that call returns `void`,
  each leaf item gets a unique tag + a target/action
  (`-contextMenuItemChosen:`) so the chosen item is still recoverable —
  the same mechanism a real `NSMenuItem`'s action provides.

**Keyboard scripting during native tracking.** Both calls above block
synchronously inside AppKit's own event-tracking loop, so a scripted key
sequence has to be posted from a timer scheduled *before* the call, in a
run loop mode that tracking loop still services
(`kCFRunLoopCommonModes` covers `NSEventTrackingRunLoopMode`) —
`-postMenuKeyScript:`/`-fireMenuKeyScriptEvent:`, posting via
`[NSApp postEvent:atStart:NO]`. This drives the self-test through real,
unmodified `NSMenu` keyboard handling rather than reimplementing
arrow/Return/Escape logic by hand. Four scripted runs, each asserted:

| Script | Expected | Result |
|---|---|---|
| `<select>`: Down, type `'D'` (type-select), Return | jumps to "Delta" (index 4) | **PASS** — chose index 4 |
| `<select>`: Down, Escape | cancel leaves selection at the original index (0) | **PASS** — chose index 0 |
| Context menu: Down, Return | selects the top item ("Open Link", tag 0) | **PASS** — tag 0, "Open Link" |
| Context menu: Down, Down, Right (open "Share" submenu), Down, Escape | Escape inside a submenu cancels the whole menu, nothing chosen | **PASS** — no item chosen |

All 34 self-test assertions (30 text-input + 4 menu) passed, 0 failed, in
the same run (`textinput-selftest.log`).

**Verified by screenshot against real native controls.** A genuinely
separate reference `NSPopUpButton` (not built through `tiBuildPopupCell` at
all — `_referencePopup` in `TigerBrowserController`, three plain items) sits
permanently in the toolbar ("Ref A" visible in every screenshot below,
docked top-right) for a side-by-side comparison; `-showReferencePopupForScreenshot`
opens it too, auto-dismissed via a scripted Return, exercised at the end of
the self-test run. Because both the shell's serialized-item popup and the
reference button are built on the exact same `NSPopUpButtonCell`/`NSMenu`
machinery, the two are pixel-identical by construction, not just by visual
inspection — confirmed against the actual screenshots:

- `screenshot-select-popup.png` — the serialized-item `<select>` popup open
  mid-navigation: "Alpha" (the original `selectedIndex`) carries the
  checkmark, "Bravo" is blue-highlighted from the scripted Down arrow,
  "Charlie (disabled)" is greyed out and correctly un-highlightable,
  "Delta" enabled below the separator gap. The reference `NSPopUpButton`
  ("Ref A") is visible closed in the toolbar for comparison — standard Aqua
  popup-button chrome on both.
- `screenshot-context-menu.png` — the context menu open after one Down
  arrow: "Open Link" highlighted, "Share ▸" showing the submenu arrow,
  "Inspect Element", and "Reload" carrying a checkmark from `checked: YES`
  — all real `NSMenuItem` state, not hand-drawn.
  Also shows the `<select>`/right-click page anchors ("<select> stub",
  "right-click stub" boxes) the popups are positioned against.
- `screenshot-context-menu-submenu.png` — same menu with "Share" opened via
  the scripted Right arrow, "Messages" highlighted via a further Down arrow
  — real native submenu tracking, correct indentation/arrow/positioning.

### What Tiger's `NSMenu`/`NSMenuItem`/`NSPopUpButtonCell` model lacks vs. upstream's

Checked directly against the 10.4u SDK headers, not assumed. **Result: very
little is actually missing** — Tiger's Aqua menu API is close to complete
for this item model:

- **Present and used**: separators (`+[NSMenuItem separatorItem]`),
  submenus (`-setSubmenu:`), checked state (`-setState:`/`NSOnState`),
  per-item enable/disable (`-setEnabled:`, `-setAutoenablesItems:NO`),
  `-setIndentationLevel:`, `-setImage:`, `-attributedTitle`/
  `-setAttributedTitle:` (used by `WebPopupMenuProxyMac` for per-item text
  direction/font/language — not exercised by this spike, but confirmed
  present in `NSMenuItem.h`) — all genuinely 10.0-10.4 vintage API, not
  backports or workarounds.
- **The one real, general Aqua constraint** (not new to Tiger — true of
  every Aqua version, Carbon-era through at least Leopard): a menu item's
  reserved left-edge gutter shows *either* the on/off-state checkmark *or*
  a custom `-setImage:`, never both at once — a checked item with a custom
  icon has to draw its own "checked" visual into the image itself. Not
  exercised in this spike (none of the test items combine `checked` with an
  image), flagged since `WebContextMenuProxyMac`'s real item model can
  carry SF Symbols/template images for menu icons that a checkable item
  would lose on any Aqua version, Tiger included.
- **No vibrancy/translucent materials or vector (PDF/SF Symbols) icons** —
  10.10+/10.11+ respectively. Tiger's menus are opaque, fixed Aqua chrome
  only; purely a visual-polish gap, not a functional/item-model one.
- **`WebPopupItem` (the `<select>` type) has no `checked` field at all**,
  matching upstream exactly — HTML `<option>` has no notion of a checkmark;
  "checked" only ever applies to the `ContextMenuItem`/`CheckableAction`
  path. Worth calling out since the task's phrasing listed both field sets
  together — they don't actually share a `checked` concept in the real
  WebKit model either.
- **`NSPopUpButtonCell`'s items are flat, no submenu concept** — matches
  `<select>`/`<optgroup>` having no nested popups either; this is a
  deliberate shape difference between the two proxy types in the real
  WebKit code, preserved here, not a Tiger limitation.

## Archived: the WebView1 era (superseded 2026-09-20)

Before this merge, `TigerBrowser.m` hosted a classic WebKit1 `WebView`
against Tiger's system `WebKit.framework` and could actually load pages.
That work is superseded but the findings remain useful background:

- `screenshot.png`, `screenshot-layout.png`, `screenshot-images.png`,
  `screenshot-form.png` — WebView1 rendering local test pages (CSS layout,
  PNG/JPEG, native Aqua form controls) correctly on Tiger's original 2007
  system WebKit.
- `screenshot-js.png`, `screenshot-js-run{1,2,3}.png` — a 2,000,000-iteration
  JS loop took **5298 ms** on Tiger's original non-JIT WebKit, and **~59 ms**
  (59/62/56 ms across three runs) after the user installed Safari 4.1.3,
  which upgraded `/System/Library/Frameworks/WebKit.framework` to 533.19.4
  (first JIT-capable JavaScriptCore on this box) — roughly a 90x speedup,
  and the number that flips the comparison against the project's
  interpreter-only C-loop jsc build (2.24s on the same loop).
- `screenshot-https.png` — `https://www.apple.com/` fails ("secure
  connection failed") through Tiger's system WebKit both before and after
  the Safari 4.1.3 update; the update evidently doesn't touch whatever TLS
  bits CFNetwork/`NSURLConnection` use underneath. Not relevant to the
  current CA-hosted shell (no networking at all right now), kept as a
  finding for whenever the 64-bit content process's curl-based networking
  lands.
- No missing/renamed `WebFrameLoadDelegate`/`WebPolicyDelegate`/
  `WebUIDelegate` methods were ever hit on Tiger's WebKit.
- `testpages/` is this era's test fixture set; unused by the current shell.
