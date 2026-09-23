#!/bin/sh
# Recovery/memory track: stage-app.sh staged into /Users/shg/wk2recover (own HOME), with a
# timestamped app.log, an on-box RSS sampler (every 2 s, per process type, bounded), and
# the TIGER_SCRIPT `shot` files brought back.
#   OUT=logs/recover/x.log SCRIPT='wait 12; killproc web; wait 10; shot /Users/shg/wk2recover/shots/after.png' \
#     UIDIR=build/tiger-ui-recover WEBDIR=build/tiger-web-recover spike/wk2web/stage-recover.sh <url> [seconds]
# Results: $OUT (app.log), $OUT.rss ("t=<s> web=<MB> net=<MB> gpu=<MB> ui=<MB>"), ${OUT%.log}.png
# (final shot) and ${OUT%.log}-<name>.png for each shot the script took.
set -e
WKT=/Users/shg/Developer/WebKitTiger
DEST=/Users/shg/wk2recover
exec 9>"$WKT/spike/wk2web/box.lock"
python3 -c 'import fcntl,sys,time
f=9
for _ in range(600):
    try: fcntl.flock(f, fcntl.LOCK_EX|fcntl.LOCK_NB); sys.exit(0)
    except OSError: time.sleep(1)
sys.exit(1)' || { echo "stage-recover: box busy for 10 min, giving up"; exit 1; }
URL=${1:-http://example.com/}
SECS=${2:-30}
APP=${APP:-TigerBrowser2}
SCRIPT=${SCRIPT:-}
# KILLAT="12:web 40:gpu": SIGKILL a staged helper at that second from the box side (for builds
# without the killproc script verb). Names: web, net, gpu.
KILLS=""
for k in ${KILLAT:-}; do
    case ${k#*:} in web) p=TigerWebProcess;; net) p=TigerNetworkProcess;; gpu) p=TigerGPUProcess;; *) echo "stage-recover: bad KILLAT $k"; exit 1;; esac
    KILLS="$KILLS (sleep ${k%%:*}; ps -axo pid,command | awk '/[w]k2recover\/bin\/.*$p/ {print \$1}' | xargs kill -9) & "
done
OUT=${OUT:-$WKT/logs/recover/last.log}
UIDIR=${UIDIR:-build/tiger-ui-recover}; WEBDIR=${WEBDIR:-build/tiger-web-recover}; GPUDIR=${GPUDIR:-build/tiger-gpu}
APP_ENV="HOME=$DEST/home TIGER_FONT_MANIFEST=$DEST/share/tiger-fonts.json TIGER_CA_BUNDLE=$DEST/share/cacert.pem ${APP_ENV:-}"
for i in $(seq 1 120); do
    busy=$(ssh tiger-eth "ps -axo pid,command | grep -E '[/]Applications/TigerBrowser.app|[T]iger(WK2App|Browser2|WebProcess|NetworkProcess|GPUProcess)|[p]agedriver|[t]igeraudio32' | head -3")
    [ -z "$busy" ] && break
    [ "$i" = 1 ] && echo "stage-recover: waiting, already on the box:" && echo "$busy"
    sleep 10
done
if [ -n "$busy" ]; then echo "stage-recover: box still busy; aborting, nothing killed"; exit 1; fi
ssh tiger-eth "mkdir -p $DEST/bin $DEST/Frameworks $DEST/share $DEST/home $DEST/shots; rm -f $DEST/shots/*.png"
rsync -t -z "$WKT/logs/tiger-fonts.json" "$WKT/deps/src/cacert.pem" "$WKT"/spike/wk2web/*.html "$WKT"/spike/jsc64/memdrop.html tiger-eth:$DEST/share/
FILES=""
for f in "$WKT/$UIDIR/bin/TigerBrowser2" "$WKT/$WEBDIR/bin/TigerWebProcess" "$WKT/$WEBDIR/bin/TigerNetworkProcess" "$WKT/$GPUDIR/bin/TigerGPUProcess" "$WKT/build/tigeraudio32"; do
    [ -f "$f" ] && FILES="$FILES $f" || echo "stage-recover: missing $f (not copied)"
done
rsync -t -z --partial --inplace --bwlimit=20000 $FILES tiger-eth:$DEST/bin/
rsync -rtl -z --partial --inplace --bwlimit=20000 "$WKT/spike/CAHost/Frameworks/QuartzCore.framework" tiger-eth:$DEST/Frameworks/
# The sampler is a bounded loop (SECS / 2 samples, 2 s apart): it ends with the run.
ssh tiger-eth "if ps -axo command | grep -qE '[/]Applications/TigerBrowser.app|[T]iger(WK2App|Browser2|WebProcess|NetworkProcess|GPUProcess)'; then echo 'stage-recover: box occupied at launch, aborting'; exit 3; fi; \
  cd $DEST/bin && (perl -e 'alarm $((SECS + 3)); exec @ARGV' -- env $APP_ENV TIGER_SCRIPT='$SCRIPT' ./$APP '$URL' $SECS -WebKitLogging ${LOGGING:-Process,Loading} 2>&1 | perl -MTime::HiRes=time -ne 'BEGIN{\$t0=time} printf \"%8.3f %s\", time-\$t0, \$_' > $DEST/app.log &) ; \
  (i=0; while [ \$i -lt $((SECS / 2)) ]; do ps -axo rss,command | awk -v t=\$((i * 2)) '/[w]k2recover\/bin\/.*TigerWebProcess/ {w += \$1} /[w]k2recover\/bin\/.*TigerNetworkProcess/ {n += \$1} /[w]k2recover\/bin\/.*TigerGPUProcess/ {g += \$1} /[.]\/TigerBrowser2/ {u += \$1} END {printf \"t=%d web=%d net=%d gpu=%d ui=%d\n\", t, w / 1024, n / 1024, g / 1024, u / 1024}'; i=\$((i + 1)); sleep 2; done > $DEST/rss.log &) ; $KILLS \
  sleep $((SECS - 2)); screencapture -x $DEST/shot.png; sleep 5; \
  ps -axo pid,command | awk '\$2 ~ /^\.\/TigerBrowser2/ || \$2 ~ /\/wk2recover\// {print \$1}' | xargs kill 2>/dev/null; sleep 1; echo == after; ps -axo pid,command | grep -E 'Tiger(Browser2|WebProcess|NetworkProcess|GPUProcess)|tigeraudio32' | grep -v grep; true"
mkdir -p "$(dirname "$OUT")"
scp -qO tiger-eth:$DEST/app.log "$OUT"
scp -qO tiger-eth:$DEST/rss.log "$OUT.rss" || true
scp -qO tiger-eth:$DEST/shot.png "${OUT%.log}.png"
for s in $(ssh tiger-eth "ls $DEST/shots 2>/dev/null"); do scp -qO tiger-eth:$DEST/shots/$s "${OUT%.log}-$s"; done
echo "log: $OUT ($(wc -l < "$OUT") lines, $(grep -c 'TIGER-CRASH pid' "$OUT") TIGER-CRASH)"
awk '{ split($2, w, "="); if (w[2] + 0 > peak) peak = w[2] + 0; last = w[2] } END { print "web RSS peak " peak " MB, last " last " MB" }' "$OUT.rss" 2>/dev/null || true
