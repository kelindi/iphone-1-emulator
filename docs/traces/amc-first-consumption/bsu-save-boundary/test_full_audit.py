"""Exercise complete auditors with preserved P bytes + explicitly synthetic receipt.
This is not an artifact-only retirement proof and never changes P or runs guest.
"""
from pathlib import Path
import tempfile,shutil,runpy,contextlib,io,re,json
b=Path(__file__).parent;p=Path('.context/amc-first/stage-p/run-1');checks=0
with tempfile.TemporaryDirectory(prefix='amc-synthetic-receipt-') as td:
 out=Path(td)
 for f in p.iterdir():
  if f.name.startswith('negative-'):continue
  if f.is_dir():shutil.copytree(f,out/f.name)
  else:shutil.copy2(f,out/f.name)
 for f in ('audit.py','retirement-audit.py'):shutil.copy2(b/f,out/f)
 trace=(out/'trace.log').read_text()
 step='RELEASE STEP startCycles9873752633 startPCc02726a8 startCPSR20000013 instructione1c030b0 callbackCycles9873752634 callbackPCc02726a8 callbackCPSR20000013\n'
 trace=trace.replace('OWNER WRITE accepted address2200f208',step+'OWNER WRITE accepted address2200f208',1)
 retired='RELEASE RETIRED startCycles9873752633 callbackCycles9873752634 endCycles9873752634 startPCc02726a8 afterPCc02726ac startCPSR20000013 afterCPSR20000013 instructione1c030b0 emuRunReturned1 inGuest0\nPASS first PCM read and ownership returned cycles9873752634 releaseStoreCycles9873752634 pcc02726a8 reads1024 covered4096 ipc1 copy0 published2; no next guest instruction or service\n'
 trace=re.sub(r'^STOP release store not retired.*\nSTOP context between-instructions\n',retired,trace,flags=re.M)
 for old,new in [('FINAL result1','FINAL result6'),('BOUNDARY result1','BOUNDARY result6'),('CONSUMPTION result1','CONSUMPTION result6')]:trace=trace.replace(old,new)
 (out/'trace.log').write_text(trace)
 import sys
 oldargv=sys.argv;sys.argv=['retirement-audit.py',str(out)]
 with contextlib.redirect_stdout(io.StringIO()):runpy.run_path(str(out/'retirement-audit.py'),run_name='__main__')
 sys.argv=oldargv
 audit=runpy.run_path(str(out/'audit.py'))['run']
 with contextlib.redirect_stdout(io.StringIO()):audit(out)
 checks+=1
 for target,offset in [('owned-queue.bin',123),('read-coverage.bin',123),('read-bytes.bin',123),('dma-bytes.bin',123)]:
  data=(out/target).read_bytes();bad=bytearray(data);bad[offset]^=1;(out/target).write_bytes(bad)
  try:
   with contextlib.redirect_stdout(io.StringIO()):audit(out)
  except AssertionError:checks+=1
  else:raise AssertionError('complete audit accepted '+target)
  (out/target).write_bytes(data)
print(f'complete consumption auditor: {checks} PASS; P data plus SYNTHETIC receipt only, not guest retirement proof')
