#!/bin/bash
# Build scrollerprobe for the box, run it from a .app bundle (a bare tool never gets a key
# window; see spike/aquaatlas/build.sh), fetch the images into spike/aquascroller/out.
set -e
WKT=/Users/shg/Developer/WebKitTiger
HERE=$WKT/spike/aquascroller
# The artwork lives in the UI process's sources; compile the same file.
ART=${ART:-$WKT/WebKit-faithful/Source/WebKit/UIProcess/tiger}
BOX=${BOX:-tiger-eth}
# The shared box lock (spike/wk2web/stage-controls.sh): a window of ours in someone's screenshot
# would fail their run.
exec 9>"$WKT/spike/wk2web/box.lock"
python3 -c 'import fcntl,sys,time
for _ in range(600):
    try: fcntl.flock(9, fcntl.LOCK_EX|fcntl.LOCK_NB); sys.exit(0)
    except OSError: time.sleep(1)
sys.exit(1)' || { echo "run.sh: box busy for 10 min"; exit 1; }
[ "$(ssh $BOX "ps -axo command | grep -c '[/]Applications/TigerBrowser.app'")" = 0 ] || { echo "run.sh: the user's TigerBrowser is up; not running"; exit 1; }
"$WKT/toolchain/bin/tiger-clang" -g -O1 -Wall -fobjc-runtime=macosx-fragile-10.4 -fobjc-exceptions \
    -isystem "$WKT/compat/include" -I"$ART" "$HERE/scrollerprobe.m" "$ART/TigerScrollbarArtwork.c" \
    -framework Cocoa -framework Carbon -o "$HERE/scrollerprobe"
scp -qO "$HERE/scrollerprobe" $BOX:/tmp/scrollerprobe
ssh $BOX 'set -e
rm -rf /tmp/ScrollerProbe.app /tmp/scrollerprobe-out
mkdir -p /tmp/ScrollerProbe.app/Contents/MacOS
cp /tmp/scrollerprobe /tmp/ScrollerProbe.app/Contents/MacOS/ScrollerProbe
cat > /tmp/ScrollerProbe.app/Contents/Info.plist <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>ScrollerProbe</string>
  <key>CFBundleIdentifier</key><string>org.webkit.tiger.scrollerprobe</string>
  <key>CFBundlePackageType</key><string>APPL</string>
</dict></plist>
PLIST
/tmp/ScrollerProbe.app/Contents/MacOS/ScrollerProbe /tmp/scrollerprobe-warm >/dev/null 2>&1 || true
rm -rf /tmp/scrollerprobe-warm
/tmp/ScrollerProbe.app/Contents/MacOS/ScrollerProbe /tmp/scrollerprobe-out | grep -v "^   hit"'
rm -rf "$HERE/out"; mkdir -p "$HERE/out"
scp -qO "$BOX:/tmp/scrollerprobe-out/*" "$HERE/out/"
ssh $BOX 'rm -rf /tmp/ScrollerProbe.app /tmp/scrollerprobe /tmp/scrollerprobe-out'
