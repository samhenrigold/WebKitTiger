#!/bin/sh
# Render sample.html on the box with pagedriver, once with the pre-change TigerWebProcess
# and once with the post-change one, draw the Quartz reference for the same integer
# baselines with ctref32int, and score both on the host (score.py, fast64's metric).
#   spike/fasttext/run-box.sh            (box must be free; each run < 30 s, nothing left behind)
set -e
WKT=/Users/shg/Developer/WebKitTiger
B=$WKT/build/tiger-web-text
# SAMPLE/REF/LINE0/LEADING select the page and its matching reference: the default pair, or
#   SAMPLE=sample-zoom.html REF=ctref32zoom LINE0=19.125 LEADING=33.75 OUT=.../out/zoom
SAMPLE=${SAMPLE:-sample.html}; REF=${REF:-ctref32int}; LINE0=${LINE0:-17}; LEADING=${LEADING:-30}
OUT=${OUT:-$WKT/spike/fasttext/out/pagedriver}
# One user of the box at a time (same lock as spike/wk2web/stage-app.sh); held on fd 9 for the run.
exec 9>"$WKT/spike/wk2web/box.lock"
python3 -c 'import fcntl,os,sys,time
f=os.fdopen(9,"w")
for _ in range(600):
    try: fcntl.flock(f, fcntl.LOCK_EX|fcntl.LOCK_NB); sys.exit(0)
    except OSError: time.sleep(1)
sys.exit(1)' || { echo "run-box: box busy for 10 min, giving up"; exit 1; }
mkdir -p "$OUT"
ssh tiger-eth 'mkdir -p /tmp/fasttext/before /tmp/fasttext/after /tmp/fasttext/www /tmp/fasttext/ref'
rsync -t "$B/bin/pagedriver" "$B/bin/TigerNetworkProcess" "$B/bin/TigerWebProcess" tiger-eth:/tmp/fasttext/after/
rsync -t "$B/bin-before/TigerWebProcess" tiger-eth:/tmp/fasttext/before/
rsync -t "$WKT/spike/fasttext/$REF" tiger-eth:/tmp/fasttext/
rsync -t "$WKT/spike/fasttext/$SAMPLE" tiger-eth:/tmp/fasttext/www/
# One ssh session per step so a hang is visible and bounded. 10.4 has no timeout(1); perl's alarm does.
ssh tiger-eth "cd /tmp/fasttext && ./$REF ref smooth 2>&1 | tail -1"
ssh tiger-eth 'cd /tmp/fasttext/www && (python -c "
import SimpleHTTPServer, SocketServer
SocketServer.TCPServer.allow_reuse_address = True
SocketServer.TCPServer((\"127.0.0.1\", 8392), SimpleHTTPServer.SimpleHTTPRequestHandler).serve_forever()
" > /tmp/fasttext/httpd.log 2>&1 & echo $! > /tmp/fasttext/httpd.pid); sleep 1; curl -s -o /dev/null -w "httpd %{http_code}\n" http://127.0.0.1:8392/sample.html'
for v in before after; do
    ssh tiger-eth "cd /tmp/fasttext && TIGER_FONT_MANIFEST=/Users/shg/wk2/share/tiger-fonts.json perl -e 'alarm 28; exec @ARGV' ./after/pagedriver ./$v/TigerWebProcess ./after/TigerNetworkProcess http://127.0.0.1:8392/$SAMPLE /tmp/fasttext/$v.png > /tmp/fasttext/$v.log 2>&1; grep -E 'wrote|FAILED|timed out|DidFinishLoad' /tmp/fasttext/$v.log | tail -3; killall TigerWebProcess TigerNetworkProcess pagedriver 2>/dev/null || true"
done
ssh tiger-eth 'kill $(cat /tmp/fasttext/httpd.pid) 2>/dev/null; sleep 1; ps -axo pid,command | grep -E "fasttext|SimpleHTTP" | grep -v grep || echo "box clean"'
scp -qO tiger-eth:/tmp/fasttext/before.png tiger-eth:/tmp/fasttext/after.png tiger-eth:/tmp/fasttext/ref/ref-smooth.bin tiger-eth:/tmp/fasttext/ref/ref.glyphs "$OUT/"
cd "$WKT/spike/fasttext" && python3 score.py "$OUT/ref-smooth.bin" $LINE0 $LEADING "before=$OUT/before.png" "after=$OUT/after.png" --sheet "$OUT/contact.png" --zoom "$OUT/zoom.png" | tee "$OUT/results.txt"
