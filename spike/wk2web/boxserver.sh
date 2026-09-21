#!/bin/sh
# TIGER: the local HTTP server the netdriver fetch runs against.
mkdir -p ~/wk2web/www
cat > ~/wk2web/www/hello.txt <<'BODY'
tiger-netdriver-ok
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
curl -s -o /dev/null -w "local server says %{http_code}\n" http://127.0.0.1:8391/hello.txt
