#!/bin/bash
# Build the acceptance test, run it on the box from inside a .app bundle, and
# fetch the mismatch pairs.
set -e
WKT=/Users/shg/Developer/WebKitTiger
HERE=$WKT/spike/aquaatlas
OUT=$HERE/compare

if ! "$WKT/toolchain/bin/tiger-clang" -g -O1 -Wall -fobjc-runtime=macosx-fragile-10.4 \
    -fobjc-exceptions -isystem "$WKT/compat/include" \
    "$HERE/comparecontrols.m" "$WKT/compat/aquacontrols.m" \
    -framework Cocoa -framework Carbon -framework ApplicationServices \
    -o "$HERE/comparecontrols" 2> "$HERE/compare.log"; then
    grep -v 'unused during compilation' "$HERE/compare.log" >&2
    echo "COMPILE FAILED" >&2
    exit 1
fi

scp -qO "$HERE/comparecontrols" tiger:/tmp/comparecontrols
ssh tiger 'set -e
rm -rf /tmp/Compare.app /tmp/compare-out
mkdir -p /tmp/Compare.app/Contents/MacOS
cp /tmp/comparecontrols /tmp/Compare.app/Contents/MacOS/Compare
cat > /tmp/Compare.app/Contents/Info.plist <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key><string>Compare</string>
  <key>CFBundleIdentifier</key><string>org.webkit.tiger.comparecontrols</string>
  <key>CFBundleName</key><string>Compare</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleSignature</key><string>????</string>
</dict>
</plist>
PLIST
printf "APPL????" > /tmp/Compare.app/Contents/PkgInfo
# First launch only registers the bundle with LaunchServices and cannot be
# foregrounded; this test needs a real key window, so burn one.
/tmp/Compare.app/Contents/MacOS/Compare /tmp/compare-warm >/dev/null 2>&1 || true
rm -rf /tmp/compare-warm
/tmp/Compare.app/Contents/MacOS/Compare /tmp/compare-out'

rm -rf "$OUT"; mkdir -p "$OUT"
scp -qO 'tiger:/tmp/compare-out/*' "$OUT/" 2>/dev/null || echo "(no mismatch pairs)"
