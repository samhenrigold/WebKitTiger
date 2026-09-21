# TigerBrowser

A small MRR Cocoa app that hosts the classic WebKit1 `WebView` API
(`WebView`/`WebFrame`/`WebFrameLoadDelegate`/`WebPolicyDelegate`/`WebUIDelegate`
from `WebKit/WebKit.h`). This is the browser shell we will later relink
against our own WebKitLegacy build — the API is identical between Tiger's
system WebKit and modern WebKitLegacy. For now it links against the 10.4u
SDK's `/System/Library/Frameworks/WebKit.framework` and runs against Tiger's
own WebKit on the box (originally build 4522/2007; **as of 2026-09-20 the box
has Safari 4.1.3 installed, which upgraded the system WebKit.framework to
533.19.4, build 4533.19.4** — TigerBrowser needed no changes to pick this up,
since it always links the system framework by path).

## Layout

- `TigerBrowser.m` — the whole app (one file, no nib; all UI built by hand
  in `-initWithURLString:`).
- `Makefile` — builds `build/TigerBrowser.app` with `tiger-clang`.
- `Info.plist` — bundle metadata, `LSMinimumSystemVersion` 10.4.
- `testpages/` — five local test pages plus an `index.html` linking them
  (layout/CSS, images (PNG+JPEG), a form, a JS timing loop, and an
  `https://www.apple.com/` link).

## Build

```
cd spike/TigerBrowser
make
```

Produces `build/TigerBrowser.app`. Links `-framework Cocoa -framework WebKit`
against the 10.4u SDK, `-fobjc-runtime=macosx-fragile-10.4`, MRR.

## Features implemented

- Toolbar row: back (`<`), forward (`>`), reload (`R`) buttons, and an
  `NSTextField` address bar that loads its text on Enter (bare host names
  get `http://` prepended).
- Status/progress text field along the bottom, driven by
  `webView:setStatusText:` (WebUIDelegate) and by load-start/load-finish
  (shows "Loading..." / clears on finish/error).
- `WebView` filling the rest of the content view, full autoresizing.
- Window title tracks the page title via `webView:didReceiveTitle:forFrame:`.
- Cmd-L ("Open Location..." in the File menu) focuses and selects the
  address field.
- Loads `about:blank` on launch, or `argv[1]` if one is passed.
- Load lifecycle logged to stderr: `didStartProvisionalLoadForFrame:`,
  `didCommitLoadForFrame:`, `didFinishLoadForFrame:`,
  `didFailProvisionalLoadWithError:forFrame:`, `didFailLoadWithError:forFrame:`.
- `decidePolicyForNavigationAction:` always calls `[listener use]` (no
  interception).
- JS `alert()` → `NSRunAlertPanel` via `webView:runJavaScriptAlertPanelWithMessage:`;
  `confirm()` wired the same way, mapped to OK/Cancel.

## Running on the box

```
scp -O -r build/TigerBrowser.app tiger:/Users/shg/
scp -O -r spike/TigerBrowser/testpages tiger:/Users/shg/TigerBrowser-testpages
ssh tiger '/Users/shg/TigerBrowser.app/Contents/MacOS/TigerBrowser \
    "file:///Users/shg/TigerBrowser-testpages/index.html" > /tmp/tb.log 2>&1 & sleep 3; screencapture -x /tmp/tb.png'
```

## Screenshots (all taken on the 10.4.11 box)

- `screenshot.png` — index page, toolbar + address bar + status bar.
- `screenshot-layout.png` — CSS floats/borders/table/fonts.
- `screenshot-images.png` — PNG and JPEG both decode and render.
- `screenshot-form.png` — native Aqua text field, popup button, checkbox, submit button.
- `screenshot-js.png` — JS timing loop; 2,000,000-iteration loop took **5298 ms**
  on Tiger's original 2007 system WebKit (build 4522, non-JIT interpreter-only
  JavaScriptCore). **Update 2026-09-20, after the user installed Safari 4.1.3
  for Tiger** (which replaces `/System/Library/Frameworks/WebKit.framework`
  with WebKit 533.19.4, build 4533.19.4 — first JIT-capable JavaScriptCore on
  Tiger): the same loop now takes **~59 ms** (three runs: 59, 62, 56 ms;
  screenshots `screenshot-js-run1.png`..`run3.png`), roughly **90x faster**.
  TigerBrowser itself is unchanged — it always linked the system
  `WebKit.framework` by path, so it picked up Safari 4.1.3's WebKit
  automatically on relaunch. This is the number to compare against the
  project's C-loop jsc build (2.24 s on the same loop) — Safari 4.1.3's JIT
  is now dramatically faster than our interpreter-only C-loop jsc, the
  opposite of the relationship implied by the original 5.3 s baseline.

## What works

Everything above works cleanly: native WebKit1 delegate methods, page
loading, title tracking, status text, CSS layout (floats, tables, fonts),
image decoding (PNG + JPEG), native form controls, and JavaScript execution.
No crashes, no missing delegate methods, no fragile-ABI issues in the app
shell itself.

## Tiger WebKit / environment quirks found

- **HTTPS to a modern TLS 1.2/SNI site fails through Tiger's system WebKit.**
  `https://www.apple.com/` fails provisional load with "secure connection
  failed" (`webView:didFailProvisionalLoadWithError:forFrame:`). Tiger's
  system CFNetwork/SSL stack predates SNI and modern TLS ciphers — this is
  the same gap the deps track's curl/LibreSSL work is meant to cover once
  WebKitLegacy's own network stack replaces Tiger's. Plain HTTP and
  `file://` loads both work fine. Not a WebView API problem, a system
  TLS-stack limitation. **Still fails identically after the Safari 4.1.3
  update** (re-checked 2026-09-20, `screenshot-https.png`) — the Safari
  update replaces `WebKit.framework` but evidently not whatever TLS bits
  `NSURLConnection`/CFNetwork use underneath, so this gap is unaffected by
  the WebKit version bump.
- **No missing/renamed WebFrameLoadDelegate, WebPolicyDelegate, or
  WebUIDelegate methods were hit.** Every delegate method used here exists
  on Tiger's 10.4.11 WebKit exactly as declared in the 10.4u SDK headers
  (`WebView.h`, `WebFrame.h`, `WebFrameLoadDelegate.h`, `WebPolicyDelegate.h`,
  `WebUIDelegate.h`) — no `-fobjc-fragile-extension-ivars` issues, no
  `NSApplicationActivationPolicyRegular` (10.6+, not in the 10.4u SDK headers,
  omitted here; the app defaults to a regular foreground app without it).
- **A GUI app launched via `ssh tiger 'cmd &'` (even with `nohup`) reliably
  dies within a couple of seconds of the *launching* ssh connection closing**,
  even though it renders correctly first. `nohup` alone does not fully detach
  it from the session (no `setsid` on Tiger's shell). Workaround used here:
  do the launch, `sleep`, and `screencapture -x` all inside **one** ssh
  invocation, so the process's controlling session stays alive for the whole
  capture. Something in AppKit's window-server bootstrap-port connection
  appears tied to that session; worth another look if we need long-running
  on-box processes for automated testing later.
