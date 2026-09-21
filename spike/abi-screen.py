#!/usr/bin/env python3
"""Screen Tiger's frameworks for functions that link but do not behave.

Three failure modes have bitten this port, each found by hand the first time:

  1. exported but empty        CTRunGetGlyphs is `push ebp; mov ebp,esp; pop ebp; ret`,
                               so it silently leaves the caller's buffer untouched.
  2. exported but ignores its  CTLineGetImageBounds copies a fixed global into its
     arguments                 struct return and never looks at the line.
  3. exported with a different CTLineDraw takes (line, context, CFRange) on Tiger and
     signature                 (line, context) today, so the range is stack junk and
                               Tiger's range check usually makes it draw nothing.

None of these produce a link error, a warning, or a crash. This screens for all
three mechanically, over whichever set of functions you point it at.

Mode 3 compares the highest incoming-argument slot each function READS against the
i386 stack slots the modern prototype implies, parsed from the host SDK headers.
Two details are load-bearing and both were bugs first:

  * `-0x20(%ebp)` is a local, `0x20(%ebp)` is an argument. Without the sign check
    every function with a stack frame looks mismatched.
  * attribute macros trail the return type (`CG_EXTERN CGAffineTransform CG_PURE`),
    so the return type is the last word that is not one of those macros. Otherwise
    the hidden struct-return slot is never counted and every CGAffineTransform
    function looks like it reads one slot too many.

With both fixed the screen has no false positives over the 436 CoreText,
CoreGraphics and CoreFoundation functions WebCore calls that Tiger exports.

Usage:
    spike/abi-screen.py CT CG CF          # the three frameworks WebCore leans on
    spike/abi-screen.py --all CT          # every Tiger export, not just WebCore's callers
"""
import re,os,subprocess
SDK=subprocess.run(["xcrun","--show-sdk-path"],capture_output=True,text=True).stdout.strip()
DECL=re.compile(r'^([A-Za-z_][\w \t\*]{0,80}?)\b([A-Za-z_]\w*)\s*\(([^;{)]{0,400}(?:\([^;{)]{0,80}\)[^;{)]{0,200})*)\)\s*[^;{]{0,120};')
SLOTS={'double':2,'CFTimeInterval':2,'CFAbsoluteTime':2,'CGPoint':2,'CGSize':2,'CGVector':2,
 'CGRect':4,'CGAffineTransform':6,'CFRange':2,'long long':2,'int64_t':2,'uint64_t':2,'CFUUIDBytes':4,
 'CGDeviceColor':3,'CGPatternCallbacks':0}
BIGRET={'CGRect':1,'CGAffineTransform':1,'CFRange':0,'CGPoint':0,'CGSize':0,'CFUUIDBytes':1,'CGDeviceColor':1}
def tslots(t):
    t=t.strip()
    if '*' in t or t.endswith(']'): return 1
    t=re.sub(r'\b(const|volatile|struct|enum|_Nullable|_Nonnull|__nullable|__nonnull)\b','',t).strip()
    if t in SLOTS: return SLOTS[t]
    if t in ('void',''): return 0
    return 1
def parse(dirs):
    protos={}
    for d in dirs:
        if not os.path.isdir(d): continue
        for fn in sorted(os.listdir(d)):
            if not fn.endswith('.h'): continue
            txt=open(os.path.join(d,fn),errors='ignore').read()
            txt=re.sub(r'/\*.*?\*/','',txt,flags=re.S); txt=re.sub(r'//[^\n]*','',txt)
            txt=re.sub(r'\s*\n\s*',' ',txt)
            for chunk in txt.split(';'):
                m=DECL.match((chunk+';').strip())
                if m: protos.setdefault(m.group(2),(m.group(1).strip(),m.group(3)))
    return protos
def argslots(ret,args):
    a=args.strip()
    if '...' in a: return None
    n=0
    if a not in ('void',''):
        parts=[];d=0;cur=''
        for ch in a:
            if ch=='(':d+=1
            if ch==')':d-=1
            if ch==',' and d==0: parts.append(cur);cur=''
            else: cur+=ch
        parts.append(cur)
        for p in parts:
            p=p.strip()
            if not p: continue
            if '(' in p: n+=1; continue
            toks=p.replace('*',' * ').split()
            while len(toks)>1 and re.match(r'^[a-z_]\w*$',toks[-1]) and toks[-1] not in SLOTS:
                toks=toks[:-1]
            n+=tslots(' '.join(toks))
    # Strip attribute macros that trail the return type (CG_PURE, CF_SWIFT_NAME, the
    # availability macros); without this the hidden struct-return slot is never added and
    # every CGAffineTransform-returning function looks like it reads one slot too many.
    rt=[w for w in ret.split() if not re.match(r'^(CG_|CF_|CT_|NS_|API_|__)[A-Z_]',w) and w not in ('extern','EXTERN')]
    r=rt[-1].replace('*','') if rt else 'void'
    return n+BIGRET.get(r,0)
# The lookbehind matters: -0x20(%ebp) is a local, 0x20(%ebp) is an incoming argument.
EBP=re.compile(r'(?<![-\w])0x([0-9a-f]+)\(%ebp\)')
OT="/Users/shg/Developer/WebKitTiger/toolchain/bin/tiger-otool"
def disasm(p):
    out=subprocess.run([OT,"-arch","i386","-tV",p],capture_output=True,text=True).stdout
    b={};cur=None
    for l in out.split("\n"):
        if l.endswith(":") and l and not l[0].isdigit() and "\t" not in l: cur=l[:-1];b[cur]=[]
        elif cur is not None and "\t" in l: b[cur].append(l)
    return b
def maxslot(body):
    """Highest incoming-argument slot the function READS.

    Only reads count. A write such as `movl %eax, 0x14(%ebp)` is the compiler
    staging outgoing arguments in the caller's frame before a tail jmp, not the
    function consuming an argument of its own, and counting those produced a
    false positive on nearly every function with a tail call."""
    b=[l for l in body if "\tnop" not in l]; end=None
    for i,l in enumerate(b):
        if "\tretl" in l: end=i;break
    head=b[:(end+1) if end is not None else len(b)]; mx=0
    for l in head:
        parts=l.split("\t")
        if len(parts)<3: continue
        mnem=parts[1].strip(); ops=parts[2]
        ops=ops.split("##")[0]
        # split source,dest at the top-level comma
        d=0; srcop=""; rest=""
        for i,ch in enumerate(ops):
            if ch=='(': d+=1
            elif ch==')': d-=1
            elif ch==',' and d==0:
                srcop=ops[:i]; rest=ops[i+1:]; break
        else:
            srcop=ops
        read_parts=[srcop]
        # cmp/test read both operands; a one-operand instruction reads its operand
        if mnem.startswith(('cmp','test','push','call','jmp','imul','add','sub','or','and','xor')):
            read_parts.append(rest)
        for rp in read_parts:
            for m in EBP.finditer(rp):
                v=int(m.group(1),16)
                if 8<=v<=0x60: mx=max(mx,v)
    return ((mx-8)//4+1 if mx else 0),len(head)
def run(label,binp,used_f,tiger_f,hdrs):
    used=set(l.split()[-1] for l in open(used_f) if l.split())
    tiger=set(x.strip() for x in open(tiger_f) if x.strip())
    pr=parse(hdrs); bo=disasm(binp)
    hits=[]; checked=0
    for fn in sorted(used&tiger):
        b=bo.get("_"+fn); p=pr.get(fn)
        if b is None or p is None: continue
        exp=argslots(*p)
        if exp is None: continue
        checked+=1
        got,n=maxslot(b)
        if got>exp: hits.append((fn,exp,got,n,p))
    print(f"\n##### {label}: compared {checked} functions")
    print(f"-- reads MORE stack than the modern prototype declares ({len(hits)}):")
    for fn,exp,got,n,p in sorted(hits,key=lambda r:-(r[2]-r[1])):
        print(f"   {fn:42s} modern={exp} reads>={got} ins={n}")
    return hits

# ---- empty and argument-ignoring detectors -------------------------------

def body_shape(body):
    b=[l for l in body if "\tnop" not in l]
    end=None
    for i,l in enumerate(b):
        if "\tretl" in l: end=i; break
    head=b[:(end+1) if end is not None else len(b)]
    # a get_pc_thunk call is position-independent-code boilerplate, not a real call
    ncall=sum(1 for l in head if "\tcall" in l and "get_pc_thunk" not in l)
    reads=set()
    for l in head:
        for m in EBP.finditer(l):
            v=int(m.group(1),16)
            if 8<=v<=0x60: reads.add((v-8)//4)
    globalrefs=sum(1 for l in head if re.search(r'0x[0-9a-f]{5,}\(%ebx\)',l))
    mnem=[l.split("\t")[1].strip() for l in head if len(l.split("\t"))>1]
    core=[m for m in mnem if m not in ("pushl","popl","movl","retl","leave")]
    empty = end is not None and end<=4 and not core and not reads
    const = end is not None and end<=7 and not reads and ncall==0
    ignores = ncall==0 and globalrefs>0 and reads in ([],[0]) and len(head)<=25
    return empty, const, ignores, len(head)

def stubs(label, binpath, names_file, only_used=None, protos=None):
    names=set(l.split()[-1] for l in open(names_file) if l.split())
    if only_used:
        names &= set(l.split()[-1] for l in open(only_used) if l.split())
    bodies=disasm(binpath)
    e=[]; g=[]
    for fn in sorted(names):
        b=bodies.get("_"+fn)
        if b is None: continue
        empty, const, ignores, n = body_shape(b)
        # A function that takes no arguments and returns a constant is just a
        # constant, e.g. CFArrayGetTypeID. Only a function that was given
        # something to work with and ignored it is interesting.
        if const and protos is not None:
            pr=protos.get(fn)
            if pr is not None and (argslots(*pr) or 0) == 0:
                const=False
        if empty or const: e.append((fn,"empty" if empty else "returns a constant"))
        if ignores: g.append(fn)
    print(f"\n##### {label}")
    print(f"-- exported but empty or constant-returning ({len(e)}):")
    for fn,k in e: print(f"   {fn:48s} {k}")
    print(f"-- ignores its arguments, returns data from a global ({len(g)}):")
    for fn in g: print(f"   {fn}")

FRAMEWORKS = {
 "CT": ("CoreText",
        "/sysroot/System/Library/Frameworks/ApplicationServices.framework/Versions/A/Frameworks/CoreText.framework/Versions/A/CoreText",
        "CoreText.framework/Headers"),
 "CG": ("CoreGraphics",
        "/sysroot/System/Library/Frameworks/ApplicationServices.framework/Versions/A/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics",
        "CoreGraphics.framework/Headers"),
 "CF": ("CoreFoundation",
        "/sysroot/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation",
        "CoreFoundation.framework/Headers"),
}

if __name__ == "__main__":
    import sys
    W=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    args=[a for a in sys.argv[1:] if not a.startswith("-")]
    every="--all" in sys.argv
    for key in (args or ["CT","CG","CF"]):
        label,binrel,hdr=FRAMEWORKS[key]
        binp=W+binrel
        tiger=f"{W}/logs/api/tiger-{key}.txt"
        used=f"{W}/logs/api/used-{key}.txt"
        protos=parse([SDK+"/System/Library/Frameworks/"+hdr])
        stubs(label+(" (all exports)" if every else ""), binp, tiger,
              None if every else used, protos)
        if not every:
            run(label, binp, used, tiger, [SDK+"/System/Library/Frameworks/"+hdr])
