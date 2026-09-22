#!/bin/sh
# Stage TigerWK2App with its helper processes and the rebundled CoreAnimation on the
# box, run it against a URL, and bring back a screenshot and the log.
#   spike/wk2web/stage-app.sh [url] [seconds]
set -e
WKT=/Users/shg/Developer/WebKitTiger
URL=${1:-http://example.com/}
SECS=${2:-15}
ssh tiger 'mkdir -p /tmp/wk2/bin /tmp/wk2/Frameworks'
# rsync -t skips binaries that have not changed since the last stage.
FILES=""
for f in "$WKT/build/tiger-ui-port/bin/TigerWK2App" "$WKT/build/tiger-web-port/bin/TigerWebProcess" "$WKT/build/tiger-web-port/bin/TigerNetworkProcess" "$WKT/build/tiger-gpu/bin/TigerGPUProcess"; do
    [ -f "$f" ] && FILES="$FILES $f" || echo "stage-app: missing $f (not copied)"
done
rsync -t $FILES tiger:/tmp/wk2/bin/
ssh tiger 'test -d /tmp/wk2/Frameworks/QuartzCore.framework' 2>/dev/null || scp -qrO "$WKT/spike/CAHost/Frameworks/QuartzCore.framework" tiger:/tmp/wk2/Frameworks/
# One ssh invocation: launch, wait, capture. The app looks for its helpers next to itself.
ssh tiger "cd /tmp/wk2/bin && (WEBKIT_DEBUG=Process,IPC,Loading,Network ./TigerWK2App '$URL' $SECS -WebKitLogging Process,Loading,IPC > /tmp/wk2/app.log 2>&1 &) ; sleep 8; echo == procs at 8s; ps -axo pid,rss,%cpu,command | grep -E 'Tiger(WK2App|WebProcess|NetworkProcess|GPUProcess)' | grep -v grep | cut -c1-90; sleep $((SECS - 11)); screencapture -x /tmp/wk2/shot.png; sleep 4; ps -axo pid,rss,command | grep -E 'Tiger(WK2App|WebProcess|NetworkProcess)' | grep -v grep; true"
scp -qO tiger:/tmp/wk2/shot.png "$WKT/spike/wk2web/first-window.png"
echo "== app.log"; ssh tiger 'tail -40 /tmp/wk2/app.log'
echo "screenshot: $WKT/spike/wk2web/first-window.png"
