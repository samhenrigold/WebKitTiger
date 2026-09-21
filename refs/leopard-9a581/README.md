# Leopard 9A581 (Mac OS X 10.5.0 GM) i386 system tree

Source: `OS installers/leopard_9a581_userdvd.dmg` (retail 10.5.0 user DVD), mounted with
`hdiutil attach -nobrowse -readonly`.

- `ProductVersion` / `ProductBuildVersion` from `/System/Library/CoreServices/SystemVersion.plist`
  on the mounted volume: **10.5 / 9A581**.
- Packages unpacked (flat xar, `xar -xf` then `gzip -dc Payload | cpio -idmu <paths>`):
  `System/Installation/Packages/BaseSystem.pkg`, `Essentials.pkg`, `BSD.pkg`.
- Extracted only `./System/Library/Frameworks/*`, `./System/Library/PrivateFrameworks/*`,
  `./usr/lib/*` (plus `SystemVersion.plist` for the build-number check, not kept in `root/`).

## Thinning

Every fat Mach-O under `root/` was thinned to i386 in place with `toolchain/bin/tiger-lipo -thin i386`
(1034 binaries thinned; none were missing an i386 slice, so nothing was dropped). Non-fat files,
headers, and Resources bundles were left untouched.

## Sizes

| Path | Size |
|---|---|
| `root/` (total) | 869 MB |
| `root/System/Library/Frameworks` | 526 MB |
| `root/System/Library/PrivateFrameworks` | 160 MB |
| `root/usr/lib` | 183 MB |
