#!/usr/bin/env python3
"""Turn TIGER-SAMPLE lines from a stage-perf.sh log into a per-process profile.

  tools/tiger-profile.py logs/perf/wiki-before.log build/tiger-web-port/bin/TigerWebProcess [pid-hex]

Picks the x86_64 process with the most samples unless a pid is given, symbolizes every
frame inside the executable with atos (x86_64 loads at 0x100000000, no ASLR on 10.4) and
buckets each main-thread sample by the highest-priority category any frame matches.
Categories are a heuristic over symbol names; the raw hot-frame list follows for checks.
"""
import collections, re, subprocess, sys

log, binary = sys.argv[1], sys.argv[2]
want_pid = sys.argv[3] if len(sys.argv) > 3 else None

# The executable's mapped range: everything above it (0x1098xxxxx) is libSystem and friends.
TEXT_END = 0x100000000
for line in subprocess.run(['otool', '-l', binary], capture_output=True, text=True).stdout.splitlines():
    w = line.split()
    if len(w) == 2 and w[0] == 'vmaddr':
        addr = int(w[1], 16)
    elif len(w) == 2 and w[0] == 'vmsize' and 0x100000000 <= addr < 0x110000000:
        TEXT_END = max(TEXT_END, addr + int(w[1], 16))

samples = []  # (t, pid, isMain, thread, leaf, frames)
rx = re.compile(r'^\s*([\d.]+) TIGER-SAMPLE pid (0x[0-9a-f]+) n 0x[0-9a-f]+ (main|thread) 0x[0-9a-f]+ thread (0x[0-9a-f]+) state-kr 0x[0-9a-f]+ pc 0x[0-9a-f]+(?: leaf (\S+))? frames:((?: 0x[0-9a-f]+)*)')
for line in open(log, errors='replace'):
    m = rx.match(line)
    if not m:
        continue
    t, pid, kind, thread, leaf, frames = m.groups()
    samples.append((float(t), pid, kind == 'main', thread, leaf or '', [int(f, 16) for f in frames.split()]))

def is64(p):
    return any(0x100000000 <= f < TEXT_END for s in samples if s[1] == p for f in s[5])
# Without a pid: the 64-bit process with the most thread-samples (the web process has
# far more threads than the network process; pass the pid from ps to be sure).
by_pid = collections.Counter(s[1] for s in samples)
pid = want_pid or max((p for p in by_pid if by_pid[p] > 50 and is64(p)), key=lambda p: by_pid[p])
mine = [s for s in samples if s[1] == pid]
print(f"process {pid}: {len(mine)} thread-samples, {sum(1 for s in mine if s[2])} main-thread samples, "
      f"{min(s[0] for s in mine):.1f}s .. {max(s[0] for s in mine):.1f}s")

addrs = sorted({f for s in mine for f in s[5] if 0x100000000 <= f < TEXT_END})
names = {}
for i in range(0, len(addrs), 2000):
    chunk = addrs[i:i + 2000]
    out = subprocess.run(['atos', '-o', binary, '-l', '0x100000000'] + [hex(a) for a in chunk], capture_output=True, text=True).stdout.splitlines()
    for a, n in zip(chunk, out):
        names[a] = re.sub(r' \(in [^)]*\)| \+ \d+$', '', n)

def sym(f):
    if f in names:
        return names[f]
    return 'SYSTEM' if f < 0x100000000 else hex(f)

CATS = [
    ('JS: JIT compile', r'JSC::(DFG|FTL|JIT|B3|Air)|JSC::(DFG::|FTL::)|JSC::Baseline|JIT::compile|::Plan::|Worklist'),
    ('JS: GC', r'JSC::Heap::|JSC::SlotVisitor|JSC::MarkedBlock|JSC::MarkingConstraint|JSC::Heap::collect'),
    ('JS: parse/bytecode', r'JSC::Parser|JSC::Lexer|BytecodeGenerator|UnlinkedCodeBlock|JSC::CodeBlock::finishCreation|SourceProvider'),
    ('JS: execute', r'JSC::|llint_|vmEntryToJavaScript|jsc_|WebCore::JS|JSDOM|toJS|Bindings'),
    ('Layout', r'::layout|Layout|LineLayout|InlineIterator|RenderBlock|RenderFlexible|RenderGrid|RenderTable|RenderText::|FrameView::updateLayout|LocalFrameView::layout'),
    ('Style', r'Style::|StyleResolver|Selector|RuleSet|RenderStyle|resolveStyle|StyleBuilder|CSSParser|CSSPropertyParser|StyleSheetContents|MediaQuery'),
    ('Paint', r'::paint|Paint|GraphicsContext|DisplayList|DrawingArea|BackingStore|ShareableBitmap|CGContext|Cairo|drawGlyphs|GraphicsLayer|TiledBacking'),
    ('Text/fonts', r'Font|Glyph|hb_|HarfBuzz|ComplexTextController|WidthIterator|TextRun|CTFont|FontCascade|FreeType|FT_'),
    ('Images', r'ImageDecoder|ScalableImageDecoder|BitmapImage::|jpeg_|png_|WebP|ImageSource|ImageFrameCache|CachedImage::'),
    ('HTML parse', r'HTMLDocumentParser|HTMLTokenizer|HTMLTreeBuilder|HTMLConstruction|XMLDocumentParser|TextResourceDecoder'),
    ('DOM/other WebCore', r'WebCore::'),
    ('Network/loader', r'ResourceLoader|CachedResource|DocumentLoader|curl|Curl|NetworkProcess|NetworkResourceLoader|SubresourceLoader'),
    ('IPC', r'IPC::|WebKit::.*Connection|MessageReceiver|didReceive'),
    ('Malloc/WTF', r'bmalloc|WTF::|pas_|fastMalloc|malloc|free\b'),
    ('WebKit glue', r'WebKit::'),
]
IDLE_LEAF = re.compile(r'mach_msg|poll|kevent|select|semaphore|__psynch|pthread_cond|sleep|usleep|read$|recvmsg|__wait')
# The 64-bit leaf is often a dyld stub (unsymbolized, inside the binary), so also
# recognise the wait by the frames above it.
IDLE_STACK = re.compile(r'ParkingLot::park|Condition::wait|ThreadCondition::timedWait|RunLoop::populateTasks|WTF::Thread::.*wait|mach_msg|kevent|poll\(')

def classify(s):
    _, _, _, _, leaf, frames = s
    syms = [sym(f) for f in frames]
    joined = '\n'.join(syms)
    if (IDLE_LEAF.search(leaf) or IDLE_STACK.search(joined)) and not re.search(r'::layout|::paint|JSC::(?!Heap)', joined):
        return 'idle (blocked in kernel)'
    for name, pat in CATS:
        if re.search(pat, joined):
            return name
    if all(x == 'SYSTEM' for x in syms):
        return 'system libs only'
    return 'other'

def report(title, subset):
    c = collections.Counter(classify(s) for s in subset)
    n = len(subset) or 1
    print(f"\n{title}: {len(subset)} samples (250 ms each => {len(subset)/4:.1f} s)")
    print(f"{'bucket':32s} {'samples':>8s} {'%':>6s} {'~sec':>6s}")
    for k, v in c.most_common():
        print(f"{k:32s} {v:8d} {100*v/n:6.1f} {v/4:6.1f}")
    busy = [s for s in subset if not classify(s).startswith(('idle', 'system libs'))]
    print(f"busy: {len(busy)} samples => ~{len(busy)/4:.1f} s of main-thread work")
    return busy

main = [s for s in mine if s[2]]
busy = report('MAIN THREAD', main)
others = [s for s in mine if not s[2]]
report('OTHER THREADS (all, mostly waiting)', [s for s in others if not classify(s).startswith('idle')] + [])

# Where does the busy time go, by leaf-most WebKit frame and by hottest frames overall.
print("\nHot top-of-stack (first in-binary frame) among busy main-thread samples:")
top = collections.Counter(next((sym(f) for f in s[5] if f in names), sym(s[5][0]) if s[5] else '?') for s in busy)
for k, v in top.most_common(25):
    print(f"{v:5d}  {k[:120]}")
print("\nInclusive frame counts (frame appears anywhere in a busy main-thread sample):")
inc = collections.Counter()
for s in busy:
    for k in {sym(f) for f in s[5] if f in names}:
        inc[k] += 1
for k, v in inc.most_common(40):
    print(f"{v:5d}  {k[:120]}")

# Timeline: busy main-thread samples per second, so time-to-quiet is visible.
print("\nBusy main-thread samples per second (t):")
tl = collections.Counter(int(s[0]) for s in busy)
if tl:
    print(' '.join(f"{t}s:{tl.get(t,0)}" for t in range(min(tl), max(tl) + 1)))

# Allocator share: busy main-thread samples whose leaf is in the allocator (libSystem's
# malloc shows as malloc_jumpstart+N for its static szone functions; libpas/bmalloc are in-binary).
MALLOC = re.compile(r'malloc|free\b|free\+|realloc|memalign|calloc|szone|pas_|bmalloc|fastMalloc|fastFree|fastRealloc|fastZeroed|fastCompact|tryFast|FastMalloc')
def leafname(s):
    n = sym(s[5][0]) if s[5] and s[5][0] in names else s[4]
    n = n.split('(')[0]
    while re.search(r'<[^<>]*>', n):  # drop template arguments (FastMalloc is a Vector policy there)
        n = re.sub(r'<[^<>]*>', '', n)
    return n
nm = sum(1 for s in busy if MALLOC.search(leafname(s)))
print(f"\nmalloc-family leaf: {nm} of {len(busy)} busy main-thread samples = {100*nm/(len(busy) or 1):.1f}%")
print("  " + ", ".join(f"{k} {v}" for k, v in collections.Counter(re.sub(r'\+0x[0-9a-f]+$', '', leafname(s)) for s in busy if MALLOC.search(leafname(s))).most_common(8)))
