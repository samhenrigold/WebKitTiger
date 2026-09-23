#!/usr/bin/env python3
"""Serves spike/wk2web/features.html for tools/regress.sh `features`.

  tools/features-server.py [port]     # https://192.168.1.253:8444/features.html

HTTPS (a secure context, so service workers, the Cache API and crossOriginIsolated are
available) with COOP/COEP on every response (SharedArrayBuffer), plus two endpoints the
page's functional tests need: /stream (a chunked body in three parts, for fetch streams)
and /ws (a one-message WebSocket echo). The certificate is tools/cookie-server.py's
(IP SAN 192.168.1.253), so the box trusts it through that bundle.
"""
import base64, hashlib, http.server, os, ssl, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
ensure_cert = __import__("cookie-server").ensure_cert

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "spike", "wk2web")
TYPES = {".html": "text/html", ".js": "text/javascript"}


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def head(self, code, ctype, length=None):
        self.send_response(code)
        for k, v in (("Cross-Origin-Opener-Policy", "same-origin"), ("Cross-Origin-Embedder-Policy", "require-corp"),
                     ("Cross-Origin-Resource-Policy", "same-origin"), ("Cache-Control", "no-store"), ("Content-Type", ctype)):
            self.send_header(k, v)
        self.send_header(*(("Content-Length", str(length)) if length is not None else ("Transfer-Encoding", "chunked")))
        self.end_headers()

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/ws":
            key = self.headers["Sec-WebSocket-Key"] + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
            self.send_response(101)
            self.send_header("Upgrade", "websocket")
            self.send_header("Connection", "Upgrade")
            self.send_header("Sec-WebSocket-Accept", base64.b64encode(hashlib.sha1(key.encode()).digest()).decode())
            self.end_headers()
            f = self.rfile
            b0, b1 = f.read(2)
            n = b1 & 0x7F  # the page sends one short masked text frame
            mask = f.read(4)
            data = bytes(c ^ mask[i % 4] for i, c in enumerate(f.read(n)))
            self.wfile.write(bytes([0x81, len(data)]) + data)
            self.wfile.flush()
            time.sleep(1)
            self.close_connection = True
            return
        if path == "/stream":
            self.head(200, "text/plain")
            for part in (b"one,", b"two,", b"three"):
                self.wfile.write(b"%x\r\n%s\r\n" % (len(part), part))
                self.wfile.flush()
                time.sleep(0.3)
            self.wfile.write(b"0\r\n\r\n")
            return
        file = os.path.join(ROOT, os.path.basename(path) or "features.html")
        if not os.path.isfile(file):
            self.head(404, "text/plain", 0)
            return
        body = open(file, "rb").read()
        self.head(200, TYPES.get(os.path.splitext(file)[1], "application/octet-stream"), len(body))
        self.wfile.write(body)


if __name__ == "__main__":
    cert, key = ensure_cert()
    server = http.server.ThreadingHTTPServer(("0.0.0.0", int(sys.argv[1]) if len(sys.argv) > 1 else 8444), Handler)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(cert, key)
    server.socket = ctx.wrap_socket(server.socket, server_side=True)
    server.serve_forever()
