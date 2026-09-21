#!/bin/bash
# Build GPUReplay, copy it to the Tiger box, run the headless pixel compare,
# then run it again with a window and screenshot the result.
#
# Launch, sleep and screencapture have to be ONE ssh invocation: a GUI process
# started over ssh dies when that session closes, and nohup does not detach on
# Tiger (NOTES.md, "GUI processes launched over ssh").
set -e
WKT=/Users/shg/Developer/WebKitTiger
APP=$WKT/build/GPUReplay.app
SHOT=${1:-$WKT/spike/gpureplay/gpureplay.png}

make -C "$WKT/spike/gpureplay"

echo "== copy"
ssh tiger 'rm -rf /tmp/GPUReplay.app'
scp -qrO "$APP" tiger:/tmp/

echo "== headless pixel compare"
ssh tiger '/tmp/GPUReplay.app/Contents/MacOS/GPUReplay'
status=$?

# On screen. Launched with `open`, not directly: a window opened by a process
# started over ssh never comes up (makeKeyAndOrderFront blocks), and Tiger's
# open has no --args, so the marker file is how the window is asked for.
echo "== on screen"
ssh tiger "touch /tmp/gpureplay-show
           open /tmp/GPUReplay.app
           sleep 6
           screencapture -x /tmp/gpureplay.png
           killall GPUReplay 2>/dev/null
           rm -f /tmp/gpureplay-show
           true"
scp -qO tiger:/tmp/gpureplay.png "$SHOT"
echo "screenshot: $SHOT"
ssh tiger 'cat /tmp/gpureplay.log' || true
exit $status
