#!/bin/sh
# RSS runs for the memory track, through stage-recover.sh (on-box sampler, every 2 s).
#   spike/wk2web/rss-recover.sh <tag> nytimes|youtube|idle|memdrop   (UIDIR/WEBDIR as for stage-recover.sh)
# nytimes: 10 s load, then ~80 s of wheel ticks. youtube: a watch page playing for 90 s.
# idle: nytimes for 60 s scrolled, then example.com and 30 s idle. memdrop: memdrop.html?mode=objects.
WKT=/Users/shg/Developer/WebKitTiger
TAG=$1; KIND=$2
mkdir -p "$WKT/logs/recover"
case $KIND in
nytimes)
    S="wait 10"; i=0; while [ $i -lt 260 ]; do S="$S; wheel 400,300 0,-3"; i=$((i + 1)); done
    URL=https://www.nytimes.com/; SECS=100 ;;
youtube)
    S="wait 90"; URL="https://www.youtube.com/watch?v=f7NwyBnIRTE"; SECS=100 ;;
idle)
    S="wait 10"; i=0; while [ $i -lt 160 ]; do S="$S; wheel 400,300 0,-3"; i=$((i + 1)); done
    S="$S; wait 2; load http://example.com/; wait 40"
    URL=https://www.nytimes.com/; SECS=110 ;;
memdrop)
    S="wait 60"; URL="file:///Users/shg/wk2recover/share/memdrop.html?mode=objects"; SECS=65 ;;
*) echo "rss-recover: unknown kind $KIND"; exit 1 ;;
esac
OUT="$WKT/logs/recover/rss-$KIND-$TAG.log" SCRIPT="${PRE_SCRIPT:+$PRE_SCRIPT; }$S" "$WKT/spike/wk2web/stage-recover.sh" "$URL" "$SECS"
