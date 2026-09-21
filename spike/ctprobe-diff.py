#!/usr/bin/env python3
"""Diff two ctprobe dumps with a numeric tolerance.

Metrics are allowed to differ by up to 1/64 pt, which is the rasteriser's own
granularity. Integers - glyph ids, counts, string ranges, units per em - must
match exactly, because both sides measured the same font bytes.

A value is treated as integral when neither side wrote a decimal point.

usage: ctprobe-diff.py <mac-dump> <tiger-dump>
"""
import re
import sys

TOL = 1.0 / 64.0
# A systematic difference shows up as a constant *relative* error, which an
# absolute tolerance flags at large sizes and nowhere else. Those are reported
# separately rather than passed silently: visible, but not counted as failures.
REL_TOL = 0.0005

NUM = re.compile(r'^-?(?:\d+\.\d+|\d+|NaN)$')


def load(path):
    """key -> (value-string, line-number), in file order."""
    out, order = {}, []
    with open(path) as fh:
        for n, raw in enumerate(fh, 1):
            line = raw.rstrip('\n')
            if not line.strip() or line.startswith('#') or line.startswith('## '):
                continue
            # the dump is a left-justified key padded to a column, then the value
            m = re.match(r'^(\S(?:.*?\S)?)\s\s+(.*)$', line)
            if not m:
                continue
            key, val = m.group(1), m.group(2).strip()
            if key.startswith('info.'):     # platform labels, not subjects
                continue
            if key in out:                      # keys repeat across sizes only via prefixes
                key = f'{key}#{n}'
            out[key] = (val, n)
            order.append(key)
    return out, order


def tokens(v):
    """Split a value into comparable tokens, keeping braces/brackets out."""
    return [t for t in re.split(r'[\s,{}\[\]]+', v) if t]


def compare(a, b):
    """None if equal within tolerance, 'rel:...' if only within the relative
    tolerance, else a reason string."""
    if a == b:
        return None
    ta, tb = tokens(a), tokens(b)
    if len(ta) != len(tb):
        return 'different shape'
    worst = 0.0
    worst_rel = 0.0
    numeric_any = False
    for x, y in zip(ta, tb):
        if x == y:
            continue
        if not (NUM.match(x) and NUM.match(y)):
            return 'text differs'
        if x == 'NaN' or y == 'NaN':
            return 'NaN on one side'
        numeric_any = True
        # integral on both sides means an exact quantity: ids, counts, ranges
        if '.' not in x and '.' not in y:
            return 'integer differs'
        fx, fy = float(x), float(y)
        d = abs(fx - fy)
        worst = max(worst, d)
        scale = max(abs(fx), abs(fy))
        worst_rel = max(worst_rel, d / scale if scale else 0.0)
    if not numeric_any:
        return 'text differs'
    if worst > TOL:
        if worst_rel is not None and worst_rel <= REL_TOL:
            return f'rel:delta {worst:.6f} > 1/64 but {worst_rel * 100:.3f}% relative'
        return f'delta {worst:.6f} > 1/64'
    return None


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    mac, mac_order = load(sys.argv[1])
    tig, _ = load(sys.argv[2])

    only_mac, only_tiger, diffs, near, same = [], [], [], [], 0
    for key in mac_order:
        if key not in tig:
            only_mac.append(key)
            continue
        why = compare(mac[key][0], tig[key][0])
        if why and why.startswith('rel:'):
            near.append((key, mac[key][0], tig[key][0], why[4:]))
        elif why:
            diffs.append((key, mac[key][0], tig[key][0], why))
        else:
            same += 1
    for key in tig:
        if key not in mac:
            only_tiger.append(key)

    print(f'matched   {same}')
    print(f'near      {len(near)}   (outside 1/64 pt but within {REL_TOL * 100:g}% relative)')
    print(f'differing {len(diffs)}')
    print(f'only-mac  {len(only_mac)}')
    print(f'only-tiger {len(only_tiger)}')

    if diffs:
        print('\n--- divergences ---')
        for key, m, t, why in diffs:
            print(f'{key}\n    mac   : {m}\n    tiger : {t}\n    why   : {why}')
    if near:
        print('\n--- within the relative tolerance, shown but not counted as failures ---')
        for key, m, t, why in near:
            print(f'{key}\n    mac   : {m}\n    tiger : {t}\n    why   : {why}')
    if only_mac:
        print('\n--- present only in the mac dump (tiger printed nothing here) ---')
        for k in only_mac:
            print(f'  {k} = {mac[k][0]}')
    if only_tiger:
        print('\n--- present only in the tiger dump ---')
        for k in only_tiger:
            print(f'  {k} = {tig[k][0]}')
    return 1 if (diffs or only_mac or only_tiger) else 0


if __name__ == '__main__':
    sys.exit(main())
