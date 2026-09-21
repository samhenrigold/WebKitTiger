import struct, sys, shutil
src, dst, symfile, shimpath = sys.argv[1:5]
shutil.copy(src, dst)
d = bytearray(open(dst,'rb').read())
want = set(l.strip() for l in open(symfile) if l.strip())

magic,cpu,sub,ftype,ncmds,sizeofcmds,flags = struct.unpack_from('<7I',d,0)
assert magic == 0xfeedface

# locate LC_SYMTAB and count existing dylib load commands
off = 28; symoff=stroff=nsyms=None; ndylib = 0; lowsect = None
for i in range(ncmds):
    cmd,sz = struct.unpack_from('<2I',d,off)
    if cmd == 2:  # LC_SYMTAB
        symoff,nsyms,stroff,strsize = struct.unpack_from('<4I',d,off+8)
    if cmd in (0x0c, 0x80000018, 0x8000001f):
        ndylib += 1
    if cmd == 1:
        nsects = struct.unpack_from('<I',d,off+48)[0]; so = off+56
        for s in range(nsects):
            fo = struct.unpack_from('<I',d,so+40)[0]
            if fo and (lowsect is None or fo < lowsect): lowsect = fo
            so += 68
    off += sz

newordinal = ndylib + 1
name = shimpath.encode() + b'\0'
cmdsize = (24 + len(name) + 3) // 4 * 4
assert 28 + sizeofcmds + cmdsize <= lowsect, "no header padding"
lc = struct.pack('<6I', 0x0c, cmdsize, 24, 0, 1<<16, 1<<16) + name
lc += b'\0' * (cmdsize - len(lc))
d[28+sizeofcmds:28+sizeofcmds] = lc
del d[28+sizeofcmds+cmdsize : 28+sizeofcmds+cmdsize+cmdsize]   # keep file length: consume padding
struct.pack_into('<I', d, 16, ncmds+1)
struct.pack_into('<I', d, 20, sizeofcmds+cmdsize)
# keep two-level namespace set
struct.pack_into('<I', d, 24, flags | 0x80)

# repoint the requested undefined symbols at the new ordinal
def cstr(base):
    e = d.index(b'\0', base); return d[base:e].decode()
patched = []
for i in range(nsyms):
    e = symoff + i*12
    n_strx, n_type, n_sect, n_desc, n_value = struct.unpack_from('<IBBHI', d, e)
    # N_UNDF (0x0) or, in a prebound binary such as a Tiger-era one, N_PBUD (0xc)
    if (n_type & 0x0e) not in (0x0, 0xc) or not (n_type & 0x01):
        continue
    nm = cstr(stroff + n_strx)
    if nm in want:
        struct.pack_into('<H', d, e+6, (n_desc & 0x00ff) | (newordinal << 8))
        patched.append(nm)
open(dst,'wb').write(d)
print("new ordinal %d -> %s" % (newordinal, shimpath))
print("patched %d/%d symbols" % (len(patched), len(want)))
missing = want - set(patched)
if missing: print("NOT FOUND:", " ".join(sorted(missing)))
