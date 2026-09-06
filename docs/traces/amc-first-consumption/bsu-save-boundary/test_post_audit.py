from pathlib import Path
import runpy,json,struct
b=Path('.context/amc-first/stage-w');a=runpy.run_path(str(b/'post-audit.py'))['records'];windows={x['pc']:bytes.fromhex(x['hex']) for x in json.loads((b/'post-original.json').read_text())};pc=0x312288f0
r=bytearray(296);regs=[0]*16;regs[15]=pc;struct.pack_into('<25I',r,0,pc,0x60000010,*regs,pc+4,0x60000010,0,1,pc+4,16,0);struct.pack_into('<QQ',r,104,100,101);r[120:152]=windows[pc]
assert a(bytes(r),windows)[0]['retired'];checks=1
for off,msg in [(120,'post original mapped code differs'),(84,'post retirement receipt differs'),(88,'post expected successor differs'),(152,'post unavailable stack fabricated'),(216,'post unavailable object fabricated'),(280,'post unavailable output fabricated'),(112,'post cycles differ')]:
 m=r.copy();m[off]^=1
 try:a(m,windows);raise RuntimeError('accepted mutant')
 except AssertionError as e:assert msg in str(e),(msg,str(e));checks+=1
print('post receipt auditor',checks,'PASS; synthetic only')
