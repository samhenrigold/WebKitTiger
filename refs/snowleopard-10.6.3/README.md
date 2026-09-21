# Snow Leopard 10D575 (Mac OS X 10.6.3 DVD) i386 system tree

Source: `OS installers/691-6634-A,2Z,Mac OS Snow Leopard. Install DVD. v10.6.3 (DVD DL).iso`,
mounted with `hdiutil attach -nobrowse -readonly` (Apple_partition_scheme, needed the same
`.iso` handling as the other two images — hdiutil accepted it directly, no `-imagekey` override
was required).

- `ProductVersion` / `ProductBuildVersion` from `/System/Library/CoreServices/SystemVersion.plist`
  inside the extracted BaseSystem payload: **10.6.3 / 10D575** (matches the disc's printed
  version; Apple's combo-updater numbering for 10.6.3 elsewhere is 10D573 — this GM/retail DVD
  build is 10D575).
- Packages unpacked (flat xar, `xar -xf` then `gzip -dc Payload | cpio -idmu <paths>`):
  `System/Installation/Packages/BaseSystem.pkg`, `Essentials.pkg`, `BSD.pkg`.
- Extracted only `./System/Library/Frameworks/*`, `./System/Library/PrivateFrameworks/*`,
  `./usr/lib/*` (plus `SystemVersion.plist` for the build-number check, not kept in `root/`).

## Thinning

Every fat Mach-O under `root/` was thinned to i386 in place with `toolchain/bin/tiger-lipo -thin i386`
(1409 binaries thinned; none were missing an i386 slice — Snow Leopard was still shipping i386
GM/retail builds alongside x86_64/ppc). Non-fat files, headers, and Resources bundles were left
untouched.

## Sizes

| Path | Size |
|---|---|
| `root/` (total) | 1.3 GB |
| `root/System/Library/Frameworks` | 940 MB |
| `root/System/Library/PrivateFrameworks` | 284 MB |
| `root/usr/lib` | 140 MB |
