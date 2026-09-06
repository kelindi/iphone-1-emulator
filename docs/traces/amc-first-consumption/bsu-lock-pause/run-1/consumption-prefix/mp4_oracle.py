from pathlib import Path
import struct,hashlib,json
root=Path('.context/amc-first/stage-d');run=root/'verified'
media=(root/'Alarm.original.m4a').read_bytes()
sha=lambda b:hashlib.sha256(b).hexdigest()
assert sha(media)=='56dece4e074bf622e0eb815331c65fb6c4e653e33b827e9a1ef30c4e0063136e'
# Parse bounded boxes independently of the device/probe and other oracle.
boxes={};pending=[(0,len(media))]
while pending:
 begin,end=pending.pop();cursor=begin
 while cursor<end:
  assert cursor+8<=end
  size,kind=struct.unpack_from('>I4s',media,cursor)
  assert size>=8 and cursor+size<=end
  if kind in (b'moov',b'trak',b'mdia',b'minf',b'stbl'):
   pending.append((cursor+8,cursor+size))
  elif kind in (b'stsz',b'stco',b'stsc',b'mdat'):
   assert kind not in boxes;boxes[kind]=(cursor,cursor+size)
  cursor+=size
 assert cursor==end
start,end=boxes[b'stsz'];version_flags,fixed,count=struct.unpack_from('>III',media,start+8)
assert version_flags==fixed==0 and count==1069 and start+20+4*count==end
sizes=struct.unpack_from('>'+str(count)+'I',media,start+20)
start,end=boxes[b'stco'];flags,chunks=struct.unpack_from('>II',media,start+8)
assert flags==0 and start+16+4*chunks==end
positions=struct.unpack_from('>'+str(chunks)+'I',media,start+16)
start,end=boxes[b'stsc'];flags,rules=struct.unpack_from('>II',media,start+8)
assert flags==0 and start+16+12*rules==end
mapping=[struct.unpack_from('>III',media,start+16+12*i) for i in range(rules)]
assert mapping[0][0]==1
sample_offsets=[];sample=0;rule=0
for chunk,position in enumerate(positions,1):
 while rule+1<len(mapping) and mapping[rule+1][0]<=chunk:rule+=1
 _,per_chunk,description=mapping[rule];assert description==1
 for _ in range(per_chunk):
  assert sample<count
  assert boxes[b'mdat'][0]+8<=position and position+sizes[sample]<=boxes[b'mdat'][1]
  sample_offsets.append(position);position+=sizes[sample];sample+=1
assert sample==count

stream=b"".join(media[p:p+n] for p,n in zip(sample_offsets,sizes))
