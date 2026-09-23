#!/bin/sh
# features-track copy of stage-app.sh (box dir /Users/shg/wk2features).
# Stage TigerWK2App with its helper processes and the rebundled CoreAnimation on the
# box, run it against a URL, and bring back a screenshot and the log.
#   spike/wk2web/stage-app-features.sh [url] [seconds]
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
APP=${APP:-TigerWK2App}   # or TigerBrowser2 (honours TIGER_SCRIPT)
SHOT=${SHOT:-$WKT/spike/wk2web/first-window.png}   # where the screenshot lands here
LOG=${LOG:-}                                       # if set, the whole app.log is copied here
APP_ENV=${APP_ENV:-}   # e.g. APP_ENV=TIGER_GPU=0 to keep the GPU process out
# Font manifest and CA bundle live under the staging dir; the processes read these two env vars.
# Own HOME so staged runs never touch the installed app's cookies/storage under ~/Library.
APP_ENV="HOME=/Users/shg/wk2features/home TIGER_FONT_MANIFEST=/Users/shg/wk2features/share/tiger-fonts.json TIGER_CA_BUNDLE=/Users/shg/wk2features/share/cacert.pem $APP_ENV"
# Never run on top of someone else's processes: results would be meaningless and the
# cleanup would kill theirs. Wait up to 5 minutes for the box to be clear.
for i in $(seq 1 60); do
    busy=$(ssh tiger-eth "ps -axo pid,command | grep -E '[T]iger(WK2App|Browser2|WebProcess|NetworkProcess|GPUProcess)|[p]agedriver' | head -3")
    [ -z "$busy" ] && break
    [ "$i" = 1 ] && echo "stage-app: waiting, already on the box:" && echo "$busy"
    sleep 5
done
if [ -n "$busy" ]; then echo "stage: box still busy (the user may be using TigerBrowser.app); aborting, nothing killed"; exit 1; fi
ssh tiger-eth 'mkdir -p /Users/shg/wk2features/bin /Users/shg/wk2features/Frameworks /Users/shg/wk2features/share /Users/shg/wk2features/home'
rsync -t -z "$WKT/logs/tiger-fonts.json" "$WKT/deps/src/cacert.pem" "$WKT"/spike/wk2web/*.html tiger-eth:/Users/shg/wk2features/share/ 2>/dev/null || echo "stage-app: share files not all copied (cacert.pem present?)"
# rsync -t skips binaries that have not changed since the last stage.
FILES=""
# UIDIR/WEBDIR/GPUDIR let a track's own build dirs through the same harness (tools/regress.sh
# on build/tiger-*-faithful, say); the defaults are the main dirs.
UIDIR=${UIDIR:-build/tiger-ui-port}; WEBDIR=${WEBDIR:-build/tiger-web-port}; GPUDIR=${GPUDIR:-build/tiger-gpu}
for f in "$WKT/$UIDIR/bin/TigerWK2App" "$WKT/$UIDIR/bin/TigerBrowser2" "$WKT/$WEBDIR/bin/TigerWebProcess" "$WKT/$WEBDIR/bin/TigerNetworkProcess" "$WKT/$GPUDIR/bin/TigerGPUProcess" "$WKT/build/tigeraudio32"; do
    [ -f "$f" ] && FILES="$FILES $f" || echo "stage-app: missing $f (not copied)"
done
rsync -t -z --partial --inplace --bwlimit=20000 $FILES tiger-eth:/Users/shg/wk2features/bin/
# Always sync the framework: the rebased QuartzCore changed twice today (rebase-dylib passes).
rsync -rtl -z --partial --inplace --bwlimit=20000 "$WKT/spike/CAHost/Frameworks/QuartzCore.framework" tiger-eth:/Users/shg/wk2features/Frameworks/
# One ssh invocation: launch, wait, capture. The app looks for its helpers next to itself.
# Safety net: the app is killed by alarm 3 s after it would have exited on its own, and
# every helper is killed after the run, so nothing can sit on the socket pool if the run
# goes wrong. One ssh invocation: launch, wait, capture, clean up, report the pool.
ssh tiger-eth "cd /Users/shg/wk2features/bin && (perl -e 'alarm $((SECS + 3)); exec @ARGV' -- env $APP_ENV ./$APP '$URL' $SECS -WebKitLogging Process,Loading > /Users/shg/wk2features/app.log 2>&1 &) ; sleep 8; echo == procs at 8s; ps -axo pid,rss,%cpu,command | grep -E 'Tiger(WK2App|Browser2|WebProcess|NetworkProcess|GPUProcess)' | grep -v grep | cut -c1-90; netstat -m | grep 'clusters in use'; sleep $((SECS - 11)); screencapture -x /Users/shg/wk2features/shot.png; sleep 5; ps -axo pid,command | awk '\$2 ~ /^\.\/Tiger/ || \$2 ~ /\/wk2[a-z]*\// {print \$1}' | xargs kill 2>/dev/null; rm -f /tmp/webkit-audio-*; sleep 1; echo == after; ps -axo pid,command | grep -E 'Tiger(WK2App|Browser2|WebProcess|NetworkProcess|GPUProcess)' | grep -v grep; netstat -m | grep -E 'clusters in use|denied'; true"
scp -qO tiger-eth:/Users/shg/wk2features/shot.png "$SHOT"
if [ -n "$LOG" ]; then scp -qO tiger-eth:/Users/shg/wk2features/app.log "$LOG"; fi
echo "== app.log"; ssh tiger-eth 'tail -40 /Users/shg/wk2features/app.log'
echo "screenshot: $SHOT"
