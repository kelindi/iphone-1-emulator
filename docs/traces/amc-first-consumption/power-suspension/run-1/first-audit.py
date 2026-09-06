"""External actual-artifact audit. The MP4 box/sample parser below is copied
from root's independent 072 parser, not the engine or guest packet metadata.
No device implementation imports this file. The original-kernel oracle is run
separately against upload/ by launch.py before this audit.
"""
from pathlib import Path
import struct,hashlib,json
import sys
root=Path('.context/amc-first/stage-d');run=Path(sys.argv[1])
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

import re
stream=b''.join(media[p:p+n] for p,n in zip(sample_offsets,sizes))
trace=(run/'trace.log').read_text()
assert 'PASS first PCM publication after retired instruction' in trace
assert 'BOUNDARY result5 interruptedInstruction0' in trace
assert 'HOST layout return0 firstOffset0000f220 firstCapacity8192 secondOffset00011220 secondCapacity8192' in trace
assert 'STOP ' not in trace
input_bytes=16384
for name in ('input-final.bin','input-rounded.bin','input-virtual.bin'):
 assert (run/name).read_bytes()==stream[:input_bytes], name
capacity=(run/'input-capacity.bin').read_bytes()
assert len(capacity)==65536 and capacity[:input_bytes]==stream[:input_bytes] and not any(capacity[input_bytes:])
# Guest packet metadata is an independent diagnostic oracle, not engine input.
packet=(run/'packet-data.bin').read_bytes();assert len(packet)==18732
assert struct.unpack_from('<I',packet)[0]==50 and not any(packet[404:])
cursor=0
for index in range(50):
 offset,size=struct.unpack_from('<II',packet,4+8*index)
 assert offset==cursor and size==sizes[index]
 cursor+=size
assert sum(sizes[:49])==16065 and sizes[49]==371 and cursor==16436
row=re.search(r'ENGINE state configured(\d+) copiedBytes(\d+) copiedWords(\d+) queueBytes(\d+) decodedBytes(\d+) decodedFrames(\d+) publishedFrames(\d+) sampleUnits(\d+) events([0-9a-f]+) mask([0-9a-f]+) inputPending(\d+)',trace)
assert row
configured,copied,words,queued,consumed,frames,published,samples=map(int,row.groups()[:8])
assert (configured,copied,words,frames,published,samples)==(1,16384,4096,2,1,2048)
assert consumed==sum(sizes[:2]) and queued==copied-consumed
assert int(row[9],16)&0x400
assert (run/'owned-queue.bin').read_bytes()==stream[consumed:copied]
golden=Path('.context/amc-first/stage-e/framing/run-1/packetized.pcm').read_bytes()
assert sha(golden)=='44feeb01ec9697c2f620d2262d606a8c3cf0bd657ed746ad8c0eacee8d3c077f'
pcm=(run/'first-pcm.bin').read_bytes();assert len(pcm)==4096 and pcm==golden[:4096], 'PCM differs from independent packetized decode'
original=(run/'upload/sram.bin').read_bytes();actual=(run/'sram.bin').read_bytes()
assert sha(original)=='011452fdff9699442c49115354c9b0bced1b98265ba64ee5b95c1c48bbb01583'
assert len(original)==len(actual)==0x2c000
masks=[(run/name).read_bytes() for name in ('init-written.bin','pcm-written.bin','host-written.bin')]
assert all(len(m)==len(actual) and all(v in (0,1) for v in m) for m in masks)
init,pcm_mask,host=masks
expected_init=bytearray(len(actual));expected_init[0xf208:0xf214]=b'\1'*12
expected_pcm=bytearray(len(actual));expected_pcm[0xf208:0xf20a]=b'\1'*2;expected_pcm[0xf20c:0xf20e]=b'\1'*2;expected_pcm[0xf220:0x10220]=b'\1'*4096
assert init==expected_init and pcm_mask==expected_pcm
allowed_host=set(range(0xf208,0xf20c))|set(range(0x2a000,0x2a006))|set(range(0x2a00a,0x2a00c))
assert all(i in allowed_host for i,v in enumerate(host) if v)
assert all(host[i] for i in list(range(0x2a000,0x2a006))+[0x2a00a,0x2a00b])
assert actual[0xf208:0xf214]==struct.pack('<6H',0,1,2048,0,0x10,0x1010)
assert actual[0x2a000:0x2a006]==struct.pack('<3H',0x1b,2,0) and actual[0x2a00a:0x2a00c]==b'\3\0'
assert actual[0xf220:0x10220]==pcm
unchanged=0
for i,(before,after) in enumerate(zip(original,actual)):
 if not any(m[i] for m in masks):assert before==after;unchanged+=1
result={'passed':True,'scope':'Original media sample parser + prior independently packetized decoder PCM + exact upload and intentional SRAM write masks. One buffer, before host consumption; no playback or sample-clock claim.',
 'mediaSHA256':sha(media),'inputBytes':copied,'inputSHA256':sha(stream[:copied]),'decodedFrames':frames,'decodedBytes':consumed,'queuedBytes':queued,'pcmBytes':len(pcm),'pcmSHA256':sha(pcm),'nonzeroPCMBytes':sum(v!=0 for v in pcm),'unchangedSRAMBytes':unchanged,'maskByteCounts':[sum(m) for m in masks],'negativeControls':'Separate artifact-mutant executions are reported by launch.py'}
(run/'functional-audit.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
