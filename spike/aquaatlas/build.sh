#!/bin/bash
# Build aquaatlas for the Tiger box, run it there, and bring the atlas back.
# 32-bit MRR Cocoa; deliberately does NOT link libtigercompat, because this is a
# plain Tiger-era tool and should use Tiger's own AppKit spellings.
set -e
WKT=/Users/shg/Developer/WebKitTiger
HERE=$WKT/spike/aquaatlas
OUT=$HERE/out

# No pipe into grep, and no `|| true`. A pipeline's status is the last command's,
# so a compile error used to print, be ignored, and the previous binary shipped
# and ran -- which silently turned two regenerations into no-ops.
if ! "$WKT/toolchain/bin/tiger-clang" -g -O1 -Wall -fobjc-runtime=macosx-fragile-10.4 \
    -fobjc-exceptions "$HERE/aquaatlas.m" \
    -framework Cocoa -framework Carbon -framework ApplicationServices \
    -o "$HERE/aquaatlas" 2> "$HERE/build.log"; then
    grep -v 'unused during compilation' "$HERE/build.log" >&2
    echo "COMPILE FAILED" >&2
    exit 1
fi

scp -qO "$HERE/aquaatlas" tiger:/tmp/aquaatlas

# Run from inside a minimal .app bundle. A bare executable cannot be promoted to
# a foreground application by LaunchServices, so NSApp never becomes active and
# no window can become key -- every NSCell then draws its window-inactive
# artwork, silently, with the active and inactive passes coming out identical.
ssh tiger 'set -e
rm -rf /tmp/AquaAtlas.app /tmp/aqua-out
mkdir -p /tmp/AquaAtlas.app/Contents/MacOS
cp /tmp/aquaatlas /tmp/AquaAtlas.app/Contents/MacOS/AquaAtlas
cat > /tmp/AquaAtlas.app/Contents/Info.plist <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key><string>AquaAtlas</string>
  <key>CFBundleIdentifier</key><string>org.webkit.tiger.aquaatlas</string>
  <key>CFBundleName</key><string>AquaAtlas</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleSignature</key><string>????</string>
</dict>
</plist>
PLIST
printf "APPL????" > /tmp/AquaAtlas.app/Contents/PkgInfo

# The first launch of a freshly created bundle only registers it with
# LaunchServices and cannot be foregrounded, so the tool would draw every
# control in its window-inactive state. Burn that launch, then run for real.
AQUAATLAS_ALLOW_INACTIVE=1 /tmp/AquaAtlas.app/Contents/MacOS/AquaAtlas /tmp/aqua-warmup >/dev/null 2>&1 || true
rm -rf /tmp/aqua-warmup
/tmp/AquaAtlas.app/Contents/MacOS/AquaAtlas /tmp/aqua-out'

rm -rf "$OUT"
mkdir -p "$OUT"
scp -qO 'tiger:/tmp/aqua-out/*' "$OUT/"
echo "fetched $(ls "$OUT" | grep -c '\.png$') PNGs into $OUT"
