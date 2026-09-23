#!/usr/bin/env python3
"""Cookie conformance server for the Tiger port (tools/regress.sh `cookies`).

  tools/cookie-server.py serve LOG      # HTTP :8480 and HTTPS :8443 on this Mac, appends to LOG
  tools/cookie-server.py report LOG     # the pass/fail table from what the server saw

Two sites: A = https://shg-mbp.local:8443 (plus http://shg-mbp.local:8480, same host, not
secure) and B = https://192.168.1.253:8443 (cross-site). The browser's first run loads
A/start, which sets every cookie case at once and walks a chain of fetches, an iframe
from B, a redirect chain, an http page, a top-level visit to B and back to A. The second
run (after quitting the app) loads A/echo?k=relaunch. The server itself is the oracle:
it logs the Cookie header of every request, and the pages POST document.cookie to
/report. The certificate is self-signed for both names; the box trusts it through a
copy of cacert.pem with it appended (CERT_DIR/bundle.pem).
"""
import http.server, json, os, ssl, subprocess, sys, threading, time, urllib.parse

A_HOST, B_HOST = "shg-mbp.local", "192.168.1.253"
HTTP_PORT, HTTPS_PORT = 8480, 8443
A, A_HTTP, B = f"https://{A_HOST}:{HTTPS_PORT}", f"http://{A_HOST}:{HTTP_PORT}", f"https://{B_HOST}:{HTTPS_PORT}"
CERT_DIR = "/tmp/tiger-cookie-cert"
LONG = "L" * 3000
FAR = "Max-Age=31536000"

# name -> Set-Cookie attributes, all set by A/start over https.
START_COOKIES = {
    "c_plain": "Path=/",
    "c_httponly": "Path=/; HttpOnly",
    "c_secure": "Path=/; Secure",
    "c_sn": "Path=/; Secure; SameSite=None",
    "c_lax": "Path=/; SameSite=Lax",
    "c_strict": "Path=/; SameSite=Strict",
    "c_domain": f"Path=/; Domain={A_HOST}",
    "c_path": "Path=/sub",
    "c_maxage": "Path=/; Max-Age=86400",
    "c_expires": "Path=/; Expires=Wed, 01 Jan 2031 00:00:00 GMT",
    "c_exp2039": "Path=/; Expires=Sat, 01 Jan 2039 00:00:00 GMT",
    "__Host-ok": "Path=/; Secure",
    "__Host-bad": f"Path=/; Secure; Domain={A_HOST}",
    "__Secure-ok": "Path=/; Secure",
    "__Secure-bad": "Path=/",
    "c_long": "Path=/",
    "c_del": f"Path=/; {FAR}",
    # the x.com shapes: ct0 is read by the page's JS for x-csrf-token, auth_token is not
    "ct0": f"Path=/; Domain=.{A_HOST}; Secure; SameSite=Lax; {FAR}",
    "auth_token": f"Path=/; Domain=.{A_HOST}; Secure; HttpOnly; SameSite=None; {FAR}",
}
SESSION = {"c_plain", "c_httponly", "c_secure", "c_sn", "c_lax", "c_strict", "c_domain", "__Host-ok",
           "__Secure-ok", "c_long", "c_js", "c_xhr", "c_r1", "c_r2"}
PERSISTENT = {"c_maxage", "c_expires", "c_exp2039", "ct0", "auth_token", "c_jsp"}
SECURE = {"c_secure", "c_sn", "__Host-ok", "__Secure-ok", "ct0", "auth_token", "c_xhr"}
HTTPONLY = {"c_httponly", "auth_token"}
BADPREFIX = {"__Host-bad", "__Secure-bad"}  # refused at set time
NEVER = BADPREFIX | {"c_del"}  # c_del is deleted by /xhr-set
AT_ROOT = SESSION | PERSISTENT  # everything a request for A:/ should carry after /start's script

PAGE = """<!doctype html><title>cookies</title><body style="font:14px Helvetica"><h3 id=h>cookies: %s</h3><pre id=o></pre>
<script>
function log(s) { document.getElementById('o').textContent += s + '\\n'; console.log('COOKIE-TEST ' + s); }
function report(k, v) { return fetch('/report?k=' + k, {method: 'POST', body: v, credentials: 'include'}); }
%s
</script>"""

START_JS = """
(async () => {
  await report('dom1', document.cookie);
  document.cookie = 'c_js=1; path=/';
  document.cookie = 'c_jsp=1; max-age=86400; path=/';
  document.cookie = 'c_httponly=overwritten-by-js; path=/';
  await fetch('/xhr-set', {credentials: 'include'});
  await fetch('/echo?k=fetch', {credentials: 'include'});
  await fetch('/sub/echo?k=path', {credentials: 'include'});
  await report('dom2', document.cookie);
  log('iframe');
  await new Promise(done => {
    addEventListener('message', done);
    setTimeout(done, 6000);
    const f = document.createElement('iframe'); f.src = '%s/frame'; document.body.appendChild(f);
  });
  log('redirect chain');
  location = '/redir1';
})();
""" % B

FRAME_JS = """
(async () => {
  await fetch('/echo?k=frame-fetch', {credentials: 'include'});
  await report('frame-dom', document.cookie);
  await fetch('%s/echo?k=xsite-fetch', {credentials: 'include', mode: 'no-cors'}).catch(() => 0);
  parent.postMessage('done', '*');
})();
""" % A

# The top-level chain after the redirects: k -> next URL.
CHAIN = {"redirect": f"{A_HTTP}/echo?k=http", "http": f"{B}/echo?k=crosssite",
         "crosssite": f"{A}/echo?k=back", "back": None, "relaunch": None}

LOCK = threading.Lock()
LOG_PATH = None


def record(entry):
    with LOCK, open(LOG_PATH, "a") as f:
        f.write(json.dumps(entry) + "\n")


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def send(self, code, body=b"", ctype="text/html", cookies=(), headers=()):
        self.send_response(code)
        for c in cookies:
            self.send_header("Set-Cookie", c)
        for k, v in headers:
            self.send_header(k, v)
        origin = self.headers.get("Origin")
        if origin:  # fetches from the other site read nothing, but the preflight-free ones must not fail
            self.send_header("Access-Control-Allow-Origin", origin)
            self.send_header("Access-Control-Allow-Credentials", "true")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def handle_any(self):
        url = urllib.parse.urlsplit(self.path)
        q = dict(urllib.parse.parse_qsl(url.query))
        secure = isinstance(self.connection, ssl.SSLSocket)
        host = self.headers.get("Host", "")
        body = self.rfile.read(int(self.headers.get("Content-Length") or 0)).decode("utf-8", "replace")
        record({"t": time.time(), "method": self.command, "host": host, "secure": secure, "path": url.path,
                "k": q.get("k"), "cookie": self.headers.get("Cookie", ""), "body": body})
        p = url.path
        if p == "/start":
            cookies = [f"{n}={LONG if n == 'c_long' else '1'}; {a}" for n, a in START_COOKIES.items()]
            self.send(200, (PAGE % ("start", START_JS)).encode(), cookies=cookies)
        elif p == "/xhr-set":
            self.send(200, b"ok", "text/plain", cookies=["c_xhr=1; Path=/; Secure; SameSite=None", "c_del=; Path=/; Max-Age=0"])
        elif p == "/frame":
            self.send(200, (PAGE % ("frame", FRAME_JS)).encode(),
                      cookies=["c_frame=1; Path=/; Secure; SameSite=None", "c_frame_lax=1; Path=/; SameSite=Lax"])
        elif p == "/redir1":
            self.send(302, cookies=["c_r1=1; Path=/"], headers=[("Location", "/redir2")])
        elif p == "/redir2":
            self.send(302, cookies=["c_r2=1; Path=/"], headers=[("Location", "/echo?k=redirect")])
        elif p == "/report":
            self.send(200, b"ok", "text/plain")
        elif p.endswith("/echo"):
            k = q.get("k", "")
            cookies = ["c_insecure_secure=1; Path=/; Secure"] if k == "http" else []
            nxt = CHAIN.get(k)
            js = "log(%s);" % json.dumps(self.headers.get("Cookie", ""))
            js += ("setTimeout(() => location = %s, 300);" % json.dumps(nxt)) if nxt else "document.title = 'COOKIES DONE';"
            if self.command == "GET" and self.headers.get("Sec-Fetch-Dest", "document") == "document" and k in CHAIN:
                self.send(200, (PAGE % (k, js)).encode(), cookies=cookies)
            else:
                self.send(200, self.headers.get("Cookie", "").encode(), "text/plain", cookies=cookies)
        else:
            self.send(404, b"no", "text/plain")

    do_GET = do_POST = handle_any


def ensure_cert():
    os.makedirs(CERT_DIR, exist_ok=True)
    cert, key = f"{CERT_DIR}/cert.pem", f"{CERT_DIR}/key.pem"
    if not os.path.exists(cert):
        subprocess.check_call(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "3650",
                               "-keyout", key, "-out", cert, "-subj", f"/CN={A_HOST}",
                               "-addext", f"subjectAltName=DNS:{A_HOST},IP:{B_HOST}"],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    bundle = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "deps", "src", "cacert.pem")
    with open(f"{CERT_DIR}/bundle.pem", "w") as out:
        out.write(open(bundle).read() + "\n" + open(cert).read())
    return cert, key


def serve(log_path):
    global LOG_PATH
    LOG_PATH = log_path
    cert, key = ensure_cert()
    plain = http.server.ThreadingHTTPServer(("0.0.0.0", HTTP_PORT), Handler)
    tls = http.server.ThreadingHTTPServer(("0.0.0.0", HTTPS_PORT), Handler)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(cert, key)
    tls.socket = ctx.wrap_socket(tls.socket, server_side=True)
    threading.Thread(target=plain.serve_forever, daemon=True).start()
    print(f"cookie-server: {A}/start, bundle {CERT_DIR}/bundle.pem, log {log_path}", flush=True)
    tls.serve_forever()


def parse(header):
    out = {}
    for part in header.split(";"):
        if "=" in part:
            n, v = part.strip().split("=", 1)
            out[n] = v
    return out


def report(log_path):
    seen = {}
    for line in open(log_path):
        e = json.loads(line)
        if e["path"] == "/report":
            seen[e["k"]] = parse(e["body"])
        elif e["k"] and e["k"] not in seen:
            seen[e["k"]] = parse(e["cookie"])
    rows = []

    def check(name, k, want=(), absent=(), info=False):
        if k not in seen:
            rows.append((name, "INFO" if info else "FAIL", f"never reached ({k})"))
            return
        got = seen[k]
        missing = sorted(set(want) - set(got))
        extra = sorted(set(absent) & set(got))
        detail = "; ".join(filter(None, [missing and "missing " + " ".join(missing), extra and "unexpected " + " ".join(extra)]))
        verdict = "PASS" if not detail else ("INFO" if info else "FAIL")
        rows.append((name, verdict, detail or "ok"))

    visible = AT_ROOT - HTTPONLY - {"c_js", "c_jsp", "c_xhr", "c_r1", "c_r2"}
    check("document.cookie shows non-HttpOnly (incl. ct0)", "dom1", visible | {"c_del"}, HTTPONLY | BADPREFIX | {"c_path"})
    check("__Host-/__Secure- prefix rules", "dom1", {"__Host-ok", "__Secure-ok"}, {"__Host-bad", "__Secure-bad"})
    check("long value (3000 bytes)", "dom1", {"c_long"})
    check("Expires in 2031 and 2039", "dom1", {"c_expires", "c_exp2039"})
    check("fetch(credentials) sends all, incl. HttpOnly", "fetch", AT_ROOT - {"c_r1", "c_r2"}, NEVER | {"c_path"})
    check("JS cannot overwrite HttpOnly", "fetch")
    if "fetch" in seen and seen["fetch"].get("c_httponly") != "1":
        rows[-1] = (rows[-1][0], "FAIL", f"c_httponly={seen['fetch'].get('c_httponly')!r}")
    check("Set-Cookie on fetch response; Max-Age=0 deletes", "dom2", {"c_xhr", "c_js", "c_jsp"}, {"c_del", "c_httponly"})
    check("Path=/sub sent under /sub only", "path", {"c_path"})
    check("302 chain sets cookies on each hop", "redirect", {"c_r1", "c_r2"} | AT_ROOT, NEVER)
    check("http: no Secure cookies sent", "http", AT_ROOT - SECURE, SECURE)
    check("http: Set-Cookie with Secure refused", "back", absent={"c_insecure_secure"}, info=True)
    check("cross-site top level: A's cookies stay on A", "crosssite", absent=AT_ROOT)
    check("third-party iframe cookie (Safari blocks)", "frame-dom", absent={"c_frame"}, info=True)
    check("cross-site fetch: Lax/Strict withheld", "xsite-fetch", absent={"c_lax", "c_strict", "c_plain"}, info=True)
    check("cross-site nav back: Strict withheld", "back", absent={"c_strict"}, info=True)
    check("back on A after B: session cookies persist", "back", AT_ROOT - {"c_strict"}, NEVER | {"c_path"})
    check("relaunch: persistent survive", "relaunch", PERSISTENT)
    check("relaunch: session cookies gone", "relaunch", absent=SESSION)
    print("| case | verdict | detail |\n| --- | --- | --- |")
    for r in rows:
        print("| %s | %s | %s |" % r)
    fails = sum(r[1] == "FAIL" for r in rows)
    print(f"\n{len(rows) - fails}/{len(rows)} not failing")
    return fails


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] not in ("serve", "report"):
        sys.exit(__doc__)
    if sys.argv[1] == "serve":
        serve(sys.argv[2])
    else:
        sys.exit(1 if report(sys.argv[2]) else 0)
