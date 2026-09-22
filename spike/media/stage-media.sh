#!/bin/sh
# Media track variant of spike/wk2web/stage-app.sh: same lock, same wait-for-clear-box,
# same cleanup; staged in its OWN directory /Users/shg/wk2media so a concurrent stage-app
# cannot swap the binaries underneath (it did). Web and network processes come from build/tiger-web-media and the
# i386 audio helper build/tigeraudio32 (make -C spike/media) goes next to them. The test
# page is served from this Mac: `python3 -m http.server 8765 -d spike/media` first.
#   spike/media/stage-media.sh [url] [seconds]
set -e
WKT=/Users/shg/Developer/WebKitTiger
exec 9>"$WKT/spike/wk2web/box.lock"
python3 -c 'import fcntl,sys,time
f=9  # the shell holds fd 9 for the whole run; locking a private open() would release on exit
for _ in range(600):
    try: fcntl.flock(f, fcntl.LOCK_EX|fcntl.LOCK_NB); sys.exit(0)
    except OSError: time.sleep(1)
sys.exit(1)' "$WKT/spike/wk2web/box.lock" || { echo "stage-media: box busy for 10 min, giving up"; exit 1; }
URL=${1:-http://192.168.1.253:8765/video.html}
SECS=${2:-30}
APP=${APP:-TigerBrowser2}
APP_ENV=${APP_ENV:-}
APP_ENV="TIGER_FONT_MANIFEST=/Users/shg/wk2/share/tiger-fonts.json TIGER_CA_BUNDLE=/Users/shg/wk2/share/cacert.pem $APP_ENV"
SHOT=${SHOT:-$WKT/spike/media/shot.png}
SHOT_AT=${SHOT_AT:-12}
WEBBIN=${WEBBIN:-$WKT/build/tiger-web-media}
for i in $(seq 1 60); do
    busy=$(ssh tiger-eth "ps -axo pid,command | grep -E '[T]iger(WK2App|Browser2|WebProcess|NetworkProcess|GPUProcess)|[p]agedriver|[t]igeraudio32' | head -3")
    [ -z "$busy" ] && break
    [ "$i" = 1 ] && echo "stage-media: waiting, already on the box:" && echo "$busy"
    sleep 5
done
ssh tiger-eth 'mkdir -p /Users/shg/wk2media/bin /Users/shg/wk2media/Frameworks /Users/shg/wk2/share'
rsync -t -z "$WKT/logs/tiger-fonts.json" "$WKT/deps/src/cacert.pem" tiger-eth:/Users/shg/wk2/share/ 2>/dev/null || true
FILES=""
for f in "$WKT/build/tiger-ui-port/bin/TigerWK2App" "$WKT/build/tiger-ui-port/bin/TigerBrowser2" "$WEBBIN/bin/TigerWebProcess" "$WEBBIN/bin/TigerNetworkProcess" "$WKT/build/tiger-gpu/bin/TigerGPUProcess" "$WKT/build/tigeraudio32"; do
    [ -f "$f" ] && FILES="$FILES $f" || echo "stage-media: missing $f (not copied)"
done
rsync -t -z --partial --inplace --bwlimit=20000 $FILES tiger-eth:/Users/shg/wk2media/bin/
rsync -rtl -z --partial --inplace --bwlimit=20000 "$WKT/spike/CAHost/Frameworks/QuartzCore.framework" tiger-eth:/Users/shg/wk2media/Frameworks/
ssh tiger-eth "cd /Users/shg/wk2media/bin && (perl -e 'alarm $((SECS + 3)); exec @ARGV' -- env $APP_ENV ./$APP '$URL' $SECS -WebKitLogging ${LOGGING:-Process,Loading} > /Users/shg/wk2media/app.log 2>&1 &) ; sleep $SHOT_AT; echo == procs at ${SHOT_AT}s; ps -axo pid,rss,%cpu,command | grep -E 'Tiger(WK2App|Browser2|WebProcess|NetworkProcess|GPUProcess)|tigeraudio32' | grep -v grep | cut -c1-90; screencapture -x /Users/shg/wk2media/shot.png; sleep $((SECS - SHOT_AT)); sleep 4; killall TigerWK2App TigerBrowser2 TigerWebProcess TigerNetworkProcess TigerGPUProcess tigeraudio32 2>/dev/null; sleep 1; echo == after; ps -axo pid,command | grep -E 'Tiger(WK2App|Browser2|WebProcess|NetworkProcess|GPUProcess)|tigeraudio32' | grep -v grep; rm -f /tmp/webkit-audio-*; true"
scp -qO tiger-eth:/Users/shg/wk2media/shot.png "$SHOT"
echo "== app.log (media lines)"; ssh tiger-eth 'grep -aE "TIGER-MEDIA|tigeraudio32|TIGER-CRASH|TIGER-TOMBSTONE|TIGER-CHILD" /Users/shg/wk2media/app.log | head -60; echo == tail; tail -15 /Users/shg/wk2media/app.log'
echo "screenshot: $SHOT"
