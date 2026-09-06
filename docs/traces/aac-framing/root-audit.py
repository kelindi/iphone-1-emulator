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

import csv
framing=Path('.context/amc-first/stage-e/framing'); actual_run=framing/'run-1'
stream=b''.join(media[p:p+n] for p,n in zip(sample_offsets,sizes))
assert len(stream)==364719 and stream==(framing/'fixture/stream.aac').read_bytes()
assert stream[:16384]==(run/'input-rounded.bin').read_bytes()
assert (framing/'fixture/packet50.aac').read_bytes()==media[sample_offsets[49]:sample_offsets[49]+sizes[49]]
all_rows=[];all_pcm=[]
for mode in ['packetized','raw','cut']:
 rows=list(csv.DictReader((actual_run/(mode+'.csv')).open()))
 assert len(rows)==1069
 cursor=0
 for index,(row,size) in enumerate(zip(rows,sizes)):
  assert int(row['frame'])==index+1 and int(row['offset'])==cursor and int(row['consumed'])==size
  assert int(row['rate'])==48000 and int(row['channels'])==2 and int(row['object'])==2
  assert int(row['sbr'])==int(row['ps'])==0
  assert int(row['samples'])==(0 if index==0 else 2048)
  cursor+=size
 assert cursor==len(stream)
 pcm=(actual_run/(mode+'.pcm')).read_bytes()
 assert len(pcm)==sum(int(row['samples']) for row in rows)*2 and any(pcm)
 all_pcm.append(pcm);all_rows.append(rows)
assert all_rows[0]==all_rows[1]==all_rows[2] and all_pcm[0]==all_pcm[1]==all_pcm[2]
trunc=list(csv.reader((actual_run/'truncate.csv').open()))[1:]
assert len(trunc)==372
for index,row in enumerate(trunc):
 assert int(row[0])==index and int(row[1])==(371 if index==371 else 0)
log=(actual_run/'cut.log').read_text()
assert 'UNRESOLVED offset16065 available319' in log and 'committed49' in log
assert 'CONTINUATION boundary16436 committed50' in log
result={'passed':True,'scope':'Independent original MP4 sizes/chunk offsets compared with every actual raw, cutoff and packetized result. No agent sample-size oracle or expected PCM imported. This is standalone decoder feasibility, not guest hardware playback.', 'mediaSHA256':sha(media),'streamBytes':len(stream),'streamSHA256':sha(stream),'packets':1069,'truncationCases':len(trunc),'pcmBytes':len(all_pcm[0]),'pcmSHA256':sha(all_pcm[0]),'allPCMExact':True,'allFrameBoundariesExact':True,'cutCompletePackets':49,'cutPartialBytes':319,'appendedOriginalBytes':52}
Path('docs/traces/aac-framing/root-audit.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))
