"""Read-only classic Mach-O import/relocation audit; never executes guest code."""
import hashlib,json,struct
from pathlib import Path


def parse(path):
 b=path.read_bytes();h=struct.unpack_from('<7I',b);assert h[0]==0xfeedface
 pos=28;cmds=[];symbols=[];sections=[];segments=[];dyld=None;deps=[];entry=None;ds=None
 for _ in range(h[4]):
  cmd,size=struct.unpack_from('<2I',b,pos);assert size>=8 and pos+size<=28+h[5];cmds.append(cmd)
  if cmd==1:
   n,va,vs,off,fs,ma,ini,ns,flags=struct.unpack_from('<16s8I',b,pos+8);assert off+fs<=len(b);segments.append((n.rstrip(b'\0').decode(),va,vs,off,fs,ini))
   for i in range(ns):
    a=struct.unpack_from('<16s16s9I',b,pos+56+i*68);sections.append({'name':a[0].rstrip(b'\0').decode(),'segment':a[1].rstrip(b'\0').decode(),'address':a[2],'size':a[3],'fileOffset':a[4]})
  if cmd==2:
   so,count,st,ss=struct.unpack_from('<4I',b,pos+8);assert so+count*12<=len(b) and st+ss<=len(b);strings=b[st:st+ss]
   for i in range(count):
    index,typ,sec,desc,value=struct.unpack_from('<IBBHI',b,so+i*12);assert index<ss;name=strings[index:].split(b'\0')[0].decode();symbols.append({'name':name,'type':typ,'section':sec,'description':desc,'value':value})
  if cmd==0xb:ds=struct.unpack_from('<18I',b,pos+8)
  if cmd in (0xc,0xe):
   off=struct.unpack_from('<I',b,pos+8)[0];value=b[pos+off:pos+size].split(b'\0')[0].decode()
   if cmd==0xe:dyld=value
   else:deps.append(value)
  if cmd==5:
   flavor,count=struct.unpack_from('<2I',b,pos+8);assert flavor==1 and count==17
   regs=struct.unpack_from('<17I',b,pos+16);entry=regs[15];assert regs[16]&0x20==0
  pos+=size
 assert pos==28+h[5]
 return b,h,cmds,symbols,sections,segments,dyld,deps,entry,ds



ORIGINAL_LIBSYSTEM_SHA256 = "6243ad0b202fa364ac7b243f0416013f81416e32abb3c69e30c6b020250448e2"
EXPECTED_IMPORTS = {"__exit", "___error", "_socket", "_bind", "_listen", "_accept",
                    "_getsockname", "_setsockopt", "_fcntl", "_ioctl", "_poll",
                    "_sysctl", "_read", "_write", "_close", "_usleep"}

def audit(executable, library, expected_imports=None):
    declared = list(EXPECTED_IMPORTS if expected_imports is None else expected_imports)
    expected = set(declared)
    assert len(expected) == len(declared), "duplicate declared import"
    assert 1 <= len(expected) <= 64 and all(isinstance(name, str) and name.startswith("_") for name in expected)
    b, header, commands, symbols, sections, segments, dyld, deps, entry, ds = parse(Path(executable))
    assert header[1:4] == (12, 6, 2) and header[6] == 0x85, "ARMv6 classic executable required"
    assert commands == [1, 1, 1, 1, 2, 0xb, 0xe, 5, 0xc], "unexpected load command"
    assert dyld == "/usr/lib/dyld" and deps == ["/usr/lib/libSystem.B.dylib"]
    undefined = [s for s in symbols if s["type"] == 1]
    assert {s["name"] for s in undefined} == expected
    assert ds[5] == len(expected) and ds[13] == 0 and ds[15] == len(expected) and ds[17] == 0
    lib = parse(Path(library))
    library_hash = hashlib.sha256(lib[0]).hexdigest()
    assert library_hash == ORIGINAL_LIBSYSTEM_SHA256, "wrong original OS1 library"
    exports = {s["name"]: s for s in lib[3] if s["type"] == 15}
    text = next(s for s in sections if s["name"] == "__text")
    assert text["address"] <= entry < text["address"] + text["size"]
    data = next(s for s in sections if s["name"] == "__data")
    imports = []
    for i in range(ds[15]):
        address, info = struct.unpack_from("<II", b, ds[14] + i * 8)
        index = info & 0xffffff
        assert (info >> 24) & 1 == 0 and (info >> 25) & 3 == 2 and (info >> 27) & 1 == 1 and info >> 28 == 0
        symbol = symbols[index]
        assert symbol["name"] in expected and symbol["name"] in exports
        assert data["address"] <= address <= data["address"] + data["size"] - 4
        offset = data["fileOffset"] + address - data["address"]
        assert b[offset:offset + 4] == bytes(4)
        imports.append({"address": hex(address), "symbol": symbol["name"], "originalExportVA": hex(exports[symbol["name"]]["value"])})
    assert len({r["address"] for r in imports}) == len(expected)
    assert {r["symbol"] for r in imports} == expected, "each declared import needs one eager relocation"
    return {"passed": True, "bytes": len(b), "sha256": hashlib.sha256(b).hexdigest(),
            "entry": hex(entry), "text": text, "imports": imports,
            "commands": [hex(x) for x in commands], "sourceLibrarySHA256": library_hash,
            "ownSymbols": {s["name"]: hex(s["value"]) for s in symbols if s["type"] & 0xe == 0xe and s["section"] != 0},
            "scope": "Own program and eager original imports; this audit does not execute it."}

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("libsystem", type=Path)
    args = parser.parse_args()
    print(json.dumps(audit(args.executable, args.libsystem), indent=2))
