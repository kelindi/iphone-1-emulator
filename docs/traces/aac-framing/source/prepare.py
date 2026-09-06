"""Read-only original MP4 fixture extraction; packet sizes stay in the oracle."""
from pathlib import Path
import struct, hashlib, json

here = Path(__file__).resolve().parent
media = (here.parent.parent / 'stage-d/Alarm.original.m4a').read_bytes()
sha = lambda x: hashlib.sha256(x).hexdigest()
assert sha(media) == '56dece4e074bf622e0eb815331c65fb6c4e653e33b827e9a1ef30c4e0063136e'
u32 = lambda p: struct.unpack_from('>I', media, p)[0]
boxes = {}
def walk(start, end):
    while start < end:
        assert start + 8 <= end
        size, kind = struct.unpack_from('>I4s', media, start)
        assert size >= 8 and start + size <= end
        if kind in (b'moov', b'trak', b'mdia', b'minf', b'stbl'):
            walk(start + 8, start + size)
        elif kind in (b'stsz', b'stsc', b'stco', b'stsd', b'mdat'):
            assert kind not in boxes
            boxes[kind] = (start, size)
        start += size
    assert start == end
walk(0, len(media))
p, n = boxes[b'stsz']; assert u32(p+8) == u32(p+12) == 0
sizes = [u32(p+20+4*i) for i in range(u32(p+16))]
assert len(sizes) == 1069 and n == 20+4*len(sizes)
p, n = boxes[b'stco']; chunks = [u32(p+16+4*i) for i in range(u32(p+12))]
p, n = boxes[b'stsc']; rules = [struct.unpack_from('>III', media, p+16+12*i) for i in range(u32(p+12))]
assert rules[0][0] == 1 and all(a[0] < b[0] for a,b in zip(rules,rules[1:]))
packets=[]; offsets=[]; i=0
for chunk, pos in enumerate(chunks, 1):
    rule=next(x for x in reversed(rules) if x[0] <= chunk)
    assert rule[2] == 1
    for _ in range(rule[1]):
        size=sizes[i]; m, mn=boxes[b'mdat']
        assert m+8 <= pos and pos+size <= m+mn
        offsets.append(pos); packets.append(media[pos:pos+size]); pos+=size; i+=1
assert i == len(sizes)
# Follow the actual sample description and bounded variable-length ES descriptors.
p,n=boxes[b'stsd']; assert u32(p+12)==1 and media[p+20:p+24]==b'mp4a'
entry=p+16; assert struct.unpack_from('>H',media,entry+16)[0]==0
cursor=entry+36; entry_end=entry+u32(entry); esds=None
while cursor < entry_end:
    length=u32(cursor); assert length>=8 and cursor+length<=entry_end
    if media[cursor+4:cursor+8]==b'esds':
        assert esds is None; esds=(cursor+12,cursor+length)
    cursor+=length
assert esds
def descriptor(p,end):
    tag=media[p]; p+=1; length=0
    for _ in range(4):
        b=media[p]; p+=1; length=(length<<7)|(b&127)
        if not b&128: break
    else: raise AssertionError('overlong descriptor length')
    assert p+length<=end
    return tag,p,p+length
tag,p,end=descriptor(*esds); assert tag==3 and media[p+2]==0
tag,p,end=descriptor(p+3,end); assert tag==4 and media[p]==0x40
tag,p,end=descriptor(p+13,end); assert tag==5
asc=media[p:end]; assert asc.hex()=='1190'
stream=b''.join(packets)
captured=(here.parent.parent/'stage-d/verified/input-rounded.bin').read_bytes()
assert stream[:16384]==captured and sum(sizes[:49])==16065 and sizes[49]==371
fixture=here/'fixture'; fixture.mkdir(exist_ok=True)
(fixture/'stream.aac').write_bytes(stream)
(fixture/'config.asc').write_bytes(asc)
(fixture/'sizes.oracle').write_text(''.join(str(x)+'\n' for x in sizes))
(fixture/'packet50.aac').write_bytes(packets[49])
(fixture/'source.json').write_text(json.dumps({'scope':'Offline original file oracle only; not a device configuration or input source', 'mediaSHA256':sha(media),'streamSHA256':sha(stream),'ascSHA256':sha(asc),'ascHex':asc.hex(),'packets':len(sizes),'bytes':len(stream),'offsets':offsets,'sizes':sizes,'capturedBytes':len(captured),'completePacketsAtCut':49,'partialBytesAtCut':319,'packet50Bytes':371},indent=2)+'\n')
print(json.dumps({'packets':len(sizes),'bytes':len(stream),'asc':asc.hex(),'capturedPrefixMatches':True}))
