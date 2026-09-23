#!/bin/sh
# Allocator track: spike/media/stage-media.sh staged into /Users/shg/wk2video, with the
# web + network processes from $WEBBIN (default build/tiger-web-video) and UI from
# build/tiger-ui-port. Differences from stage-media: waits while the installed
# TigerBrowser.app runs, never removes /tmp/webkit-* files, prints the web process's
# RSS at SHOT_AT (BOXCMD runs there too, with $WPID the web process), brings the whole timestamped log back to $OUT.
#   WEBBIN=... OUT=logs/perf/malloc/x.log SCRIPT='wait 25; ...' spike/wk2web/stage-malloc.sh <url> [seconds]
set -e
WKT=/Users/shg/Developer/WebKitTiger
DEST=/Users/shg/wk2video
exec 9>"$WKT/spike/wk2web/box.lock"
python3 -c 'import fcntl,sys,time
f=9
for _ in range(600):
    try: fcntl.flock(f, fcntl.LOCK_EX|fcntl.LOCK_NB); sys.exit(0)
    except OSError: time.sleep(1)
sys.exit(1)' || { echo "stage-malloc: box busy for 10 min, giving up"; exit 1; }
URL=${1:-http://example.com/}
SECS=${2:-30}
APP=${APP:-TigerBrowser2}
SAMPLE=${SAMPLE:-0}
SAMPLE_ENV=""
if [ "$SAMPLE" != "0" ]; then SAMPLE_ENV="TIGER_SAMPLE_MAIN=$SAMPLE"; fi
APP_ENV="TIGER_FONT_MANIFEST=$DEST/share/tiger-fonts.json TIGER_CA_BUNDLE=$DEST/share/cacert.pem $SAMPLE_ENV ${APP_ENV:-}"
SHOT_AT=${SHOT_AT:-12}
SCRIPT=${SCRIPT:-}
WEBBIN=${WEBBIN:-$WKT/build/tiger-web-video}
UIBIN=${UIBIN:-$WKT/build/tiger-ui-port}
OUT=${OUT:-$WKT/logs/perf/malloc/last.log}
for i in $(seq 1 120); do
    busy=$(ssh tiger-eth "ps -axo pid,command | grep -E '[/]Applications/TigerBrowser.app|[T]iger(WK2App|Browser2|WebProcess|NetworkProcess|GPUProcess)|[p]agedriver|[t]igeraudio32' | head -3")
    [ -z "$busy" ] && break
    [ "$i" = 1 ] && echo "stage-malloc: waiting, already on the box:" && echo "$busy"
    sleep 10
done
if [ -n "$busy" ]; then echo "stage-malloc: box still busy; aborting, nothing killed"; exit 1; fi
ssh tiger-eth "mkdir -p $DEST/bin $DEST/Frameworks $DEST/share"
rsync -t -z "$WKT/logs/tiger-fonts.json" "$WKT/deps/src/cacert.pem" tiger-eth:$DEST/share/
FILES=""
for f in "$UIBIN/bin/TigerWK2App" "$UIBIN/bin/TigerBrowser2" "$WEBBIN/bin/TigerWebProcess" "$WEBBIN/bin/TigerNetworkProcess" "$WKT/build/tiger-gpu/bin/TigerGPUProcess" "$WKT/build/tigeraudio32"; do
    [ -f "$f" ] && FILES="$FILES $f" || echo "stage-malloc: missing $f (not copied)"
done
rsync -t -z --partial --inplace --bwlimit=20000 $FILES tiger-eth:$DEST/bin/
rsync -rtl -z --partial --inplace --bwlimit=20000 "$WKT/spike/CAHost/Frameworks/QuartzCore.framework" tiger-eth:$DEST/Frameworks/
ssh tiger-eth "if ps -axo command | grep -qE '[/]Applications/TigerBrowser.app|[T]iger(WK2App|Browser2|WebProcess|NetworkProcess|GPUProcess)'; then echo 'stage-malloc: box occupied at launch, aborting'; exit 3; fi; \
  cd $DEST/bin && (perl -e 'alarm $((SECS + 3)); exec @ARGV' -- env $APP_ENV TIGER_SCRIPT='$SCRIPT' ./$APP '$URL' $SECS -WebKitLogging ${LOGGING:-Process,Loading,Layout} 2>&1 | perl -MTime::HiRes=time -ne 'BEGIN{\$t0=time} printf \"%8.3f %s\", time-\$t0, \$_' > $DEST/app.log &) ; \
  sleep $SHOT_AT; echo == procs at ${SHOT_AT}s; ps -axo pid,rss,vsz,%cpu,time,command | grep -E 'Tiger(WK2App|Browser2|WebProcess|NetworkProcess|GPUProcess)|tigeraudio32' | grep -v grep | cut -c1-100; screencapture -x $DEST/shot.png; WPID=\$(ps -axo pid,command | awk '\$2 ~ /wk2video.*TigerWebProcess/ {print \$1}' | tail -1); ${BOXCMD:-true}; \
  sleep $((SECS - SHOT_AT + 4)); ps -axo pid,command | awk '\$2 ~ /^\.\/Tiger/ || \$2 ~ /\/wk2[a-z]*\// {print \$1}' | xargs kill 2>/dev/null; sleep 1; echo == after; ps -axo pid,command | grep -E 'Tiger(WK2App|Browser2|WebProcess|NetworkProcess|GPUProcess)|tigeraudio32' | grep -v grep; true"
mkdir -p "$(dirname "$OUT")"
scp -qO tiger-eth:$DEST/app.log "$OUT"
scp -qO tiger-eth:$DEST/shot.png "${OUT%.log}.png"
echo "log: $OUT ($(wc -l < "$OUT") lines, $(grep -c TIGER-SAMPLE "$OUT") samples, $(grep -c TIGER-CRASH "$OUT") TIGER-CRASH)"
grep -m1 "milestones=.*DidFirstVisuallyNonEmptyLayout\|dispatching DidFirstVisuallyNonEmptyLayoutForFrame" "$OUT" | cut -c1-12 | sed 's/^/first visually non-empty layout at: /' || true
grep -aE "TIGER-MEDIA|TIGER-CRASH|FATAL|Gigacage|gigacage" "$OUT" | head -20 || true
