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
metadata=(run/'packet-data.bin').read_bytes();packets=struct.unpack_from('<I',metadata)[0]
assert packets==50 and len(metadata)==18732
records=[struct.unpack_from('<II',metadata,4+8*i) for i in range(packets)]
assert not any(metadata[4+8*packets:])
actual=(run/'input-rounded.bin').read_bytes();assert len(actual)==16384
assert actual==(run/'input-virtual.bin').read_bytes()==(run/'input-final.bin').read_bytes()
capacity=(run/'input-capacity.bin').read_bytes();assert len(capacity)==65536
assert capacity[:len(actual)]==actual and not any(capacity[len(actual):])
assert sample_offsets[0]==51240==boxes[b'mdat'][0]+8
assert actual==media[51240:51240+len(actual)]
cursor=0;complete=0;partial=[]
for index,(offset,length) in enumerate(records):
 assert offset==cursor and length==sizes[index]
 available=min(max(0,len(actual)-offset),length)
 assert actual[offset:offset+available]==media[sample_offsets[index]:sample_offsets[index]+available]
 if available==length:complete+=1
 else:partial.append({'packet':index+1,'offset':offset,'bytes':length,'present':available,'missing':length-available})
 cursor+=length
assert cursor==16436 and complete==49
assert partial==[{'packet':50,'offset':16065,'bytes':371,'present':319,'missing':52}]
missing=media[sample_offsets[49]+319:sample_offsets[49]+371]
assert len(missing)==52 and any(missing), 'Absent continuation must differ from the zero-filled buffer'
trace=(run/'trace.log').read_text()
assert 'PASS unaccepted input request physical14a8b000 bytes16384 words4096 cycles9873233416 ticks1756770444 pcc02780e4 status00000100 state0 prefix6' in trace
report={'passed':True,'scope':'Independent original MP4 box/sample/chunk parser against actual captured request and complete descriptor capacity. No model/precomputed expected payload or other oracle imported. Descriptor ABI/mapping and full SRAM coverage remain separately checked by reviewed original-kernel oracle.','originalMediaSha256':sha(media),'payloadSha256':sha(actual),'requestBytes':len(actual),'metadataPackets':packets,'metadataBytes':cursor,'completePackets':complete,'partial':partial,'zeroBytesAfterRequest':len(capacity)-len(actual),'missingOriginalBytesSha256':sha(missing),'missingOriginalNonzeroBytes':sum(x!=0 for x in missing)}
(run/'root-input-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
