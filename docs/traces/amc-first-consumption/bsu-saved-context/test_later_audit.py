"""Synthetic continuation of frozen prefix; no guest execution or input feeding."""
from pathlib import Path
import contextlib,importlib.util,io,json,runpy,struct,tempfile
b=Path(__file__).parent;spec=importlib.util.spec_from_file_location('later',b/'later-audit.py');a=importlib.util.module_from_spec(spec);spec.loader.exec_module(a)
base=Path('.context/amc-first/stage-n/run-1');oracle=runpy.run_path(str(b/'mp4_oracle.py'));checks=0
with tempfile.TemporaryDirectory(prefix='amc-later-audit-') as td:
 out=Path(td)
 for p in base.iterdir():
  if not p.name.startswith('negative-'):(out/p.name).symlink_to(p.resolve(),target_is_directory=p.is_dir())
 (out/'fourth-request').symlink_to(base.resolve(),target_is_directory=True)
 def put(n,d):
  p=out/n
  if p.is_symlink():p.unlink()
  p.write_bytes(d)
 trace=(base/'trace.log').read_text().split('STOP fourth input request cap',1)[0];rows=[];blob=b'';copies=49152;queued=48804
 for request in (4,5):
  addr=0x14a8b000 if request==4 else 0x14ac1000;cycle=9873476740+(request-4)*10000;data=oracle['stream'][16384*(request-1):16384*request];put('fourth-input.bin' if request==4 else 'fifth-input.bin',data)
  trace+=f'CAPTURE later request{request} address{addr:08x} words4096 cycles{cycle} ticks{1756773987+(request-4)*100} pcc0278130 lrc0274874 cpsra0000013 copiedBytes{copies} queueBytes{queued} decodedBytes348 pending0 active0 events00000600 mask00002000 phase5\n'
  trace+=f'ACCEPT later request{request} address{addr:08x} words4096 cycles{cycle}; actual live RAM DMA only\n';words=0
  for size in ([4096]*4 if request==4 else [348]):
   after=words+size//4;row=(cycle+1,copies,348,348,request,addr+4*words,size,len(blob),words,after,queued,queued+size,int(after==4096),int(after!=4096));rows.append(row)
   trace+=f'DMA COPY request{request} address{addr+4*words:08x} bytes{size} offset{len(blob)} cycles{cycle+1} copiedBefore{copies} decodedBefore348 decodedAfter348 wordsBefore{words} wordsAfter{after} queueBefore{queued} queueAfter{queued+size} pending{int(after==4096)} active{int(after!=4096)}\n'
   blob+=data[4*words:4*after];copies+=size;queued+=size;words=after;cycle+=1
 trace+='DMA FINAL accepted5 start14ac1000 words4096 copiedWords87 current14ac115c copiedBytes65884 queueBytes65536 decodedBytes348 pending0 active1 ledgerRecords5 ledgerBytes16732\n'
 put('trace.log',trace.encode());put('dma-copies.bin',b''.join(struct.pack(a.FMT,*r) for r in rows));put('dma-bytes.bin',blob)
 real=(base/'first/input-final.bin').read_bytes()+(base/'second-input.bin').read_bytes()+(base/'third-input.bin').read_bytes()+blob;put('owned-queue.bin',real[348:]);
 def check():
  with contextlib.redirect_stdout(io.StringIO()):a.run(out)
 check();checks+=1
 def bad(name,change):
  global checks
  original=(out/name).read_bytes();put(name,change(original))
  try:check()
  except AssertionError:checks+=1
  else:raise AssertionError('accepted mutant '+name)
  finally:put(name,original)
 def flip(at):return lambda data:data[:at]+bytes([data[at]^1])+data[at+1:]
 for file in ('fourth-input.bin','fifth-input.bin','dma-bytes.bin','owned-queue.bin'):bad(file,flip(123))
 for field in range(14):bad('dma-copies.bin',flip(struct.calcsize('<4Q')+4*(field-4) if field>=4 else field*8))
 for old,new in [('copiedWords87','copiedWords88'),('current14ac115c','current14ac115d'),('pending0 active1 ledger','pending1 active1 ledger'),('active1 ledger','active0 ledger'),('ledgerRecords5','ledgerRecords6')]:bad('trace.log',lambda data,old=old,new=new:data.replace(old.encode(),new.encode()))
print(f'later auditor: {checks} PASS (synthetic continuation, no guest)')
