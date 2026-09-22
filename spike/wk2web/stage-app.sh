#!/bin/sh
# Stage TigerWK2App with its helper processes and the rebundled CoreAnimation on the
# box, run it against a URL, and bring back a screenshot and the log.
#   spike/wk2web/stage-app.sh [url] [seconds]
set -e
WKT=/Users/shg/Developer/WebKitTiger
URL=${1:-http://example.com/}
SECS=${2:-15}
ssh tiger 'mkdir -p /Users/shg/wk2/bin /Users/shg/wk2/Frameworks'
# rsync -t skips binaries that have not changed since the last stage.
FILES=""
for f in "$WKT/build/tiger-ui-port/bin/TigerWK2App" "$WKT/build/tiger-web-port/bin/TigerWebProcess" "$WKT/build/tiger-web-port/bin/TigerNetworkProcess" "$WKT/build/tiger-gpu/bin/TigerGPUProcess"; do
    [ -f "$f" ] && FILES="$FILES $f" || echo "stage-app: missing $f (not copied)"
done
rsync -t -z --bwlimit=2500 $FILES tiger:/Users/shg/wk2/bin/
ssh tiger 'test -d /Users/shg/wk2/Frameworks/QuartzCore.framework' 2>/dev/null || rsync -rtl -z --bwlimit=2500 "$WKT/spike/CAHost/Frameworks/QuartzCore.framework" tiger:/Users/shg/wk2/Frameworks/
# One ssh invocation: launch, wait, capture. The app looks for its helpers next to itself.
# Safety net: the app is killed by alarm 3 s after it would have exited on its own, and
# every helper is killed after the run, so nothing can sit on the socket pool if the run
# goes wrong. One ssh invocation: launch, wait, capture, clean up, report the pool.
ssh tiger "cd /Users/shg/wk2/bin && (perl -e 'alarm $((SECS + 3)); exec @ARGV' -- ./TigerWK2App '$URL' $SECS -WebKitLogging Process,Loading > /Users/shg/wk2/app.log 2>&1 &) ; sleep 8; echo == procs at 8s; ps -axo pid,rss,%cpu,command | grep -E 'Tiger(WK2App|WebProcess|NetworkProcess|GPUProcess)' | grep -v grep | cut -c1-90; netstat -m | grep 'clusters in use'; sleep $((SECS - 11)); screencapture -x /Users/shg/wk2/shot.png; sleep 5; killall TigerWK2App TigerWebProcess TigerNetworkProcess TigerGPUProcess 2>/dev/null; sleep 1; echo == after; ps -axo pid,command | grep -E 'Tiger(WK2App|WebProcess|NetworkProcess|GPUProcess)' | grep -v grep; netstat -m | grep -E 'clusters in use|denied'; true"
scp -qO tiger:/Users/shg/wk2/shot.png "$WKT/spike/wk2web/first-window.png"
echo "== app.log"; ssh tiger 'tail -40 /Users/shg/wk2/app.log'
echo "screenshot: $WKT/spike/wk2web/first-window.png"
