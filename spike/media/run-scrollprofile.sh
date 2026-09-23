#!/bin/sh
# One scripted-scroll profile run on the box: 25 s to load, then ~65 s of wheel ticks,
# all threads sampled every 250 ms and the style/image probe on.
#   spike/media/run-scrollprofile.sh <name> <url>
# Leaves logs/perf/scroll2/<name>.log and .png; bucket with
#   tools/tiger-profile.py <log> build/tiger-web-media/bin/TigerWebProcess
set -e
WKT=/Users/shg/Developer/WebKitTiger
NAME=$1
URL=$2
mkdir -p "$WKT/logs/perf/scroll2"
SCRIPT="wait 25"
i=0
while [ $i -lt 200 ]; do SCRIPT="$SCRIPT; wheel 400,300 0,-3"; i=$((i + 1)); done
WEBBIN=$WKT/build/tiger-web-media \
UIBIN=$WKT/build/tiger-ui-media \
APP_ENV="TIGER_SAMPLE_MAIN=400 TIGER_STYLE_PROBE=1" \
SCRIPT="$SCRIPT" \
SHOT="$WKT/logs/perf/scroll2/$NAME.png" \
SHOT_AT=60 \
OUT="$WKT/logs/perf/scroll2/$NAME.log" \
    "$WKT/spike/media/stage-media.sh" "$URL" 95
