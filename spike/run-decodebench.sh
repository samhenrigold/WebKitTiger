#!/bin/bash
# Deploy decodebench + clips to the Tiger box and run the whole matrix.
# The box is shared with other agents, so every configuration is run 3x and the
# BEST run is what logs/decodebench-tiger.txt reports (external load can only
# make a run slower). Raw runs land in logs/decodebench-tiger-raw.txt.
set -euo pipefail
WKT=/Users/shg/Developer/WebKitTiger
cd "$WKT"
REPS=${REPS:-3}

bash spike/build-decodebench.sh
ssh tiger 'mkdir -p /tmp/media64'
scp -O build/decodebench64 tiger:/tmp/ >/dev/null
scp -O spike/media64/h264-*.mp4 spike/media64/vp9-*.webm spike/media64/av1-*.mp4 \
       spike/media64/aac-128k.m4a spike/media64/opus-96k.webm spike/media64/mp3-128k.mp3 \
       tiger:/tmp/media64/ >/dev/null

ssh tiger "chmod +x /tmp/decodebench64
uptime
for r in \$(jot $REPS); do
  for f in h264-480p30.mp4 h264-720p30.mp4 h264-1080p30.mp4 vp9-480p30.webm vp9-720p30.webm av1-480p30.mp4; do
    for t in 1 2; do
      # throughput: decode only, nothing serialized behind the decoder
      /tmp/decodebench64 -t \$t /tmp/media64/\$f | tr '\n' ' '; echo
      # conversion cost: yuv420p -> BGRA, single decoder thread
      /tmp/decodebench64 -t \$t -s /tmp/media64/\$f | tr '\n' ' '; echo
    done
  done
  for f in aac-128k.m4a opus-96k.webm mp3-128k.mp3; do
    /tmp/decodebench64 -a -t 1 /tmp/media64/\$f 2>/dev/null | tr '\n' ' '; echo
  done
done" 2>&1 | tee "$WKT/logs/decodebench-tiger-raw.txt" \
  | awk '
  /FILE=/ {
    split("", kv); n = split($0, f, " ");
    for (i = 1; i <= n; i++) { p = index(f[i], "="); if (p) kv[substr(f[i],1,p-1)] = substr(f[i],p+1) }
    key = kv["FILE"] "|" kv["THREADS"] "|" ("SWS_MS_PER_FRAME" in kv ? "sws" : "dec")
    val = ("FPS" in kv) ? kv["FPS"] : kv["REALTIME_X"]
    if (!(key in best) || val + 0 > best[key] + 0) {
      best[key] = val; line[key] = $0
    }
  }
  END { for (k in line) print line[k] }' | sort | tee "$WKT/logs/decodebench-tiger.txt"
