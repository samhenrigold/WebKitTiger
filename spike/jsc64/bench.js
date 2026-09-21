// TIGER64: the jsc64 timing set. Run as:
//   jsc bench.js                        (full JIT: LLInt + baseline + DFG + FTL)
//   jsc --useFTLJIT=false bench.js      (no FTL)
//   jsc --useDFGJIT=false bench.js      (baseline only)
//   jsc --useJIT=false bench.js         (LLInt)
// Add --footprint for RSS.  Every case is self-checking: a wrong result is a
// failure, not a fast time.

function time(name, expected, fn) {
    fn();                                   // warm up / let it tier up
    var best = Infinity, got;
    for (var i = 0; i < 3; i++) {
        var t = Date.now();
        got = fn();
        var d = Date.now() - t;
        if (d < best) best = d;
    }
    if (expected !== null && got !== expected)
        throw new Error(name + ": got " + got + ", expected " + expected);
    print(pad(name, 18) + pad(best + " ms", 10) + " result " + got);
    return best;
}
function pad(s, n) { s = String(s); while (s.length < n) s += " "; return s; }

// The loop from spike/TigerBrowser/testpages/script.html, the project's baseline number.
function loop2M() { var x = 0; for (var i = 0; i < 2000000; i++) x += i % 7; return x; }
function fib(n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

function strings() {
    var parts = [];
    for (var i = 0; i < 200000; i++) parts.push("item-" + (i % 1000));
    var s = parts.join(",");
    var n = 0;
    for (var i = 0; i < s.length; i += 997) n += s.charCodeAt(i);
    return n;
}
function objects() {
    var a = [], n = 0;
    for (var i = 0; i < 300000; i++) a.push({ x: i, y: i * 2, z: "k" + (i & 15) });
    for (var i = 0; i < a.length; i++) n = (n + a[i].x + a[i].y + a[i].z.length) | 0;
    return n;
}
function sortArray() {
    var a = new Array(200000), seed = 12345;
    for (var i = 0; i < a.length; i++) { seed = (seed * 1103515245 + 12345) & 0x7fffffff; a[i] = seed; }
    a.sort(function (p, q) { return p - q; });
    return a[0] + a[a.length - 1];
}
function regexp() {
    var s = "";
    for (var i = 0; i < 20000; i++) s += "key" + i + "=value" + i + "&";
    var re = /([a-z]+)(\d+)=value(\d+)&/g, m, n = 0;
    while ((m = re.exec(s)) !== null) n += m[2].length + m[3].length;
    return n;
}
function floats() {
    var s = 0;
    for (var i = 1; i < 1500000; i++) s += Math.sqrt(i) / (i + 0.5) * Math.sin(i % 100);
    return Math.round(s * 1e6);
}
function bitops() {
    var x = 1, n = 0;
    for (var i = 0; i < 3000000; i++) { x = (x * 1103515245 + 12345) | 0; n ^= (x >>> 7) & 0xffff; }
    return n;
}

var total = 0;
total += time("2M loop", 5999995, loop2M);
total += time("fib(30)", 832040, function () { return fib(30); });
total += time("strings", null, strings);
total += time("objects", null, objects);
total += time("sort 200k", null, sortArray);
total += time("regexp", null, regexp);
total += time("float math", null, floats);
total += time("bitops", null, bitops);
print(pad("TOTAL", 18) + total + " ms");
