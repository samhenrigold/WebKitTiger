#!/bin/sh
# stage-app.sh, pointed at another staging dir and other build dirs (the cookie work).
#   BOXDIR=/Users/shg/wk2regress UIDIR=build/tiger-ui-regress WEBDIR=build/tiger-web-regress \
#     spike/wk2web/stage-cookies.sh [url] [seconds]
# FROM_INSTALLED=1 copies the binaries of the installed TigerBrowser.app on the box
# instead of rsyncing a build (the "before" of a fix). HOME is $BOXDIR/home, so the
# run's storage (cookies included) is its own, never the user's profile.
set -e
WKT=/Users/shg/Developer/WebKitTiger
# One user of the box at a time: several agents share it. Waits up to 10 minutes.
exec 9>"$WKT/spike/wk2web/box.lock"
python3 -c 'import fcntl,sys,time
f=9  # the shell holds fd 9 for the whole run; locking a private open() would release on exit
for _ in range(600):
    try: fcntl.flock(f, fcntl.LOCK_EX|fcntl.LOCK_NB); sys.exit(0)
    except OSError: time.sleep(1)
sys.exit(1)' "$WKT/spike/wk2web/box.lock" || { echo "stage-app: box busy for 10 min, giving up"; exit 1; }
URL=${1:-http://example.com/}
SECS=${2:-15}
BOXDIR=${BOXDIR:-/Users/shg/wk2regress}
UIDIR=${UIDIR:-build/tiger-ui-regress}
WEBDIR=${WEBDIR:-build/tiger-web-regress}
GPUDIR=${GPUDIR:-build/tiger-gpu-regress}
APP=${APP:-TigerWK2App}   # or TigerBrowser2 (honours TIGER_SCRIPT)
SHOT=${SHOT:-$WKT/spike/wk2web/first-window.png}   # where the screenshot lands here
LOG=${LOG:-}                                       # if set, the whole app.log is copied here
APP_ENV=${APP_ENV:-}   # e.g. APP_ENV=TIGER_GPU=0 to keep the GPU process out
# Font manifest and CA bundle live under the staging dir; the processes read these two env vars.
APP_ENV="HOME=$BOXDIR/home TIGER_FONT_MANIFEST=$BOXDIR/share/tiger-fonts.json TIGER_CA_BUNDLE=$BOXDIR/share/cacert.pem $APP_ENV"
# Never run on top of someone else's processes: results would be meaningless and the
# cleanup would kill theirs. Wait up to 5 minutes for the box to be clear.
for i in $(seq 1 60); do
    busy=$(ssh tiger-eth "ps -axo pid,command | grep -E '[T]iger(WK2App|Browser2|WebProcess|NetworkProcess|GPUProcess)|[p]agedriver' | head -3")
    [ -z "$busy" ] && break
    [ "$i" = 1 ] && echo "stage-app: waiting, already on the box:" && echo "$busy"
    sleep 5
done
if [ -n "$busy" ]; then echo "stage: box still busy (the user may be using TigerBrowser.app); aborting, nothing killed"; exit 1; fi
ssh tiger-eth "mkdir -p $BOXDIR/bin $BOXDIR/Frameworks $BOXDIR/share $BOXDIR/home"
rsync -t -z "$WKT/logs/tiger-fonts.json" "$WKT/deps/src/cacert.pem" "$WKT"/spike/wk2web/*.html tiger-eth:$BOXDIR/share/ 2>/dev/null || echo "stage-app: share files not all copied (cacert.pem present?)"
# rsync -t skips binaries that have not changed since the last stage.
FILES=""
for f in "$WKT/$UIDIR/bin/TigerBrowser2" "$WKT/$WEBDIR/bin/TigerWebProcess" "$WKT/$WEBDIR/bin/TigerNetworkProcess" "$WKT/$GPUDIR/bin/TigerGPUProcess" "$WKT/build/tigeraudio32"; do
    [ -f "$f" ] && FILES="$FILES $f" || echo "stage-app: missing $f (not copied)"
done
if [ -n "${FROM_INSTALLED:-}" ]; then
    ssh tiger-eth "cp -p /Users/shg/Applications/TigerBrowser.app/Contents/MacOS/Tiger[BNGW]* /Users/shg/Applications/TigerBrowser.app/Contents/MacOS/tigeraudio32 $BOXDIR/bin/"
else
    rsync -t -z --partial --inplace --bwlimit=20000 $FILES tiger-eth:$BOXDIR/bin/
fi
# Always sync the framework: the rebased QuartzCore changed twice today (rebase-dylib passes).
rsync -rtl -z --partial --inplace --bwlimit=20000 "$WKT/spike/CAHost/Frameworks/QuartzCore.framework" tiger-eth:$BOXDIR/Frameworks/
# One ssh invocation: launch, wait, capture. The app looks for its helpers next to itself.
# Safety net: the app is killed by alarm 3 s after it would have exited on its own, and
# every helper is killed after the run, so nothing can sit on the socket pool if the run
# goes wrong. One ssh invocation: launch, wait, capture, clean up, report the pool.
ssh tiger-eth "cd $BOXDIR/bin && (perl -e 'alarm $((SECS + 3)); exec @ARGV' -- env $APP_ENV ./$APP '$URL' $SECS -WebKitLogging Process,Loading > $BOXDIR/app.log 2>&1 &) ; sleep 8; echo == procs at 8s; ps -axo pid,rss,%cpu,command | grep -E 'Tiger(WK2App|Browser2|WebProcess|NetworkProcess|GPUProcess)' | grep -v grep | cut -c1-90; netstat -m | grep 'clusters in use'; sleep $((SECS - 11)); screencapture -x $BOXDIR/shot.png; sleep 5; ps -axo pid,command | awk '\$2 ~ /^\.\/Tiger/ || \$2 ~ /\/wk2[a-z]*\// {print \$1}' | xargs kill 2>/dev/null; sleep 1; echo == after; ps -axo pid,command | grep -E 'Tiger(WK2App|Browser2|WebProcess|NetworkProcess|GPUProcess)' | grep -v grep; netstat -m | grep -E 'clusters in use|denied'; true"
scp -qO tiger-eth:$BOXDIR/shot.png "$SHOT"
if [ -n "$LOG" ]; then scp -qO tiger-eth:$BOXDIR/app.log "$LOG"; fi
echo "== app.log"; ssh tiger-eth "tail -40 $BOXDIR/app.log"
echo "screenshot: $SHOT"
