#!/usr/bin/env python3
"""Summarize a tools/run-jsc-tests-box.sh run: pass/fail/timeout lists and failures
grouped by signature.   tools/jsc-results.py logs/jsc-tests/<run>"""
import collections, os, re, subprocess, sys

run = sys.argv[1]
verdicts = collections.defaultdict(list)
scripts = {}
for line in open(os.path.join(run, "results.txt")):
    v, script, secs, name = line.split(None, 3)
    verdicts[v].append(name.strip())
    scripts[name.strip()] = script
for v, f in (("P", "pass.txt"), ("F", "fail.txt"), ("T", "timeout.txt")):
    open(os.path.join(run, f), "w").write("".join(n + "\n" for n in sorted(verdicts[v])))


BINARY = os.path.join(run, "jsc")  # hard link to the binary that ran
_symbols = {}


def symbolize(addrs):
    """Frames below the leaf, by name (atos; Tiger has no ASLR, x86_64 loads at 4 GB)."""
    todo = [a for a in addrs if a not in _symbols]
    if todo and os.path.exists(BINARY):
        out = subprocess.run(["atos", "-o", BINARY, "-l", "0x100000000"] + todo,
                             capture_output=True, text=True).stdout.splitlines()
        for a, name in zip(todo, out):
            _symbols[a] = re.sub(r"\(.*", "", name).strip()
    names = [_symbols.get(a, a) for a in addrs]
    return (" < " + " < ".join(names)) if names else ""


def norm(s):
    s = re.sub(r"0x[0-9a-f]+", "X", s)
    s = re.sub(r"\d+(\.\d+)?", "N", s)
    return s.strip()[:140]


def signature(name):
    path = os.path.join(run, "out", scripts[name] + ".log")
    try:
        text = open(path, errors="replace").read()
    except OSError:
        return "no log"
    # Drop the "<test>: " prefix the scripts put on every line.
    lines = [l.split(": ", 1)[1] if l.startswith(name + ":") else l for l in text.splitlines()]
    body = "\n".join(lines)
    if "Timed out after" in body or "HARD TIMEOUT" in body:
        return "timeout (jsc's own JSCTEST_timeout)"
    for l in lines:
        m = re.search(r"TIGER-CRASH pid \S+ .*exception (\S+) code (\S+)", l)
        if m:
            leaf = re.search(r" leaf (\S+?)\+", l)
            frames = re.search(r"frames: (.*)", l)
            where = symbolize(frames.group(1).split()[1:4]) if frames else ""
            return "crash exc %s code %s %s%s" % (m.group(1), m.group(2), leaf.group(1) if leaf else "(jit/no symbol)", where)
        m = re.search(r"TIGER-CRASH-SIGNAL pid \S+ signal (\S+)", l)
        if m:
            return "crash signal %s" % m.group(1)
    if "TIGER-ABORT" in body:
        why = [l for l in lines if re.search(r"ASSERT|failed|Abort", l) and "TIGER" not in l]
        return "abort " + norm(why[0] if why else "")
    for pat in (r"^Exception: .*", r"^Error: .*", r".*Error: .*", r"^FAIL .*", r".*[Ff]ail.*"):
        for l in lines:
            if re.match(pat, l) and not l.startswith("FAIL: ") and "Running " not in l:
                return norm(l)
    m = re.search(r"Unexpected exit code: (\d+)", body)
    if m:
        return "exit code " + m.group(1)
    return "other"


groups = collections.defaultdict(list)
for n in verdicts["F"]:
    groups[signature(n)].append(n)
for n in verdicts["T"]:
    groups["timeout (driver SIGKILL)"].append(n)
with open(os.path.join(run, "signatures.txt"), "w") as f:
    for sig, names in sorted(groups.items(), key=lambda kv: -len(kv[1])):
        f.write("%5d  %s\n" % (len(names), sig))
        for n in sorted(names)[:8]:
            f.write("         %s\n" % n)
print("pass %d  fail %d  timeout %d" % (len(verdicts["P"]), len(verdicts["F"]), len(verdicts["T"])))
print("signatures: %s (%d groups)" % (os.path.join(run, "signatures.txt"), len(groups)))
