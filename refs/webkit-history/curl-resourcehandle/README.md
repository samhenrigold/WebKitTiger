# curl ResourceHandle backend — last version before removal

Plan reference: `logs/webcore-plan.md` §2 ("Networking: curl under the
NSURLRequest API"). The plan's own claim that WK1 curl was removed "around
2019-2021, when WinCairo moved to the NetworkProcess" is **wrong by a few
years** — see below. Use `ResourceHandleCurl-2023-03-19-BEST-functional.cpp`
as the reference; do not use the small stub file.

## Timeline (verified against github.com/WebKit/WebKit)

| Date | Commit | Event |
|---|---|---|
| 2018-02-16 | `5e8cece14712b470e24f2150daca0877f4d69a7f` | "[Curl] Unify logic of ResourceHandleCurlDelegate into ResourceHandle" (bug 182578) — merges the old `ResourceHandleCurlDelegate` into a new `CurlResourceHandleDelegate`; `ResourceHandleCurl.cpp` is still a **full, working** implementation, 566 LOC |
| ... | ... | ResourceHandleCurl.cpp kept working and was actively maintained through 2022 (WinCairo WK1) |
| 2023-03-19 | `ee329e96b8557f3d0ca8dacb0da8734f532c1a5f` | **last commit with a functional WK1 curl backend** — the parent of the removal commit below |
| 2023-03-20 | `f57e6ee12e74...` | **"[curl] Remove the legacy ResourceHandle implementation"** (bug 254141) — "WinCairo legacy WK1 was removed. It's no longer needed." Guts `ResourceHandleCurl.cpp`/`SynchronousLoaderClientCurl.cpp` down to `ASSERT_NOT_REACHED()` stubs, deletes `CurlCacheEntry.{cpp,h}`, `CurlCacheManager.{cpp,h}`, `CurlResourceHandleDelegate.{cpp,h}` outright |
| 2023-06-16 | `d8328394a067174adc2e2c93d7e41758eb78fa83` | "[Curl][Soup] Move empty functions..." — deletes the now-stub `ResourceHandleCurl.cpp` and `SynchronousLoaderClientCurl.cpp` files entirely (their `ASSERT_NOT_REACHED()` bodies moved into the shared cross-port files) |

So the real deletion — the one that matters for reference purposes — is
**f57e6ee12e74 on 2023-03-20** ("Remove the legacy ResourceHandle
implementation"), reason: "WinCairo legacy WK1 was removed. It's no longer
needed." The June 2023 commit just cleans up dead stub code that was already
non-functional.

## Files fetched (all at `ee329e96b8557f3d0ca8dacb0da8734f532c1a5f`, 2023-03-19, the last commit before removal)

| File | LOC | Role |
|---|---|---|
| `ResourceHandleCurl-2023-03-19-BEST-functional.cpp` | 589 | **The reference implementation.** `start`/`cancel`/`platformSetDefersLoading`/`loadResourceSynchronously`/`continueWillSendRequest`/`receivedCredential`, `CurlResourceHandleDelegate` wiring, cookie glue — this is what §2.8 of the plan estimates "550-750 LOC" to rewrite |
| `SynchronousLoaderClientCurl-2023-03-19.cpp` | 54 | sync-load message pump + `platformBadResponseError` |
| `CurlResourceHandleDelegate.cpp` / `.h` | 196 / 72 | the delegate object `ResourceHandleCurl.cpp` drives — `curlDidSendData`/`DidReceiveResponse`/`DidReceiveData`/`DidComplete`/`DidFailWithError` glue between `CurlRequest` and `ResourceHandle` |
| `CurlCacheManager.cpp` / `.h` | 340 / 82 | disk cache manager — the plan's §2.8 "Honest unknowns" flags this as worth reviving later; fetched now while it's available |
| `CurlCacheEntry.cpp` / `.h` | 336 / 98 | one cache entry (headers + body file), used by `CurlCacheManager` |
| `CurlDownload.cpp` / `.h` | 207 / 96 | `NSURLDownload`-equivalent standalone download object (used by `WebDownload` in WebKitLegacy in other ports) |
| `NetworkStorageSessionCurl.cpp` | 253 | curl's `NetworkStorageSession` — plan §2.3 says **keep Cocoa** for this type, so treat as background reading only |
| `CookieStorageCurl.cpp` | 47 | curl's cookie storage glue — same caveat, plan says use `NSHTTPCookieStorage` instead (§2.6) |

## Also fetched, for context (do not use as a reference)

- `ResourceHandleCurl-2018-02-functional-ref.cpp` (566 LOC, commit
  `5e8cece14712b470e24f2150daca0877f4d69a7f`, 2018-02-16) — an earlier
  functional version, right after the `CurlResourceHandleDelegate` merge.
  Useful only to diff against the 2023 version if the 2023 one looks odd.
- `ResourceHandleCurl-2023-06-stub-DO-NOT-USE.cpp` (commit
  `d8328394a067174adc2e2c93d7e41758eb78fa83`'s parent,
  `211a030a510d221c9286ee7a4166dddf79e352e0`, 2023-06) — **111 lines, every
  method is `ASSERT_NOT_REACHED()`.** This is what you get if you naively grab
  "the last commit before ResourceHandleCurl.cpp was deleted" without checking
  content — it had already been gutted three months earlier. Kept only as a
  trap warning for the next person who queries `commits?path=...`.

## Not found / not applicable

- `ResourceHandleCurlDelegate.{h,cpp}` (the *pre*-2018 name) — superseded by
  `CurlResourceHandleDelegate` in the Feb 2018 commit above; the old-named
  files were deleted in that same commit. Use `CurlResourceHandleDelegate.*`
  instead.
- `CookieJarCurl` — no file by this name was ever found in `git log --all
  -- '**/CookieJarCurl*'`-equivalent path searches (`commits?path=`). The
  curl cookie store is `CookieJarDB.{cpp,h}` (still present in the upstream
  tree today, zero callers per plan §2.2) plus `CookieStorageCurl.cpp`
  (fetched above).
- `SocketStreamHandleImplCurl` — not fetched; plan §2.2 says WebSockets are
  WK2-only now and this file is not needed for WebKitLegacy.

## Method used

`gh api "repos/WebKit/WebKit/commits?path=<path>&per_page=100"` to walk each
file's commit history looking for the removal/gutting commit, then
`gh api repos/WebKit/WebKit/commits/<sha>` for `.parents[0].sha` to get the
last-good tree, then `curl -sL
https://raw.githubusercontent.com/WebKit/WebKit/<sha>/<path>`.
