#!/usr/bin/env python3
"""Rebase a 32-bit i386 Mach-O dylib on disk to a new __TEXT address (Apple's old rebase(1)).

usage: rebase-dylib.py IN OUT NEW_TEXT_VMADDR_HEX

Why: the rebundled Leopard QuartzCore is a split-seg dylib prebound into the shared-region
range (__TEXT 0x93c42000). On 10.4 that makes dyld either map it into the system-wide
shared region (shared_region_make_private_np, fragile) or slide it at load, and its
runtime rebase writes into read-only __TEXT and faults (dyld doRebase, EXC_BAD_ACCESS).
Rebasing the file to a free address below the shared region means dyld maps it at its
preferred address, no shared region, no runtime rebase. Clears MH_PREBOUND (stale on
10.4 anyway; dyld rebinds externals into __IMPORT/__DATA, which are writable).
"""
import struct, sys

LC_SEGMENT, LC_SYMTAB, LC_DYSYMTAB = 0x1, 0x2, 0xb
MH_SPLIT_SEGS, MH_PREBOUND = 0x20, 0x10

src, dst, new_text = sys.argv[1], sys.argv[2], int(sys.argv[3], 16)
b = bytearray(open(src, "rb").read())
magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags = struct.unpack_from("<7I", b, 0)
assert magic == 0xfeedface and filetype == 6, "expects a 32-bit little-endian MH_DYLIB"

# Pass 1: segments (vmaddr/fileoff for address->file mapping), symtab, dysymtab.
segs, off = [], 28
symtab = dysymtab = None
for _ in range(ncmds):
    cmd, cmdsize = struct.unpack_from("<2I", b, off)
    if cmd == LC_SEGMENT:
        name = b[off+8:off+24].rstrip(b"\0").decode()
        vmaddr, vmsize, fileoff, filesize, maxprot, initprot, nsects, sflags = struct.unpack_from("<8I", b, off+24)
        segs.append((off, name, vmaddr, vmsize, fileoff, filesize, nsects, initprot))
    elif cmd == LC_SYMTAB:
        symtab = struct.unpack_from("<4I", b, off+8)  # symoff nsyms stroff strsize
    elif cmd == LC_DYSYMTAB:
        dysymtab = struct.unpack_from("<18I", b, off+8)
    off += cmdsize

text = next(s for s in segs if s[1] == "__TEXT")
slide = (new_text - text[2]) & 0xffffffff
print(f"__TEXT 0x{text[2]:x} -> 0x{new_text:x}, slide 0x{slide:x}")

def file_offset(addr):
    for _, name, vmaddr, vmsize, fileoff, filesize, _, _ in segs:
        if vmaddr <= addr < vmaddr + filesize:
            return fileoff + (addr - vmaddr)
    raise ValueError(f"address 0x{addr:x} not in any segment's file image")

def add32(fo, delta):
    v, = struct.unpack_from("<I", b, fo)
    struct.pack_into("<I", b, fo, (v + delta) & 0xffffffff)

# Pass 2: local relocations. r_address is relative to the first segment for an ordinary
# dylib, but for a split-seg dylib dyld (ImageLoaderMachO::getRelocBase, i386) uses the
# first *writable* segment, i.e. __DATA. Getting this wrong writes fixups into __TEXT.
if flags & MH_SPLIT_SEGS:
    base = next(s for s in segs if s[7] & 2)[2]
else:
    base = segs[0][2]
print(f"reloc base 0x{base:x}")
locreloff, nlocrel = dysymtab[16], dysymtab[17]
n_applied = n_skipped = 0
for i in range(nlocrel):
    r_address, r_info = struct.unpack_from("<Ii", b, locreloff + 8*i)
    if r_address & 0x80000000:  # scattered
        word = r_address & 0xffffffff
        r_type, r_length, r_pcrel = (word >> 24) & 0xf, (word >> 28) & 3, (word >> 30) & 1
        addr = word & 0x00ffffff
        if r_type != 0 or r_length != 2 or r_pcrel:
            n_skipped += 1; continue  # SECTDIFF/PAIR encode differences, not absolute addresses
        add32(file_offset(base + addr), slide)
        # r_value holds the target address; slide it so dyld's bookkeeping stays coherent
        struct.pack_into("<I", b, locreloff + 8*i + 4, (r_info + slide) & 0xffffffff)
        n_applied += 1
        continue
    r_type, r_extern, r_length, r_pcrel = (r_info >> 28) & 0xf, (r_info >> 27) & 1, (r_info >> 25) & 3, (r_info >> 24) & 1
    if r_extern or r_type != 0 or r_length != 2 or r_pcrel:
        n_skipped += 1; continue
    add32(file_offset(base + r_address), slide)
    n_applied += 1
print(f"local relocs: {n_applied} rebased, {n_skipped} left (non-VANILLA)")

# Pass 3: segment and section addresses in the load commands.
for off, name, vmaddr, vmsize, fileoff, filesize, nsects, _ in segs:
    add32(off + 24, slide)
    so = off + 56
    for _ in range(nsects):
        add32(so + 32, slide)  # section addr
        so += 68

# Pass 4: symbol values for symbols defined in a section.
symoff, nsyms = symtab[0], symtab[1]
n_syms = 0
for i in range(nsyms):
    n_strx, n_type, n_sect, n_desc, n_value = struct.unpack_from("<IBBhI", b, symoff + 12*i)
    if n_sect and (n_type & 0x0e) == 0x0e:  # N_SECT
        struct.pack_into("<I", b, symoff + 12*i + 8, (n_value + slide) & 0xffffffff)
        n_syms += 1
print(f"symbols: {n_syms} slid")

# Pass 4b: make the relocation table __TEXT-relative and drop MH_SPLIT_SEGS. With the flag
# set dyld still tries shared_region_map_file_np first (EINVAL outside the region) and
# then makes the whole shared region private. Without it dyld maps the image normally and,
# should it ever have to slide, reads r_address relative to __TEXT -- so rewrite them.
text_base = segs[0][2]
delta = (base - text_base) & 0xffffffff
n_rewritten = n_scattered = 0
for reloff, nrel in ((dysymtab[16], dysymtab[17]), (dysymtab[14], dysymtab[15])):
    for i in range(nrel):
        r_address, = struct.unpack_from("<I", b, reloff + 8*i)
        if r_address & 0x80000000:
            n_scattered += 1; continue  # 24-bit field, cannot hold the offset
        struct.pack_into("<I", b, reloff + 8*i, (r_address + delta) & 0xffffffff); n_rewritten += 1
print(f"relocs rewritten __TEXT-relative: {n_rewritten}, scattered (left): {n_scattered}")
assert n_scattered == 0, "scattered relocations cannot be made __TEXT-relative; keep MH_SPLIT_SEGS"
flags &= ~MH_SPLIT_SEGS

# Pass 5: module table objc_module_info_addr (absolute, in __OBJC).
modtaboff, nmodtab = dysymtab[8], dysymtab[9]
n_mod = 0
for i in range(nmodtab):
    addr, = struct.unpack_from("<I", b, modtaboff + 52*i + 44)
    if addr:
        struct.pack_into("<I", b, modtaboff + 52*i + 44, (addr + slide) & 0xffffffff); n_mod += 1
print(f"module table: {n_mod} objc_module_info_addr slid")

# Prebinding is stale on 10.4 (Leopard dependents), so drop it and let dyld bind.
struct.pack_into("<I", b, 24, flags & ~MH_PREBOUND)
open(dst, "wb").write(b)
