#!/usr/bin/env python3
"""Turn a failed link's "Undefined symbols" report into a tombstone list.

usage: tombstones-from-link.py LINK_LOG LIBDIR OUT_LIST [EXCLUDE_REGEX]

ld prints demangled names. This maps each back to every mangled spelling that
some archive in LIBDIR leaves undefined and no archive defines (destructors have
two spellings that demangle alike -- both are emitted), so the result is exactly
what the link is missing. Feed OUT_LIST to make-tombstones.py. EXCLUDE_REGEX
(matched against the demangled name) keeps out symbols you are providing for real.
"""
import glob, os, re, subprocess, sys
log, libdir, out = sys.argv[1:4]
exclude = re.compile(sys.argv[4]) if len(sys.argv) > 4 else None
want = []
seen = set()
in_report = False
for line in open(log, errors="replace"):
    if "Undefined symbols for architecture" in line:
        in_report = True; continue
    if in_report and line.startswith("ld:"):
        in_report = False
    m = re.match(r'\s+"(.+)", referenced from:$', line)
    if in_report and m and m.group(1) not in seen:
        seen.add(m.group(1)); want.append(m.group(1))
und, defined = set(), set()
for lib in glob.glob(os.path.join(libdir, "*.a")):
    for line in subprocess.run(["nm", lib], capture_output=True, text=True).stdout.splitlines():
        p = line.split()
        if len(p) >= 2 and not p[-2].endswith(":"):
            (und if p[-2] == "U" else defined if p[-2].upper() in "TDSBCRW" else set()).add(p[-1])
und = sorted(und - defined)
dem = subprocess.run(["c++filt"], input="\n".join(und), capture_output=True, text=True).stdout.splitlines()
by_dem = {}
for a, b in zip(und, dem):
    by_dem.setdefault(b, []).append(a)
result, unmapped = set(), []
for w in want:
    if exclude and exclude.search(w):
        continue
    c = by_dem.get(w)
    if not c:
        unmapped.append(w); c = [w]  # a C symbol: ld already printed the real name
    result.update(c)
open(out, "w").write("\n".join(sorted(result)) + "\n")
print(f"{len(want)} undefined names -> {len(result)} symbols ({len(unmapped)} taken verbatim)")
