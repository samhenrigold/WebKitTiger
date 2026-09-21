# Tiger box update mirror (2026-09-20)

## Status: STOPPED EARLY (user budget cut, 2026-09-20 ~22:35 EDT). Swap NOT performed.
`sysroot/` is still the **pre-update** mirror. `sysroot-new/` holds the complete,
verified post-update mirror but has not been promoted. Do the swap (sysroot ->
sysroot-old-preupdates with sysroot-old/'s QTKit-7.2 content nested under a
QTKit-7.2 subdir, sysroot-new -> sysroot) and regenerate logs/api/*.txt before
relying on sysroot/ again.

## Receipts (last 20, `ssh tiger 'ls -lt /Library/Receipts | head -20'`, box time)
```
21:58  JavaForMacOSX10.4Release9.pkg
21:58  iPhoto_715.pkg
21:56  Safari4.1.3Tiger.pkg
21:56  iDVD_704.pkg
21:56  JavaForMacOSX10.4Release8.pkg
21:56  iMovie_714.pkg
21:55  iPhoto_714.pkg
21:50  SecUpd2009-005Intel.pkg
21:50  QuickTime764_Tiger.pkg
21:50  MigrationDVDCDSharingTiger.pkg
21:50  RAWCamera.pkg
21:50  ImageIO.pkg
21:50  iLifeMediaBrowser.pkg
21:50  iWeb_204.pkg
21:49  AirPortExtremeUpdate2008002.pkg
21:49  JavaForMacOSX10.4Release7.pkg
21:49  GarageBand_412.pkg
20:05  DevToolsSystem.pkg   (Xcode 2.5)
20:05  Java14Documentation.pkg
```
111 receipts total. Box software state is final as of 21:58 EDT 2026-09-20; it
rebooted 21:57. WebKit.framework is 4533.19.4 (Safari 4.1.3); QTKit is 7.6.4.

## Mirror
`sysroot-new/` was built clean (previous partial attempt removed) via tar-over-ssh,
per top-level directory (usr/lib, usr/include, System/Library/Frameworks,
System/Library/PrivateFrameworks, System/Library/QuickTime), all four transfers
completed with exit 0. Copy started 22:00 EDT, i.e. after the box's last install
(21:58); verified with
`ssh tiger 'find /System/Library/Frameworks /System/Library/PrivateFrameworks /usr/lib -newer /Library/Receipts/JavaForMacOSX10.4Release9.pkg -type f'`
→ 0 results, so nothing changed on the box after the copy began.

Key binaries confirmed present in sysroot-new/: usr/lib/libSystem.B.dylib,
.../CoreGraphics, .../CoreText, .../ImageIO, .../ATS (all under
ApplicationServices.framework/Frameworks/), QTKit, WebKit.

## File-level inventory (sysroot = pre-update, sysroot-new = post-update)
Mach-O dylib/framework-binary inventory built by scanning `*.dylib` and
extension-less files under `*/Versions/*/*` in both trees (script parked at
`/private/tmp/.../scratchpad/list-machos.sh`, not checked in — recreate if needed).

- old: 1644 binaries, new: 1758 binaries
- common to both: 1635
- added only in new: 123 — almost all `_debug`/`_profile` variants (Xcode 2.5,
  installed 20:05, e.g. `AppKit_profile`, `ApplicationServices_debug`, `ATS_debug`,
  `CoreGraphics_profile`, `HIServices_debug`, ...); matches NOTES.md's expectation.
- removed only in old: 9 — JavaVM 1.4.2/1.5.0 `zi/Asia/{Calcutta,Katmandu,Saigon}`
  timezone data (renamed upstream to Kolkata/Kathmandu/Ho_Chi_Minh by the Java 9
  update) and `IIDCVideoAssistant`/`VDCAssistant` under
  CoreMediaIOServicesPrivate.framework (not real binaries in most cases; not
  investigated further).
- byte-identical (md5) among the 1635 common: 937
- byte-different among the 1635 common: **698** — exact per-framework
  exports-changed vs. prebinding-only breakdown was **not completed**: the
  `tiger-nm -g -arch i386` export-list extraction over these 698 files (parallel
  background job) was stopped mid-run on the budget cut before any file was
  written out. Re-run: see `exports.sh`/loop parked in the scratchpad dir, or
  redo directly.

## Known result from elsewhere on the team
The leopard-track agent independently diffed the *linkable surface* (used by our
port) and found it effectively unchanged: exactly one new export,
`_NSHTTPCookieHTTPOnly` (Foundation). Shim owners re-verified their areas against
that. So the 698 byte-different binaries are overwhelmingly prebinding/timestamp
churn from the installers, not real ABI/export change, but this was not
independently re-confirmed by this pass for CoreText/CoreGraphics/ImageIO/ATS/
CoreFoundation/AppKit/libSystem/libobjc/QTKit/WebKit individually (task item 2's
per-framework verdict table and Info.plist version-string capture were not done).

## Remaining work (not done, budget cut)
1. Per-framework verdict table + added/removed export names + version strings for
   CoreText, CoreGraphics, ImageIO, ATS, CoreFoundation, Foundation, AppKit,
   libSystem, libobjc, QTKit, WebKit.
2. Swap: `sysroot` -> `sysroot-old-preupdates` (nest current `sysroot-old/`
   content under a `QTKit-7.2/` subdir inside it), `sysroot-new` -> `sysroot`.
3. Regenerate `logs/api/tiger-*.txt` from the new `sysroot/`; keep the current
   versions as `logs/api/preupdate/*.txt`; `git diff --stat logs/api`.
4. Commit `logs/api/*` alongside this file.
