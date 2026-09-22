#!/bin/sh
# Faithful-mode variant of stage-app.sh: stages build/tiger-{ui,gpu}-faithful binaries with TIGER_FAITHFUL=1.
# Stage TigerWK2App with its helper processes and the rebundled CoreAnimation on the
# box, run it against a URL, and bring back a screenshot and the log.
#   spike/wk2web/stage-app.sh [url] [seconds]
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
# Never start while another agent's Tiger* processes are alive on the box (the cleanup
# below kills every Tiger* process). Waits up to 2 minutes for them to go away.
for i in $(seq 1 60); do
    ALIVE=$(ssh tiger-eth "ps -axo command | grep -E 'Tiger(WK2App|Browser2|WebProcess|NetworkProcess|GPUProcess)' | grep -v grep | wc -l")
    [ "${ALIVE:-0}" -eq 0 ] && break
    [ $i -eq 1 ] && echo "stage-app: $ALIVE Tiger* process(es) alive on the box, waiting"
    sleep 2
done
[ "${ALIVE:-0}" -eq 0 ] || { echo "stage-app: box still busy after 2 min, giving up"; exit 1; }
URL=${1:-file:///Users/shg/wk2faithful/share/faithful-test.html}
SECS=${2:-15}
APP_ENV=${APP_ENV:-}   # e.g. APP_ENV=TIGER_GPU=0 to keep the GPU process out
# Font manifest and CA bundle live under the staging dir; the processes read these two env vars.
APP_ENV="TIGER_FAITHFUL=${TIGER_FAITHFUL:-1} TIGER_FONT_MANIFEST=/Users/shg/wk2faithful/share/tiger-fonts.json TIGER_CA_BUNDLE=/Users/shg/wk2faithful/share/cacert.pem $APP_ENV"
ssh tiger-eth 'mkdir -p /Users/shg/wk2faithful/bin /Users/shg/wk2faithful/Frameworks /Users/shg/wk2faithful/share'
rsync -t -z "$WKT/logs/tiger-fonts.json" "$WKT/deps/src/cacert.pem" "$WKT/spike/wk2web/faithful-test.html" tiger-eth:/Users/shg/wk2faithful/share/ 2>/dev/null || echo "stage-app: share files not all copied (cacert.pem present?)"
# rsync -t skips binaries that have not changed since the last stage.
FILES=""
for f in "$WKT/build/tiger-ui-faithful/bin/TigerWK2App" "$WKT/build/tiger-web-port/bin/TigerWebProcess" "$WKT/build/tiger-web-port/bin/TigerNetworkProcess" "$WKT/build/tiger-gpu-faithful/bin/TigerGPUProcess"; do
    [ -f "$f" ] && FILES="$FILES $f" || echo "stage-app: missing $f (not copied)"
done
rsync -t -z --partial --inplace --bwlimit=20000 $FILES tiger-eth:/Users/shg/wk2faithful/bin/ || echo "stage-app: rsync of binaries incomplete (another build relinking?)"
ssh tiger-eth 'test -d /Users/shg/wk2faithful/Frameworks/QuartzCore.framework' 2>/dev/null || rsync -rtl -z --partial --inplace --bwlimit=20000 "$WKT/spike/CAHost/Frameworks/QuartzCore.framework" tiger-eth:/Users/shg/wk2faithful/Frameworks/
# One ssh invocation: launch, wait, capture. The app looks for its helpers next to itself.
# Safety net: the app is killed by alarm 3 s after it would have exited on its own, and
# every helper is killed after the run, so nothing can sit on the socket pool if the run
# goes wrong. One ssh invocation: launch, wait, capture, clean up, report the pool.
ssh tiger-eth "cd /Users/shg/wk2faithful/bin && (perl -e 'alarm $((SECS + 3)); exec @ARGV' -- env $APP_ENV ./TigerWK2App '$URL' $SECS -WebKitLogging Process,Loading > /Users/shg/wk2faithful/app.log 2>&1 &) ; sleep 8; echo == procs at 8s; ps -axo pid,rss,%cpu,command | grep -E 'Tiger(WK2App|WebProcess|NetworkProcess|GPUProcess)' | grep -v grep | cut -c1-90; netstat -m | grep 'clusters in use'; sleep $((SECS - 11)); screencapture -x /Users/shg/wk2faithful/shot.png; sleep 5; ps -axo pid,command | awk '\$2 ~ /^\\.\\/Tiger/ || \$2 ~ /\\/wk2[a-z]*\\// {print \$1}' | xargs kill 2>/dev/null; sleep 1; echo == after; ps -axo pid,command | grep -E 'Tiger(WK2App|WebProcess|NetworkProcess|GPUProcess)' | grep -v grep; netstat -m | grep -E 'clusters in use|denied'; true"
scp -qO tiger-eth:/Users/shg/wk2faithful/shot.png "$WKT/spike/wk2web/faithful-window.png"
echo "== app.log"; ssh tiger-eth 'tail -40 /Users/shg/wk2faithful/app.log'
echo "screenshot: $WKT/spike/wk2web/faithful-window.png"
