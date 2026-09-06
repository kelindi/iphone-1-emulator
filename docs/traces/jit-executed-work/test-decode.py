from pathlib import Path
import gzip,json,struct,copy
from decode import decode
p=Path('.context/jit-executed-work');b=p/'raw-focused';raw=[gzip.decompress((b/(n+'.bin.gz')).read_bytes())for n in ['rows','generated','summary']];meta=json.loads((b/'metadata.json').read_text());result=decode(*raw,meta);assert result['generatedRetired']==1 and result['work'][0]['pc']==0
(b/'decoded.json').write_text(json.dumps(result,indent=2)+'\n');tests=[{'name':'actual emitted committed-prefix raw fixture','passed':True}]
def reject(name,parts=None,m=None):
 try:decode(*(parts or raw),m or meta)
 except AssertionError as e:tests.append({'name':name,'rejected':True,'error':str(e)});return
 raise AssertionError('accepted '+name)
for name,index,value in [('row overflow',5,1),('map overflow',6,1),('capture install',8,1),('reset epoch',7,999)]:
 parts=list(raw);s=bytearray(parts[2]);struct.pack_into('<Q',s,index*8,value);parts[2]=s;reject(name,parts)
s=bytearray(raw[2]);struct.pack_into('<I',s,76,1);reject('active capture',[raw[0],raw[1],s])
m=copy.deepcopy(meta);m['generatedOverflow']='1';reject('generated saturation',m=m)
for name,slot,value in [('lost generated credit',0,0),('extra generated credit',0,2),('unknown generated ordinal',262143,1)]:
 g=bytearray(raw[1]);struct.pack_into('<Q',g,slot*8,value);reject(name,[raw[0],g,raw[2]])
m=copy.deepcopy(meta);m['positions'].append(m['positions'][0]);reject('ambiguous metadata slot',m=m)
reject('truncated table',[raw[0][:-1],raw[1],raw[2]])
# Keep same-PC changed opcode and a second virtual alias explicitly distinct.
rr=bytearray(raw[0]);s=bytearray(raw[2]);extra=[(0,0xe2800007),(0x1000,0xe2800001)]
for i,(pc,opcode)in enumerate(extra):struct.pack_into('<8IQ',rr,i*40,pc,opcode,1,0,0,0,0,1,1)
for idx in [0,1,2]:struct.pack_into('<Q',s,idx*8,2)
struct.pack_into('<I',s,72,2);m=copy.deepcopy(meta);m['retiredDelta']=3;r=decode(rr,raw[1],s,m);assert len(r['work'])==3;assert all(x['routine'].startswith('unknown')for x in r['generated']);tests.append({'name':'changed opcode and virtual alias stay separate unknown mappings','passed':True})
rr[8]=0;reject('missing interpreter fetch event',[rr,raw[1],s],m)
(p/'decode-tests.json').write_text(json.dumps({'passed':True,'checks':len(tests),'tests':tests},indent=2)+'\n');print('PASS',len(tests),'retained byte decoder controls')
