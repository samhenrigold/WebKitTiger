#!/bin/sh
# Render sample.html on the box with pagedriver, once with the pre-change TigerWebProcess
# and once with the post-change one, draw the Quartz reference for the same integer
# baselines with ctref32int, and score both on the host (score.py, fast64's metric).
#   spike/fasttext/run-box.sh            (box must be free; each run < 30 s, nothing left behind)
set -e
WKT=/Users/shg/Developer/WebKitTiger
B=$WKT/build/tiger-web-text
OUT=$WKT/spike/fasttext/out/pagedriver
mkdir -p "$OUT"
ssh tiger 'mkdir -p /tmp/fasttext/before /tmp/fasttext/after /tmp/fasttext/www /tmp/fasttext/ref'
rsync -t "$B/bin/pagedriver" "$B/bin/TigerNetworkProcess" "$B/bin/TigerWebProcess" tiger:/tmp/fasttext/after/
rsync -t "$B/bin-before/TigerWebProcess" tiger:/tmp/fasttext/before/
rsync -t "$WKT/spike/fasttext/ctref32int" tiger:/tmp/fasttext/
rsync -t "$WKT/spike/fasttext/sample.html" tiger:/tmp/fasttext/www/
# One ssh session per step so a hang is visible and bounded. 10.4 has no timeout(1); perl's alarm does.
ssh tiger 'cd /tmp/fasttext && ./ctref32int ref smooth 2>&1 | tail -1'
ssh tiger 'cd /tmp/fasttext/www && (python -c "
import SimpleHTTPServer, SocketServer
SocketServer.TCPServer.allow_reuse_address = True
SocketServer.TCPServer((\"127.0.0.1\", 8392), SimpleHTTPServer.SimpleHTTPRequestHandler).serve_forever()
" > /tmp/fasttext/httpd.log 2>&1 & echo $! > /tmp/fasttext/httpd.pid); sleep 1; curl -s -o /dev/null -w "httpd %{http_code}\n" http://127.0.0.1:8392/sample.html'
for v in before after; do
    ssh tiger "cd /tmp/fasttext && perl -e 'alarm 28; exec @ARGV' ./after/pagedriver ./$v/TigerWebProcess ./after/TigerNetworkProcess http://127.0.0.1:8392/sample.html /tmp/fasttext/$v.png > /tmp/fasttext/$v.log 2>&1; grep -E 'wrote|FAILED|timed out|DidFinishLoad' /tmp/fasttext/$v.log | tail -3; pkill -f /tmp/fasttext/.*/Tiger || true"
done
ssh tiger 'kill $(cat /tmp/fasttext/httpd.pid) 2>/dev/null; sleep 1; ps -axo pid,command | grep -E "fasttext|SimpleHTTP" | grep -v grep || echo "box clean"'
scp -qO tiger:/tmp/fasttext/before.png tiger:/tmp/fasttext/after.png tiger:/tmp/fasttext/ref/ref-smooth.bin tiger:/tmp/fasttext/ref/ref.glyphs "$OUT/"
cd "$WKT/spike/fasttext" && python3 score.py "$OUT/ref-smooth.bin" 17 30 "before=$OUT/before.png" "after=$OUT/after.png" --sheet "$OUT/contact.png" --zoom "$OUT/zoom.png" | tee "$OUT/results.txt"
