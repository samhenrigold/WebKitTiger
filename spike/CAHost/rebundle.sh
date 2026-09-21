#!/bin/bash
# Rebundle the Apple TV 3.0.2 QuartzCore for private use inside CAHost.app.
# The stock install name is /System/Library/Frameworks/QuartzCore.framework/...,
# which collides with Tiger's own QuartzCore (207 duplicate CI* classes).
# Tiger's dyld has no @rpath, so we use @executable_path.
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
src=$root/atv/extracted/3.0.2/QuartzCore.framework
dst=$root/spike/CAHost/Frameworks/QuartzCore.framework
hdr=$root/sdk/MacOSX10.5.sdk/System/Library/Frameworks/QuartzCore.framework/Versions/A/Headers

rm -rf "$dst"
mkdir -p "$(dirname "$dst")"
cp -R "$src" "$dst"
find "$dst" -name .DS_Store -delete

# Leopard (CA 1.x) headers match this binary's API generation; the framework ships none.
cp -R "$hdr" "$dst/Versions/A/Headers"
ln -sf Versions/Current/Headers "$dst/Headers"
chmod -R u+w "$dst"    # the SDK headers arrive read-only, which breaks rm -rf later

# Stop the Core Image half of this framework from colliding with Tiger's own
# QuartzCore. Without this, any QuickTime playback in the same process dies on
# "+[CIFilter filterWithName:]: selector not recognized".
python3 "$root/spike/CAHost/decollide.py" "$dst/Versions/A/QuartzCore"

"$root/toolchain/bin/tiger-install_name_tool" -id \
  '@executable_path/../Frameworks/QuartzCore.framework/Versions/A/QuartzCore' \
  "$dst/Versions/A/QuartzCore"

"$root/toolchain/bin/tiger-otool" -D "$dst/Versions/A/QuartzCore"
