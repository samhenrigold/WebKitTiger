// TIGER64: conservative GC with several JS threads allocating under the JIT.
//
// Each $.agent worker is its own VM on its own pthread, but each VM's collector
// runs on its own thread too, so every collection here suspends a *different*
// thread and reads its registers with thread_get_state(x86_THREAD_STATE64) --
// the call this whole port was unsure of on a 32-bit 10.4 kernel.  Run it with
//   --collectContinuously=true
// so the collector is also firing on its own, not only where gc() asks.
//
// The liveness half: the only reference to `keep` inside hot() is a local, so it
// lives in a register or a JIT stack slot while gc() runs.  If the conservative
// scan of a suspended thread missed those, the sums below would come back wrong
// or the process would die.  Values are checked, not just survived.

var THREADS = 3;          // workers, plus the main thread
var ROUNDS = 160;          // gc() calls per thread
var WIDTH = 4000;         // live objects held across each gc()

var body = `
function hot(seed, rounds, width) {
    var gcs = 0, check = 0;
    for (var r = 0; r < rounds; r++) {
        // Only reference is this local -> register or JIT frame slot.
        var keep = [];
        for (var i = 0; i < width; i++)
            keep.push({ a: (seed + i) | 0, b: "s" + ((seed + i) % 97), c: [i, i + 1, i + 2] });
        // Garbage with no roots at all, so there is something to actually collect.
        for (var i = 0; i < width; i++) { var junk = { x: i, y: "t" + i }; junk.self = junk; }
        gc(); gcs++;
        var sum = 0;
        for (var i = 0; i < keep.length; i++) {
            var o = keep[i];
            sum += o.a + o.b.length + o.c[2];
        }
        var want = 0;
        for (var i = 0; i < width; i++)
            want += ((seed + i) | 0) + ("s" + ((seed + i) % 97)).length + (i + 2);
        if (sum !== want)
            throw new Error("seed " + seed + " round " + r + ": live data changed across GC: " + sum + " != " + want);
        check = (check + sum) | 0;
    }
    return [gcs, check];
}
`;

var workerSrc = body + `
var seed = SEED;
var r = hot(seed, ${ROUNDS}, ${WIDTH});
$.agent.report(seed + " " + r[0] + " " + r[1]);
$.agent.leaving();
`;

if (typeof $ === "undefined" || typeof $.agent === "undefined")
    throw new Error("this jsc shell has no $.agent");

for (var t = 1; t <= THREADS; t++)
    $.agent.start(workerSrc.replace("SEED", String(t * 1000)));

eval(body);
var mine = hot(0, ROUNDS, WIDTH);

var seen = 0, totalGCs = mine[0];
while (seen < THREADS) {
    var rep = $.agent.getReport();
    if (rep === null) { $.agent.sleep(10); continue; }
    var parts = rep.split(" ");
    totalGCs += parseInt(parts[1], 10);
    seen++;
    print("worker seed=" + parts[0] + " gcs=" + parts[1] + " checksum=" + parts[2]);
}
print("main gcs=" + mine[0] + " checksum=" + mine[1]);
print("threads=" + (THREADS + 1) + " explicit_gcs=" + totalGCs);
if (totalGCs < 200)
    throw new Error("not enough GCs: " + totalGCs);
print("PASS gcthreads");
