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

# otool labels only exported symbols, so a scan can run on past the end of a
# function into unlabelled code or data and decode nonsense. A real argument
# offset is small; anything past this is a decode artifact, not an access.
MAX_ARG_OFF = 0x400

def analyse(body):
    if not body: return dict(status="empty")
    frame = (len(body) >= 2 and body[0][0] == "pushl" and "%ebp" in body[0][1]
             and body[1][0] == "movl" and body[1][1].replace(" ", "") == "%esp,%ebp")
    end, acc, lea, junk = None, {}, None, False
    for op, args in body:
        w = width(op)
        for m in EBP.finditer(args):
            v = int(m.group(1), 16)
            if v > MAX_ARG_OFF: junk = True; continue
            if v < 8: continue
            if op.startswith("lea"):
                # `leal 0xc(%ebp)` takes the address of a by-value argument and
                # hands it on (CGContextFillRect does this with its CGRect). The
                # callee never loads the fields, so the extent is invisible here.
                if lea is None or v < lea: lea = v
                acc.setdefault(v, set()).add((op, 0))
                continue
            acc.setdefault(v, set()).add((op, w))
            if end is None or v + w > end: end = v + w
    thunk = len(body) <= 4 and any(o.startswith("jmp") for o, _ in body)
    return dict(status="ok", has_frame=frame, n=len(body), acc=acc, thunk=thunk,
                lea=lea, junk=junk,
                bytes=(((end - 8) + 3) & ~3) if end is not None else None)

# ------------------------------------------------------------- stub detectors --
# Two additive modes from the audit track's screen, which found real stubs the
# footprint comparison cannot see: a function whose signature matches perfectly
# can still do nothing at all. Credit: the `audit` agent.

PIC = re.compile(r'___i686\.get_pc_thunk\.([a-z]{2})')
GLOBAL = re.compile(r'0x[0-9a-f]+\(%e([a-z]{2})\)')
QUIET = {"pushl", "popl", "movl", "mov", "ret", "retl", "leave", "nop", "push", "pop"}

def stub_kind(body, acc, modern, tiger_nargs=None):
    """Classify a body as an empty stub, a fixed-global-return stub, or neither."""
    if not body: return None
    mnems = [op for op, _ in body]

    # Mode 1 -- empty body. First ret within five instructions, nothing but
    # frame bookkeeping, and no argument read at all.
    #   Tiger CTRunGetGlyphs: push ebp; mov esp,ebp; pop ebp; ret
    ret_at = next((i for i, op in enumerate(mnems) if op.startswith("ret")), None)
    if ret_at is not None and ret_at < 5 and not acc \
            and all(op in QUIET for op in mnems[:ret_at + 1]):
        return ("empty", "returns immediately, reads no argument")

    # Scan calls once: a get_pc_thunk is bookkeeping, anything else is real work.
    # A tail `jmp` to another symbol counts as a real call -- the audit track flagged
    # this as the soft spot in their version (Tiger's CTFrameDraw is one).
    picreg, ncall = None, 0
    for op, args in body:
        if op.startswith("call"):
            m = PIC.search(args)
            if m: picreg = "e" + m.group(1)
            else: ncall += 1
        elif op.startswith("jmp") and re.search(r'(^|[\s,])_[A-Za-z_]', args):
            ncall += 1                     # tail call into a real implementation
                                           # (targets can be C++-mangled: __ZNK...)

    # Mode 3 -- constant return. Returns within seven instructions without reading
    # an argument or calling anything: the result cannot depend on the inputs.
    # Gated on the prototype declaring at least one argument, because a zero-argument
    # function returning a constant is just a constant (CFArrayGetTypeID and the
    # other type-ID getters trip this otherwise).
    # Suppress only when a prototype positively says the function takes no
    # arguments (CFArrayGetTypeID and the other type-ID getters are constants by
    # definition). When no header declares it -- true of Tiger's private CoreText,
    # which neither the modern SDK nor the 10.4u/10.5 SDKs describe -- report it
    # and mark the uncertainty, rather than discarding a real finding for want of
    # a prototype.
    nargs = len(modern["plist"]) if modern else tiger_nargs
    if ret_at is not None and ret_at <= 7 and not acc and ncall == 0 and nargs != 0:
        return ("const" if nargs else "const?",
                "returns a constant without reading any argument"
                + ("" if nargs else "; no prototype, may simply take none"))

    # Mode 2 -- fixed-global-return stub. No real call, at least one PIC-relative
    # global reference, short body, and the only argument slot touched is the
    # hidden sret pointer, i.e. it fills the caller's struct from a constant.
    #   Tiger CTRunGetImageBounds: copies 16 bytes from a global into *sret.
    if len(body) > 25 or ncall: return None
    # A PIC-relative global load only exists if a get_pc_thunk set up a base
    # register first. Without that, `0x10(%eax)` is an ordinary struct field
    # dereference -- which is what a genuine accessor like CTRunGetGlyphCount
    # does, and matching it made every accessor look like a stub.
    if picreg is None: return None
    if not any(m.group(1) == picreg[1:] for op, args in body
               for m in GLOBAL.finditer(args)):
        return None
    if sorted(acc) != [8]: return None
    nargs2 = len(modern["plist"]) if modern else tiger_nargs
    if nargs2 == 0: return None
    if not modern:
        return ("global?", "reads only arg 0 and a global; no prototype to confirm sret")
    if "sret" not in modern["notes"]:
        # Slot 0 is a real first argument, not a hidden sret: the function does
        # read something. Tiger's CGLayerGetSize looks like this -- it reads the
        # layer and only falls back to a global when the layer is NULL.
        return ("global?", "reads only arg 0 and a global; arg 0 is NOT sret, likely genuine")
    return ("global", "fills the sret struct from a fixed global, ignores every argument")


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

def ir_struct_sizes(ir_path):
    """Size every `%struct.X = type {...}` in the IR, for byval parameters.

    i386 Darwin aligns every scalar in a struct to at most 4 bytes, including
    double, so laying the fields out at 4-byte granularity is exact here."""
    defs, sizes = {}, {}
    for line in open(ir_path):
        m = re.match(r'^(%[A-Za-z0-9_.]+) = type \{(.*)\}\s*$', line.rstrip())
        if m: defs[m.group(1)] = split_params(m.group(2))
    def size(t, seen=()):
        t = t.strip()
        if t in sizes: return sizes[t]
        if t.startswith("%"):
            if t in seen or t not in defs: return 4
            n = sum(size(f, seen + (t,)) for f in defs[t])
            n = (n + 3) & ~3
            sizes[t] = n
            return n
        if t in ("double", "i64"): return 8
        if t.startswith(("[", "<")):
            m = re.match(r'[\[<](\d+) x (.+)[\]>]', t)
            if m: return int(m.group(1)) * size(m.group(2), seen)
        return 4
    return {t: size(t) for t in defs}


def tiger_lowering(names, workdir):
    """Lower the same names against the 10.4u SDK with tiger-clang.

    Only used to answer "does this function declare any arguments?" for the stub
    modes. The modern SDK drops API that Tiger still ships, and without this
    fallback the gate silently discards real stubs: CTFontCreateUIFontForLocale,
    CTFontCreateWithQuickdrawNameAndStyle and CTRunGetEmbeddedObject are each
    `xorl %eax,%eax; ret` and each undeclared in the Xcode 27 SDK.

    It is deliberately NOT used for the size comparison, where checking Tiger's
    code against Tiger's own header would be circular."""
    hdr = ("#include <CoreFoundation/CoreFoundation.h>\n"
           "#include <ApplicationServices/ApplicationServices.h>\n")
    cur, src = list(names), os.path.join(workdir, "tdecls.c")
    ir = os.path.join(workdir, "tir.ll")
    for _ in range(20):
        open(src, "w").write(hdr + "\n".join(
            "void* u_%d=(void*)&%s;" % (i, n) for i, n in enumerate(cur)) + "\n")
        r = subprocess.run([os.path.join(ROOT, "toolchain/bin/tiger-clang"),
                            "-S", "-emit-llvm", "-Wno-everything", "-o", ir, src],
                           capture_output=True, text=True)
        if r.returncode == 0: break
        bad = set()
        for pat in (r"use of undeclared identifier '([A-Za-z0-9_]+)'",
                    r"'([A-Za-z0-9_]+)' is unavailable"):
            bad.update(m.group(1) for m in re.finditer(pat, r.stderr))
        if not bad: return {}
        cur = [n for n in cur if n not in bad]
    out = {}
    for line in open(ir):
        m = DECL.match(line.rstrip())
        if m: out[m.group(1)] = len(split_params(m.group(2)))
    return out


SPI_DIRS = ["WebKit/Source/WebCore/PAL/pal/spi", "WebKit/Source/WTF/wtf/spi"]

def spi_declarations(names):
    """Pull C prototypes for `names` out of WebKit's own SPI headers.

    These headers are what WebKit actually compiles against for API the current
    SDK no longer declares, so they are the right prototype of record there. They
    cannot be included directly -- they pull in wtf/Platform.h and WTFString.h --
    so the declarations are lifted out textually instead."""
    want, found = set(names), {}
    decl = re.compile(
        r'(?m)^([A-Za-z_][A-Za-z0-9_ *&:<>,]*?[ *&])(' + "|".join(map(re.escape, sorted(want)))
        + r')\s*\(([^;{]*?)\)\s*(?:WTF_[A-Z_]+\s*)?;')
    for d in SPI_DIRS:
        base = os.path.join(ROOT, d)
        for dirpath, _, files in os.walk(base):
            for f in files:
                if not f.endswith(".h"): continue
                try: text = open(os.path.join(dirpath, f), errors="ignore").read()
                except OSError: continue
                text = text.replace("\\\n", " ")
                for m in decl.finditer(text):
                    ret, name, args = m.group(1).strip(), m.group(2), m.group(3)
                    if name in found: continue
                    if "template" in ret or "class " in ret: continue
                    found[name] = "%s %s(%s);" % (ret, name, args.strip() or "void")
    # Some are declared inline in a .mm next to the call rather than in a header:
    # FormDataStreamCFNet.mm declares CFReadStreamCreate with an EXTERN prefix.
    missing = want - set(found)
    if missing:
        for d in WEBKIT_DIRS:
            for dirpath, _, files in os.walk(os.path.join(ROOT, "WebKit/Source", d)):
                for f in files:
                    if not f.endswith((".h", ".mm", ".cpp", ".m")): continue
                    try: text = open(os.path.join(dirpath, f), errors="ignore").read()
                    except OSError: continue
                    for m in decl.finditer(text.replace("\\\n", " ")):
                        ret, name, args = m.group(1).strip(), m.group(2), m.group(3)
                        if name in found or name not in missing: continue
                        ret = re.sub(r'^(EXTERN|extern "C"|extern|WTF_EXTERN_C_BEGIN)\s+', "", ret)
                        if "template" in ret or "class " in ret: continue
                        found[name] = "%s %s(%s);" % (ret, name, args.strip() or "void")
    return found


def spi_lowering(names, workdir):
    """Lower WebKit's own SPI prototypes for i386, synthesising missing types.

    Unknown type names are healed iteratively: `FooRef` becomes an opaque pointer,
    anything else an int. Every SPI type these declarations use is an opaque handle
    or an enum, both of which are 4 bytes on i386, so the totals stay exact."""
    decls = spi_declarations(names)
    if not decls: return {}, {}
    hdr = ("#include <CoreFoundation/CoreFoundation.h>\n"
           "#include <CoreGraphics/CoreGraphics.h>\n"
           "#include <CoreText/CoreText.h>\n"
           "#include <ApplicationServices/ApplicationServices.h>\n")
    cur, synth, src = dict(decls), {}, os.path.join(workdir, "sdecls.c")
    ir = os.path.join(workdir, "sir.ll")
    for _ in range(40):
        body = hdr + "".join("typedef %s;\n" % t for t in synth.values())
        body += "".join(d + "\n" for d in cur.values())
        body += "".join("void* v_%d=(void*)&%s;\n" % (i, n) for i, n in enumerate(cur))
        open(src, "w").write(body)
        r = subprocess.run(["clang", "-target", "i386-apple-macosx10.13", "-isysroot", SDK,
                            "-Wno-everything", "-S", "-emit-llvm", "-o", ir, src],
                           capture_output=True, text=True)
        if r.returncode == 0: break
        unknown = set(re.findall(r"unknown type name '([A-Za-z_][A-Za-z0-9_]*)'", r.stderr))
        unknown |= set(re.findall(r"use of undeclared identifier '([A-Za-z_][A-Za-z0-9_]*)'",
                                  r.stderr))
        new = {u for u in unknown if u not in synth}
        if new:
            for u in new:
                synth[u] = ("struct %s_s *%s" % (u, u)) if u.endswith("Ref") else ("int %s" % u)
            continue
        bad = set(re.findall(r"conflicting types for '([A-Za-z0-9_]+)'", r.stderr))
        bad |= set(re.findall(r"redefinition of '([A-Za-z0-9_]+)'", r.stderr))
        if not bad: return {}, synth
        for b in bad: cur.pop(b, None)
    out = {}
    for line in open(ir):
        m = DECL.match(line.rstrip())
        if m: out[m.group(1)] = m.group(2)
    return out, synth


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
    structs = ir_struct_sizes(ir)
    out = {}
    for line in open(ir):
        m = DECL.match(line.rstrip())
        if not m: continue
        out[m.group(1)] = parse_sig(m.group(2), structs)
    return out


def parse_sig(params, structs):
    """Turn one IR `declare` parameter list into an i386 stack footprint."""
    if True:
        total, variadic, notes, plist = 0, False, [], []
        for p in split_params(params):
            if p == "...": variadic = True; continue
            if "inreg" in p: notes.append("inreg"); continue
            if "sret(" in p: notes.append("sret"); total += 4; continue
            mb = re.search(r'byval\(([^)]*)\)', p)
            if mb:
                # A byval struct occupies its full size on the stack. Skipping it
                # scored CGContextFillRect(ctx, CGRect) as 4 bytes instead of 20
                # and made most of CoreGraphics look mismatched.
                n = structs.get(mb.group(1).strip(), 4)
                notes.append("byval:%s=%d" % (mb.group(1), n))
                plist.append((total, n, "agg")); total += n
                continue
            base = p.split()[0]
            if base in ("i64", "double"):
                plist.append((total, 8, "fp8" if base == "double" else "int8")); total += 8
            elif base in ("ptr", "i32", "i8", "i16", "float", "i1"):
                plist.append((total, 4, "fp4" if base == "float" else "int4")); total += 4
            else: notes.append("?:" + base); total += 4
        return dict(bytes=total, variadic=variadic, notes=notes, plist=plist, sig=params), dropped

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
    tnargs = tiger_lowering(sorted(dropped), workdir) if dropped else {}
    dis = {fw: disassemble(FRAMEWORKS[fw][0]) for fw in fws}

    clean, cands, undet, shape, under, stubs = [], [], [], [], [], []
    for n in screened:
        fw = exports[n]
        t = analyse(dis[fw].get(n, []))
        m = modern.get(n)
        row = (n, counts.get(n, 0), m, t)
        if t["status"] == "ok":
            k = stub_kind(dis[fw].get(n, []), t["acc"], m, tnargs.get(n))
            if k: stubs.append((n, counts.get(n, 0), k[0], k[1]))
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
        elif t.get("junk") and t["bytes"] > m["bytes"]:
            undet.append(row + ("disassembly ran past the function",))
        elif t["bytes"] > m["bytes"]:
            # The dangerous direction: the callee consumes stack the caller did
            # not push. This is the CTLineDraw shape.
            cands.append(row)
        elif t.get("lea") is not None and t["lea"] - 8 <= t["bytes"]:
            undet.append(row + ("argument address taken; extent not visible",))
        else:
            # Callee reads fewer bytes than the prototype passes. Harmless under
            # cdecl (the caller pops), so informational rather than a break:
            # CGRectIsNull only needs a CGRect's origin to answer.
            under.append(row)

    print("frameworks: %s" % ", ".join(fws))
    print("screened %d functions (%d call sites)  clean=%d over-reads=%d "
          "under-reads=%d undetermined=%d"
          % (len(screened), sum(counts.get(n, 0) for n in screened),
             len(clean), len(cands), len(under), len(undet)))
    for title, items in (("OVER-READS (callee consumes stack the caller did not push)", cands),
                         ("SHAPE MISMATCHES", shape),
                         ("under-reads (callee reads less than passed; benign in cdecl)", under),
                         ("STUBS (signature matches, implementation does nothing)", stubs),
                         ("UNDETERMINED", undet)):
        print("\n--- %s ---" % title)
        if not items: print("   none")
        for it in items:
            if title.startswith("SHAPE"): print("   %s param@%d: %s" % it); continue
            if title.startswith("STUBS"):
                print("   %-44s calls=%-4d [%s] %s" % it); continue
            n, c, m, t = it[0], it[1], it[2], it[3]
            why = it[4] if len(it) > 4 else ("" if m else "(no modern prototype)")
            print("   %-44s calls=%-4d tiger=%-5s modern=%-5s %s"
                  % (n, c, t.get("bytes"), m["bytes"] if m else "-", why))
    if dropped:
        print("\ndropped (no modern declaration): %s" % ", ".join(sorted(dropped)))
    return 1 if cands or shape or [x for x in stubs if x[2] != "global?"] else 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
