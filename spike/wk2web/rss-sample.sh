#!/bin/sh
# Runs ON THE BOX (stage-malloc.sh copies it to ~/wk2video/share): N samples, 2 s apart,
# of the summed RSS of the staged web processes. Bounded; leaves nothing running.
#   BOXCMD='sh /Users/shg/wk2video/share/rss-sample.sh 44' spike/wk2web/stage-malloc.sh ...
n=${1:-30}; i=0
while [ $i -lt $n ]; do
    r=$(ps -axo rss,command | awk '/[w]k2video\/bin\/.\/TigerWebProcess/ {s += $1} END {print int(s / 1024)}')
    echo "RSS t=$((i * 2)) rss=$r"
    i=$((i + 1)); sleep 2
done
