from pathlib import Path
import subprocess,os,json,time,shutil,signal,hashlib
p=Path('.context/web-lto').resolve();out=p/'replay';out.mkdir();receipt={'passed':False,'scope':'One candidate interpreter+seeded100M pair, retained exact control, no timing/retry','freeBefore':shutil.disk_usage('.').free,'arms':[]};assert receipt['freeBefore']>5.2*2**30
try:
 for kind in ['interpreter','seeded']:
  with open(out/(kind+'.log'),'w')as log:
   child=subprocess.Popen(['node',str(p/'run-arm.mjs'),kind],stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
   try:code=child.wait(timeout=180)
   except subprocess.TimeoutExpired:
    os.killpg(child.pid,signal.SIGKILL);child.wait();raise RuntimeError('180s arm cap '+kind)
  receipt['arms'].append({'kind':kind,'pid':child.pid,'exitCode':code});assert code==0,(kind,code)
 receipt['passed']=True
finally:
 receipt['freeAfter']=shutil.disk_usage('.').free;(out/'controller.json').write_text(json.dumps(receipt,indent=2))
print(json.dumps(receipt))
