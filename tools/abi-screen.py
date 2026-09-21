#!/usr/bin/env python3
"""i386 ABI screen: Tiger's implementation vs the modern prototype.

For every function WebKit calls by name that a Tiger framework also exports,
compare two numbers:

  * Tiger side   -- the highest byte of the argument area the callee actually
                    touches, from `tiger-otool -arch i386 -tV`. Arguments start
                    at 8(%ebp), so bytes = max(displacement + access width) - 8,
                    rounded up to a 4-byte cdecl slot. The access width matters:
                    a trailing double arrives as one `movsd 0xc(%ebp)`, and
                    counting the displacement alone undercounts it by 4.
  * modern side  -- the same figure for the current SDK prototype, taken from
                    clang's own i386 lowering (`-target i386-... -emit-llvm`)
                    rather than from reading headers, so struct flattening
                    (CFRange -> two i32), sret and byval are exact.

A difference means Tiger's entry point has a different signature under the same
name -- the CTLineDraw case, where Tiger takes a trailing CFRange the modern
prototype dropped. Hand-check every candidate: the screen cannot see a
parameter Tiger declares but never reads.

Usage:  tools/abi-screen.py <framework> [<framework> ...]
        tools/abi-screen.py --control            # re-run the CoreText positive control

Frameworks are named in FRAMEWORKS below. Run from the project root.
"""
import collections, json, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SDK = subprocess.run(["xcrun", "--show-sdk-path"], capture_output=True, text=True).stdout.strip()
OTOOL = os.path.join(ROOT, "toolchain/bin/tiger-otool")
NM = os.path.join(ROOT, "toolchain/bin/tiger-nm")
SYS = os.path.join(ROOT, "sysroot/System/Library/Frameworks")
AS = SYS + "/ApplicationServices.framework/Versions/A/Frameworks"

FRAMEWORKS = {
    "CoreFoundation":  (SYS + "/CoreFoundation.framework/Versions/A/CoreFoundation", "CF"),
    "ATS":             (AS + "/ATS.framework/Versions/A/ATS", "ATS"),
    "LaunchServices":  (AS + "/LaunchServices.framework/Versions/A/LaunchServices", "LS"),
    "HIServices":      (AS + "/HIServices.framework/Versions/A/HIServices", "HI"),
    "CoreText":        (AS + "/CoreText.framework/Versions/A/CoreText", "CT"),
    "CoreGraphics":    (AS + "/CoreGraphics.framework/Versions/A/CoreGraphics", "CG"),
    "Security":        (SYS + "/Security.framework/Versions/A/Security", "Sec"),
}
WEBKIT_DIRS = ["WTF", "JavaScriptCore", "WebCore", "WebKitLegacy/mac"]
UMBRELLAS = ["CoreFoundation/CoreFoundation.h", "CoreGraphics/CoreGraphics.h",
             "CoreText/CoreText.h", "ApplicationServices/ApplicationServices.h",
             "Security/Security.h"]

# ---------------------------------------------------------------- Tiger side --
SYM = re.compile(r'^_([A-Za-z0-9_$.]+):$')
INSN = re.compile(r'^[0-9a-f]{8}\t\s*(\S+)\s*(.*)$')
EBP = re.compile(r'(-?0x[0-9a-f]+)\(%ebp\)')

_W = {"movsd": 8, "movq": 8, "movlpd": 8, "movhpd": 8, "fldl": 8, "fstl": 8, "fstpl": 8,
      "fildll": 8, "fistpll": 8, "fisttpll": 8, "addsd": 8, "subsd": 8, "mulsd": 8, "divsd": 8,
      "movss": 4, "flds": 4, "fsts": 4, "fstps": 4, "fildl": 4, "fistpl": 4,
      "fldt": 10, "fstpt": 10,
      "movaps": 16, "movapd": 16, "movups": 16, "movupd": 16, "movdqa": 16, "movdqu": 16}

def width(op):
    if op in _W: return _W[op]
    if op.startswith(("movz", "movs")) and len(op) >= 6:
        return {"b": 1, "w": 2, "l": 4}.get(op[4], 4)
    if op.endswith("b"): return 1
    if op.endswith("w"): return 2
    return 4

def disassemble(path):
    out = subprocess.run([OTOOL, "-arch", "i386", "-tV", path],
                         capture_output=True, text=True).stdout
    syms, cur, body = {}, None, []
    for line in out.splitlines():
        m = SYM.match(line)
        if m:
            if cur and cur not in syms: syms[cur] = body
            cur, body = m.group(1), []
            continue
        mi = INSN.match(line)
        if mi and cur: body.append((mi.group(1), mi.group(2)))
    if cur and cur not in syms: syms[cur] = body
    return syms

def analyse(body):
    if not body: return dict(status="empty")
    frame = (len(body) >= 2 and body[0][0] == "pushl" and "%ebp" in body[0][1]
             and body[1][0] == "movl" and body[1][1].replace(" ", "") == "%esp,%ebp")
    end, acc = None, {}
    for op, args in body:
        w = width(op)
        for m in EBP.finditer(args):
            v = int(m.group(1), 16)
            if v >= 8:
                acc.setdefault(v, set()).add((op, w))
                if end is None or v + w > end: end = v + w
    thunk = len(body) <= 4 and any(o.startswith("jmp") for o, _ in body)
    return dict(status="ok", has_frame=frame, n=len(body), acc=acc, thunk=thunk,
                bytes=(((end - 8) + 3) & ~3) if end is not None else None)

# --------------------------------------------------------------- modern side --
DECL = re.compile(r'^declare[^@]*@([A-Za-z0-9_]+)\((.*?)\)\s*(?:#\d+)?\s*$')

def split_params(s):
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch in "([{<": depth += 1
        elif ch in ")]}>": depth -= 1
        if ch == "," and depth == 0: out.append(cur.strip()); cur = ""
        else: cur += ch
    if cur.strip(): out.append(cur.strip())
    return out

def modern_lowering(names, workdir):
    """Ask clang to lower each name for i386 and read the resulting `declare`."""
    hdr = "".join("#include <%s>\n" % h for h in UMBRELLAS)
    cur, dropped = list(names), {}
    src, ir = os.path.join(workdir, "decls.mm"), os.path.join(workdir, "ir.ll")
    for _ in range(12):
        open(src, "w").write(hdr + "\n".join(
            "void* u_%d = (void*)&%s;" % (i, n) for i, n in enumerate(cur)) + "\n")
        r = subprocess.run(["clang++", "-target", "i386-apple-macosx10.13", "-isysroot", SDK,
                            "-std=c++17", "-Wno-deprecated-declarations",
                            "-Wno-unguarded-availability-new", "-S", "-emit-llvm",
                            "-o", ir, src], capture_output=True, text=True)
        if r.returncode == 0: break
        bad = set()
        for pat in (r"use of undeclared identifier '([A-Za-z0-9_]+)'",
                    r"no member named '([A-Za-z0-9_]+)'",
                    r"'([A-Za-z0-9_]+)' is unavailable"):
            bad.update(m.group(1) for m in re.finditer(pat, r.stderr))
        if not bad:
            sys.stderr.write(r.stderr[:4000]); sys.exit("clang failed and named no symbol")
        for b in bad: dropped[b] = "not declared in the modern SDK"
        cur = [n for n in cur if n not in bad]
    out = {}
    for line in open(ir):
        m = DECL.match(line.rstrip())
        if not m: continue
        total, variadic, notes, plist = 0, False, [], []
        for p in split_params(m.group(2)):
            if p == "...": variadic = True; continue
            if "inreg" in p: notes.append("inreg"); continue
            if "sret(" in p: notes.append("sret"); total += 4; continue
            mb = re.search(r'byval\(([^)]*)\)', p)
            if mb: notes.append("byval:" + mb.group(1)); continue
            base = p.split()[0]
            if base in ("i64", "double"):
                plist.append((total, 8, "fp8" if base == "double" else "int8")); total += 8
            elif base in ("ptr", "i32", "i8", "i16", "float", "i1"):
                plist.append((total, 4, "fp4" if base == "float" else "int4")); total += 4
            else: notes.append("?:" + base); total += 4
        out[m.group(1)] = dict(bytes=total, variadic=variadic, notes=notes, plist=plist,
                               sig=m.group(2))
    return out, dropped

# -------------------------------------------------------------------- driver --
def called_names(prefixes):
    pat = r"\b(" + "|".join(prefixes) + r")[A-Za-z0-9_]+ *\("
    counts = collections.Counter()
    for d in WEBKIT_DIRS:
        base = os.path.join(ROOT, "WebKit/Source", d)
        for dirpath, _, files in os.walk(base):
            if "/spi/" in dirpath + "/" or "/tests/" in dirpath + "/": continue
            for f in files:
                if not f.endswith((".cpp", ".mm", ".m", ".c", ".h")): continue
                if f.endswith("SPI.h") or "SoftLink" in f: continue
                try: text = open(os.path.join(dirpath, f), errors="ignore").read()
                except OSError: continue
                for m in re.finditer(pat, text):
                    counts[m.group(0)[:-1].strip()] += 1
    return counts

FP4 = {"movss", "flds", "fsts", "fstps", "addss", "mulss", "subss", "divss"}
FP8 = {"movsd", "fldl", "fstl", "fstpl", "addsd", "mulsd", "subsd", "divsd", "movq",
       "movlpd", "movhpd"}

def main(argv):
    control = "--control" in argv
    fws = [a for a in argv if not a.startswith("--")] or ["CoreFoundation"]
    if control: fws = ["CoreText"]
    workdir = os.environ.get("TMPDIR", "/tmp")

    exports, prefixes = {}, set()
    for fw in fws:
        path, prefix = FRAMEWORKS[fw]
        prefixes.add(prefix)
        out = subprocess.run([NM, "-arch", "i386", "-g", path],
                             capture_output=True, text=True).stdout
        for line in out.splitlines():
            f = line.split()
            if len(f) == 3 and re.fullmatch(r"[A-TV-Z]", f[1]) and not f[2].endswith(".eh"):
                # Exactly one leading underscore is the Mach-O C prefix. Stripping
                # all of them makes the private ___CFRangeMake look like the
                # public CFRangeMake and pairs a public name with private code.
                exports.setdefault(f[2][1:] if f[2].startswith("_") else f[2], fw)

    counts = called_names(sorted(prefixes))
    screened = sorted(n for n in counts if n in exports)
    if control:
        screened = ["CTLineDraw", "CTFramesetterCreateFrame", "CTFontGetDescent",
                    "CTLineGetTypographicBounds"]

    modern, dropped = modern_lowering(screened, workdir)
    dis = {fw: disassemble(FRAMEWORKS[fw][0]) for fw in fws}

    clean, cands, undet, shape = [], [], [], []
    for n in screened:
        fw = exports[n]
        t = analyse(dis[fw].get(n, []))
        m = modern.get(n)
        row = (n, counts.get(n, 0), m, t)
        if not m or t["status"] != "ok": undet.append(row); continue
        for off, _, kind in m["plist"]:
            ops = {o for o, _ in t["acc"].get(off + 8, set())}
            if kind == "fp8" and ops & FP4 and not ops & FP8:
                shape.append((n, off, "modern double, Tiger 4-byte FP load"))
            if kind == "fp4" and ops & FP8:
                shape.append((n, off, "modern float, Tiger 8-byte FP load"))
        if t["bytes"] is None:
            (clean if m["bytes"] == 0 and not m["variadic"] else undet).append(row)
        elif m["variadic"]:
            (clean if t["bytes"] >= m["bytes"] else cands).append(row)
        elif t["bytes"] == m["bytes"]: clean.append(row)
        else: cands.append(row)

    print("frameworks: %s" % ", ".join(fws))
    print("screened %d functions (%d call sites)  clean=%d candidates=%d undetermined=%d"
          % (len(screened), sum(counts.get(n, 0) for n in screened),
             len(clean), len(cands), len(undet)))
    for title, items in (("CANDIDATES (stack footprint differs)", cands),
                         ("SHAPE MISMATCHES", shape), ("UNDETERMINED", undet)):
        print("\n--- %s ---" % title)
        if not items: print("   none")
        for it in items:
            if title.startswith("SHAPE"): print("   %s param@%d: %s" % it); continue
            n, c, m, t = it
            print("   %-44s calls=%-4d tiger=%-5s modern=%-5s %s"
                  % (n, c, t.get("bytes"), m["bytes"] if m else "-",
                     "" if m else "(no modern prototype)"))
    if dropped:
        print("\ndropped (no modern declaration): %s" % ", ".join(sorted(dropped)))
    return 1 if cands or shape else 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
