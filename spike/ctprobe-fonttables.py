#!/usr/bin/env python
"""List the sfnt tables of every font file in a directory tree.

Runs on the Tiger box under Python 2.3 as well as here under 3.x, so the same
script answers "which of Tiger's shipped fonts carry AAT shaping tables". Reads
plain sfnt (.ttf/.otf), TrueType collections, and .dfont resource containers,
which is how most of Tiger's system fonts are packaged.

usage: ctprobe-fonttables.py <dir> [<dir> ...]
"""
import os
import struct
import sys

# The box runs Python 2.3, which has no b'' literals, so byte constants go
# through lit(). In Python 2 file bytes are str; in Python 3 they are bytes.
PY3 = sys.version_info[0] >= 3        # 2.3 has neither bytes nor b'' literals
if PY3:
    def lit(s):
        return s.encode('latin-1')
else:
    def lit(s):
        return s

try:                                   # set() became a builtin in 2.4
    set
except NameError:
    from sets import Set as set


def ssort(seq):                        # sorted() also arrived in 2.4
    out = list(seq)
    out.sort()
    return out

TTCF = lit('ttcf')
SFNT = lit('sfnt')
HEADS = (lit('\x00\x01\x00\x00'), lit('OTTO'), lit('true'), lit('typ1'))

# what decides whether a font can be shaped, and by whom
INTEREST = ('morx', 'mort', 'GSUB', 'GPOS', 'kern', 'feat', 'cmap', 'glyf', 'CFF ')


def tags_from_sfnt(buf, off):
    """Table tags of the sfnt whose header starts at off."""
    if len(buf) < off + 12:
        return []
    tag = buf[off:off + 4]
    if tag == TTCF:
        n = struct.unpack('>I', buf[off + 8:off + 12])[0]
        out = []
        for i in range(min(n, 8)):
            sub = struct.unpack('>I', buf[off + 12 + 4 * i:off + 16 + 4 * i])[0]
            out.extend(tags_from_sfnt(buf, off + sub))
        return out
    if tag not in HEADS:
        return []
    num = struct.unpack('>H', buf[off + 4:off + 6])[0]
    tags = []
    for i in range(num):
        p = off + 12 + 16 * i
        if p + 4 > len(buf):
            break
        t = buf[p:p + 4]
        if PY3:
            t = t.decode('latin-1')
        tags.append(t)
    return tags


def tags_from_dfont(buf):
    """A .dfont keeps Mac resources in the data fork; pull every 'sfnt'."""
    if len(buf) < 16:
        return []
    dataOff, mapOff = struct.unpack('>II', buf[0:8])
    if mapOff + 30 > len(buf):
        return []
    typeListOff = struct.unpack('>H', buf[mapOff + 24:mapOff + 26])[0]
    tl = mapOff + typeListOff
    if tl + 2 > len(buf):
        return []
    ntypes = struct.unpack('>h', buf[tl:tl + 2])[0] + 1
    tags = []
    for i in range(ntypes):
        e = tl + 2 + 8 * i
        if e + 8 > len(buf):
            break
        rtype = buf[e:e + 4]
        nrefs = struct.unpack('>h', buf[e + 4:e + 6])[0] + 1
        refOff = struct.unpack('>H', buf[e + 6:e + 8])[0]
        if rtype != SFNT:
            continue
        for r in range(nrefs):
            q = tl + refOff + 12 * r
            if q + 12 > len(buf):
                break
            dOff = struct.unpack('>I', lit('\x00') + buf[q + 5:q + 8])[0]
            res = dataOff + dOff
            if res + 4 > len(buf):
                continue
            tags.extend(tags_from_sfnt(buf, res + 4))
    return tags


def tables(path):
    try:
        fh = open(path, 'rb')
        buf = fh.read()
        fh.close()
    except Exception:
        return None
    if len(buf) < 16:
        return None
    if buf[0:4] in HEADS or buf[0:4] == TTCF:
        return tags_from_sfnt(buf, 0)
    return tags_from_dfont(buf) or None


def main():
    roots = sys.argv[1:] or ['/System/Library/Fonts', '/Library/Fonts']
    rows = []
    for root in roots:
        for dirpath, _dirs, files in os.walk(root):
            for name in ssort(files):
                if name.startswith('._'):
                    continue
                t = tables(os.path.join(dirpath, name))
                if not t:
                    continue
                rows.append((name, len(set(t)), [x for x in INTEREST if x in t]))
    print('%-42s %6s  %s' % ('font file', 'tables', 'of interest'))
    for name, n, present in ssort(rows):
        print('%-42s %6d  %s' % (name[:42], n, ' '.join(present)))
    aat = [r[0] for r in rows if 'morx' in r[2] or 'mort' in r[2]]
    ot = [r[0] for r in rows if 'GSUB' in r[2]]
    both = [f for f in aat if f in ot]
    print('')
    print('font files scanned:                 %d' % len(rows))
    print('with AAT shaping (morx/mort):       %d' % len(aat))
    print('with OpenType GSUB:                 %d' % len(ot))
    print('with both:                          %d' % len(both))
    print('with neither:                       %d' %
          len([r for r in rows if not (set(('morx', 'mort', 'GSUB')) & set(r[2]))]))


if __name__ == '__main__':
    main()
