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
# The 2M-iteration loop from spike/TigerBrowser/testpages/script.html, which is
# what the i386 browser spike timed, so the numbers are comparable.
cat > ~/wk2web/www/script.html <<'BODY'
<!DOCTYPE html>
<html><head><title>JS Timing Test</title></head>
<body><h1>JS Timing Test</h1>
<div id="out">running...</div>
<script>
  var start = new Date().getTime();
  var sum = 0;
  for (var i = 0; i < 2000000; i++) { sum += i % 7; }
  var elapsed = new Date().getTime() - start;
  document.getElementById('out').innerHTML =
    'RESULT sum=' + sum + ' loopms=' + elapsed;
</script>
</body></html>
BODY

# 2,000 nodes built from script, then styles mutated for 60 frames in a
# requestAnimationFrame loop with the per-frame cost written back into the DOM.
# rAF is the interesting part on this port: there is no display refresh monitor,
# so frames only advance when the UI process answers an update with
# DrawingArea::DisplayDidRefresh. If rAF runs at all here, the loop is closed.
cat > ~/wk2web/www/domloop.html <<'BODY'
<!DOCTYPE html>
<html><head><title>DOM loop</title>
<style>
 body { font: 12px "Lucida Grande", sans-serif; margin: 8px; background: #ffffff; }
 #grid { width: 780px; }
 .cell { display: inline-block; width: 6px; height: 6px; margin: 1px; background: #d0d0d0; }
 #out { font: 14px monospace; color: #103a70; }
</style></head>
<body>
<div id="out">building...</div>
<div id="grid"></div>
<script>
var buildStart = new Date().getTime();
var grid = document.getElementById('grid');
var cells = [];
for (var i = 0; i < 2000; i++) {
  var d = document.createElement('div');
  d.className = 'cell';
  grid.appendChild(d);
  cells.push(d);
}
var buildMs = new Date().getTime() - buildStart;

var frames = 0, totalMs = 0, worst = 0, last = new Date().getTime();
function frame() {
  var now = new Date().getTime();
  var dt = now - last; last = now;
  if (frames) { totalMs += dt; if (dt > worst) worst = dt; }
  var phase = frames * 7;
  for (var i = 0; i < cells.length; i++) {
    var v = (i * 3 + phase) % 255;
    cells[i].style.backgroundColor = 'rgb(' + v + ',' + (255 - v) + ',128)';
  }
  frames++;
  if (frames <= 60) {
    requestAnimationFrame(frame);
  } else {
    document.getElementById('out').innerHTML =
      'RESULT nodes=' + cells.length + ' buildms=' + buildMs +
      ' frames=' + (frames - 1) + ' avgms=' + Math.round(totalMs / (frames - 2)) +
      ' worstms=' + worst;
  }
}
requestAnimationFrame(frame);
</script>
</body></html>
BODY

# Taller than the 800x600 view, for the wheel test. The marker div's colour at a
# known y only appears once the view has scrolled.
cat > ~/wk2web/www/tall.html <<'BODY'
<!DOCTYPE html>
<html><head><title>Tall</title>
<style>
 body { margin: 0; background: #ffffff; }
 .band { height: 400px; }
 #top { background: #ffffff; }
 #mark { background: #1e8c32; }
 .filler { height: 2000px; background: #ffffff; }
</style></head>
<body>
<div class="band" id="top">top band, white</div>
<div class="band" id="mark">the green marker band, 400px tall, starting at y=400</div>
<div class="filler"></div>
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
curl -s -o /dev/null -w "  page.html %{http_code}" http://127.0.0.1:8391/page.html
curl -s -o /dev/null -w "  script.html %{http_code}" http://127.0.0.1:8391/script.html
curl -s -o /dev/null -w "  domloop.html %{http_code}" http://127.0.0.1:8391/domloop.html
curl -s -o /dev/null -w "  tall.html %{http_code}\n" http://127.0.0.1:8391/tall.html
