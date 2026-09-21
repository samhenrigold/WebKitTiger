#!/usr/bin/env python3
"""Turn LC_REEXPORT_DYLIB into LC_LOAD_DYLIB in place.

Tiger's dyld-46 predates LC_REEXPORT_DYLIB (0x1f | LC_REQ_DYLD) and refuses any
image carrying one. Umbrella frameworks such as CoreServices and
ApplicationServices use it to re-export their sub-frameworks.

Both commands are a dylib_command with identical layout, so the only difference
is the command word. Demoting keeps the dependency loaded and drops only the
re-export, which costs nothing when the process runs flat-namespace: a flat
lookup searches every loaded image anyway.

usage: demote-reexports.py <macho> [<macho> ...]
"""
import struct
import sys

LC_LOAD_DYLIB = 0x0C
LC_REEXPORT_DYLIB = 0x8000001F
MAGICS = {0xfeedface: ('<', 28), 0xfeedfacf: ('<', 32),
          0xcefaedfe: ('>', 28), 0xcffaedfe: ('>', 32)}


def demote(path):
    d = bytearray(open(path, 'rb').read())
    magic = struct.unpack('<I', d[0:4])[0]
    if magic not in MAGICS:
        return f'{path}: not a thin Mach-O'
    endian, hdr = MAGICS[magic]
    ncmds = struct.unpack(endian + 'I', d[16:20])[0]
    off, n = hdr, 0
    for _ in range(ncmds):
        cmd, size = struct.unpack(endian + '2I', d[off:off + 8])
        if cmd == LC_REEXPORT_DYLIB:
            struct.pack_into(endian + 'I', d, off, LC_LOAD_DYLIB)
            n += 1
        off += size
    if n:
        open(path, 'wb').write(d)
    return f'{path}: demoted {n}'


if __name__ == '__main__':
    for p in sys.argv[1:]:
        print(demote(p))
