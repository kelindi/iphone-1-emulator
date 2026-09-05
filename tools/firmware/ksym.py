#!/usr/bin/env python3
"""Dump the kernel Mach-O symbol table (addr name) and provide a disassembly helper."""
import struct, sys, re
b = open(sys.argv[1], "rb").read()
magic, cputype, cpusub, ftype, ncmds, sizeofcmds, flags = struct.unpack("<7I", b[:28])
off = 28; segs = []; symoff = stroff = nsyms = 0
for _ in range(ncmds):
    cmd, size = struct.unpack("<II", b[off:off+8])
    if cmd == 1:
        name = b[off+8:off+24].rstrip(b"\0").decode()
        vmaddr, vmsize, fileoff, filesize = struct.unpack("<4I", b[off+24:off+40])
        segs.append((name, vmaddr, vmsize, fileoff, filesize))
    elif cmd == 2:
        symoff, nsyms, stroff, strsize = struct.unpack("<4I", b[off+8:off+24])
    off += size
syms = []
for i in range(nsyms):
    n_strx, n_type, n_sect, n_desc, n_value = struct.unpack("<IBBHI", b[symoff+12*i:symoff+12*i+12])
    if n_type & 0xe0: continue
    e = b.index(b"\0", stroff+n_strx); name = b[stroff+n_strx:e].decode("latin1")
    syms.append((n_value, name))
syms.sort()
if len(sys.argv) > 2 and sys.argv[2] == "dis":
    import capstone
    va = int(sys.argv[3], 16); n = int(sys.argv[4])
    for name, vmaddr, vmsize, fileoff, filesize in segs:
        if vmaddr <= va < vmaddr + filesize:
            fo = fileoff + (va & ~1) - vmaddr
            md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM if not (va & 1) else capstone.CS_MODE_THUMB)
            def rd(a):
                for nm,v0,vs,f,fs in segs:
                    if v0<=a<v0+fs: return b[f+a-v0:f+a-v0+64]
                return None
            symmap = dict(syms)
            for ins in md.disasm(b[fo:fo+n*4], va & ~1):
                extra = ""
                m = re.search(r"\[pc, #(0x[0-9a-f]+|\d+)\]", ins.op_str)
                if m and ins.mnemonic.startswith("ldr"):
                    imm = int(m.group(1), 0)
                    thumb = (va & 1) != 0
                    pcv = ((ins.address + 4) & ~3) if thumb else ins.address + 8
                    d = rd(pcv + imm)
                    if d:
                        v = int.from_bytes(d[:4], "little")
                        extra = "  ; =0x%08x" % v
                        if v in symmap: extra += " " + symmap[v]
                        t = rd(v)
                        if t and all(32 <= c < 127 for c in t[:4]):
                            extra += " \"%s\"" % t.split(b"\0")[0][:48].decode("latin1")
                m2 = re.match(r"#(0x[0-9a-f]+)$", ins.op_str)
                if m2 and ins.mnemonic.startswith(("bl","b")):
                    v=int(m2.group(1),16)
                    if v in symmap: extra = "  ; " + symmap[v]
                print("0x%08x  %-8s %s%s" % (ins.address, ins.mnemonic, ins.op_str, extra))
else:
    for v, n in syms: print("0x%08x %s" % (v, n))
