// Same shapes as spike/TigerBrowser/testpages/script.html and logs/jsc64-spike.md.
function loop() { var s = 0; for (var i = 0; i < 2000000; i++) s += i; return s; }
function fib(n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
function time(name, f) { var t = Date.now(); var r = f(); print(name + ": " + (Date.now() - t) + " ms (" + r + ")"); }
for (var k = 0; k < 3; k++) time("2M loop", loop);
time("fib(30)", function () { return fib(30); });
time("fib(25)", function () { return fib(25); });
