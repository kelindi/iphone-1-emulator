from pathlib import Path
import json,hashlib,struct,sys,re

def records(data,windows):
 assert len(data)%296==0 and len(data)<=128*296,'post record bounds';out=[]
 for off in range(0,len(data),296):
  b=data[off:off+296];u=struct.unpack_from('<25I',b);pc,cpsr=u[:2];regs=u[2:18];afterpc,aftercpsr,lr,retired,expected,mode,avail=u[18:25];cycles,after=struct.unpack_from('<QQ',b,104);code,stack,obj,output=b[120:152],b[152:216],b[216:280],b[280:296]
  assert pc in windows and code==windows[pc],'post original mapped code differs';assert cpsr&63==16 and pc==regs[15],'post user context differs';assert after==cycles+1,'post cycles differ';assert avail&~7==0,'post availability invalid'
  if not avail&1:assert stack==bytes(64),'post unavailable stack fabricated'
  if not avail&2:assert obj==bytes(64),'post unavailable object fabricated'
  if not avail&4:assert output==bytes(16),'post unavailable output fabricated'
  ep=pc+4;em=16
  if pc==0x312367f8:
   assert avail&1,'post return stack absent';raw=struct.unpack_from('<I',stack,16)[0];assert raw and (raw&1 or not raw&3);ep=raw&~1;em=16|(32 if raw&1 else 0)
  assert expected==ep and mode==em,'post expected successor differs'
  flags=(cpsr&~32)|(em&32)
  if pc==0x312366a0:flags=(flags&0x0fffffff)|0x20000000|(regs[0]&0x80000000)|(0 if regs[0] else 0x40000000)
  assert retired==int(afterpc==ep and aftercpsr==flags),'post retirement receipt differs'
  out.append({'pc':pc,'cpsr':cpsr,'registers':regs,'cycles':cycles,'afterCycles':after,'afterPC':afterpc,'afterCPSR':aftercpsr,'retired':bool(retired),'available':avail,'stackHex':stack.hex(),'objectHex':obj.hex(),'outputHex':output.hex()})
 return out

def run(p):
 p=Path(p);prefix=p/'consumption-prefix';assert json.loads((prefix/'transport-audit.json').read_text())['passed'];assert json.loads((prefix/'caller-audit.json').read_text())['passed']
 baseline=Path('.context/amc-first/stage-s/run-1')
 for artifact in list(baseline.glob('*.bin'))+list(baseline.glob('transport-*.json')):
  if artifact.name=='transport-audit.json':continue
  assert (prefix/artifact.name).read_bytes()==artifact.read_bytes(),'actual Stage S prefix artifact differs: '+artifact.name
 old=(prefix/'trace.log').read_bytes();trace=(p/'trace.log').read_bytes();assert trace.startswith(old),'fresh prefix not preserved';tail=trace[len(old):].decode();assert tail.startswith('POST START cycles9873752634 '),'post start absent'
 start=re.search(r'^POST START cycles9873752634 pcc02726ac cpsr20000013 engineService(\d+) events([0-9a-f]+) nextOutput(\d+) owner(\d+) published(\d+) queue(\d+); first receipt frozen no device change$',tail,re.M)
 assert start and start.groups()==('0','00000200','0','1','2','65533'),'actual post start state differs'
 windows={r['pc']:bytes.fromhex(r['hex']) for r in json.loads((p/'post-original.json').read_text())}
 b=Path('.context/amc-first/stage-m/MeCCA.original').read_bytes();assert hashlib.sha256(b).hexdigest()=='15f8f049239e64f88660aeb1ce99aa07273ccd19ca636e624ff19e9a68e7f4ec';off=28;segments=[]
 for _ in range(struct.unpack_from('<I',b,16)[0]):
  cmd,n=struct.unpack_from('<II',b,off)
  if cmd==1:segments.append(struct.unpack_from('<4I',b,off+24))
  off+=n
 for pc,w in windows.items():assert w==next(b[f+pc-v:f+pc-v+32] for v,z,f,n in segments if v<=pc and pc+32<=v+n),'post fixture differs from original'
 rec=records((p/'post-records.bin').read_bytes(),windows)
 logs=re.findall(r'^POST record(\d+) pc([0-9a-f]+) cpsr([0-9a-f]+) cycles(\d+) afterPC([0-9a-f]+) afterCPSR([0-9a-f]+) afterCycles(\d+) retired(\d+) available(\d+)$',tail,re.M)
 assert len(logs)==len(rec),'post record/log count differs'
 for i,(row,log) in enumerate(zip(rec,logs)):
  actual=[int(log[0]),int(log[1],16),int(log[2],16),int(log[3]),int(log[4],16),int(log[5],16),int(log[6]),int(log[7]),int(log[8])];expected=[i+1,row['pc'],row['cpsr'],row['cycles'],row['afterPC'],row['afterCPSR'],row['afterCycles'],int(row['retired']),row['available']];assert actual==expected,'post record/log differs'
 stops=re.findall(r'^STOP (.+) cycles(\d+) ticks(\d+) pc([0-9a-f]+) lr([0-9a-f]+) address([0-9a-f]+) width(\d+) value([0-9a-f]+)$',tail,re.M);assert len(stops)==1,'post earliest stop absent';why,cycles,*rest=stops[0];assert 9873752634<=int(cycles)<=9873752634+5000000,'post continuation cap exceeded'
 assert 'publishedFrames2' in tail and 'DMA FINAL accepted5 ' in tail,'post engine bounds differ'
 report={'passed':True,'scope':'Fresh exact083/R/S prefix and bounded observational continuation; earliest stop retained, not downstream audio success.','stop':why,'cycles':int(cycles),'additionalRetiredOrInFlightCycles':int(cycles)-9873752634,'records':rec,'fifoOriginUnresolved':'FIFO SENTINEL' in tail,'halReturnObserved':any(x['pc']==0x312367f8 and x['retired'] for x in rec)}
 (p/'post-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':run(sys.argv[1])
