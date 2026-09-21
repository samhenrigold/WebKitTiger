# Leopard DP1 9A241 (WWDC 2006 Leopard preview) i386 system tree

Source: `OS installers/Leopard DP1 9A241/Mac OS X 10.5.9A241 Install DVD.iso`, mounted with
`hdiutil attach -nobrowse -readonly` (old Apple_partition_scheme HFS layout).

- `ProductVersion` / `ProductBuildVersion` from `/System/Library/CoreServices/SystemVersion.plist`
  on the mounted volume itself (the installer's own running system): **10.5 / 9A241**.
- Packages unpacked (old bundle-style `.pkg`, not flat xar: `Contents/Resources/<Name>.pax.gz`
  is a symlink to `../Archive.pax.gz`, extracted with `gzip -dc Archive.pax.gz | cpio -idmu <paths>`):
  `System/Installation/Packages/Essentials.pkg`, `BSD.pkg`.
  **`BaseSystem.pkg` has no payload in this image** — its `Contents/Resources/BaseSystem.pax.gz`
  symlink points to a `Contents/Archive.pax.gz` that does not exist (only `Archive.bom` is present).
  Essentials.pkg + BSD.pkg supplied most of Frameworks/PrivateFrameworks/usr-lib, but critically
  **not** QuartzCore, CoreText, or the rest of what a real BaseSystem install would provide
  (they normally live in the missing BaseSystem payload).
- To fill that gap, `System/Library/{Frameworks,PrivateFrameworks}` and `usr/lib` were also
  rsync'd directly from the mounted volume's own root (the install DVD boots a real Mac OS X
  environment to run the installer, and its `SystemVersion.plist` reports the same 10.5 / 9A241 —
  confirmed genuinely build 9A241, not a different bootstrap OS). This is where `QuartzCore.framework`
  and `ApplicationServices.framework/.../Frameworks/CoreText.framework` came from; rsync only
  added files that weren't already present from the package extraction, so nothing was overwritten.
- Extracted/merged only `./System/Library/Frameworks/*`, `./System/Library/PrivateFrameworks/*`,
  `./usr/lib/*`.

## Thinning

Every fat Mach-O under `root/` was thinned to i386 in place with `toolchain/bin/tiger-lipo -thin i386`,
run twice (once after the package extraction, once after the volume-root merge): 632 + 254 = 886
binaries thinned total; none were missing an i386 slice. Non-fat files, headers, and Resources
bundles were left untouched.

## Sizes

| Path | Size |
|---|---|
| `root/` (total) | 485 MB |
| `root/System/Library/Frameworks` | 341 MB |
| `root/System/Library/PrivateFrameworks` | 113 MB |
| `root/usr/lib` | 32 MB |

## Analysis: is 9A241's CoreText/LayerKit closer to Tiger than 10.5.0?

**Yes, dramatically so.** Method: same as `logs/leopard-backport.md` — `tiger-otool -L` /
`tiger-nm -u` on the candidate binary, resolved against a combined Tiger export list built with
`tiger-nm -g` over every Mach-O in `sysroot/usr/lib`, `sysroot/System/Library/Frameworks`, and
`sysroot/System/Library/PrivateFrameworks` (166,483 exports; leopard-backport.md's own count was
163,188 from a slightly earlier sysroot snapshot).

**CoreText** (`ApplicationServices.framework/.../CoreText.framework/Versions/A/CoreText`,
`current version 1.0.0`, i.e. pre-versioned — 10.5.8's is 110.5.0):

- Link deps (`tiger-otool -L`): ATS, CoreGraphics, CoreFoundation, CoreServices, libicucore.A,
  libobjc.A, libstdc++.6, libgcc_s.1, libSystem.B — a much smaller/older dependency graph than
  10.5.8's CoreText.
- 286 undefined symbols (`tiger-nm -u`); only **3 fail to resolve** against Tiger's export list:
  `_kill$UNIX2003` (a renameable `$UNIX2003` conformance alias, not a hard miss, per the same
  convention leopard-backport.md used), `_object_getClass`, and `___CFRuntimeClassTableSize`.
- Checked laziness via `tiger-otool -l` section layout: this is a fragile-ABI image, so imports
  resolve through `__IMPORT,__jump_table` (lazy, address range `0xa023e000`–`0xa023e4f6`) and
  `__IMPORT,__pointers` (non-lazy, `0xa023e4f6`–`0xa023e7b6`). `_object_getClass` sits inside
  `__jump_table` (lazy — an ObjC2 accessor Tiger's fragile runtime lacks, but only resolved if
  actually called; likely dead code on this codepath). `___CFRuntimeClassTableSize` sits inside
  `__pointers` (**non-lazy** — a CF-runtime data symbol Tiger's older CoreFoundation doesn't
  export under that name).
- **Verdict: 1 hard non-lazy unresolved import**, vs. 10.5.8's CoreText at 39 unresolved (5
  non-lazy) per `logs/leopard-backport.md`. 9A241's CoreText is an order of magnitude closer to
  loadable-on-Tiger than 10.5.0's shipping CoreText — consistent with it being built against a
  CoreFoundation/ObjC/ATS stack still close to Tiger's, well before the 10.5 GM's CF took its
  final (incompatible) object layout.

**LayerKit / QuartzCore:** no `LayerKit.framework` exists anywhere in this build (searched the
whole extracted+merged tree). `QuartzCore.framework` does exist (version 1.0.0, in
`System/Library/Frameworks/QuartzCore.framework`), but at DP1 it is **Core Image only**: `tiger-nm
-g | grep -c objc_class_name_` finds 1193 Objective-C classes, of which 706 are `CI`-prefixed
(Core Image filters/contexts) and **zero are `CA`-prefixed**. There is no Core Animation in this
build under either the LayerKit or QuartzCore name — it hadn't been built yet at WWDC 2006.
