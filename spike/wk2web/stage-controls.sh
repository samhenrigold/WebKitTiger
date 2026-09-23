#!/bin/sh
# Stage TigerBrowser2 and its helpers from the FAITHFUL track's three build dirs, load the
# Aqua control test page and bring back a screenshot.
#   MODE=fast|faithful spike/wk2web/stage-controls.sh [url] [seconds]
# MODE=fast leaves TIGER_FAITHFUL unset (software raster in the web process);
# MODE=faithful sets TIGER_FAITHFUL=1 (layers composited by the GPU process).
set -e
WKT=/Users/shg/Developer/WebKitTiger
MODE=${MODE:-fast}
SECS=${2:-16}
DIR=/Users/shg/wk2controls
URL=${1:-file://$DIR/share/controls-test.html}

# One user of the box at a time: several agents share it. Waits up to 10 minutes.
exec 9>"$WKT/spike/wk2web/box.lock"
python3 -c 'import fcntl,sys,time
f=9  # the shell holds fd 9 for the whole run; locking a private open() would release on exit
for _ in range(600):
    try: fcntl.flock(f, fcntl.LOCK_EX|fcntl.LOCK_NB); sys.exit(0)
    except OSError: time.sleep(1)
sys.exit(1)' || { echo "stage-controls: box busy for 10 min, giving up"; exit 1; }
for i in $(seq 1 60); do
    busy=$(ssh tiger-eth "ps -axo pid,command | grep -E '[T]iger(WK2App|Browser2|WebProcess|NetworkProcess|GPUProcess)' | head -3")
    [ -z "$busy" ] && break
    [ "$i" = 1 ] && echo "stage-controls: waiting, already on the box:" && echo "$busy"
    sleep 5
done
if [ -n "$busy" ]; then echo "stage: box still busy (the user may be using TigerBrowser.app); aborting, nothing killed"; exit 1; fi

APP_ENV="TIGER_FONT_MANIFEST=$DIR/share/tiger-fonts.json TIGER_CA_BUNDLE=$DIR/share/cacert.pem"
[ "$MODE" = faithful ] && APP_ENV="TIGER_FAITHFUL=1 $APP_ENV"
APP_ENV="$APP_ENV ${EXTRA_ENV:-}"
SCRIPT=${SCRIPT:-}   # TIGER_SCRIPT steps, e.g. SCRIPT='wait 6; click 80,150; type hi; shot /tmp/a.png'

ssh tiger-eth "mkdir -p $DIR/bin $DIR/Frameworks $DIR/share"
rsync -t -z "$WKT/logs/tiger-fonts.json" "$WKT/deps/src/cacert.pem" "$WKT/spike/wk2web/controls-test.html" tiger-eth:$DIR/share/
FILES=""
for f in "$WKT/build/tiger-ui-faithful/bin/TigerBrowser2" "$WKT/build/tiger-web-faithful/bin/TigerWebProcess" "$WKT/build/tiger-web-faithful/bin/TigerNetworkProcess" "$WKT/build/tiger-gpu-faithful/bin/TigerGPUProcess" "$WKT/build/tigeraudio32"; do
    [ -f "$f" ] && FILES="$FILES $f" || echo "stage-controls: missing $f (not copied)"
done
rsync -t -z --partial --inplace --bwlimit=20000 $FILES tiger-eth:$DIR/bin/
ssh tiger-eth "test -d $DIR/Frameworks/QuartzCore.framework" 2>/dev/null || rsync -rtl -z --partial --inplace --bwlimit=20000 "$WKT/spike/CAHost/Frameworks/QuartzCore.framework" tiger-eth:$DIR/Frameworks/

ssh tiger-eth "cd $DIR/bin && (perl -e 'alarm $((SECS + 3)); exec @ARGV' -- env $APP_ENV TIGER_SCRIPT='$SCRIPT' ./TigerBrowser2 '$URL' $SECS > $DIR/app-$MODE.log 2>&1 &) ; sleep $((SECS - 4)); screencapture -x $DIR/shot-$MODE.png; sleep 3; ps -axo pid,command | awk '\$2 ~ /^\.\/Tiger/ || \$2 ~ /\/wk2[a-z]*\// {print \$1}' | xargs kill 2>/dev/null; sleep 1; echo == after; ps -axo pid,command | grep -E '[T]iger(Browser2|WebProcess|NetworkProcess|GPUProcess)'; true"
scp -qO tiger-eth:$DIR/shot-$MODE.png "$WKT/spike/wk2web/controls-$MODE.png"
# Anything the script's own `shot` verb wrote, under $DIR/step-*.png.
scp -qO "tiger-eth:$DIR/step-*.png" "$WKT/spike/wk2web/" 2>/dev/null || true
echo "== app.log"; ssh tiger-eth "tail -25 $DIR/app-$MODE.log"
echo "screenshot: $WKT/spike/wk2web/controls-$MODE.png"
