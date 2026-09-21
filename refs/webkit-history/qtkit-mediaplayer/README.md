# QTKit media backend — last version before removal

Not analyzed in `logs/webcore-plan.md` at all — video is `ENABLE_VIDEO=OFF`
for every port already (§1.2), so there's no phase-1 need. Fetched per the
team lead's direction as a cheap forward-reference for whenever `<video>` is
revived on Tiger later: QTKit (10.0+) is the only media framework Tiger has
(no AVFoundation, no QuickTime X), so this is the actual, only viable route
back to `<video>` on this port.

## Timeline

| Date | Commit | Event |
|---|---|---|
| 2018-01-23 | `6c43356995c13ef9b95e15458bb2c2809102edc6` | **"Begin removing QTKit code"** (bug 181951): "QTKit was being used on El Capitan and before." Deletes `MediaPlayerPrivateQTKit.{h,mm}` and `MediaTimeQTKit.{h,mm}` outright, edits `MediaPlayer.cpp` to drop the engine registration, edits `WebVideoFullscreenController.mm` |
| (last-good) | `5dfc9ef9c532286804cd5af8eccf6b48c7b41776` | parent of the above — the commit fetched from |

So QTKit support ran until **El Capitan (10.11)**, i.e. essentially the whole
life of the Cocoa port up to 2018, not "~2015-2017" as originally guessed —
it was dropped in a single January 2018 commit once AVFoundation-only became
viable.

## Files fetched (all at `5dfc9ef9c532286804cd5af8eccf6b48c7b41776`)

| File | LOC | Role |
|---|---|---|
| `MediaPlayerPrivateQTKit.h` | 225 | the `MediaPlayerPrivateInterface` implementation class declaration |
| `MediaPlayerPrivateQTKit.mm` | 1727 | the whole QTKit engine: `QTMovie`/`QTMovieView` creation, load state, buffering, seeking, full-screen, captions, `QTMovieLayer` (the `QTKitSPI.h`-declared private `CALayer` subclass for compositing) |
| `MediaTimeQTKit.h` / `.mm` | 43 / 66 | `MediaTime` ⟷ `QTTime` conversion helpers used throughout the `.mm` above |
| `QTKitSPI.h` | 150 | the private-API header `MediaPlayerPrivateQTKit.mm` imports (`#import <pal/spi/mac/QTKitSPI.h>`) — `QTMovie`, `QTMovieView`, `QTMovieLayer` and friends' undocumented methods/notifications |

## Not fetched

- `WebVideoFullscreenController.mm` (`Source/WebCore/platform/mac/`, 569 LOC
  at this commit) — the 2018 removal commit only touched two of its methods
  (`setVideoElement:`, `updatePowerAssertions`), the rest of the file is
  generic full-screen chrome, not QTKit-specific. At 569 lines it's past what
  "only if small" was meant to cover, and it's not actually the QTKit
  media-backend file requested — skipped. If full-screen video chrome is
  needed later, fetch this file separately once the media backend itself
  is working.

## Method

Same pattern as the other two topics: found the removal commit via `gh api
"repos/WebKit/WebKit/commits?path=<path>&per_page=1"`, read `.parents[0].sha`
for the last-good tree, fetched via `raw.githubusercontent.com`.
