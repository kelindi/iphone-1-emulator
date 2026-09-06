"""Exactly one pinned 4B-step native fixture boot; normal unchanged CLI serializer."""
from pathlib import Path
import hashlib,json,os,resource,subprocess,time,signal
B=Path('.context/photo-bootstrap');R=B/'native-continuation-1';assert not R.exists()
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
pins=json.loads((B/'continuation-pins.json').read_text())
for p,h in pins.items():assert sha(p)==h,p
assert json.loads((B/'installed/photos.img.json').read_text())['passed']
R.mkdir();start=time.monotonic();report={'passed':False,'wallCapSeconds':450,'stepCap':10000000000,'initialSteps':4000000000,'sourcePins':pins,'startedUTC':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'scope':'Separately bounded4B→10B continuation. First boot Home gate incomplete and retained; no new cold boot or Photos UI acceptance.'}
cmd=['.context/build-rtc-release/tools/iphone1','--board','m68','--kernel','web/firmware/kernel_1A543a.macho','--devicetree','web/firmware/devicetree_m68ap.bin','--ramdisk',str(B/'installed/photos.img'),'--ram-mb','256','--debug-enabled','--cmdline','debug=0x8 serial=3 rd=md0 -v','--load',str(B/'native-run-1/home.snap'),'--steps','10000000000','--serial',str(R/'serial.log'),'--screenshot',str(R/'frame'),'--shot-every','500000000','--save-at','10000000000',str(R/'home.snap'),'--dump-ram',str(R/'ram.bin')]
report['command']=cmd

def bounds():
 resource.setrlimit(resource.RLIMIT_CPU,(400,405));resource.setrlimit(resource.RLIMIT_FSIZE,(1024**3,1024**3))

def save():(R/'run.json').write_text(json.dumps(report,indent=2)+'\n')
child=None
try:
 with (R/'stdout.log').open('wb')as out,(R/'stderr.log').open('wb')as err:
  child=subprocess.Popen(cmd,stdout=out,stderr=err,start_new_session=True,preexec_fn=bounds)
  report['pid']=child.pid;save()
  try:report['exitCode']=child.wait(timeout=450)
  except subprocess.TimeoutExpired:
   os.killpg(child.pid,signal.SIGKILL);report['exitCode']=child.wait();raise
 assert report['exitCode']==0
 text=(R/'stdout.log').read_text();serial=(R/'serial.log').read_bytes().replace(b'\r',b'')
 assert 'reason=steps exhausted instructions=10000000000 'in text
 assert (R/'home.snap').is_file() and (R/'ram.bin').stat().st_size==256*1024*1024
 assert 'loaded snapshot .context/photo-bootstrap/native-run-1/home.snap at step 4000000000' in (R/'stderr.log').read_text()
 report['priorActualReadbacks']='../native-run-1/artifact-audit-corrected.json'
 for p,h in pins.items():assert sha(p)==h,p
 report['passed']=True
except BaseException as e:
 report['error']={'type':type(e).__name__,'message':str(e)}
 raise
finally:
 if child is not None and child.poll()is None:os.killpg(child.pid,signal.SIGKILL);child.wait()
 report['processClosed']=child is not None and child.poll()is not None
 report['elapsedSeconds']=time.monotonic()-start;save()
