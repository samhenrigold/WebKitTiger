#!/usr/bin/env python3
"""Rename the Apple TV QuartzCore's CoreImage class names so they stop colliding
with Tiger's own QuartzCore.

The Apple TV build carries Core Image and Core Animation in one binary and
duplicates 207 CI* class names. Tiger's libobjc does not keep the two apart:
once the Apple TV image is in the process, +[CIFilter filterWithName:] stops
resolving no matter which QuartzCore is loaded first, and any QuickTime playback
that touches CoreImage throws. We only ever want the CA* classes out of this
image, so rewrite every CI* name to ZI*: same length, so it is a pure in-place
byte patch, and it stays internally consistent because every reference inside
this one binary is patched too.

Only whole NUL-delimited strings matching ^CI[A-Za-z0-9_]*$ are touched, so
substrings of longer names are left alone.
"""
import re, sys

path = sys.argv[1]
data = bytearray(open(path, 'rb').read())
pattern = re.compile(rb'(?<=\x00)CI[A-Za-z0-9_]*\x00')

names, count = set(), 0
for m in pattern.finditer(bytes(data)):
    names.add(m.group()[:-1].decode())
    data[m.start()] = ord('Z')
    count += 1

open(path, 'wb').write(data)
print("decollide: renamed %d CI* strings (%d distinct) to ZI* in %s"
      % (count, len(names), path))
