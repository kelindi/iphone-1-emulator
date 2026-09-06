"""Synthetic record parser controls, not guest execution or real artifacts."""
from pathlib import Path
import importlib.util,json,tempfile,struct,contextlib,io
base=Path(__file__).parent
spec=importlib.util.spec_from_file_location('audit',base/'suspend-audit.py');a=importlib.util.module_from_spec(spec);spec.loader.exec_module(a)
old=Path('.context/amc-first/stage-x/run-1').resolve();code=a.sa.original_code();checks=0
with tempfile.TemporaryDirectory(prefix='amc-suspend-audit-') as directory:
 p=Path(directory);(p/'power-prefix').symlink_to(old,target_is_directory=True)
 for name in ('sram.bin','owned-queue.bin','init-written.bin','pcm-written.bin','host-written.bin','dma-bytes.bin','dma-copies.bin','stream-reads.bin','stream.json'):(p/name).symlink_to(old/name)
 (p/'suspended-queue.bin').write_bytes((old/'owned-queue.bin').read_bytes());(p/'suspended-sram.bin').write_bytes((old/'sram.bin').read_bytes())
 controls=[1,1,1,1,0,0x200,0x2000,1,0x14ae1000,4096,0x14ae1000,4096,346,0,1,1,1,4,1,1];(p/'suspended-control.bin').write_bytes(struct.pack('<20I',*controls))
 j=dict(phase=4,owned=1,resourcesValid=1,controllerAvailable=1,clock=0x4fffe27f,power=0x12fc,generation=1,copied=83304,decoded=17770,queue=65534,cycles=9874735700,driverStateAvailable=1,driverState=5,driverStoreRetired=1)
 for name in ('activeInitialized','activeConfigured','activeDecoder','activeQueue','activeIRQ','activeEvents','activeMask','activeService','activeDMA','activeIdleEnabled','activeResetComplete'):j[name]=0
 rows=['SUSPEND PREFIX synthetic parser fixture'];c=0x4fffe267;pw=0x12ec
 for i in range(4):
  address,value,afterc,afterp=[(0x39a0000c,3,c,pw),(0x39a00010,0x10,c,pw|16),(0x3c500048,c|8,c|8,pw),(0x3c500048,c|16,c|16,pw)][i];pc=0xc0411f3c;op=int.from_bytes(code(pc),'little');cy=9874735590+10*i
  rows.append(f'SUSPEND WRITE index{i} address{address:08x} value{value:08x} clockBefore{c:08x} clockAfter{afterc:08x} powerBefore{pw:08x} powerAfter{afterp:08x} pc{pc:08x} cpsr20000013 opcode{op:08x} cycles{cy} owned{int(i>0)} active{int(i==0)}')
  rows.append(f'SUSPEND RETIRED index{i} start{cy-1} callback{cy} end{cy} pc{pc:08x} cpsr20000013 opcode{op:08x} address{address:08x} value{value:08x} afterPC{pc+4:08x} afterCPSR20000013 emuRunReturned1');c,pw=afterc,afterp
 rows.append('SUSPEND DRIVER stateBefore3 stateAfter5 addressccaa2ce0 pcc0272640 cpsr20000013 opcodee58430e0 start9874735680 end9874735681 afterPCc0272644 afterCPSR20000013 emuRunReturned1')
 rows.append('STOP unmodeled suspended device read cycles9874735700 ticks1756792400 pcc0277f00 lrc0271234 address38500000 width4 value00000000');trace='\n'.join(rows)+'\n'
 def restore():(p/'suspend.json').write_text(json.dumps(j));(p/'trace.log').write_text(trace)
 def run():
  global checks
  with contextlib.redirect_stdout(io.StringIO()):a.run(p)
  checks+=1
 restore();run()
 for field,value,expected in [('owned',0,'transaction incomplete'),('activeInitialized',1,'active hardware'),('clock',j['clock']^0x10000,'final controller'),('driverStoreRetired',0,'receipt absent'),('queue',0,'conservation')]:
  restore();bad=dict(j);bad[field]=value;(p/'suspend.json').write_text(json.dumps(bad))
  try:run();raise RuntimeError('mutant accepted')
  except AssertionError as e:assert expected in str(e);checks+=1
 for before,after,expected in [('afterPCc0411f40','afterPCffffffff','retirement differs'),('powerAfter000012fc','powerAfter000012ec','transition/order'),('opcodee7832001','opcodee7932001','store instruction'),('afterPCc0272644','afterPCc0272648','driver state store')]:
  restore();assert before in trace;(p/'trace.log').write_text(trace.replace(before,after))
  try:run();raise RuntimeError('mutant accepted')
  except AssertionError as e:assert expected in str(e);checks+=1
print(f'PASS {checks} synthetic suspension auditor controls; no guest run')
