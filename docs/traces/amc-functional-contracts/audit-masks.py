"""Independent byte construction from the retained original and072 PCM control.
No engine/helper import; no guest run. This is an offline model-contract audit.
"""
from pathlib import Path
import hashlib,json,struct,sys
p=Path(__file__).resolve().parent
run=p/(sys.argv[1] if len(sys.argv)>1 else 'run-3')
sha=lambda b:hashlib.sha256(b).hexdigest()
original=(p.parent/'stage-d/verified/sram.bin').read_bytes()
assert len(original)==0x2c000 and sha(original)=='011452fdff9699442c49115354c9b0bced1b98265ba64ee5b95c1c48bbb01583'
pcm=(p.parent/'stage-e/framing/run-1/packetized.pcm').read_bytes()
assert sha(pcm)=='44feeb01ec9697c2f620d2262d606a8c3cf0bd657ed746ad8c0eacee8d3c077f'
actual=(run/'after-frame50-sram.bin').read_bytes();expected=bytearray(original)
for off,val in [(0xf208,0),(0xf20a,1),(0xf20c,2048),(0xf20e,2048),(0xf210,0x10),(0xf212,0x1010),(0x2a000,0x1b),(0x2a002,2),(0x2a004,0),(0x2a00a,3)]:
    struct.pack_into('<H',expected,off,val)
expected[0xf220:0x10220]=pcm[48*4096:49*4096]
expected[0x11220:0x12220]=pcm[47*4096:48*4096]
assert actual==expected
ranges={
 'init-written.bin':[(0xf208,0xf214)],
 'pcm-written.bin':[(0xf208,0xf210),(0xf220,0x10220),(0x11220,0x12220)],
 'host-written.bin':[(0xf208,0xf20c),(0x2a000,0x2a006),(0x2a00a,0x2a00c)]}
report={}
union=bytearray(len(original))
for name,spans in ranges.items():
    wanted=bytearray(len(original))
    for begin,end in spans:wanted[begin:end]=b'\1'*(end-begin);union[begin:end]=b'\1'*(end-begin)
    recorded=(run/name).read_bytes();assert recorded==wanted
    report[name]={'sha256':sha(recorded),'writtenBytes':sum(recorded),'spans':[[hex(a),hex(b)] for a,b in spans]}
assert all(actual[i]==original[i] for i,b in enumerate(union) if not b)
published=(run/'published-pcm.bin').read_bytes();assert published==pcm[:49*4096]
result={'pass':True,'scope':'Independent offline SRAM/count/ownership/write-mask reconstruction; no guest audio or admission claim','fullSRAMSha256':sha(actual),'originalSRAMSha256':sha(original),'publishedPCMSha256':sha(published),'publishedPCMBytes':len(published),'nonzeroPCMBytes':sum(bool(x) for x in published),'exactOriginalBytesOutsideMasks':sum(not b for b in union),'masks':report}
(run/'mask-audit.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
