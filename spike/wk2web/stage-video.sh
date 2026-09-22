#!/bin/sh
# stage-app.sh for the direct-video track: stages into /Users/shg/wk2video (so a
# concurrent stage-app cannot swap the binaries underneath) from build/tiger-*-video,
# and reports ps at 8 s and 30 s plus a screenshot mid-play.
#   spike/wk2web/stage-video.sh [url] [seconds]
# WEBDIR=/.../build/tiger-web-port UIDIR=/.../build/tiger-ui-port runs the OLD path
# (the "before" measurement) from the same harness.
set -e
WKT=/Users/shg/Developer/WebKitTiger
DEST=/Users/shg/wk2video
WEBDIR=${WEBDIR:-$WKT/build/tiger-web-video}
UIDIR=${UIDIR:-$WKT/build/tiger-ui-video}
LOG=${LOG:-$WKT/logs/video-run.log}
SHOT=${SHOT:-$WKT/spike/media/shot-video480.png}
# One user of the box at a time: several agents share it. Waits up to 10 minutes.
exec 9>"$WKT/spike/wk2web/box.lock"
python3 -c 'import fcntl,sys,time
f=9
for _ in range(600):
    try: fcntl.flock(f, fcntl.LOCK_EX|fcntl.LOCK_NB); sys.exit(0)
    except OSError: time.sleep(1)
sys.exit(1)' || { echo "stage-video: box busy for 10 min, giving up"; exit 1; }
URL=${1:-http://192.168.1.253:8765/video480loop.html}
SECS=${2:-60}
APP=${APP:-TigerBrowser2}
APP_ENV=${APP_ENV:-}
APP_ENV="TIGER_FONT_MANIFEST=$DEST/share/tiger-fonts.json TIGER_CA_BUNDLE=$DEST/share/cacert.pem $APP_ENV"
for i in $(seq 1 60); do
    busy=$(ssh tiger-eth "ps -axo pid,command | grep -E '[T]iger(WK2App|Browser2|WebProcess|NetworkProcess|GPUProcess)|[p]agedriver' | head -3")
    [ -z "$busy" ] && break
    [ "$i" = 1 ] && echo "stage-video: waiting, already on the box:" && echo "$busy"
    sleep 5
done
ssh tiger-eth "mkdir -p $DEST/bin $DEST/Frameworks $DEST/share"
rsync -t -z "$WKT/logs/tiger-fonts.json" "$WKT/deps/src/cacert.pem" tiger-eth:$DEST/share/ 2>/dev/null || echo "stage-video: share files not all copied"
FILES=""
for f in "$UIDIR/bin/TigerBrowser2" "$WEBDIR/bin/TigerWebProcess" "$WEBDIR/bin/TigerNetworkProcess" "$WKT/build/tiger-gpu/bin/TigerGPUProcess" "$WKT/build/tigeraudio32"; do
    [ -f "$f" ] && FILES="$FILES $f" || echo "stage-video: missing $f (not copied)"
done
rsync -t -z --partial --inplace --bwlimit=20000 $FILES tiger-eth:$DEST/bin/
rsync -rtl -z --partial --inplace --bwlimit=20000 "$WKT/spike/CAHost/Frameworks/QuartzCore.framework" tiger-eth:$DEST/Frameworks/
ssh tiger-eth "cd $DEST/bin && (perl -e 'alarm $((SECS + 3)); exec @ARGV' -- env $APP_ENV ./$APP '$URL' $SECS > $DEST/app.log 2>&1 &) ; \
  sleep 8; echo '== procs at 8s'; ps -axo pid,rss,%cpu,command | grep -E 'Tiger(Browser2|WebProcess|NetworkProcess|GPUProcess)|tigeraudio32' | grep -v grep | cut -c1-80; \
  sleep 12; screencapture -x $DEST/shot.png; \
  sleep 10; echo '== procs at 30s'; ps -axo pid,rss,%cpu,command | grep -E 'Tiger(Browser2|WebProcess|NetworkProcess|GPUProcess)|tigeraudio32' | grep -v grep | cut -c1-80; \
  sleep $((SECS - 30)); killall TigerBrowser2 TigerWebProcess TigerNetworkProcess TigerGPUProcess tigeraudio32 2>/dev/null; rm -f /tmp/webkit-audio-* /tmp/webkit-video-*; sleep 1; \
  echo '== after'; ps -axo pid,command | grep -E 'Tiger(Browser2|WebProcess|NetworkProcess|GPUProcess)' | grep -v grep; true"
scp -qO tiger-eth:$DEST/shot.png "$SHOT"
ssh tiger-eth "cat $DEST/app.log" > "$LOG"
echo "== TIGER-MEDIA"; grep TIGER-MEDIA "$LOG" || tail -20 "$LOG"
echo "screenshot: $SHOT   log: $LOG"
