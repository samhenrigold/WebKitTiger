#!/bin/sh
# JS-perf variant of stage-perf.sh: web + network processes from $WEBBIN (default
# build/tiger-web-jsperf), TIGER_SAMPLE_MAIN=100 and the Layout log channel on, whole
# timestamped app.log back to $OUT.
#   WEBBIN=build/tiger-web-port OUT=logs/perf/x-before.log spike/wk2web/stage-jsperf.sh <url> [seconds]
set -e
WKT=/Users/shg/Developer/WebKitTiger
exec 9>"$WKT/spike/wk2web/box.lock"
python3 -c 'import fcntl,sys,time
f=9  # the shell holds fd 9 for the whole run; locking a private open() would release on exit
for _ in range(600):
    try: fcntl.flock(f, fcntl.LOCK_EX|fcntl.LOCK_NB); sys.exit(0)
    except OSError: time.sleep(1)
sys.exit(1)' "$WKT/spike/wk2web/box.lock" || { echo "stage-jsperf: box busy for 10 min, giving up"; exit 1; }
URL=${1:-http://example.com/}
SECS=${2:-30}
WEBBIN=${WEBBIN:-$WKT/build/tiger-web-jsperf}
OUT=${OUT:-$WKT/logs/perf/jsperf-last.log}
APP_ENV=${APP_ENV:-}
APP=${APP:-TigerWK2App}   # or TigerBrowser2 (honours TIGER_SCRIPT)
SCRIPT=${SCRIPT:-}   # TIGER_SCRIPT steps, e.g. SCRIPT='wait 20; scroll 0,-600'
APP_ENV="TIGER_FONT_MANIFEST=/Users/shg/wk2jsperf/share/tiger-fonts.json TIGER_CA_BUNDLE=/Users/shg/wk2jsperf/share/cacert.pem TIGER_SAMPLE_MAIN=100 WEBKIT_DEBUG=Process,Loading,Layout $APP_ENV"
for i in $(seq 1 60); do
    busy=$(ssh tiger-eth "ps -axo pid,command | grep -E '[T]iger(WK2App|Browser2|WebProcess|NetworkProcess|GPUProcess)|[p]agedriver' | head -3")
    [ -z "$busy" ] && break
    [ "$i" = 1 ] && echo "stage-jsperf: waiting, already on the box:" && echo "$busy"
    sleep 5
done
if [ -n "$busy" ]; then echo "stage: box still busy (the user may be using TigerBrowser.app); aborting, nothing killed"; exit 1; fi
ssh tiger-eth 'mkdir -p /Users/shg/wk2jsperf/bin /Users/shg/wk2jsperf/Frameworks /Users/shg/wk2jsperf/share'
rsync -t -z "$WKT/logs/tiger-fonts.json" "$WKT/deps/src/cacert.pem" tiger-eth:/Users/shg/wk2jsperf/share/
FILES=""
for f in "$WKT/build/tiger-ui-port/bin/TigerWK2App" "$WKT/build/tiger-ui-port/bin/TigerBrowser2" "$WEBBIN/bin/TigerWebProcess" "$WEBBIN/bin/TigerNetworkProcess" "$WKT/build/tiger-gpu/bin/TigerGPUProcess"; do
    [ -f "$f" ] && FILES="$FILES $f" || echo "stage-jsperf: missing $f (not copied)"
done
rsync -t -z --partial --inplace --bwlimit=20000 $FILES tiger-eth:/Users/shg/wk2jsperf/bin/
ssh tiger-eth 'test -d /Users/shg/wk2jsperf/Frameworks/QuartzCore.framework' 2>/dev/null || rsync -rtl -z --partial --inplace --bwlimit=20000 "$WKT/spike/CAHost/Frameworks/QuartzCore.framework" tiger-eth:/Users/shg/wk2jsperf/Frameworks/
# Last check right before launch, on the box itself: abort rather than run on top of someone.
ssh tiger-eth "if ps -axo pid,command | grep -qE '[T]iger(WK2App|Browser2|WebProcess|NetworkProcess|GPUProcess)|[p]agedriver'; then echo 'stage-jsperf: box occupied at launch, aborting'; ps -axo pid,command | grep -E '[T]iger|[p]agedriver'; exit 3; fi; cd /Users/shg/wk2jsperf/bin && (perl -e 'alarm $((SECS + 3)); exec @ARGV' -- env $APP_ENV TIGER_SCRIPT='$SCRIPT' ./$APP '$URL' $SECS -WebKitLogging Process,Loading,Layout 2>&1 | perl -MTime::HiRes=time -ne 'BEGIN{\$t0=time} printf \"%8.3f %s\", time-\$t0, \$_' > /Users/shg/wk2jsperf/app.log &) ; sleep 8; echo == procs at 8s; ps -axo pid,rss,%cpu,command | grep -E 'Tiger(WK2App|WebProcess|NetworkProcess|GPUProcess)' | grep -v grep | cut -c1-90; sleep $((SECS - 11)); echo == procs at $((SECS - 3))s; ps -axo pid,rss,%cpu,time,command | grep -E 'Tiger(WK2App|WebProcess|NetworkProcess|GPUProcess)' | grep -v grep | cut -c1-90; screencapture -x /Users/shg/wk2jsperf/shot.png; sleep 5; ps -axo pid,command | awk '\$2 ~ /^\.\/Tiger/ || \$2 ~ /\/wk2[a-z]*\// {print \$1}' | xargs kill 2>/dev/null; sleep 1; echo == after; ps -axo pid,command | grep -E 'Tiger(WK2App|Browser2|WebProcess|NetworkProcess|GPUProcess)' | grep -v grep; true"
mkdir -p "$(dirname "$OUT")"
scp -qO tiger-eth:/Users/shg/wk2jsperf/app.log "$OUT"
scp -qO tiger-eth:/Users/shg/wk2jsperf/shot.png "${OUT%.log}.png"
echo "log: $OUT ($(wc -l < "$OUT") lines, $(grep -c TIGER-SAMPLE "$OUT") samples)"
grep -m1 "milestones=.*DidFirstVisuallyNonEmptyLayout\|dispatching DidFirstVisuallyNonEmptyLayoutForFrame" "$OUT" | cut -c1-12 | sed 's/^/first visually non-empty layout at: /'
