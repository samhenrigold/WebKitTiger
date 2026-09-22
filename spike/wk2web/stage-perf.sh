#!/bin/sh
# Perf-track variant of stage-app.sh: same box lock and cleanup, but the web and network
# processes come from $WEBBIN (default build/tiger-web-perf) and the whole app.log comes
# back to $OUT for offline symbolization.
#   WEBBIN=build/tiger-web-port OUT=logs/perf/wiki-before.log spike/wk2web/stage-perf.sh <url> [seconds]
set -e
WKT=/Users/shg/Developer/WebKitTiger
exec 9>"$WKT/spike/wk2web/box.lock"
python3 -c 'import fcntl,sys,time
f=open(sys.argv[1],"w")
for _ in range(600):
    try: fcntl.flock(f, fcntl.LOCK_EX|fcntl.LOCK_NB); sys.exit(0)
    except OSError: time.sleep(1)
sys.exit(1)' "$WKT/spike/wk2web/box.lock" || { echo "stage-perf: box busy for 10 min, giving up"; exit 1; }
URL=${1:-http://example.com/}
SECS=${2:-30}
WEBBIN=${WEBBIN:-$WKT/build/tiger-web-perf}
OUT=${OUT:-$WKT/logs/perf/last.log}
APP_ENV=${APP_ENV:-}
KILL_WEB_AT=${KILL_WEB_AT:-0}   # >0: SIGKILL the web process at that second (peer-death test for the IPC monitor)
APP_ENV="TIGER_FONT_MANIFEST=/Users/shg/wk2/share/tiger-fonts.json TIGER_CA_BUNDLE=/Users/shg/wk2/share/cacert.pem $APP_ENV"
ssh tiger-eth 'mkdir -p /Users/shg/wk2/bin /Users/shg/wk2/Frameworks /Users/shg/wk2/share'
rsync -t -z "$WKT/logs/tiger-fonts.json" "$WKT/deps/src/cacert.pem" tiger-eth:/Users/shg/wk2/share/
FILES=""
for f in "$WKT/build/tiger-ui-port/bin/TigerWK2App" "$WEBBIN/bin/TigerWebProcess" "$WEBBIN/bin/TigerNetworkProcess" "$WKT/build/tiger-gpu/bin/TigerGPUProcess"; do
    [ -f "$f" ] && FILES="$FILES $f" || echo "stage-perf: missing $f (not copied)"
done
rsync -t -z --partial --inplace --bwlimit=20000 $FILES tiger-eth:/Users/shg/wk2/bin/
ssh tiger-eth 'test -d /Users/shg/wk2/Frameworks/QuartzCore.framework' 2>/dev/null || rsync -rtl -z --partial --inplace --bwlimit=20000 "$WKT/spike/CAHost/Frameworks/QuartzCore.framework" tiger-eth:/Users/shg/wk2/Frameworks/
# perl's %time-stamps the log lines so time-to-first-paint can be read off the log.
ssh tiger-eth "cd /Users/shg/wk2/bin && (perl -e 'alarm $((SECS + 3)); exec @ARGV' -- env $APP_ENV ./TigerWK2App '$URL' $SECS -WebKitLogging Process,Loading 2>&1 | perl -MTime::HiRes=time -ne 'BEGIN{\$t0=time} printf \"%8.3f %s\", time-\$t0, \$_' > /Users/shg/wk2/app.log &) ; if [ $KILL_WEB_AT -gt 0 ]; then sleep $KILL_WEB_AT; killall -9 TigerWebProcess; echo == web process killed at ${KILL_WEB_AT}s; sleep $((8 - KILL_WEB_AT)); else sleep 8; fi; echo == procs at 8s; ps -axo pid,rss,%cpu,command | grep -E 'Tiger(WK2App|WebProcess|NetworkProcess|GPUProcess)' | grep -v grep | cut -c1-90; sleep $((SECS - 11)); echo == procs at $((SECS - 3))s; ps -axo pid,rss,%cpu,time,command | grep -E 'Tiger(WK2App|WebProcess|NetworkProcess|GPUProcess)' | grep -v grep | cut -c1-90; screencapture -x /Users/shg/wk2/shot.png; sleep 5; killall TigerWK2App TigerWebProcess TigerNetworkProcess TigerGPUProcess 2>/dev/null; sleep 1; echo == after; ps -axo pid,command | grep -E 'Tiger(WK2App|WebProcess|NetworkProcess|GPUProcess)' | grep -v grep; true"
mkdir -p "$(dirname "$OUT")"
scp -qO tiger-eth:/Users/shg/wk2/app.log "$OUT"
scp -qO tiger-eth:/Users/shg/wk2/shot.png "${OUT%.log}.png"
echo "log: $OUT ($(wc -l < "$OUT") lines, $(grep -c TIGER-SAMPLE "$OUT") samples)"
