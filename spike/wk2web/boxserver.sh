#!/bin/sh
# TIGER: the local HTTP server the netdriver and pagedriver runs fetch from.
# 10.4 ships Python 2.3.5, which has SimpleHTTPServer, so no C server is needed.
mkdir -p ~/wk2web/www
cat > ~/wk2web/www/hello.txt <<'BODY'
tiger-netdriver-ok
BODY
cat > ~/wk2web/www/page.html <<'BODY'
<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>Tiger page driver</title>
<style>
  body { background: #ffffff; font: 16px "Lucida Grande", sans-serif; margin: 20px; }
  h1   { font-size: 28px; color: #103a70; }
  .box { width: 320px; height: 120px; background: #c83232; color: #ffffff;
         padding: 12px; margin: 16px 0; }
  .controls { margin-top: 18px; }
</style></head>
<body>
  <h1>Tiger WebKit2</h1>
  <p>Text in the default serif-ish stack, laid out by WebCore and rasterized by cairo
     in the 64-bit web process.</p>
  <div class="box">A coloured div, 320&times;120, #c83232.</div>
  <div class="controls">
    <input type="text" value="an input">
    <select><option>first</option><option>second</option></select>
  </div>
</body></html>
BODY
cd ~/wk2web/www
python -c "
import SimpleHTTPServer, SocketServer
SocketServer.TCPServer.allow_reuse_address = True
httpd = SocketServer.TCPServer(('127.0.0.1', 8391), SimpleHTTPServer.SimpleHTTPRequestHandler)
httpd.serve_forever()
" > /tmp/httpd.log 2>&1 &
echo $! > /tmp/httpd.pid
sleep 1
curl -s -o /dev/null -w "local server: hello.txt %{http_code}" http://127.0.0.1:8391/hello.txt
curl -s -o /dev/null -w "  page.html %{http_code}\n" http://127.0.0.1:8391/page.html
