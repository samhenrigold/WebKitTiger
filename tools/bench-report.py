#!/usr/bin/env python3
"""Build report.md tables from a tools/bench.sh run directory.

  tools/bench-report.py logs/bench/<ts>

Reads <name>.log (the box's app.log, every line prefixed with seconds since launch by
spike/bench/benchstamp.pl) and <name>.url for every page. Main-thread buckets come from
tools/tiger-profile.py (run in-process with runpy so its classifier and symbols are reused
as they are); its full output is kept as <name>.profile.txt. Writes <dir>/tables.md
(report.md is the hand-written report that quotes it) and <dir>/metrics.json.
"""
import collections, io, json, os, re, runpy, statistics, sys, contextlib, hashlib

WKT = '/Users/shg/Developer/WebKitTiger'
WEB = WKT + '/build/tiger-web-port/bin/TigerWebProcess'
d = sys.argv[1]
paint_summary = runpy.run_path(WKT + '/tools/paint-metrics.py')['summarize']
build_record = os.path.join(d, 'web-build-dir.txt')
if os.path.isfile(build_record):
    WEB = os.path.join(open(build_record).read().strip(), 'bin', 'TigerWebProcess')
    candidate = json.load(open(os.path.join(d, 'candidate.json')))
    fingerprint = hashlib.sha256()
    with open(WEB, 'rb') as binary:
        for block in iter(lambda: binary.read(1024 * 1024), b''):
            fingerprint.update(block)
    if fingerprint.hexdigest() != candidate['processes']['WEB']['TigerWebProcess']:
        raise SystemExit('bench-report: web binary changed since the run; restore the matching executable before symbolizing')
T = re.compile(r'^\s*(\d+\.\d+) (.*)')

def lines(name):
    for raw in open(os.path.join(d, name + '.log'), errors='replace'):
        m = T.match(raw)
        if m:
            yield float(m.group(1)), m.group(2)

def pct(xs, p):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(round(p / 100 * (len(xs) - 1))))] if xs else None

def ms(x):
    return '-' if x is None else ('%.0f' % (x * 1000) if x < 10 else '%.1fs' % x)

def input_events(name):
    """TIGER-INPUT events (they can be glued to the end of another process's line)."""
    ev = []
    for t, s in lines(name):
        for m in re.finditer(r'TIGER-INPUT: (mouse type=5 at \S+|cursor type=\d+|wheel dy=\S+ at \S+|frame (\S+) at \S+ rects=\d+ scroll=(-?\d+),(-?\d+))', s):
            kind = m.group(1).split()[0]
            scrolled = kind == 'frame' and (m.group(3), m.group(4)) != ('0', '0')
            ev.append((t, kind, scrolled))
    return ev

def first_after(ev, t, pred):
    return next((e[0] for e in ev if e[0] > t and pred(e)), None)

def input_metrics(name):
    ev = input_events(name)
    moves = [e[0] for e in ev if e[1] == 'mouse']
    # time to interactive: the first scripted move in the 0-30 s phase answered within 100 ms
    tti = None
    for i, t in enumerate(moves[:58]):
        c = first_after(ev, t, lambda e: e[1] == 'cursor')
        if c is not None and c - t <= 0.1:
            tti = t
            break
    hover = []
    for t in moves[58:]:
        c = first_after(ev, t, lambda e: e[1] == 'cursor')
        if c is not None:
            hover.append(c - t)
    wheels = [e[0] for e in ev if e[1] == 'wheel']
    wheel = []
    for t in wheels:
        f = first_after(ev, t, lambda e: e[1] == 'frame' and e[2])
        if f is not None:
            wheel.append(f - t)
    return dict(tti=tti, tti_moves=len(moves[:58]), hover=hover, hover_n=len(moves[58:]), wheel=wheel, wheel_n=len(wheels))

def ps_table(name):
    """BENCH-PS lines -> {at: {proc: (pid, rss_kb, cpu%, cputime)}} plus the load average."""
    out = collections.defaultdict(dict)
    for t, s in lines(name):
        m = re.match(r'BENCH-PS (\d+): load \{?\s*([\d.]+)', s)
        if m:
            out[int(m.group(1))]['load'] = float(m.group(2))
            continue
        m = re.match(r'BENCH-PS (\d+):\s+(\d+)\s+\d+\s+(\d+)\s+\d+\s+([\d.]+)\s+(\S+)\s+(\S+)', s)
        if m:
            at, pid, rss, cpu, cput, cmd = m.groups()
            proc = os.path.basename(cmd)
            prev = out[int(at)].get(proc)
            if prev is None or float(cpu) > prev[2]:
                out[int(at)][proc] = (int(pid), int(rss), float(cpu), cput)
    return out

def media(name):
    w = []
    for t, s in lines(name):
        if 'TIGER-MEDIA' in s and 'fps=' in s:
            g = lambda k: re.search(k + r'=(-?[\d.]+)', s)
            w.append(dict(t=t, fps=float(g('fps').group(1)), dropped=int(g('dropped').group(1)) if g('dropped') else 0,
                          lag=float(g('lag').group(1)) if g('lag') else None, pts=float(g('pts').group(1)) if g('pts') else None,
                          line=s[s.index('TIGER-MEDIA'):][:160]))
    return w

def fvnl(name):
    for t, s in lines(name):
        if re.search(r'milestones=.*DidFirstVisuallyNonEmptyLayout|dispatching DidFirstVisuallyNonEmptyLayoutForFrame', s):
            return t
    return None

def titles(name):
    return [(t, s.split('TIGER title:', 1)[1].strip()) for t, s in lines(name) if 'TIGER title:' in s]

def crashes(name):
    return [(t, s[:200]) for t, s in lines(name) if 'TIGER-CRASH' in s]

# Own-CDN hosts counted as first party (the page's registrable domain is always first party).
OWN = {'nytimes.com': ['nyt.com'], 'youtube.com': ['ytimg.com', 'ggpht.com', 'googlevideo.com'],
       'x.com': ['twimg.com', 'twitter.com'], 'github.com': ['githubassets.com', 'githubusercontent.com'],
       'theverge.com': ['vox-cdn.com', 'voxmedia.com', 'chorus.cdn'], 'wikipedia.org': ['wikimedia.org'],
       'react.dev': [], 'apple.com': ['cdn-apple.com']}
def site(host):
    parts = host.split('.')
    return '.'.join(parts[-2:])

def js_split(name, url):
    blocks, cur = [], None
    for t, s in lines(name):
        m = re.search(r'TIGER-JS: cumulative ([\d.]+) s in (\d+) scripts', s)
        if m:
            cur = dict(t=t, total=float(m.group(1)), scripts=[]); blocks.append(cur); continue
        m = re.search(r'TIGER-JS:\s+([\d.]+) s\s+(\d+) calls\s+(.*)', s)
        if m and cur is not None:
            cur['scripts'].append((float(m.group(1)), m.group(3).strip()))
    if not blocks:
        return None
    last = blocks[-1]
    page = site(re.match(r'\w+://([^/:]+)', url).group(1))
    own = [page] + OWN.get(page, [])
    split = collections.Counter()
    third = collections.Counter()
    for sec, label in last['scripts']:
        m = re.match(r'\w+://([^/:]+)', label)
        if not m:
            split['inline/eval/native'] += sec
        elif site(m.group(1)) in own or any(m.group(1).endswith(o) for o in own):
            split['first party'] += sec
        else:
            split['third party'] += sec
            third[site(m.group(1))] += sec
    split['unlisted (below top 25)'] = max(0.0, last['total'] - sum(split.values()))
    return dict(t=last['t'], total=last['total'], split=dict(split), top_third=third.most_common(6),
                top=last['scripts'][:8])

def profile(name, web_pid):
    """Main-thread buckets per window, from tiger-profile.py's own classifier."""
    buf = io.StringIO()
    argv = sys.argv
    sys.argv = ['tiger-profile.py', os.path.join(d, name + '.log'), WEB] + ([web_pid] if web_pid else [])
    try:
        with contextlib.redirect_stdout(buf):
            g = runpy.run_path(WKT + '/tools/tiger-profile.py', run_name='__main__')
    except Exception as e:  # no samples, wrong pid ...
        sys.argv = argv
        open(os.path.join(d, name + '.profile.txt'), 'w').write(buf.getvalue() + '\nERROR %r\n' % e)
        return None
    sys.argv = argv
    open(os.path.join(d, name + '.profile.txt'), 'w').write(buf.getvalue())
    sym, classify = g['sym'], g['classify']
    SYNC = re.compile(r'waitForSyncReply|sendSync|waitForMessage|SyncMessage')
    def bucket(s):
        if any(SYNC.search(sym(f)) for f in s[5]):
            return 'IPC: sync wait'
        return classify(s)
    out = {'pid': g['pid']}
    for label, lo, hi in (('0-30', 0, 30), ('30-90', 30, 90)):
        sub = [s for s in g['main'] if lo <= s[0] < hi]
        c = collections.Counter(bucket(s) for s in sub)
        n = len(sub)
        idle = sum(v for k, v in c.items() if k.startswith(('idle', 'system libs')))
        out[label] = dict(n=n, busy=(n - idle - c['IPC: sync wait']) / n * 100 if n else None,
                          buckets={k: v / n * 100 for k, v in c.items()} if n else {})
    return out

GROUP = [  # report columns -> tiger-profile buckets
    ('JS exec', ['JS: execute']), ('JS parse', ['JS: parse/bytecode']), ('GC/JIT', ['JS: GC', 'JS: JIT compile']),
    ('style', ['Style']), ('layout', ['Layout']), ('paint', ['Paint', 'Text/fonts']), ('images', ['Images']),
    ('DOM/HTML/other', ['DOM/other WebCore', 'HTML parse', 'Network/loader', 'Malloc/WTF', 'WebKit glue', 'other']),
    ('IPC', ['IPC', 'IPC: sync wait']),
]

names = sorted({f[:-4] for f in os.listdir(d) if f.endswith('.log') and os.path.exists(os.path.join(d, f[:-4] + '.url'))},
               key=lambda n: os.path.getmtime(os.path.join(d, n + '.log')))
M = {}
for name in names:
    url = open(os.path.join(d, name + '.url')).read().strip()
    ps = ps_table(name)
    web30 = ps.get(30, {}).get('TigerWebProcess')
    r = dict(url=url, fvnl=fvnl(name), crashes=crashes(name), ps={str(k): v for k, v in ps.items()},
             media=media(name), titles=titles(name)[-40:])
    status = os.path.join(d, name + '.exit-status')
    r['stage_exit_status'] = int(open(status).read()) if os.path.exists(status) else None
    with open(os.path.join(d, name + '.log'), errors='replace') as stream:
        r['paint_operations'] = paint_summary(stream)
    if not name.startswith(('octane', 'sp3')):
        r['input'] = input_metrics(name)
        r['profile'] = profile(name, hex(web30[0]) if web30 else None)
        r['js'] = js_split(name, url)
    M[name] = r
json.dump(M, open(os.path.join(d, 'metrics.json'), 'w'), indent=1, default=str)

o = []
P = lambda *a: o.append(' '.join(str(x) for x in a))
def secs(cput):
    x = 0.0
    for part in cput.split(':'):
        x = x * 60 + float(part)
    return x
def cpu(r, at, proc):
    """CPU % over the interval ending at `at` (0-30 s, then 30 s-at), from ps's cumulative
    TIME: ps's own %cpu is a decaying average. Same pid at both ends or it is a fresh process."""
    v = r['ps'].get(str(at), {}).get(proc)
    if not v:
        return '-'
    if at <= 30:
        return '%.0f' % (secs(v[3]) / at * 100)
    u = r['ps'].get('30', {}).get(proc)
    base = secs(u[3]) if u and u[0] == v[0] else 0.0
    return '%.0f' % ((secs(v[3]) - base) / (at - 30) * 100)
def later(r):
    ats = sorted(int(a) for a in r['ps'] if int(a) > 30)
    return ats[-1] if ats else 88

failed = [name for name, r in M.items() if r['stage_exit_status'] not in (None, 0)]
if failed:
    P('Failed staging runs (diagnostics only, not successful benchmarks): ' + ', '.join(failed) + '\n')
P('## Load, responsiveness, CPU (fast mode unless the name starts with f)\n')
P('| page | first non-empty layout | TTI (move answered <100 ms) | wheel->frame median / p90 (n) | hover->cursor median / p90 (n/15) | web / UI / GPU / net %CPU 0-30 s | ... 30-88 s | web RSS @88 s | load @30/88 | crashes |')
P('|---|---|---|---|---|---|---|---|---|---|')
for name, r in M.items():
    if 'input' not in r:
        continue
    i = r['input']; at = later(r)
    web88 = r['ps'].get(str(at), {}).get('TigerWebProcess')
    P('| %s | %s | %s | %s / %s (%d/%d) | %s / %s (%d/%d) | %s / %s / %s / %s | %s / %s / %s / %s | %s | %s / %s | %d |' % (
        name, ms(r['fvnl']), ('%.1f s' % i['tti']) if i['tti'] is not None else 'never (%d moves)' % i['tti_moves'],
        ms(statistics.median(i['wheel']) if i['wheel'] else None), ms(pct(i['wheel'], 90)), len(i['wheel']), i['wheel_n'],
        ms(statistics.median(i['hover']) if i['hover'] else None), ms(pct(i['hover'], 90)), len(i['hover']), i['hover_n'],
        cpu(r, 30, 'TigerWebProcess'), cpu(r, 30, 'TigerBrowser2'), cpu(r, 30, 'TigerGPUProcess'), cpu(r, 30, 'TigerNetworkProcess'),
        cpu(r, at, 'TigerWebProcess'), cpu(r, at, 'TigerBrowser2'), cpu(r, at, 'TigerGPUProcess'), cpu(r, at, 'TigerNetworkProcess'),
        ('%d MB' % (web88[1] / 1024)) if web88 else '-',
        r['ps'].get('30', {}).get('load', '-'), r['ps'].get(str(at), {}).get('load', '-'), len(r['crashes'])))

P('\n## Web-process main thread: busy % and buckets (% of all main-thread samples in the window)\n')
P('| page | window | samples | busy % | ' + ' | '.join(g for g, _ in GROUP) + ' | idle |')
P('|---|---|---|---|' + '---|' * len(GROUP) + '---|')
for name, r in M.items():
    pr = r.get('profile')
    if not pr:
        continue
    for w in ('0-30', '30-90'):
        x = pr[w]
        if not x['n']:
            continue
        b = x['buckets']
        cols = ['%.0f' % sum(b.get(k, 0) for k in ks) for _, ks in GROUP]
        idle = sum(v for k, v in b.items() if k.startswith(('idle', 'system libs')))
        P('| %s | %s | %d | %.0f | %s | %.0f |' % (name, w, x['n'], x['busy'], ' | '.join(cols), idle))

P('\n## JavaScript by owner (TIGER_JS_PROBE, cumulative main-thread seconds under script at the last report)\n')
P('| page | at | total s | first party | third party | inline/eval/native | unlisted | top third parties (s) |')
P('|---|---|---|---|---|---|---|---|')
for name, r in M.items():
    j = r.get('js')
    if not j:
        continue
    s = j['split']
    P('| %s | %.0f s | %.1f | %.1f | %.1f | %.1f | %.1f | %s |' % (name, j['t'], j['total'], s.get('first party', 0), s.get('third party', 0),
      s.get('inline/eval/native', 0), s.get('unlisted (below top 25)', 0), ', '.join('%s %.1f' % kv for kv in j['top_third'])))

P('\n## Paint operations after warmup (not distinct video frames or physical scanout)\n')
P('| page | interval s | GPU renders/s | UI incorporates/s | UI draws/s | GPU render / readback p95 ms | UI draw gap p95 / max ms |')
P('|---|---|---|---|---|---|---|')
def number(value):
    return '-' if value is None else '%.1f' % value
for name, r in M.items():
    p = r['paint_operations']; ops = p['operations']
    if not any(op['count'] for op in ops.values()):
        continue
    P('| %s | %.1f | %s | %s | %s | %s / %s | %s / %s |' % (
        name, p['duration_s'], number(ops['gpu_render']['per_second']),
        number(ops['ui_incorporate']['per_second']), number(ops['ui_draw']['per_second']),
        number(ops['gpu_render']['cost_p95_ms']), number(ops['gpu_readback']['cost_p95_ms']),
        number(ops['ui_draw']['gap_p95_ms']), number(ops['ui_draw']['gap_max_ms'])))

P('\n## Decoder output (TIGER-MEDIA; this does not measure displayed FPS)\n')
P('Two-second windows after the first four seconds; loop-wrap windows are excluded.\n')
P('| page | windows | fps median / min | dropped total | clock lag max ms | web / UI / GPU / audio %CPU 0-30 s | 30-88 s |')
P('|---|---|---|---|---|---|---|')
for name, r in M.items():
    w = [x for x in r['media'] if x['t'] > 4]
    keep = []
    for x in w:
        if keep and x['pts'] is not None and keep[-1]['pts'] is not None and x['pts'] < keep[-1]['pts']:
            continue
        keep.append(x)
    if not keep:
        continue
    at = later(r)
    lags = [x['lag'] for x in keep if x['lag'] is not None]
    P('| %s | %d | %.1f / %.1f | %d | %s | %s / %s / %s / %s | %s / %s / %s / %s |' % (
        name, len(keep), statistics.median(x['fps'] for x in keep), min(x['fps'] for x in keep), sum(x['dropped'] for x in keep),
        ('%.0f' % max(lags)) if lags else '-',
        cpu(r, 30, 'TigerWebProcess'), cpu(r, 30, 'TigerBrowser2'), cpu(r, 30, 'TigerGPUProcess'), cpu(r, 30, 'tigeraudio32'),
        cpu(r, at, 'TigerWebProcess'), cpu(r, at, 'TigerBrowser2'), cpu(r, at, 'TigerGPUProcess'), cpu(r, at, 'tigeraudio32')))

P('\n## JS suites (last titles)\n')
for name, r in M.items():
    if name.startswith(('octane', 'sp3')):
        P('- **%s**: ' % name + '; '.join('%.0fs %s' % tt for tt in r['titles'][-25:]) + (' -- %d crash lines' % len(r['crashes']) if r['crashes'] else ''))

P('\n## Crash lines\n')
for name, r in M.items():
    for t, s in r['crashes']:
        P('- %s @ %.1f s: `%s`' % (name, t, s.replace('`', "'")))
open(os.path.join(d, 'tables.md'), 'w').write('\n'.join(o) + '\n')
print('\n'.join(o))
