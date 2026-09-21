#!/usr/bin/env python3
"""Repoint chosen undefined symbols at a shim library, in place.

A two-level-namespace Mach-O records, for every undefined symbol, which library
it must come from. When a foreign binary imports something the host's libSystem
does not have, the fix is to make those particular imports come from a shim
instead. This appends an LC_LOAD_DYLIB for the shim into the header padding and
rewrites the library ordinal in each chosen symbol's n_desc.

The alternative, running the process flat-namespace, is a spike technique: it
flattens every lookup in the process and invites collisions. This is the honest
version.

Handles 32-bit and 64-bit thin Mach-O, little and big endian, and prebound
binaries (a Tiger-era image marks undefined symbols N_PBUD rather than N_UNDF).

usage: repoint-imports.py <in> <out> <symbol-list> <shim-install-name>

The shim install name is baked in here and into install_name_tool's -id, so it
must be the path the host will dlopen.
"""
import shutil
import struct
import sys

LC_SEGMENT, LC_SYMTAB, LC_SEGMENT_64 = 0x01, 0x02, 0x19
LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB, LC_REEXPORT_DYLIB = 0x0C, 0x80000018, 0x8000001F
DYLIB_CMDS = (LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB, LC_REEXPORT_DYLIB)

# magic -> (endian, header size, is 64-bit)
KINDS = {
    0xfeedface: ('<', 28, False), 0xcefaedfe: ('>', 28, False),
    0xfeedfacf: ('<', 32, True),  0xcffaedfe: ('>', 32, True),
}


def repoint(src, dst, symbols, shimpath):
    shutil.copy(src, dst)
    d = bytearray(open(dst, 'rb').read())
    want = set(symbols)

    magic = struct.unpack('<I', d[0:4])[0]
    if magic not in KINDS:
        raise SystemExit(f'{src}: not a thin Mach-O (magic {magic:#x}); lipo -thin it first')
    e, hdr, is64 = KINDS[magic]

    ncmds, sizeofcmds, flags = struct.unpack_from(e + '3I', d, 16)

    symoff = stroff = nsyms = None
    ndylib = 0
    lowsect = None
    off = hdr
    for _ in range(ncmds):
        cmd, size = struct.unpack_from(e + '2I', d, off)
        if cmd == LC_SYMTAB:
            symoff, nsyms, stroff, _strsize = struct.unpack_from(e + '4I', d, off + 8)
        if cmd in DYLIB_CMDS:
            ndylib += 1
        if cmd in (LC_SEGMENT, LC_SEGMENT_64):
            # nsects sits at 48 in a 32-bit segment_command, 64 in the 64-bit one;
            # a section is 68 bytes, a section_64 is 80, and its file offset is at
            # 32 in the first and 48 in the second.
            nsects_at, sect_at, sect_sz, foff_at = (64, off + 72, 80, 48) if is64 else (48, off + 56, 68, 32)
            nsects = struct.unpack_from(e + 'I', d, off + nsects_at)[0]
            so = sect_at
            for _s in range(nsects):
                fo = struct.unpack_from(e + 'I', d, so + foff_at)[0]
                if fo and (lowsect is None or fo < lowsect):
                    lowsect = fo
                so += sect_sz
        off += size

    if symoff is None:
        raise SystemExit(f'{src}: no LC_SYMTAB')

    name = shimpath.encode() + b'\0'
    cmdsize = (24 + len(name) + 7) // 8 * 8          # 8-align, valid for both widths
    if lowsect is None or hdr + sizeofcmds + cmdsize > lowsect:
        raise SystemExit(f'{src}: no room in the header for another load command')

    lc = struct.pack(e + '6I', LC_LOAD_DYLIB, cmdsize, 24, 0, 1 << 16, 1 << 16) + name
    lc += b'\0' * (cmdsize - len(lc))
    at = hdr + sizeofcmds
    d[at:at] = lc
    del d[at + cmdsize:at + 2 * cmdsize]             # consume padding, keep the file length
    struct.pack_into(e + 'I', d, 16, ncmds + 1)
    struct.pack_into(e + 'I', d, 20, sizeofcmds + cmdsize)

    newordinal = ndylib + 1
    entry = 16 if is64 else 12
    patched = []
    for i in range(nsyms):
        p = symoff + i * entry
        n_strx = struct.unpack_from(e + 'I', d, p)[0]
        n_type = d[p + 4]
        n_desc = struct.unpack_from(e + 'H', d, p + 6)[0]
        # N_UNDF (0x0), or N_PBUD (0xc) in a prebound image, and external
        if (n_type & 0x0e) not in (0x0, 0xc) or not (n_type & 0x01):
            continue
        base = stroff + n_strx
        nm = d[base:d.index(b'\0', base)].decode('latin-1')
        if nm in want:
            struct.pack_into(e + 'H', d, p + 6, (n_desc & 0x00ff) | (newordinal << 8))
            patched.append(nm)

    open(dst, 'wb').write(d)
    return newordinal, patched, want - set(patched)


if __name__ == '__main__':
    if len(sys.argv) != 5:
        raise SystemExit(__doc__)
    src, dst, symfile, shim = sys.argv[1:5]
    syms = [l.strip() for l in open(symfile) if l.strip()]
    ordinal, done, missing = repoint(src, dst, syms, shim)
    print(f'new ordinal {ordinal} -> {shim}')
    print(f'patched {len(done)}/{len(syms)} symbols')
    if missing:
        print('NOT FOUND:', ' '.join(sorted(missing)))
