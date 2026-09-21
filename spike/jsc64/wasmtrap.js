// TIGER64: WebAssembly traps that arrive as POSIX signals from JIT code.
//
// With Options::useWasmFaultSignalHandler on, JSC gives wasm memory a huge
// reservation with the tail unmapped and lets the hardware do the bounds check:
// an out-of-bounds access faults, WTF's signal handler recognises the pc as wasm
// JIT code and redirects rip at the throw thunk.  On 10.4 that fault is a SIGBUS
// (spike/jsc64/sigjit64.c), and there are no Mach exceptions to fall back on.
//
// Module below (hand-assembled, no toolchain needed):
//   (memory 1) (export "m" (memory 0))
//   (func (export "load") (param i32) (result i32) (i32.load (local.get 0)))
//   (func (export "store") (param i32) (i32.store (local.get 0) (i32.const 7)))
//   (func (export "unr") unreachable)
//   (func (export "div") (param i32 i32) (result i32) (i32.div_s ...))

// Sections are assembled here rather than written out with hand-counted sizes,
// which is the only part of a hand-built module that is easy to get wrong.
function section(id, payload) { return [id, payload.length].concat(payload); }
function vec(items) { return [items.length].concat.apply([items.length], items); }

var types = vec([
    [0x60, 0x01, 0x7f, 0x01, 0x7f],             // (i32) -> i32
    [0x60, 0x01, 0x7f, 0x00],                   // (i32) -> ()
    [0x60, 0x00, 0x00],                         // () -> ()
    [0x60, 0x02, 0x7f, 0x7f, 0x01, 0x7f]        // (i32, i32) -> i32
]);
var funcs = vec([[0x00], [0x01], [0x02], [0x03]]);
var mems = vec([[0x00, 0x01]]);                 // one memory, min 1 page, no max
function name(s) { var r = [s.length]; for (var i = 0; i < s.length; i++) r.push(s.charCodeAt(i)); return r; }
var exports_ = vec([
    name("load").concat([0x00, 0x00]),
    name("store").concat([0x00, 0x01]),
    name("unr").concat([0x00, 0x02]),
    name("div").concat([0x00, 0x03]),
    name("m").concat([0x02, 0x00])
]);
function body(code) { var b = [0x00].concat(code, [0x0b]); return [b.length].concat(b); }
var code = vec([
    body([0x20, 0x00, 0x28, 0x02, 0x00]),             // local.get 0; i32.load
    body([0x20, 0x00, 0x41, 0x07, 0x36, 0x02, 0x00]), // local.get 0; i32.const 7; i32.store
    body([0x00]),                                     // unreachable
    body([0x20, 0x00, 0x20, 0x01, 0x6d])              // local.get 0; local.get 1; i32.div_s
]);

var bytes = new Uint8Array(
    [0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00]
    .concat(section(1, types), section(3, funcs), section(5, mems), section(7, exports_), section(10, code)));

var fails = 0;
function check(name, fn, wantSubstring) {
    var got;
    try { fn(); got = "<no throw>"; }
    catch (e) { got = String(e); }
    if (wantSubstring === null) {
        if (got !== "<no throw>") { print("FAIL  " + name + ": " + got); fails++; }
        else print("PASS  " + name + ": returned normally");
        return;
    }
    if (got.indexOf(wantSubstring) < 0) { print("FAIL  " + name + ": got " + got + ", wanted /" + wantSubstring + "/"); fails++; }
    else print("PASS  " + name + ": " + got);
}

var mod = new WebAssembly.Module(bytes);
var inst = new WebAssembly.Instance(mod);
var e = inst.exports;
print("memory bytes: " + e.m.buffer.byteLength);

// Warm the functions up so they leave the interpreter and get compiled.
for (var i = 0; i < 20000; i++) { e.load(0); e.store(4); e.div(100, 3); }

check("in-bounds load", function () { if (e.load(0) !== 0) throw new Error("bad value"); }, null);
check("in-bounds store", function () { e.store(4); }, null);
check("load past the end", function () { e.load(0x7fff0000); }, "Out of bounds memory access");
check("store past the end", function () { e.store(0x7fff0000); }, "Out of bounds memory access");
check("load just past the end", function () { e.load(65536); }, "Out of bounds memory access");
check("unreachable", function () { e.unr(); }, "Unreachable code should not be executed");
check("i32.div_s by zero", function () { e.div(1, 0); }, "Division by zero");
check("i32.div_s overflow", function () { e.div(-2147483648, -1); }, "Integer overflow");

// The VM must still be alive and correct after all of that.
for (var i = 0; i < 20000; i++) e.store(8);
check("still usable after traps", function () { if (e.load(8) !== 7) throw new Error("bad value " + e.load(8)); }, null);

// Storm the handler: 20000 faults in a row must all come back as JS exceptions
// with the VM still healthy afterwards.  This is also how the per-trap cost of a
// POSIX signal round trip on 10.4 gets measured.
var n = 0, t0 = Date.now();
for (var i = 0; i < 20000; i++) { try { e.load(0x7fff0000); } catch (x) { n++; } }
print("trap storm: " + n + "/20000 traps in " + (Date.now() - t0) + " ms");
if (n !== 20000) { print("FAIL  trap storm missed " + (20000 - n)); fails++; }
check("usable after the storm", function () { if (e.load(8) !== 7) throw new Error("bad value"); }, null);

print(fails ? ("FAILURES " + fails) : "PASS wasmtrap");
if (fails) throw new Error("wasmtrap failures: " + fails);
